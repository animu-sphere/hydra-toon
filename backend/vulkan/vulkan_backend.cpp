// SPDX-License-Identifier: Apache-2.0
#include <toon/vulkan_backend.hpp>

#include <cstring>
#include <cmath>
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
      (root / "mtoon.vert.spv").string(), (root / "mtoon.frag.spv").string(),
      (root / "mtoon_transparent.vert.spv").string(),
      (root / "mtoon_transparent.frag.spv").string(),
      (root / "mtoon_outline.vert.spv").string(),
      (root / "mtoon_outline.frag.spv").string(),
      (root / "mmd.vert.spv").string(), (root / "mmd.frag.spv").string(),
      (root / "mmd_transparent.vert.spv").string(),
      (root / "mmd_transparent.frag.spv").string(),
      (root / "mmd_outline.vert.spv").string(),
      (root / "mmd_outline.frag.spv").string(),
      (root / "shadow.vert.spv").string(),
      (root / "shadow.frag.spv").string(),
      (root / "overlay.vert.spv").string(),
      (root / "overlay.frag.spv").string(),
      (root / "preview_surface.vert.spv").string(),
      (root / "preview_surface.frag.spv").string(),
      (root / "composite.vert.spv").string(),
      (root / "composite.frag.spv").string()};
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
namespace vulkan_internal {

bool SupportsOffscreenTargets(VkPhysicalDevice device) {
  VkFormatProperties color_properties{};
  VkFormatProperties depth_properties{};
  vkGetPhysicalDeviceFormatProperties(device, kOffscreenColorFormat,
      &color_properties);
  vkGetPhysicalDeviceFormatProperties(device, kOffscreenDepthFormat,
      &depth_properties);
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

} // namespace vulkan_internal

namespace {

using vulkan_internal::BeginSceneRendering;
using vulkan_internal::ChooseSampleCount;
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
using vulkan_internal::OffscreenDevice;
using vulkan_internal::ShadowMap;
using vulkan_internal::SceneAttachments;
using vulkan_internal::SceneDeviceFeatures;
using vulkan_internal::SceneShaderWords;
using vulkan_internal::ScenePipelines;
using vulkan_internal::SupportsOffscreenTargets;
using vulkan_internal::SupportsSceneFeatures;
using vulkan_internal::TextureCache;
using vulkan_internal::ValidationState;
using vulkan_internal::VulkanOk;

constexpr VkFormat kColorFormat = vulkan_internal::kOffscreenColorFormat;
constexpr VkFormat kDepthFormat = vulkan_internal::kOffscreenDepthFormat;
constexpr std::uint64_t kFrameTimeoutNs = 10'000'000'000ULL;
// The CPU reads the products back, which uncached memory makes slow
// (GPU handoff policy §8).
constexpr VkMemoryPropertyFlags kReadbackMemory =
    VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
// A frame's first and last command each write a timestamp.
constexpr std::uint32_t kTimestampCount = 2;

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

class VulkanOffscreenRenderer final : public OffscreenRenderer {
public:
  ~VulkanOffscreenRenderer() override {
    Destroy();
  }

  // Creates the renderer's own instance and device and chooses the physical
  // device: the owned path. Skip when this environment cannot render.
  FrameStatus CreateDevice(OffscreenDevice& device, std::string& detail);
  // Everything the renderer draws with, on its own device or a lent one.
  FrameStatus Initialize(const OffscreenDevice& device,
      const SceneShaders& shaders, const RenderOptions& options,
      std::string& detail);

  [[nodiscard]] bool Render(const DrawList& draws, std::uint32_t width,
      std::uint32_t height, ColorProduct& color, DepthProduct& depth,
      std::string& error) override;

  [[nodiscard]] const OffscreenStatistics& statistics() const override {
    return statistics_;
  }
  void SetLateFrameSource(LateFrameSource source) override { late_.source = source; }
  bool SetBackground(const BackgroundSettings& settings, std::string& error) override;

private:
  bool EnsureTargets(std::uint32_t width, std::uint32_t height,
      std::string& error);
  void DestroyTargets();
  bool WaitForCompletion(std::string& error);
  void WriteTimestamp(std::uint32_t index);
  void ReadTimestamps();
  void Destroy();

