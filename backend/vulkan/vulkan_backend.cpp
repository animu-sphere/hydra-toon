// SPDX-License-Identifier: Apache-2.0
#include <toon/vulkan_backend.hpp>

#include <cstring>
#include <filesystem>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#if defined(TOON_HAS_VULKAN)
#include <vulkan/vulkan.h>

#include "vulkan_internal.hpp"
#include "vulkan_scene.hpp"
#endif

namespace Toon {

SceneShaders SceneShadersIn(const std::string& directory) {
  const std::filesystem::path root(directory);
  return {(root / "mesh.vert.spv").string(), (root / "mesh.frag.spv").string(),
      (root / "mtoon.vert.spv").string(), (root / "mtoon.frag.spv").string()};
}

BackendCapability ProbeVulkanBackend() {
#if defined(TOON_HAS_VULKAN)
  std::uint32_t version = VK_API_VERSION_1_0;
  const VkResult result = vkEnumerateInstanceVersion(&version);
  if (result != VK_SUCCESS) {
    return {false, "Vulkan loader version query failed"};
  }
  std::ostringstream detail;
  detail << "Vulkan loader API " << VK_API_VERSION_MAJOR(version) << '.'
         << VK_API_VERSION_MINOR(version) << '.' << VK_API_VERSION_PATCH(version);
  return {version >= VK_API_VERSION_1_3, detail.str()};
#else
  return {false, "Vulkan 1.3 SDK/loader was not available at configure time"};
#endif
}

#if defined(TOON_HAS_VULKAN)
namespace {

using vulkan_internal::BeginSceneRendering;
using vulkan_internal::CreateDeviceImage;
using vulkan_internal::CreateHostBuffer;
using vulkan_internal::CreateInstanceWithValidation;
using vulkan_internal::CreateScenePipelines;
using vulkan_internal::DestroyDeviceImage;
using vulkan_internal::DestroyHostBuffer;
using vulkan_internal::DestroyInstance;
using vulkan_internal::DestroyScenePipelines;
using vulkan_internal::DeviceImage;
using vulkan_internal::HostBuffer;
using vulkan_internal::ImageBarrier;
using vulkan_internal::InstanceState;
using vulkan_internal::InvalidateIfNeeded;
using vulkan_internal::LoadSceneShaders;
using vulkan_internal::MaterialCache;
using vulkan_internal::MeshCache;
using vulkan_internal::SceneDeviceFeatures;
using vulkan_internal::SceneShaderWords;
using vulkan_internal::ScenePipelines;
using vulkan_internal::SupportsSceneFeatures;
using vulkan_internal::ValidationState;
using vulkan_internal::VulkanOk;

constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr std::uint64_t kFrameTimeoutNs = 10'000'000'000ULL;

std::optional<std::uint32_t> FindGraphicsQueue(VkPhysicalDevice device) {
  std::uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> properties(count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
  for (std::uint32_t index = 0; index < count; ++index) {
    if (properties[index].queueCount > 0 &&
        (properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
      return index;
    }
  }
  return std::nullopt;
}

bool SupportsTargetFormats(VkPhysicalDevice device) {
  VkFormatProperties color_properties{};
  VkFormatProperties depth_properties{};
  vkGetPhysicalDeviceFormatProperties(device, kColorFormat, &color_properties);
  vkGetPhysicalDeviceFormatProperties(device, kDepthFormat, &depth_properties);
  const VkFormatFeatureFlags color_required =
      VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
  const VkFormatFeatureFlags depth_required =
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
      VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
  return (color_properties.optimalTilingFeatures & color_required) ==
             color_required &&
         (depth_properties.optimalTilingFeatures & depth_required) ==
             depth_required;
}

class VulkanOffscreenRenderer final : public OffscreenRenderer {
public:
  ~VulkanOffscreenRenderer() override {
    Destroy();
  }

  FrameStatus Initialize(const SceneShaders& shaders, std::string& detail);

  [[nodiscard]] bool Render(const DrawList& draws, std::uint32_t width,
      std::uint32_t height, ColorProduct& color, DepthProduct& depth,
      std::string& error) override;

  [[nodiscard]] const OffscreenStatistics& statistics() const override {
    return statistics_;
  }

private:
  bool EnsureTargets(std::uint32_t width, std::uint32_t height,
      std::string& error);
  void DestroyTargets();
  bool WaitForCompletion(std::string& error);
  void Destroy();

  InstanceState instance_;
  ValidationState validation_;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkCommandBuffer command_ = VK_NULL_HANDLE;
  // One timeline semaphore carries every frame's completion; frame N signals
  // value N.
  VkSemaphore timeline_ = VK_NULL_HANDLE;
  std::uint64_t submitted_ = 0;
  ScenePipelines pipelines_;
  MaterialCache materials_;
  MeshCache meshes_;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  DeviceImage color_;
  DeviceImage depth_;
  HostBuffer color_readback_;
  HostBuffer depth_readback_;
  OffscreenStatistics statistics_;
};

FrameStatus VulkanOffscreenRenderer::Initialize(const SceneShaders& shaders,
    std::string& detail) {
  SceneShaderWords words;
  if (!LoadSceneShaders(shaders, words, detail)) {
    return FrameStatus::Fail;
  }

  if (!CreateInstanceWithValidation("toon-offscreen", {}, &validation_,
          instance_, detail)) {
    return FrameStatus::Skip;
  }

  std::uint32_t physical_count = 0;
  if (!VulkanOk(vkEnumeratePhysicalDevices(instance_.instance, &physical_count,
                    nullptr),
          "vkEnumeratePhysicalDevices", detail)) {
    return FrameStatus::Fail;
  }
  if (physical_count == 0) {
    detail = "no Vulkan physical device is available";
    return FrameStatus::Skip;
  }
  std::vector<VkPhysicalDevice> physical_devices(physical_count);
  vkEnumeratePhysicalDevices(instance_.instance, &physical_count,
      physical_devices.data());
  std::optional<std::uint32_t> queue_family;
  std::string rejection = "no Vulkan physical device exposes a graphics queue";
  for (VkPhysicalDevice physical : physical_devices) {
    const auto candidate = FindGraphicsQueue(physical);
    if (!candidate || !SupportsSceneFeatures(physical, rejection)) {
      continue;
    }
    if (!SupportsTargetFormats(physical)) {
      rejection = "required RGBA8/depth32 attachment readback formats are "
                  "unavailable";
      continue;
    }
    physical_device_ = physical;
    queue_family = candidate;
    break;
  }
  if (!queue_family) {
    detail = rejection;
    return FrameStatus::Skip;
  }

  const float priority = 1.0F;
  VkDeviceQueueCreateInfo queue_create{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queue_create.queueFamilyIndex = *queue_family;
  queue_create.queueCount = 1;
  queue_create.pQueuePriorities = &priority;
  SceneDeviceFeatures features;
  VkDeviceCreateInfo device_create{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  device_create.pNext = features.EnableRequired();
  device_create.queueCreateInfoCount = 1;
  device_create.pQueueCreateInfos = &queue_create;
  if (!VulkanOk(vkCreateDevice(physical_device_, &device_create, nullptr,
                    &device_),
          "vkCreateDevice", detail)) {
    return FrameStatus::Fail;
  }
  vkGetDeviceQueue(device_, *queue_family, 0, &queue_);
  meshes_.Initialize(physical_device_, device_);

  VkCommandPoolCreateInfo pool_create{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_create.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_create.queueFamilyIndex = *queue_family;
  if (!VulkanOk(vkCreateCommandPool(device_, &pool_create, nullptr,
                    &command_pool_),
          "vkCreateCommandPool", detail)) {
    return FrameStatus::Fail;
  }
  VkCommandBufferAllocateInfo command_allocate{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  command_allocate.commandPool = command_pool_;
  command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_allocate.commandBufferCount = 1;
  if (!VulkanOk(vkAllocateCommandBuffers(device_, &command_allocate, &command_),
          "vkAllocateCommandBuffers", detail)) {
    return FrameStatus::Fail;
  }

  VkSemaphoreTypeCreateInfo timeline_type{
      VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
  timeline_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  VkSemaphoreCreateInfo semaphore_create{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  semaphore_create.pNext = &timeline_type;
  if (!VulkanOk(vkCreateSemaphore(device_, &semaphore_create, nullptr,
                    &timeline_),
          "vkCreateSemaphore(timeline)", detail)) {
    return FrameStatus::Fail;
  }

  if (!CreateScenePipelines(device_, words, kColorFormat, kDepthFormat,
          pipelines_, detail) ||
      !materials_.Initialize(physical_device_, device_,
          pipelines_.material_layout, detail)) {
    return FrameStatus::Fail;
  }
  statistics_.pipelines_created += ScenePipelines::kCount;

  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physical_device_, &properties);
  statistics_.device_name = properties.deviceName;
  statistics_.vendor_id = properties.vendorID;
  statistics_.device_id = properties.deviceID;
  statistics_.driver_version = std::to_string(properties.driverVersion);
  std::ostringstream api_version;
  api_version << VK_API_VERSION_MAJOR(properties.apiVersion) << '.'
              << VK_API_VERSION_MINOR(properties.apiVersion) << '.'
              << VK_API_VERSION_PATCH(properties.apiVersion);
  statistics_.api_version = api_version.str();
  statistics_.validation_available = instance_.validation_available;
  statistics_.validation_detail = instance_.validation_detail;
  return FrameStatus::Pass;
}

bool VulkanOffscreenRenderer::Render(const DrawList& draws,
    std::uint32_t width, std::uint32_t height, ColorProduct& color,
    DepthProduct& depth, std::string& error) {
  if (width == 0 || height == 0) {
    error = "the render extent must be non-zero";
    return false;
  }
  // One frame in flight: the previous frame has completed before its
  // targets, geometry or material slots are touched.
  if (!WaitForCompletion(error) || !EnsureTargets(width, height, error) ||
      !meshes_.Update(draws, error) || !materials_.Update(draws, error)) {
    return false;
  }

  if (!VulkanOk(vkResetCommandBuffer(command_, 0), "vkResetCommandBuffer",
          error)) {
    return false;
  }
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (!VulkanOk(vkBeginCommandBuffer(command_, &begin), "vkBeginCommandBuffer",
          error)) {
    return false;
  }

  const VkImageMemoryBarrier2 to_attachment[] = {
      ImageBarrier(color_.image, VK_IMAGE_ASPECT_COLOR_BIT,
          VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
      ImageBarrier(depth_.image, VK_IMAGE_ASPECT_DEPTH_BIT,
          VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
          VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
              VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL),
  };
  VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.imageMemoryBarrierCount = 2;
  dependency.pImageMemoryBarriers = to_attachment;
  vkCmdPipelineBarrier2(command_, &dependency);

  BeginSceneRendering(command_, color_.view, depth_.view, {width_, height_});
  meshes_.Record(command_, pipelines_, materials_, draws);
  vkCmdEndRendering(command_);

  const VkImageMemoryBarrier2 to_transfer[] = {
      ImageBarrier(color_.image, VK_IMAGE_ASPECT_COLOR_BIT,
          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
          VK_ACCESS_2_TRANSFER_READ_BIT,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
      ImageBarrier(depth_.image, VK_IMAGE_ASPECT_DEPTH_BIT,
          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
  };
  dependency.pImageMemoryBarriers = to_transfer;
  vkCmdPipelineBarrier2(command_, &dependency);

  VkBufferImageCopy copy{};
  copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy.imageSubresource.layerCount = 1;
  copy.imageExtent = {width_, height_, 1};
  vkCmdCopyImageToBuffer(command_, color_.image,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, color_readback_.buffer, 1, &copy);
  copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  vkCmdCopyImageToBuffer(command_, depth_.image,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, depth_readback_.buffer, 1, &copy);

  VkMemoryBarrier2 to_host{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
  to_host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
  to_host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  to_host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
  to_host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
  VkDependencyInfo host_dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  host_dependency.memoryBarrierCount = 1;
  host_dependency.pMemoryBarriers = &to_host;
  vkCmdPipelineBarrier2(command_, &host_dependency);
  if (!VulkanOk(vkEndCommandBuffer(command_), "vkEndCommandBuffer", error)) {
    return false;
  }

  VkCommandBufferSubmitInfo command_submit{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  command_submit.commandBuffer = command_;
  VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  signal.semaphore = timeline_;
  signal.value = submitted_ + 1;
  signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &command_submit;
  submit.signalSemaphoreInfoCount = 1;
  submit.pSignalSemaphoreInfos = &signal;
  if (!VulkanOk(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE),
          "vkQueueSubmit2", error)) {
    return false;
  }
  ++submitted_;

  // The caller wants this frame's pixels, so the readback waits here.
  if (!WaitForCompletion(error) ||
      !InvalidateIfNeeded(device_, color_readback_, error) ||
      !InvalidateIfNeeded(device_, depth_readback_, error)) {
    return false;
  }
  color.width = width_;
  color.height = height_;
  color.row_pitch = width_ * 4U;
  color.pixel_format = "rgba8-unorm";
  color.origin = "top-left";
  color.color_space = "linear";
  color.payload.resize(static_cast<std::size_t>(width_) * height_ * 4U);
  std::memcpy(color.payload.data(), color_readback_.mapped,
      color.payload.size());
  depth.width = width_;
  depth.height = height_;
  depth.row_pitch = width_ * static_cast<std::uint32_t>(sizeof(float));
  depth.pixel_format = "d32-sfloat";
  depth.origin = "top-left";
  depth.payload.resize(static_cast<std::size_t>(width_) * height_);
  std::memcpy(depth.payload.data(), depth_readback_.mapped,
      depth.payload.size() * sizeof(float));

  ++statistics_.frames_rendered;
  statistics_.completion = submitted_;
  statistics_.topology_uploads = meshes_.topology_uploads();
  statistics_.point_uploads = meshes_.point_uploads();
  statistics_.material_writes = materials_.writes();
  statistics_.validation_message_count = validation_.message_count;
  if (!validation_.first_message.empty()) {
    statistics_.validation_detail = validation_.first_message;
  }
  return true;
}

bool VulkanOffscreenRenderer::WaitForCompletion(std::string& error) {
  if (submitted_ == 0) {
    return true;
  }
  VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
  wait.semaphoreCount = 1;
  wait.pSemaphores = &timeline_;
  wait.pValues = &submitted_;
  return VulkanOk(vkWaitSemaphores(device_, &wait, kFrameTimeoutNs),
      "vkWaitSemaphores", error);
}

bool VulkanOffscreenRenderer::EnsureTargets(std::uint32_t width,
    std::uint32_t height, std::string& error) {
  if (width == width_ && height == height_ && color_.image != VK_NULL_HANDLE) {
    return true;
  }
  DestroyTargets();
  width_ = width;
  height_ = height;
  const VkDeviceSize pixels = static_cast<VkDeviceSize>(width) * height;
  if (!CreateDeviceImage(physical_device_, device_, kColorFormat,
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
          VK_IMAGE_ASPECT_COLOR_BIT, width, height, color_, error) ||
      !CreateDeviceImage(physical_device_, device_, kDepthFormat,
          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
              VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
          VK_IMAGE_ASPECT_DEPTH_BIT, width, height, depth_, error) ||
      !CreateHostBuffer(physical_device_, device_, pixels * 4U,
          VK_BUFFER_USAGE_TRANSFER_DST_BIT, color_readback_, error) ||
      !CreateHostBuffer(physical_device_, device_, pixels * sizeof(float),
          VK_BUFFER_USAGE_TRANSFER_DST_BIT, depth_readback_, error)) {
    DestroyTargets();
    return false;
  }
  ++statistics_.target_allocations;
  return true;
}

void VulkanOffscreenRenderer::DestroyTargets() {
  DestroyHostBuffer(device_, depth_readback_);
  DestroyHostBuffer(device_, color_readback_);
  DestroyDeviceImage(device_, depth_);
  DestroyDeviceImage(device_, color_);
  width_ = 0;
  height_ = 0;
}

void VulkanOffscreenRenderer::Destroy() {
  if (device_ != VK_NULL_HANDLE) {
    // Teardown, not an ordinary frame: waiting for the device is allowed here.
    vkDeviceWaitIdle(device_);
    meshes_.Destroy();
    materials_.Destroy();
    DestroyTargets();
    DestroyScenePipelines(device_, pipelines_);
    vkDestroySemaphore(device_, timeline_, nullptr);
    vkDestroyCommandPool(device_, command_pool_, nullptr);
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
  DestroyInstance(instance_);
}

} // namespace
#endif

std::unique_ptr<OffscreenRenderer> CreateOffscreenRenderer(
    const SceneShaders& shaders, FrameStatus& status, std::string& detail) {
#if !defined(TOON_HAS_VULKAN)
  (void)shaders;
  status = FrameStatus::Skip;
  detail = "Vulkan backend was not compiled for this configuration";
  return nullptr;
#else
  auto renderer = std::make_unique<VulkanOffscreenRenderer>();
  status = renderer->Initialize(shaders, detail);
  if (status != FrameStatus::Pass) {
    return nullptr;
  }
  return renderer;
#endif
}

GpuFrameEvidence RenderOffscreen(const DrawList& draws,
    const SceneShaders& shaders, std::uint32_t frame_count) {
  GpuFrameEvidence evidence;
  if (frame_count == 0) {
    evidence.status = FrameStatus::Fail;
    evidence.detail = "frame_count must be at least 1";
    return evidence;
  }
  auto renderer =
      CreateOffscreenRenderer(shaders, evidence.status, evidence.detail);
  if (renderer == nullptr) {
#if !defined(TOON_HAS_VULKAN)
    evidence.statistics.validation_detail =
        "Vulkan validation capture is unavailable in the core-only configuration";
#endif
    return evidence;
  }
  for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
    if (!renderer->Render(draws, 64, 64, evidence.color, evidence.depth,
            evidence.detail)) {
      evidence.status = FrameStatus::Fail;
      evidence.statistics = renderer->statistics();
      return evidence;
    }
  }
  evidence.statistics = renderer->statistics();
  std::ostringstream success;
  success << "rendered " << frame_count << " frames of "
          << draws.draws.size() << " draw(s) on "
          << evidence.statistics.device_name;
  evidence.detail = success.str();
  return evidence;
}

} // namespace Toon
