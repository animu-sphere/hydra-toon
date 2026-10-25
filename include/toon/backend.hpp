// SPDX-License-Identifier: Apache-2.0
#pragma once

// The backend-neutral public contract every GPU backend implements: the
// offscreen renderer, the presentation session, their products, options and
// statistics. Each backend's own header declares its factories and the
// native inputs only it needs (vulkan_backend.hpp, vulkan_present.hpp).

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <toon/extraction.hpp>
#include <toon/overlay.hpp>

namespace Toon {

// Called after GPU/acquire waits and structural uploads, before fast buffer
// writes and command recording. Read only evaluated values here: no scene
// sync, USD authoring or structural work. False with an empty error selects
// the ordinary extracted frame; false with an error fails the frame. The host
// serializes access and returns its current sample even when unchanged.
// Context must outlive its registration. Returning a snapshot replaces every
// fast value, with the usual per-resource revisions; immutable structural
// arrays must be shared with the extracted frame. Clear with an empty source.
struct LateFrameSource {
  bool (*read)(void* context, FrameSnapshot& latest, std::string& error) = nullptr;
  void* context = nullptr;
};

struct FrameLatency {
  std::uint64_t frame = 0;
  FrameSnapshot::InputTimes inputs;
  std::int64_t latched = 0;
  std::int64_t buffers_written = 0;
  std::int64_t submitted = 0;
  // Return from the presentation API, not scanout or photon time. Zero in
  // offscreen rendering. Display latency requires separate instrumentation.
  std::int64_t present_returned = 0;
  bool late_sample_applied = false;
  double pose_write_ms = 0;
  double morph_write_ms = 0;
  double material_write_ms = 0;
  // The core's fit of the shared shadow views (ShadowFit), on the CPU.
  double shadow_fit_ms = 0;
};

// The host and renderer use the same monotonic clock. A producer clock is
// mapped explicitly by the host; zero always represents an absent input.
[[nodiscard]] inline std::int64_t SteadyNanoseconds() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct BackendCapability {
  bool available = false;
  std::string detail;
};

// How a renderer rasterizes: fixed for an offscreen renderer's life; a
// present session starts with it and can change the count
// (PresentSession::SetSamples).
struct RenderOptions {
  // Multisample anti-aliasing: samples per pixel, resolved to one before a
  // frame's products are read or presented. The device's highest count at
  // or below this is used; 1 turns it off.
  std::uint32_t samples = 4;
};

enum class FrameStatus {
  Pass,
  Fail,
  Skip,
};

enum class BackgroundMode { Opaque, Transparent };

struct BackgroundSettings {
  BackgroundMode mode = BackgroundMode::Opaque;
  // Linear RGB in [0,1]. Opaque clears alpha to one; Transparent ignores
  // this colour and clears all four channels to zero. This is independent
  // of scene lighting and of a consumer's compositing background.
  Float3 color{0.05F, 0.10F, 0.15F};
};

struct ColorProduct {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t row_pitch = 0;
  std::string pixel_format;
  std::string origin;
  std::string color_space;
  std::vector<std::uint8_t> payload;
  // Offscreen: linear transfer, premultiplied RGBA in the scene's linear
  // sRGB working space. UNORM clamps each channel to [0,1]; no tone mapping.
  // Present capture: the swapchain transfer and opaque alpha.
  std::string transfer_function;
  std::string alpha_convention;
};

struct DepthProduct {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t row_pitch = 0;
  std::string pixel_format;
  std::string origin;
  std::vector<float> payload;
};

// What an offscreen renderer has done over its lifetime. The creation and
// upload counters are the evidence that a steady frame rebuilds nothing.
struct OffscreenStatistics {
  FrameLatency latency;
  std::uint64_t late_samples_applied = 0;
  std::uint64_t late_samples_rejected = 0;
  std::string late_rejection;
  // Hull draw calls in the last frame, after width and frustum omission.
  std::uint32_t outline_draws = 0;
  // Draws of MMD casters into the shared shadow views in the last frame,
  // MMD's self-shadow map: 0 when no view was drawn.
  std::uint32_t shadow_draws = 0;
  // Shared shadow views drawn in the last frame, the draws of every
  // family's casters into them, and the shadowing lights given no view
  // (shadow policy §2, §6).
  std::uint32_t shadow_views = 0;
  std::uint32_t shadow_caster_draws = 0;
  std::uint32_t shadow_unviewed = 0;
  // The views' image: texels on a side, layers, device memory in bytes,
  // and the times it was built (shadow policy §5, §8).
  std::uint32_t shadow_resolution = 0;
  std::uint32_t shadow_layers = 0;
  std::uint64_t shadow_memory_bytes = 0;
  std::uint64_t shadow_rebuilds = 0;
  std::uint64_t frames_rendered = 0;
  // The last completed frame's timeline value.
  std::uint64_t completion = 0;
  // Whether the queue writes timestamps, and the last frame's GPU time from
  // its first command to its last, readback copies included.
  bool gpu_timing = false;
  double gpu_frame_ms = 0;
  // Bytes the last frame copied to the CPU: its colour and depth products
  // (GPU handoff policy §8).
  std::uint64_t readback_bytes = 0;
  std::uint32_t pipelines_created = 0;
  // Samples per pixel: RenderOptions' count, as far as the device offers it.
  std::uint32_t samples = 1;
  std::uint32_t target_allocations = 0;
  std::uint64_t topology_uploads = 0;
  std::uint64_t point_uploads = 0;
  // Material parameter slots written: one per new or changed material.
  std::uint64_t material_writes = 0;
  // Textures uploaded: one per new texture or pixel change.
  std::uint64_t texture_uploads = 0;
  // Skin influences uploaded: one per newly skinned mesh or binding change.
  std::uint64_t skin_uploads = 0;
  // Joint buffers written: one per skinned mesh whose pose changed, the only
  // work a pose change does (design policy §11).
  std::uint64_t pose_writes = 0;
  // Static sparse morph targets uploaded, and small weight buffers written.
  std::uint64_t morph_uploads = 0;
  std::uint64_t morph_weight_writes = 0;
  bool validation_available = false;
  std::uint32_t validation_message_count = 0;
  std::string validation_detail;
  std::string device_name;
  std::string api_version;
  std::string driver_version;
  std::uint32_t vendor_id = 0;
  std::uint32_t device_id = 0;
};

struct GpuFrameEvidence {
  FrameStatus status = FrameStatus::Skip;
  std::string detail;
  OffscreenStatistics statistics;
  ColorProduct color;
  DepthProduct depth;
};

// A persistent offscreen renderer: the device, pipeline and render targets
// outlive a frame, mesh geometry is uploaded only when its revision changes,
// and the targets are reallocated only when the extent does. One frame is in
// flight, and `Render` returns once that frame's colour and depth are read
// back.
class OffscreenRenderer {
public:
  virtual ~OffscreenRenderer() = default;
  virtual void SetLateFrameSource(LateFrameSource source) = 0;