  // Null for a lent device, whose instance is its host's.
  InstanceState instance_;
  ValidationState validation_;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  // Whether `device_` is the renderer's own to destroy.
  bool owns_device_ = false;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkCommandBuffer command_ = VK_NULL_HANDLE;
  // Null when the queue writes no timestamps.
  VkQueryPool timestamps_ = VK_NULL_HANDLE;
  double timestamp_period_ = 0.0;
  std::uint64_t timestamp_mask_ = 0;
  // One timeline semaphore carries every frame's completion; frame N signals
  // value N.
  VkSemaphore timeline_ = VK_NULL_HANDLE;
  std::uint64_t submitted_ = 0;
  ScenePipelines pipelines_;
  MaterialCache materials_;
  TextureCache textures_;
  MeshCache meshes_;
  ShadowMap shadow_;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  // The products, read back: single-sampled, the pass's own attachments;
  // multisampled, what the pass resolves `color_samples_` and
  // `depth_samples_` into.
  DeviceImage color_;
  DeviceImage depth_;
  DeviceImage color_samples_;
  DeviceImage depth_samples_;
  HostBuffer color_readback_;
  HostBuffer depth_readback_;
  OffscreenStatistics statistics_;
  vulkan_internal::LateFrameState late_;
  BackgroundSettings background_;
};

FrameStatus VulkanOffscreenRenderer::CreateDevice(OffscreenDevice& device,
    std::string& detail) {
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
    if (!SupportsOffscreenTargets(physical)) {
      rejection = "required RGBA8/depth32 attachment readback formats are "
                  "unavailable";
      continue;
    }
    device.physical_device = physical;
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
  device_create.pNext = features.EnableRequired(device.physical_device);
  device_create.queueCreateInfoCount = 1;
  device_create.pQueueCreateInfos = &queue_create;
  if (!VulkanOk(vkCreateDevice(device.physical_device, &device_create, nullptr,
                    &device.device),
          "vkCreateDevice", detail)) {
    return FrameStatus::Fail;
  }
  // Owned from here, so Destroy releases it whatever fails next.
  device_ = device.device;
  owns_device_ = true;
  vkGetDeviceQueue(device.device, *queue_family, 0, &device.queue);
  device.queue_family = *queue_family;
  device.alpha_to_one = features.core.features.alphaToOne == VK_TRUE;
  return FrameStatus::Pass;
}

FrameStatus VulkanOffscreenRenderer::Initialize(const OffscreenDevice& device,
    const SceneShaders& shaders, const RenderOptions& options,
    std::string& detail) {
  physical_device_ = device.physical_device;
  device_ = device.device;
  queue_ = device.queue;
  pipelines_.alpha_to_one = device.alpha_to_one;
  SceneShaderWords words;
  if (!LoadSceneShaders(shaders, words, detail)) {
    return FrameStatus::Fail;
  }

  VkCommandPoolCreateInfo pool_create{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_create.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_create.queueFamilyIndex = device.queue_family;
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

  std::uint32_t family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &family_count,
      nullptr);
  std::vector<VkQueueFamilyProperties> families(family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &family_count,
      families.data());
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physical_device_, &properties);
  const std::uint32_t valid_bits =
      device.queue_family < family_count
          ? families[device.queue_family].timestampValidBits
          : 0U;
  if (valid_bits != 0 && properties.limits.timestampPeriod > 0.0F) {
    VkQueryPoolCreateInfo query_create{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    query_create.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query_create.queryCount = kTimestampCount;
    if (!VulkanOk(vkCreateQueryPool(device_, &query_create, nullptr,
                      &timestamps_),
            "vkCreateQueryPool(timestamps)", detail)) {
      return FrameStatus::Fail;
    }
    timestamp_period_ = properties.limits.timestampPeriod;
    timestamp_mask_ = valid_bits >= 64U ? ~0ULL : (1ULL << valid_bits) - 1U;
    statistics_.gpu_timing = true;
  }

  const VkSampleCountFlagBits samples = ChooseSampleCount(physical_device_,
      kColorFormat, kDepthFormat, options.samples);
  if (!CreateScenePipelines(device_, words, kColorFormat, kDepthFormat,
          samples, pipelines_, detail) ||
      !materials_.Initialize(physical_device_, device_,
          pipelines_.material_layout, detail) ||
      !textures_.Initialize(physical_device_, device_,
          materials_.descriptor_set(), detail) ||
      !meshes_.Initialize(physical_device_, device_, pipelines_.skin_layout,
          detail) ||
      !shadow_.Initialize(physical_device_, device_,
          materials_.descriptor_set(), detail)) {
    return FrameStatus::Fail;
  }
  statistics_.pipelines_created += ScenePipelines::kCount;
  statistics_.samples = static_cast<std::uint32_t>(samples);

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
  statistics_.validation_detail =
      owns_device_ ? instance_.validation_detail
                   : "a lent device's validation messages go to its host's "
                     "instance";
  return FrameStatus::Pass;
}

bool VulkanOffscreenRenderer::SetBackground(const BackgroundSettings& settings,
    std::string& error) {
  const auto valid_channel = [](float value) {
    return std::isfinite(value) && value >= 0 && value <= 1;
  };
  if ((settings.mode != BackgroundMode::Opaque &&
          settings.mode != BackgroundMode::Transparent) ||
      !valid_channel(settings.color.x) || !valid_channel(settings.color.y) ||
      !valid_channel(settings.color.z)) {
    error = "background requires Opaque or Transparent and finite linear RGB in [0,1]";
    return false;
  }
  if (settings.mode == BackgroundMode::Transparent && statistics_.samples > 1 &&
      !pipelines_.alpha_to_one) {
    error = "transparent MSAA requires Vulkan alphaToOne; use one sample on this device";
    return false;
  }
  background_ = settings;
  error.clear();
  return true;
}

bool VulkanOffscreenRenderer::Render(const DrawList& extracted,
    std::uint32_t width, std::uint32_t height, ColorProduct& color,
    DepthProduct& depth, std::string& error) {
  if (width == 0 || height == 0) {
    error = "the render extent must be non-zero";
    return false;
  }
  // One frame in flight: the previous frame has completed before its
  // targets, geometry, textures or material slots are touched.
  const double pose_before = meshes_.pose_write_ms();
  const double morph_before = meshes_.morph_write_ms();
  const double material_before = materials_.parameter_write_ms();
  if (!WaitForCompletion(error) || !EnsureTargets(width, height, error) ||
      !textures_.Update(extracted, error) ||
      (late_.enabled() && (!meshes_.Update(extracted, error, false) ||
          !materials_.Update(extracted, textures_, error, false)))) {
    return false;
  }
  const DrawList* frame = nullptr;
  if (!late_.Read(extracted, frame, statistics_, error)) return false;
  const DrawList& draws = *frame;
  if (!meshes_.Update(draws, error) ||
      !shadow_.Update(draws, error, statistics_.latency.shadow_fit_ms) ||
      !materials_.Update(draws, textures_, error, true,
          &shadow_.fit().views())) {
    return false;
  }
  statistics_.latency.buffers_written = SteadyNanoseconds();
  statistics_.latency.pose_write_ms = meshes_.pose_write_ms() - pose_before;
  statistics_.latency.morph_write_ms = meshes_.morph_write_ms() - morph_before;
  statistics_.latency.material_write_ms = materials_.parameter_write_ms() - material_before;

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
  if (timestamps_ != VK_NULL_HANDLE) {
    vkCmdResetQueryPool(command_, timestamps_, 0, kTimestampCount);
  }
  WriteTimestamp(0);
  textures_.RecordUploads(command_);
  shadow_.Record(command_, pipelines_, materials_, meshes_, draws);
  statistics_.shadow_draws = shadow_.mmd_draws();
  statistics_.shadow_caster_draws = shadow_.caster_draws();
  statistics_.shadow_views =
      static_cast<std::uint32_t>(shadow_.fit().views().size());
  statistics_.shadow_unviewed = shadow_.fit().unviewed();
  statistics_.shadow_resolution = shadow_.resolution();
  statistics_.shadow_layers = shadow_.layers();
  statistics_.shadow_memory_bytes = shadow_.memory_bytes();
  statistics_.shadow_rebuilds = shadow_.rebuilds();

  // Every attachment is cleared, so none keeps its contents. Multisampled,
  // the depth product is written by the resolve, at the colour output stage
  // as a colour attachment write; single-sampled, by the depth tests.
  const bool multisampled = color_samples_.image != VK_NULL_HANDLE;
  const VkPipelineStageFlags2 depth_product_stage =
      multisampled ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                   : VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                         VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
  const VkAccessFlags2 depth_product_access =
      multisampled ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                   : VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  const auto color_to_attachment = [](VkImage image) {
    return ImageBarrier(image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  };
  const auto depth_to_attachment = [](VkImage image,
                                       VkPipelineStageFlags2 stage,
                                       VkAccessFlags2 access) {
    return ImageBarrier(image, VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, stage, access,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  };
  const VkImageMemoryBarrier2 to_attachment[] = {
      color_to_attachment(color_.image),
      depth_to_attachment(depth_.image, depth_product_stage,
          depth_product_access),
      color_to_attachment(color_samples_.image),
      depth_to_attachment(depth_samples_.image,
          VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
              VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
  };
  VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.imageMemoryBarrierCount = multisampled ? 4U : 2U;
  dependency.pImageMemoryBarriers = to_attachment;
  vkCmdPipelineBarrier2(command_, &dependency);

  SceneAttachments attachments{color_.view, depth_.view};
  if (multisampled) {
    attachments = {color_samples_.view, depth_samples_.view, color_.view,
        depth_.view};
  }
  BeginSceneRendering(command_, attachments, {width_, height_}, background_);
  vulkan_internal::SceneRecord record;
  meshes_.Record(command_, pipelines_, materials_, draws, {width_, height_}, &record);
  statistics_.outline_draws = record.hulls;
  vkCmdEndRendering(command_);

  const VkImageMemoryBarrier2 to_transfer[] = {
      ImageBarrier(color_.image, VK_IMAGE_ASPECT_COLOR_BIT,
          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
          VK_ACCESS_2_TRANSFER_READ_BIT,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
      ImageBarrier(depth_.image, VK_IMAGE_ASPECT_DEPTH_BIT,
          depth_product_stage, depth_product_access,
          VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
  };
  dependency.imageMemoryBarrierCount = 2;
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
  WriteTimestamp(1);
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
  // The previous frame has completed, as the CPU waited for it, but its
  // copies are ordered only before the host's reads. Waiting for its value
  // here too orders it before this frame on the queue itself, which a
  // queue another host shares and synchronization validation can see.
  VkSemaphoreSubmitInfo previous{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  previous.semaphore = timeline_;
  previous.value = submitted_;
  previous.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit.waitSemaphoreInfoCount = submitted_ != 0 ? 1U : 0U;
  submit.pWaitSemaphoreInfos = &previous;
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &command_submit;
  submit.signalSemaphoreInfoCount = 1;
  submit.pSignalSemaphoreInfos = &signal;
  if (!VulkanOk(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE),
          "vkQueueSubmit2", error)) {
    return false;
  }
  ++submitted_;
  statistics_.latency.frame = submitted_;
  statistics_.latency.submitted = SteadyNanoseconds();

  // The caller wants this frame's pixels, so the readback waits here.
  if (!WaitForCompletion(error) ||
      !InvalidateIfNeeded(device_, color_readback_, error) ||
      !InvalidateIfNeeded(device_, depth_readback_, error)) {
    return false;
  }
  ReadTimestamps();
  color.width = width_;
  color.height = height_;
  color.row_pitch = width_ * 4U;
  color.pixel_format = "rgba8-unorm";
  color.origin = "top-left";
  color.color_space = "linear";
  color.transfer_function = "linear";
  color.alpha_convention = "premultiplied";
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
  statistics_.readback_bytes = color.payload.size() +
                               depth.payload.size() * sizeof(float);
  statistics_.topology_uploads = meshes_.topology_uploads();
  statistics_.point_uploads = meshes_.point_uploads();
  statistics_.material_writes = materials_.writes();
  statistics_.texture_uploads = textures_.uploads();
  statistics_.skin_uploads = meshes_.skin_uploads();
  statistics_.pose_writes = meshes_.pose_writes();
  statistics_.morph_uploads = meshes_.morph_uploads();
  statistics_.morph_weight_writes = meshes_.morph_weight_writes();
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

void VulkanOffscreenRenderer::WriteTimestamp(std::uint32_t index) {
  if (timestamps_ != VK_NULL_HANDLE) {
    vkCmdWriteTimestamp2(command_, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        timestamps_, index);
  }
}

void VulkanOffscreenRenderer::ReadTimestamps() {
  if (timestamps_ == VK_NULL_HANDLE) {
    return;
  }
  // The frame has completed, so both timestamps are available.
  std::uint64_t ticks[kTimestampCount] = {};
  if (vkGetQueryPoolResults(device_, timestamps_, 0, kTimestampCount,
          sizeof(ticks), ticks, sizeof(std::uint64_t),
          VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
    statistics_.gpu_frame_ms = 0;
    return;
  }
  const std::uint64_t elapsed = (ticks[1] - ticks[0]) & timestamp_mask_;
  statistics_.gpu_frame_ms =
      static_cast<double>(elapsed) * timestamp_period_ * 1e-6;
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
          VK_BUFFER_USAGE_TRANSFER_DST_BIT, color_readback_, error,
          kReadbackMemory) ||
      !CreateHostBuffer(physical_device_, device_, pixels * sizeof(float),
          VK_BUFFER_USAGE_TRANSFER_DST_BIT, depth_readback_, error,
          kReadbackMemory)) {
    DestroyTargets();
    return false;
  }
  // The samples live only within the pass that resolves them.
  const VkSampleCountFlagBits samples = pipelines_.samples;
  if (samples != VK_SAMPLE_COUNT_1_BIT &&
      (!CreateDeviceImage(physical_device_, device_, kColorFormat,
           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
               VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
           VK_IMAGE_ASPECT_COLOR_BIT, width, height, color_samples_, error, 1,
           samples) ||
          !CreateDeviceImage(physical_device_, device_, kDepthFormat,
              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                  VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
              VK_IMAGE_ASPECT_DEPTH_BIT, width, height, depth_samples_, error,
              1, samples))) {
    DestroyTargets();
    return false;
  }
  ++statistics_.target_allocations;
  return true;
}

void VulkanOffscreenRenderer::DestroyTargets() {
  DestroyDeviceImage(device_, depth_samples_);
  DestroyDeviceImage(device_, color_samples_);
  DestroyHostBuffer(device_, depth_readback_);
  DestroyHostBuffer(device_, color_readback_);
  DestroyDeviceImage(device_, depth_);
  DestroyDeviceImage(device_, color_);
  width_ = 0;
  height_ = 0;
}

void VulkanOffscreenRenderer::Destroy() {
  if (device_ != VK_NULL_HANDLE) {
    // Teardown, not an ordinary frame: waiting is allowed here. A lent
    // device's other queues are its host's, so only this renderer's own
    // last frame is waited for there.
    if (owns_device_) {
      vkDeviceWaitIdle(device_);
    } else {
      std::string ignored;
      (void)WaitForCompletion(ignored);
    }
    shadow_.Destroy();
    meshes_.Destroy();
    textures_.Destroy();
    materials_.Destroy();
    DestroyTargets();
    DestroyScenePipelines(device_, pipelines_);
    vkDestroyQueryPool(device_, timestamps_, nullptr);
    vkDestroySemaphore(device_, timeline_, nullptr);
    vkDestroyCommandPool(device_, command_pool_, nullptr);
    if (owns_device_) {
      vkDestroyDevice(device_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
  }
  DestroyInstance(instance_);
}

} // namespace

std::unique_ptr<OffscreenRenderer> vulkan_internal::CreateLentOffscreenRenderer(
    const OffscreenDevice& device, const SceneShaders& shaders,
    const RenderOptions& options, FrameStatus& status, std::string& detail) {
  auto renderer = std::make_unique<VulkanOffscreenRenderer>();
  status = renderer->Initialize(device, shaders, options, detail);
  if (status != FrameStatus::Pass) {
    return nullptr;
  }
  return renderer;
}
#endif

std::unique_ptr<OffscreenRenderer> CreateOffscreenRenderer(
    const SceneShaders& shaders, FrameStatus& status, std::string& detail,
    const RenderOptions& options) {
#if !defined(TOON_HAS_VULKAN)
  (void)shaders;
  (void)options;
  status = FrameStatus::Skip;
  detail = "Vulkan backend was not compiled for this configuration";
  return nullptr;
#else
  auto renderer = std::make_unique<VulkanOffscreenRenderer>();
  OffscreenDevice device;
  status = renderer->CreateDevice(device, detail);
  if (status == FrameStatus::Pass) {
    status = renderer->Initialize(device, shaders, options, detail);
  }
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
