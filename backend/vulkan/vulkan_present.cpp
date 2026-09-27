// SPDX-License-Identifier: Apache-2.0
// Swapchain presentation of a scene's DrawList. The backend owns the
// VkSurfaceKHR and every swapchain object; the window layer only supplies the
// surface-creation callback and the platform instance extensions. One frame
// in flight, tracked by a timeline semaphore; the scene pipelines use dynamic
// rendering, so a resize rebuilds only the swapchain and its depth image.
#include <toon/vulkan_present.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(TOON_HAS_VULKAN)
#include <vulkan/vulkan.h>

#include "vulkan_internal.hpp"
#include "vulkan_scene.hpp"
#endif

namespace Toon {

#if !defined(TOON_HAS_VULKAN)

std::unique_ptr<PresentSession> CreatePresentSession(
    const PresentSurfaceProvider& surface, const SceneShaders& shaders,
    bool vsync, PresentSetupStatus& status, std::string& error) {
  (void)surface;
  (void)shaders;
  (void)vsync;
  status = PresentSetupStatus::Unavailable;
  error = "Vulkan backend was not compiled for this configuration";
  return nullptr;
}

#else

namespace {

using vulkan_internal::BeginSceneRendering;
using vulkan_internal::CreateDeviceImage;
using vulkan_internal::CreateInstanceWithValidation;
using vulkan_internal::CreateScenePipelines;
using vulkan_internal::DestroyDeviceImage;
using vulkan_internal::DestroyInstance;
using vulkan_internal::DestroyScenePipelines;
using vulkan_internal::DeviceImage;
using vulkan_internal::ImageBarrier;
using vulkan_internal::InstanceState;
using vulkan_internal::LoadSceneShaders;
using vulkan_internal::MaterialCache;
using vulkan_internal::MeshCache;
using vulkan_internal::SceneDeviceFeatures;
using vulkan_internal::SceneShaderWords;
using vulkan_internal::ScenePipelines;
using vulkan_internal::SupportsSceneFeatures;
using vulkan_internal::ValidationState;
using vulkan_internal::VulkanOk;

constexpr std::uint64_t kFrameTimeoutNs = 10'000'000'000ULL;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

bool SupportsDepthAttachment(VkPhysicalDevice device) {
  VkFormatProperties properties{};
  vkGetPhysicalDeviceFormatProperties(device, kDepthFormat, &properties);
  return (properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
}

template <typename Handle>
std::uintptr_t EncodeHandle(Handle handle) noexcept {
  if constexpr (std::is_pointer_v<Handle>) {
    return reinterpret_cast<std::uintptr_t>(handle);
  } else {
    return static_cast<std::uintptr_t>(handle);
  }
}

template <typename Handle>
Handle DecodeHandle(std::uintptr_t handle) noexcept {
  if constexpr (std::is_pointer_v<Handle>) {
    return reinterpret_cast<Handle>(handle);
  } else {
    return static_cast<Handle>(handle);
  }
}

bool HasDeviceExtension(VkPhysicalDevice device, const char* name) {
  std::uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count,
      extensions.data());
  for (const VkExtensionProperties& extension : extensions) {
    if (std::string_view(extension.extensionName) == name) {
      return true;
    }
  }
  return false;
}

std::optional<std::uint32_t> FindGraphicsPresentQueue(VkPhysicalDevice device,
    VkSurfaceKHR surface) {
  std::uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> properties(count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
  for (std::uint32_t index = 0; index < count; ++index) {
    if (properties[index].queueCount == 0 ||
        (properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
      continue;
    }
    VkBool32 present_supported = VK_FALSE;
    if (vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface,
            &present_supported) ==
            VK_SUCCESS &&
        present_supported == VK_TRUE) {
      return index;
    }
  }
  return std::nullopt;
}

class VulkanPresentSession final : public PresentSession {
public:
  ~VulkanPresentSession() override {
    Destroy();
  }

  PresentSetupStatus Initialize(const PresentSurfaceProvider& provider,
      const SceneShaders& shaders, bool vsync, std::string& error);

  [[nodiscard]] bool RenderFrame(const DrawList& draws, std::uint32_t width,
      std::uint32_t height, bool& presented,
      std::string& error) override;

  [[nodiscard]] const PresentStatistics& statistics() const override {
    return statistics_;
  }

private:
  bool RecreateSwapchain(std::uint32_t width, std::uint32_t height,
      std::string& error);
  void DestroySwapchainObjects();
  bool WaitForCompletion(std::string& error);
  void Destroy();

  InstanceState instance_;
  ValidationState validation_;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  std::uint32_t queue_family_ = 0;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkCommandBuffer command_ = VK_NULL_HANDLE;
  VkSurfaceFormatKHR surface_format_{};
  VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
  ScenePipelines pipelines_;
  MaterialCache materials_;
  MeshCache meshes_;
  VkSemaphore image_available_ = VK_NULL_HANDLE;
  // Frame N signals value N; one frame is in flight.
  VkSemaphore timeline_ = VK_NULL_HANDLE;
  std::uint64_t submitted_ = 0;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkExtent2D extent_{};
  std::vector<VkImage> images_;
  std::vector<VkImageView> views_;
  DeviceImage depth_;
  // Present-wait semaphores are per swapchain image: a single semaphore may
  // still be in use by an outstanding present when the next frame needs it.
  std::vector<VkSemaphore> render_finished_;
  bool swapchain_created_once_ = false;
  PresentStatistics statistics_;
};

PresentSetupStatus VulkanPresentSession::Initialize(
    const PresentSurfaceProvider& provider, const SceneShaders& shaders,
    bool vsync, std::string& error) {
  if (provider.create_surface == nullptr) {
    error = "the surface provider carries no create_surface callback";
    return PresentSetupStatus::Error;
  }
  SceneShaderWords words;
  if (!LoadSceneShaders(shaders, words, error)) {
    return PresentSetupStatus::Error;
  }

  std::vector<const char*> instance_extensions;
  instance_extensions.reserve(provider.instance_extensions.size());
  for (const std::string& extension : provider.instance_extensions) {
    instance_extensions.push_back(extension.c_str());
  }
  if (!CreateInstanceWithValidation("toon-viewport", instance_extensions,
          &validation_, instance_, error)) {
    return PresentSetupStatus::Unavailable;
  }

  std::uintptr_t encoded_surface = 0;
  const auto surface_result = static_cast<VkResult>(provider.create_surface(
      provider.user_data, EncodeHandle(instance_.instance), &encoded_surface));
  if (surface_result != VK_SUCCESS || encoded_surface == 0) {
    error = "the window layer could not create a presentation surface "
            "(VkResult " +
            std::to_string(static_cast<std::int32_t>(surface_result)) + ")";
    return PresentSetupStatus::Unavailable;
  }
  surface_ = DecodeHandle<VkSurfaceKHR>(encoded_surface);

  std::uint32_t physical_count = 0;
  if (!VulkanOk(vkEnumeratePhysicalDevices(instance_.instance, &physical_count,
                    nullptr),
          "vkEnumeratePhysicalDevices", error)) {
    return PresentSetupStatus::Error;
  }
  if (physical_count == 0) {
    error = "no Vulkan physical device is available";
    return PresentSetupStatus::Unavailable;
  }
  std::vector<VkPhysicalDevice> physical_devices(physical_count);
  vkEnumeratePhysicalDevices(instance_.instance, &physical_count,
      physical_devices.data());
  std::optional<std::uint32_t> queue_family;
  std::string rejection;
  for (VkPhysicalDevice physical : physical_devices) {
    if (!HasDeviceExtension(physical, VK_KHR_SWAPCHAIN_EXTENSION_NAME) ||
        !SupportsSceneFeatures(physical, rejection) ||
        !SupportsDepthAttachment(physical)) {
      continue;
    }
    const auto candidate = FindGraphicsPresentQueue(physical, surface_);
    if (candidate) {
      physical_device_ = physical;
      queue_family = candidate;
      break;
    }
  }
  if (!queue_family) {
    error = "no Vulkan 1.3 device offers a graphics+present queue, the "
            "swapchain extension, a D32 depth attachment, dynamic rendering, "
            "synchronization2 and timeline semaphores for this surface";
    if (!rejection.empty()) {
      error += " (" + rejection + ")";
    }
    return PresentSetupStatus::Unavailable;
  }
  queue_family_ = *queue_family;

  std::uint32_t format_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_,
      &format_count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_,
      &format_count, formats.data());
  if (formats.empty()) {
    error = "the presentation surface reports no color formats";
    return PresentSetupStatus::Unavailable;
  }
  surface_format_ = formats.front();
  for (const VkSurfaceFormatKHR& format : formats) {
    if (format.format == VK_FORMAT_B8G8R8A8_UNORM ||
        format.format == VK_FORMAT_R8G8B8A8_UNORM) {
      surface_format_ = format;
      break;
    }
  }

  present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
  if (!vsync) {
    std::uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_,
        &mode_count, nullptr);
    std::vector<VkPresentModeKHR> modes(mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_,
        &mode_count, modes.data());
    for (const VkPresentModeKHR preferred :
        {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR}) {
      if (std::find(modes.begin(), modes.end(), preferred) != modes.end()) {
        present_mode_ = preferred;
        break;
      }
    }
  }

