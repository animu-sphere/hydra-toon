// SPDX-License-Identifier: Apache-2.0
// The GPU handoff of the Vulkan backend (GPU handoff policy): the device
// requirements a host enables, and the offscreen renderer on the device it
// lends, which this file checks against them before the renderer draws on it.
#include <toon/vulkan_handoff.hpp>

#include <vector>

#if defined(TOON_HAS_VULKAN)
#include <vulkan/vulkan.h>

#include "vulkan_scene.hpp"
#endif

namespace Toon {

VulkanDeviceRequirements VulkanHandoffRequirements() {
  VulkanDeviceRequirements requirements;
#if defined(TOON_HAS_VULKAN)
  requirements.api_version = VK_API_VERSION_1_3;
  requirements.queue_flags = VK_QUEUE_GRAPHICS_BIT;
  requirements.required.timeline_semaphore = true;
  requirements.required.dynamic_rendering = true;
  requirements.required.synchronization2 = true;
  requirements.required.shader_sampled_image_array_dynamic_indexing = true;
  requirements.optional.alpha_to_one = true;
  requirements.color_format = vulkan_internal::kOffscreenColorFormat;
  requirements.depth_format = vulkan_internal::kOffscreenDepthFormat;
  requirements.shadow_format = vulkan_internal::kShadowFormat;
#endif
  return requirements;
}

#if defined(TOON_HAS_VULKAN)
namespace {

// Names every required feature the host did not enable, or nothing.
std::string MissingFeatures(const VulkanDeviceFeatures& enabled) {
  std::string missing;
  const auto require = [&](bool on, const char* name) {
    if (!on) {
      missing += missing.empty() ? name : std::string(", ") + name;
    }
  };
  require(enabled.timeline_semaphore, "timelineSemaphore");
  require(enabled.dynamic_rendering, "dynamicRendering");
  require(enabled.synchronization2, "synchronization2");
  require(enabled.shader_sampled_image_array_dynamic_indexing,
      "shaderSampledImageArrayDynamicIndexing");
  return missing;
}

// Whether the lent device meets the requirements; otherwise `detail` names
// the first thing it lacks.
bool CheckLentDevice(const LentVulkanDevice& lent, std::string& detail) {
  if (lent.instance == 0 || lent.physical_device == 0 || lent.device == 0 ||
      lent.queue == 0) {
    detail = "the lent device needs its instance, physical device, device "
             "and queue";
    return false;
  }
  if (lent.api_version < VK_API_VERSION_1_3) {
    detail = "the lent device's instance was created below Vulkan 1.3";
    return false;
  }
  const auto physical = reinterpret_cast<VkPhysicalDevice>(lent.physical_device);
  if (!vulkan_internal::SupportsSceneFeatures(physical, detail)) {
    return false;
  }
  if (!vulkan_internal::SupportsOffscreenTargets(physical)) {
    detail = "the lent device cannot draw and copy RGBA8 and depth32 targets";
    return false;
  }
  std::uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
  if (lent.queue_family >= count ||
      (families[lent.queue_family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
    detail = "the lent queue's family " + std::to_string(lent.queue_family) +
             " has no graphics queue";
    return false;
  }
  const std::string missing = MissingFeatures(lent.enabled);
  if (!missing.empty()) {
    detail = "the lent device does not enable " + missing;
    return false;
  }
  return true;
}

} // namespace
#endif

std::unique_ptr<OffscreenRenderer> CreateOffscreenRenderer(
    const LentVulkanDevice& device, const SceneShaders& shaders,
    FrameStatus& status, std::string& detail, const RenderOptions& options) {
#if !defined(TOON_HAS_VULKAN)
  (void)device;
  (void)shaders;
  (void)options;
  status = FrameStatus::Skip;
  detail = "Vulkan backend was not compiled for this configuration";
  return nullptr;
#else
  if (!CheckLentDevice(device, detail)) {
    status = FrameStatus::Fail;
    return nullptr;
  }
  const auto physical = reinterpret_cast<VkPhysicalDevice>(device.physical_device);
  VkPhysicalDeviceFeatures supported{};
  vkGetPhysicalDeviceFeatures(physical, &supported);
  vulkan_internal::OffscreenDevice lent;
  lent.physical_device = physical;
  lent.device = reinterpret_cast<VkDevice>(device.device);
  lent.queue = reinterpret_cast<VkQueue>(device.queue);
  lent.queue_family = device.queue_family;
  lent.alpha_to_one = device.enabled.alpha_to_one && supported.alphaToOne == VK_TRUE;
  return vulkan_internal::CreateLentOffscreenRenderer(lent, shaders, options,
      status, detail);
#endif
}

} // namespace Toon
