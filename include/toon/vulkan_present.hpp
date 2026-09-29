// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <toon/extraction.hpp>
#include <toon/overlay.hpp>
#include <toon/vulkan_backend.hpp>

namespace Toon {

// The window layer supplies the native pieces the backend needs to own a
// VkSurfaceKHR, without Vulkan or windowing types crossing this header.
// Handles travel encoded in std::uintptr_t: `create_surface` receives the
// encoded VkInstance, writes the encoded VkSurfaceKHR, and returns the raw
// VkResult as an integer.
struct PresentSurfaceProvider {
  std::vector<std::string> instance_extensions;
  std::int32_t (*create_surface)(void* user_data, std::uintptr_t instance,
      std::uintptr_t* surface) = nullptr;
  void* user_data = nullptr;
};

// Why session creation returned no session: `Unavailable` means this
// environment cannot present (no loader/device/present queue) and callers
// should report an explicit skip; `Error` is a real failure.
enum class PresentSetupStatus {
  Ready,
  Unavailable,
  Error,
};

// The GPU's time for each part of one frame, in milliseconds, between
// timestamps the frame wrote as it ran.
struct PresentGpuTimes {
  // Textures copied and mipmapped before the scene.
  double uploads = 0.0;
  // Waiting for the swapchain image the frame draws into to be released by
  // the presentation engine: long under vsync, not the renderer's work.
  double image_wait = 0.0;
  // The scene pass in the order it draws: unlit meshes, opaque MToon's
  // outline hulls, its surfaces, and the transparent draws with their hulls
  // (design policy §24). `resolve` ends the pass, averaging the samples into
  // the swapchain image when multisampled.
  double unlit = 0.0;
  double outline = 0.0;
  double opaque = 0.0;
  double transparent = 0.0;
  double resolve = 0.0;
  // The copy a capture reads back; 0 in a frame without one.
  double capture = 0.0;
  double overlay = 0.0;
  // From the first timestamp to the last, the image wait included.
  double frame = 0.0;
};

// What one frame recorded.
struct PresentDrawCounts {
  // Draw calls in each of the scene pass's parts, as PresentGpuTimes names
  // them; a double-sided transparent surface is two.
  std::uint32_t unlit = 0;
  std::uint32_t outline = 0;
  std::uint32_t opaque = 0;
  std::uint32_t transparent = 0;
  std::uint64_t triangles = 0;
  std::uint32_t pipeline_binds = 0;
  std::uint32_t overlay = 0;
  std::uint32_t overlay_vertices = 0;
};

struct PresentStatistics {
  std::uint64_t frames_presented = 0;
  std::uint32_t swapchain_recreates = 0;
  // Samples per pixel: RenderOptions' count, or the last SetSamples, as far
  // as the device offers it.
  std::uint32_t samples = 1;
  // Times SetSamples changed the count: each rebuilt the four scene
  // pipelines and the multisampled targets, and uploaded nothing.
  std::uint32_t sample_changes = 0;
  // Whether the swapchain encodes the pipelines' linear colour to sRGB as it
  // is written; false only on a surface that offers no 8-bit sRGB format.
  bool srgb_encoded = false;
  // Frames read back to the CPU: one per capture taken, none otherwise.
  std::uint64_t readbacks = 0;
  // What reached the GPU, as OffscreenStatistics counts it: geometry,
  // material slots, textures, skins and poses, each only when it changed.
  std::uint64_t topology_uploads = 0;
  std::uint64_t point_uploads = 0;
  std::uint64_t material_writes = 0;
  std::uint64_t texture_uploads = 0;
  std::uint64_t skin_uploads = 0;
  std::uint64_t pose_writes = 0;
  // Overlay textures uploaded: one per new texture or pixel change.
  std::uint64_t overlay_texture_uploads = 0;
  // The last frame RenderFrame recorded.
  PresentDrawCounts draws;
  // The CPU's time in the last RenderFrame that presented, in milliseconds:
  // waiting for the frame in flight and for a swapchain image, then the rest
  // (a sample change, uploads, recording, submitting and presenting).
  double cpu_wait = 0.0;
  double cpu_submit = 0.0;
  // Whether the queue writes timestamps; without them `gpu` stays zero.
  bool gpu_timing = false;
  // The GPU times of the latest frame known to have completed, and that
  // frame's number, counting every frame submitted from 1. One frame is in
  // flight, so they arrive with the frame after it.
  PresentGpuTimes gpu;
  std::uint64_t gpu_frame = 0;
  bool validation_available = false;
  std::uint32_t validation_message_count = 0;
  std::string validation_detail;
  std::string device_name;
};

// One swapchain presentation session drawing a scene's DrawList, through the
// same mesh pipeline as the offscreen renderer, and an OverlayDrawList over
// it. One frame in flight, FIFO present mode when vsync is on, IMMEDIATE
// (when available) otherwise.
class PresentSession {
public:
  virtual ~PresentSession() = default;

  // Render and present one frame at the window's current framebuffer extent,
  // `overlay` drawn over the scene after any capture is copied. A zero
  // extent (minimized window) is not an error: the frame is skipped and
  // `presented` reports false. Swapchain recreation on resize or out-of-date
  // presentation is handled internally.
  [[nodiscard]] virtual bool RenderFrame(const DrawList& draws,
      const OverlayDrawList& overlay, std::uint32_t width,
      std::uint32_t height, bool& presented,
      std::string& error) = 0;

  // Draws from the next RenderFrame on at the device's highest sample count
  // at or below `requested`; 1 turns anti-aliasing off. A count that differs
  // from the current one waits for the frame in flight and rebuilds the
  // scene pipelines and the multisampled targets, keeping every mesh,
  // material and texture on the GPU.
  virtual void SetSamples(std::uint32_t requested) = 0;

  // Asks the next frame RenderFrame presents to be copied back to the CPU as
  // well: a screenshot, never an ordinary frame (design policy §31).
  virtual void RequestCapture() = 0;

  // The captured frame's colour as presented, RGBA8 with its origin at the
  // top left, waiting for that frame to complete. False with `error` empty
  // while no captured frame is waiting; false with `error` set on a failure.
  [[nodiscard]] virtual bool TakeCapture(ColorProduct& color,
      std::string& error) = 0;

  [[nodiscard]] virtual const PresentStatistics& statistics() const = 0;
};

// Creates the presentation session, enabling Vulkan validation capture
// whenever the loader offers it (same policy as RenderOffscreen). Returns
// nullptr with `status`/`error` describing why: the core-only configuration
// and missing device capability report Unavailable, real failures Error.
// Shader paths are explicit, as in RenderOffscreen.
[[nodiscard]] std::unique_ptr<PresentSession> CreatePresentSession(
    const PresentSurfaceProvider& surface, const SceneShaders& shaders,
    bool vsync, PresentSetupStatus& status, std::string& error,
    const RenderOptions& options = {});

} // namespace Toon
