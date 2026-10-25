// SPDX-License-Identifier: Apache-2.0
#pragma once

// What a host that lends its Vulkan device consumes (GPU handoff policy):
// the device requirements it checks and enables before creating the device,
// the lent device, the offscreen renderer on it, and the handoff of its RGBA
// product on the GPU. No Vulkan type crosses this header: handles travel
// encoded in std::uintptr_t and enumerations as their numeric values, as in
// vulkan_present.hpp.

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

// The product ring (GPU handoff policy §4, HOST-Q1).
struct HandoffOptions {
  // Product slots: two to four. Three let two consumers hold different
  // slots and leave one to render into; two suit a single consumer.
  std::uint32_t slots = 3;
};

// One handed-off frame (GPU handoff policy §6). Consumers sample `view` in
// `layout` after waiting for `semaphore` to reach `value`, and never
// transition the image.
struct HandoffImage {
  // Resource: encoded VkImage and VkImageView, the extent, the VkFormat,
  // the slot and the ring generation, which changes when the extent does.
  std::uintptr_t image = 0;
  std::uintptr_t view = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t format = 0;
  std::uint32_t slot = 0;
  std::uint64_t generation = 0;
  // Interpretation, spelled as ColorProduct spells it: linear sRGB, linear
  // transfer, premultiplied alpha, origin at the top left. Static strings.
  const char* color_space = "linear";
  const char* transfer_function = "linear";
  const char* alpha_convention = "premultiplied";
  const char* origin = "top-left";
  // Readiness: the renderer's encoded timeline VkSemaphore and the value
  // this frame signals.
  std::uintptr_t semaphore = 0;
  std::uint64_t value = 0;
  // State: the VkImageLayout the image is in, SHADER_READ_ONLY_OPTIMAL, and
  // the queue family that wrote it, which consumers share.
  std::uint32_t layout = 0;
  std::uint32_t queue_family = 0;
  // Identity: the frame number, equal to `value`; the draw list's source
  // revision; whether a late sample replaced its fast values, and the input
  // times the frame carries.
  std::uint64_t frame = 0;
  std::uint64_t scene_revision = 0;
  bool late_sample_applied = false;
  FrameSnapshot::InputTimes inputs;
  // Timing, in the producer's SteadyNanoseconds: the render call's start
  // and the submit. The frame's GPU time is known once it completes:
  // OffscreenStatistics::gpu_frame_ms when gpu_frame equals `frame`.
  std::int64_t render_started = 0;
  std::int64_t submitted = 0;
};

enum class HandoffStatus {
  // A frame was drawn and handed off.
  Rendered,
  // Every slot is held: nothing was drawn, and the latest frame stays. The
  // host releases a slot or renders later (GPU handoff policy §5).
  NoFreeSlot,
  // The device was lost (VK_ERROR_DEVICE_LOST). The renderer does not
  // recover: the host destroys it, recreates its device and a renderer,
  // and acquires again (GPU handoff policy §7).
  DeviceLost,
  // Any other failure; the error says which.
  Failed,
};

struct HandoffStatistics {
  // Slots per generation, and the current generation.
  std::uint32_t slots = 0;
  std::uint64_t generation = 0;
  // Slots of any generation some consumer holds, and old-generation slots
  // not yet destroyed.
  std::uint32_t held = 0;
  std::uint32_t retired = 0;
  std::uint64_t frames_handed_off = 0;
  // Render calls that found every slot held.
  std::uint64_t no_free_slot = 0;
  std::uint64_t slots_created = 0;
  std::uint64_t slots_destroyed = 0;
  // Consumer release values a frame waited for on the GPU.
  std::uint64_t release_waits = 0;
  std::uint64_t captures = 0;
};

// An offscreen renderer that also hands its product off on the GPU (GPU
// handoff policy §4–§7). Render keeps reading back; RenderHandoff reads
// nothing back and waits on the CPU only for the renderer's previous
// frame, as every frame does (one frame in flight). Every call is on the
// host's thread; the host does not submit to the lent queue during one.
class HandoffRenderer : public OffscreenRenderer {
public:
  // Draws into a slot no consumer holds and hands it off; `image` describes
  // the frame. A new extent starts a new generation.
  [[nodiscard]] virtual HandoffStatus RenderHandoff(const DrawList& draws,
      std::uint32_t width, std::uint32_t height, HandoffImage& image,
      std::string& error) = 0;

  // The latest handed-off frame, for one consumer: false before the first.
  // Several consumers may acquire the same frame; each acquisition is ended
  // by one Release.
  [[nodiscard]] virtual bool Acquire(HandoffImage& image) = 0;

  // Ends one acquisition of `image`. `semaphore` is the consumer's encoded
  // timeline VkSemaphore on the lent device and `value` what it signals
  // once its last read of the image has executed; the renderer's next use
  // of the slot waits for it on the GPU. Zero means the consumer did not
  // read it. The slot is free once every acquisition is released; the host
  // releases every acquisition before destroying the renderer. The
  // semaphore stays valid until the renderer is destroyed, or until a
  // render call that starts after `value` is reached has returned; the
  // renderer's destruction waits for values not yet reached.
  [[nodiscard]] virtual bool Release(const HandoffImage& image,
      std::uintptr_t semaphore, std::uint64_t value, std::string& error) = 0;

  // The next RenderHandoff also copies its slot for the CPU; TakeCapture
  // waits for that frame and returns the copy, byte-identical to what
  // consumers sample (GPU handoff policy §7). False when no captured frame
  // is pending.
  virtual void RequestCapture() = 0;
  [[nodiscard]] virtual bool TakeCapture(ColorProduct& color,
      std::string& error) = 0;

  [[nodiscard]] virtual const HandoffStatistics& handoff_statistics() const = 0;
};

// The handoff renderer on a lent device: CreateOffscreenRenderer's checks,
// and a ring of `handoff.slots` slots, two to four.
[[nodiscard]] std::unique_ptr<HandoffRenderer> CreateHandoffRenderer(
    const LentVulkanDevice& device, const SceneShaders& shaders,
    FrameStatus& status, std::string& detail, const RenderOptions& options = {},
    const HandoffOptions& handoff = {});

} // namespace Toon
