// SPDX-License-Identifier: Apache-2.0
#pragma once

// What a host that lends its Vulkan device consumes (GPU handoff policy):
// the device requirements it checks and enables before creating the device,
// the lent device, and the offscreen renderer on it. No Vulkan type crosses
// this header: handles travel encoded in std::uintptr_t and enumerations as
// their numeric values, as in vulkan_present.hpp.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <toon/backend.hpp>
#include <toon/vulkan_backend.hpp>

namespace Toon {

// The device features the renderer draws with. As requirements, the ones a
// host must enable; as a lent device, the ones the host says it enabled,
// which Vulkan cannot report back from a created device.
struct VulkanDeviceFeatures {
  // VkPhysicalDeviceVulkan12Features::timelineSemaphore.
  bool timeline_semaphore = false;
  // VkPhysicalDeviceVulkan13Features::dynamicRendering and synchronization2.
  bool dynamic_rendering = false;
  bool synchronization2 = false;
  // VkPhysicalDeviceFeatures::shaderSampledImageArrayDynamicIndexing.
  bool shader_sampled_image_array_dynamic_indexing = false;
  // VkPhysicalDeviceFeatures::alphaToOne, optional: without it, transparent
  // MSAA fails (OffscreenRenderer::SetBackground).
  bool alpha_to_one = false;
};

// What the renderer needs of a device before the host creates it (GPU
// handoff policy §2). A lent device missing any of it is refused with the
// missing item named.
struct VulkanDeviceRequirements {
  // VK_API_VERSION_1_3: the least instance apiVersion and device version.
  std::uint32_t api_version = 0;
  // A queue family with VK_QUEUE_GRAPHICS_BIT.
  std::uint32_t queue_flags = 0;
  VulkanDeviceFeatures required;
  VulkanDeviceFeatures optional;
  // VkFormat values: the colour product and depth, drawn and copied with
  // optimal tiling, and the shadow map, drawn and sampled.
  std::uint32_t color_format = 0;
  std::uint32_t depth_format = 0;
  std::uint32_t shadow_format = 0;
  // Device extensions the renderer needs: none. It enables none itself.
  std::vector<std::string> device_extensions;
};

[[nodiscard]] VulkanDeviceRequirements VulkanHandoffRequirements();

// A device the host created and lends, encoded. The host owns the instance,
// the device and the queue, and destroys them only after the renderer. It
// created them through the Vulkan loader the renderer links, and does not
// submit to `queue` while a render call runs (GPU handoff policy §2).
struct LentVulkanDevice {
  std::uintptr_t instance = 0;
  std::uintptr_t physical_device = 0;
  std::uintptr_t device = 0;
  std::uintptr_t queue = 0;
  std::uint32_t queue_family = 0;
  // The apiVersion the host created its instance with.
  std::uint32_t api_version = 0;
  // The features the host enabled when it created the device.
  VulkanDeviceFeatures enabled;
};

// The offscreen renderer on a lent device: the same renderer, products and
// statistics as on the device CreateOffscreenRenderer creates. Returns
// nullptr with `status` Fail and `detail` naming what the device lacks, or
// Skip when the Vulkan backend was not compiled. Validation messages go to
// the host's instance, so the statistics carry none.
[[nodiscard]] std::unique_ptr<OffscreenRenderer> CreateOffscreenRenderer(
    const LentVulkanDevice& device, const SceneShaders& shaders,
    FrameStatus& status, std::string& detail, const RenderOptions& options = {});

} // namespace Toon