  // Changes the next clear without reallocating targets or scene resources.
  // Invalid settings retain the previous clear. Transparent MSAA must keep
  // MToon Mask coverage, which the Vulkan backend does with alphaToOne; a
  // device without it fails the request. 1x needs none.
  [[nodiscard]] virtual bool SetBackground(const BackgroundSettings& settings,
      std::string& error) = 0;

  // Render `draws` at `width` x `height` into `color` and `depth`, reusing
  // their storage. Both products have their origin at the top left.
  [[nodiscard]] virtual bool Render(const DrawList& draws, std::uint32_t width,
      std::uint32_t height, ColorProduct& color, DepthProduct& depth,
      std::string& error) = 0;

  [[nodiscard]] virtual const OffscreenStatistics& statistics() const = 0;
};

// Consumer background, in linear sRGB. It never changes the foreground.
struct CompositeBackground {
  bool checkerboard = false;
  Float3 color{0.05F, 0.10F, 0.15F};
  Float3 alternate{0.35F, 0.35F, 0.35F};
  std::uint32_t checker_size = 16; // Framebuffer pixels, at least one.
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
  // The shared shadow views drawn, MMD's self-shadow map among them,
  // before the scene; 0 in a frame without one (shadow policy §5).
  double shadow = 0.0;
  // Waiting for the swapchain image the frame draws into to be released by
  // the presentation engine: long under vsync, not the renderer's work.
  double image_wait = 0.0;
  // The scene pass in the order it draws: unlit meshes, opaque MToon's
  // outline hulls, its surfaces, and the transparent draws with their hulls
  // (design policy §24). `resolve` ends the pass, averaging the samples into
  // the reusable linear RGBA image when multisampled.
  double unlit = 0.0;
  double outline = 0.0;
  double opaque = 0.0;
  double transparent = 0.0;
  double resolve = 0.0;
  // The scene pass whole: its parts and resolve together. A backend that
  // times whole passes fills it and leaves the parts and the image wait
  // unmeasured (PresentStatistics::gpu_parts_timed).
  double scene = 0.0;
  // Fullscreen source-over and output transfer into the swapchain.
  double composite = 0.0;
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
  // All hulls, including those counted in transparent above.
  std::uint32_t hulls = 0;
  // Caster draws into the shared shadow views, before the scene pass.
  std::uint32_t shadow = 0;
};

struct PresentStatistics {
  FrameLatency latency;
  std::uint64_t late_samples_applied = 0;
  std::uint64_t late_samples_rejected = 0;
  std::string late_rejection;
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
  // Products read back to the CPU: one per capture taken, none otherwise.
  std::uint64_t readbacks = 0;
  // What reached the GPU, as OffscreenStatistics counts it: geometry,
  // material slots, textures, skins and poses, each only when it changed.
  std::uint64_t topology_uploads = 0;
  std::uint64_t point_uploads = 0;
  std::uint64_t material_writes = 0;
  std::uint64_t texture_uploads = 0;
  std::uint64_t skin_uploads = 0;
  std::uint64_t pose_writes = 0;
  // Static sparse morph targets uploaded, and small weight buffers written.
  std::uint64_t morph_uploads = 0;
  std::uint64_t morph_weight_writes = 0;
  // Overlay textures uploaded: one per new texture or pixel change.
  std::uint64_t overlay_texture_uploads = 0;
  // The shared shadow views, as OffscreenStatistics counts them: drawn in
  // the last frame and lights given none; the image's texels on a side,
  // layers, device memory in bytes and builds.
  std::uint32_t shadow_views = 0;
  std::uint32_t shadow_unviewed = 0;
  std::uint32_t shadow_resolution = 0;
  std::uint32_t shadow_layers = 0;
  std::uint64_t shadow_memory_bytes = 0;
  std::uint64_t shadow_rebuilds = 0;
  // The last frame RenderFrame recorded.
  PresentDrawCounts draws;
  // The CPU's time in the last RenderFrame that presented, in milliseconds:
  // waiting for the frame in flight and for a swapchain image, then the rest
  // (a sample change, uploads, recording, submitting and presenting).
  double cpu_wait = 0.0;
  double cpu_submit = 0.0;
  // Whether the queue writes timestamps; without them `gpu` stays zero.
  bool gpu_timing = false;
  // Whether `gpu` times the scene pass's parts and the image wait, which
  // timestamps between commands give; core WebGPU writes timestamps only
  // as passes begin and end (WebGPU policy §6), so they stay zero there.
  bool gpu_parts_timed = false;
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

// One presentation session drawing a scene's DrawList into an independent
// linear premultiplied RGBA8 image, through the offscreen scene pipelines.
// The GPU composites it over a consumer background into the swapchain, then
// draws an OverlayDrawList. One frame in flight, FIFO present mode when vsync
// is on, an unsynchronized mode (when available) otherwise.
class PresentSession {
public:
  virtual ~PresentSession() = default;
  virtual void SetLateFrameSource(LateFrameSource source) = 0;

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
  // Transparent MSAA must keep Mask coverage, as SetBackground says; a
  // device that cannot fails the next RenderFrame with an error, retaining
  // the previous count.
  virtual void SetSamples(std::uint32_t requested) = 0;