  const float priority = 1.0F;
  VkDeviceQueueCreateInfo queue_create{
      VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queue_create.queueFamilyIndex = queue_family_;
  queue_create.queueCount = 1;
  queue_create.pQueuePriorities = &priority;
  SceneDeviceFeatures features;
  const char* swapchain_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  VkDeviceCreateInfo device_create{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  device_create.pNext = features.EnableRequired();
  device_create.queueCreateInfoCount = 1;
  device_create.pQueueCreateInfos = &queue_create;
  device_create.enabledExtensionCount = 1;
  device_create.ppEnabledExtensionNames = &swapchain_extension;
  if (!VulkanOk(vkCreateDevice(physical_device_, &device_create, nullptr,
                    &device_),
          "vkCreateDevice", error)) {
    return PresentSetupStatus::Error;
  }
  vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
  meshes_.Initialize(physical_device_, device_);

  VkCommandPoolCreateInfo pool_create{
      VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_create.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_create.queueFamilyIndex = queue_family_;
  if (!VulkanOk(vkCreateCommandPool(device_, &pool_create, nullptr,
                    &command_pool_),
          "vkCreateCommandPool", error)) {
    return PresentSetupStatus::Error;
  }
  VkCommandBufferAllocateInfo command_allocate{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  command_allocate.commandPool = command_pool_;
  command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_allocate.commandBufferCount = 1;
  if (!VulkanOk(vkAllocateCommandBuffers(device_, &command_allocate,
                    &command_),
          "vkAllocateCommandBuffers", error)) {
    return PresentSetupStatus::Error;
  }

  if (!CreateScenePipelines(device_, words, surface_format_.format,
          kDepthFormat, pipelines_, error) ||
      !materials_.Initialize(physical_device_, device_,
          pipelines_.material_layout, error)) {
    return PresentSetupStatus::Error;
  }

  VkSemaphoreCreateInfo semaphore_create{
      VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphoreTypeCreateInfo timeline_type{
      VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
  timeline_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  VkSemaphoreCreateInfo timeline_create{
      VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  timeline_create.pNext = &timeline_type;
  if (!VulkanOk(vkCreateSemaphore(device_, &semaphore_create, nullptr,
                    &image_available_),
          "vkCreateSemaphore", error) ||
      !VulkanOk(vkCreateSemaphore(device_, &timeline_create, nullptr,
                    &timeline_),
          "vkCreateSemaphore(timeline)", error)) {
    return PresentSetupStatus::Error;
  }

  VkPhysicalDeviceProperties device_properties{};
  vkGetPhysicalDeviceProperties(physical_device_, &device_properties);
  statistics_.device_name = device_properties.deviceName;
  statistics_.validation_available = instance_.validation_available;
  statistics_.validation_detail = instance_.validation_detail;
  return PresentSetupStatus::Ready;
}

bool VulkanPresentSession::RecreateSwapchain(std::uint32_t width,
    std::uint32_t height,
    std::string& error) {
  // Swapchain recreation is not an ordinary frame: waiting for the device
  // is allowed here (design policy §19).
  vkDeviceWaitIdle(device_);
  DestroySwapchainObjects();

  VkSurfaceCapabilitiesKHR capabilities{};
  if (!VulkanOk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                    physical_device_, surface_, &capabilities),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", error)) {
    return false;
  }
  VkExtent2D extent = capabilities.currentExtent;
  if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
    extent.width = std::clamp(width, capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width);
    extent.height = std::clamp(height, capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height);
  }
  if (extent.width == 0 || extent.height == 0) {
    // Minimized between the caller's size query and now; skip this frame.
    extent_ = {0, 0};
    return true;
  }

  std::uint32_t image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0) {
    image_count = std::min(image_count, capabilities.maxImageCount);
  }
  VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  if ((capabilities.supportedCompositeAlpha & composite) == 0) {
    for (const VkCompositeAlphaFlagBitsKHR candidate :
        {VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
      if ((capabilities.supportedCompositeAlpha & candidate) != 0) {
        composite = candidate;
        break;
      }
    }
  }

  VkSwapchainCreateInfoKHR swapchain_create{
      VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  swapchain_create.surface = surface_;
  swapchain_create.minImageCount = image_count;
  swapchain_create.imageFormat = surface_format_.format;
  swapchain_create.imageColorSpace = surface_format_.colorSpace;
  swapchain_create.imageExtent = extent;
  swapchain_create.imageArrayLayers = 1;
  swapchain_create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  swapchain_create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchain_create.preTransform = capabilities.currentTransform;
  swapchain_create.compositeAlpha = composite;
  swapchain_create.presentMode = present_mode_;
  swapchain_create.clipped = VK_TRUE;
  if (!VulkanOk(vkCreateSwapchainKHR(device_, &swapchain_create, nullptr,
                    &swapchain_),
          "vkCreateSwapchainKHR", error)) {
    return false;
  }

  std::uint32_t count = 0;
  vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
  images_.resize(count);
  vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data());
  views_.resize(count, VK_NULL_HANDLE);
  render_finished_.resize(count, VK_NULL_HANDLE);
  for (std::uint32_t index = 0; index < count; ++index) {
    VkImageViewCreateInfo view_create{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_create.image = images_[index];
    view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_create.format = surface_format_.format;
    view_create.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_create.subresourceRange.levelCount = 1;
    view_create.subresourceRange.layerCount = 1;
    if (!VulkanOk(vkCreateImageView(device_, &view_create, nullptr,
                      &views_[index]),
            "vkCreateImageView(swapchain)", error)) {
      return false;
    }
    VkSemaphoreCreateInfo semaphore_create{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (!VulkanOk(vkCreateSemaphore(device_, &semaphore_create, nullptr,
                      &render_finished_[index]),
            "vkCreateSemaphore(present)", error)) {
      return false;
    }
  }

  if (!CreateDeviceImage(physical_device_, device_, kDepthFormat,
          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
          extent.width, extent.height, depth_, error)) {
    return false;
  }

  extent_ = extent;
  if (swapchain_created_once_) {
    ++statistics_.swapchain_recreates;
  }
  swapchain_created_once_ = true;
  return true;
}

bool VulkanPresentSession::RenderFrame(const DrawList& draws,
    std::uint32_t width,
    std::uint32_t height, bool& presented,
    std::string& error) {
  presented = false;
  if (width == 0 || height == 0) {
    return true;
  }
  if (swapchain_ == VK_NULL_HANDLE || width != extent_.width ||
      height != extent_.height) {
    if (!RecreateSwapchain(width, height, error)) {
      return false;
    }
    if (extent_.width == 0 || extent_.height == 0) {
      return true;
    }
  }

  if (!WaitForCompletion(error)) {
    return false;
  }
  std::uint32_t image_index = 0;
  const VkResult acquire =
      vkAcquireNextImageKHR(device_, swapchain_, kFrameTimeoutNs,
          image_available_, VK_NULL_HANDLE, &image_index);
  if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
    return RecreateSwapchain(width, height, error);
  }
  if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
    return VulkanOk(acquire, "vkAcquireNextImageKHR", error);
  }
  if (!meshes_.Update(draws, error) || !materials_.Update(draws, error)) {
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
  // The colour transition waits at the stage the acquire semaphore gates.
  const VkImageMemoryBarrier2 to_attachment[] = {
      ImageBarrier(images_[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
      ImageBarrier(depth_.image, VK_IMAGE_ASPECT_DEPTH_BIT,
          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
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
  BeginSceneRendering(command_, views_[image_index], depth_.view, extent_);
  meshes_.Record(command_, pipelines_, materials_, draws);
  vkCmdEndRendering(command_);
  const VkImageMemoryBarrier2 to_present = ImageBarrier(images_[image_index],
      VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE,
      VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &to_present;
  vkCmdPipelineBarrier2(command_, &dependency);
  if (!VulkanOk(vkEndCommandBuffer(command_), "vkEndCommandBuffer", error)) {
    return false;
  }

  VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  wait.semaphore = image_available_;
  wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSemaphoreSubmitInfo signals[2]{};
  signals[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  signals[0].semaphore = render_finished_[image_index];
  signals[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  signals[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  signals[1].semaphore = timeline_;
  signals[1].value = submitted_ + 1;
  signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  VkCommandBufferSubmitInfo command_submit{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  command_submit.commandBuffer = command_;
  VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit.waitSemaphoreInfoCount = 1;
  submit.pWaitSemaphoreInfos = &wait;
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &command_submit;
  submit.signalSemaphoreInfoCount = 2;
  submit.pSignalSemaphoreInfos = signals;
  if (!VulkanOk(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE),
          "vkQueueSubmit2", error)) {
    return false;
  }
  ++submitted_;

  VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &render_finished_[image_index];
  present.swapchainCount = 1;
  present.pSwapchains = &swapchain_;
  present.pImageIndices = &image_index;
  const VkResult present_result = vkQueuePresentKHR(queue_, &present);
  if (present_result == VK_ERROR_OUT_OF_DATE_KHR ||
      present_result == VK_SUBOPTIMAL_KHR) {
    if (present_result == VK_SUBOPTIMAL_KHR) {
      ++statistics_.frames_presented;
      presented = true;
    }
    if (!RecreateSwapchain(width, height, error)) {
      return false;
    }
  } else if (!VulkanOk(present_result, "vkQueuePresentKHR", error)) {
    return false;
  } else {
    ++statistics_.frames_presented;
    presented = true;
  }
  statistics_.validation_message_count = validation_.message_count;
  if (!validation_.first_message.empty()) {
    statistics_.validation_detail = validation_.first_message;
  }
  return true;
}

bool VulkanPresentSession::WaitForCompletion(std::string& error) {
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

void VulkanPresentSession::DestroySwapchainObjects() {
  for (VkSemaphore semaphore : render_finished_) {
    vkDestroySemaphore(device_, semaphore, nullptr);
  }
  render_finished_.clear();
  DestroyDeviceImage(device_, depth_);
  for (VkImageView view : views_) {
    vkDestroyImageView(device_, view, nullptr);
  }
  views_.clear();
  images_.clear();
  if (swapchain_ != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
  extent_ = {0, 0};
}

void VulkanPresentSession::Destroy() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
    DestroySwapchainObjects();
    meshes_.Destroy();
    materials_.Destroy();
    vkDestroySemaphore(device_, timeline_, nullptr);
    vkDestroySemaphore(device_, image_available_, nullptr);
    DestroyScenePipelines(device_, pipelines_);
    vkDestroyCommandPool(device_, command_pool_, nullptr);
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
  if (surface_ != VK_NULL_HANDLE && instance_.instance != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance_.instance, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }
  DestroyInstance(instance_);
}

} // namespace

std::unique_ptr<PresentSession> CreatePresentSession(
    const PresentSurfaceProvider& surface, const SceneShaders& shaders,
    bool vsync, PresentSetupStatus& status, std::string& error) {
  auto session = std::make_unique<VulkanPresentSession>();
  status = session->Initialize(surface, shaders, vsync, error);
  if (status != PresentSetupStatus::Ready) {
    return nullptr;
  }
  return session;
}

#endif

} // namespace Toon