  // Finite colours in [0,1], positive checker size; invalid settings retain
  // the last accepted background. No scene uploads or pipeline changes.
  [[nodiscard]] virtual bool SetCompositeBackground(
      const CompositeBackground& background, std::string& error) = 0;

  // The next frame belongs to a different scene, whose resource ids and
  // revisions may overlap the previous scene's. Release its cached scene
  // resources after the frame in flight completes; keep presentation and
  // overlay resources and cumulative statistics.
  virtual void ResetScene() = 0;

  // Asks the next frame RenderFrame presents to be copied back to the CPU as
  // well: a screenshot, never an ordinary frame (design policy §31).
  virtual void RequestCapture() = 0;

  // Explicit readback of the reusable foreground, before compositing/UI:
  // top-left linear-sRGB premultiplied RGBA8, like OffscreenRenderer.
  virtual void RequestForegroundCapture() = 0;
  [[nodiscard]] virtual bool TakeForegroundCapture(ColorProduct& color,
      std::string& error) = 0;

  // The captured frame's colour as presented, RGBA8 with its origin at the
  // top left, waiting for that frame to complete. False with `error` empty
  // while no captured frame is waiting; false with `error` set on a failure.
  [[nodiscard]] virtual bool TakeCapture(ColorProduct& color,
      std::string& error) = 0;

  [[nodiscard]] virtual const PresentStatistics& statistics() const = 0;
};

} // namespace Toon
