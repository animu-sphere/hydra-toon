// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <toon/extraction.hpp>

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
};

// The host and renderer use the same monotonic clock. A producer clock is
// mapped explicitly by the host; zero always represents an absent input.
[[nodiscard]] std::int64_t SteadyNanoseconds();

struct BackendCapability {
  bool available = false;
  std::string detail;
};

// The SPIR-V of every scene pipeline. Paths are explicit so build-tree and
// install-tree layouts exercise the same backend code without source-tree
// fallbacks.
struct SceneShaders {
  // Unlit, one colour per draw: meshes that bind no MToon material.
  std::string mesh_vertex;
  std::string mesh_fragment;
  // mtoon_opaque (material policy §7).
  std::string mtoon_vertex;
  std::string mtoon_fragment;
  // mtoon_transparent: MToon's Blend alpha mode, blended over what is
  // behind it (material policy §6).
  std::string mtoon_transparent_vertex;
  std::string mtoon_transparent_fragment;
  // mtoon_outline: the inverted hull of an MToon material that asks for an
  // outline (material policy §5).
  std::string mtoon_outline_vertex;
  std::string mtoon_outline_fragment;
  // overlay: a present session's 2D overlay (OverlayDrawList). The offscreen
  // renderer draws none and does not read these.
  std::string overlay_vertex;
  std::string overlay_fragment;
};

// The shaders as the build and install trees lay them out: `<module>.vert.spv`
// and `<module>.frag.spv` in one directory.
[[nodiscard]] SceneShaders SceneShadersIn(const std::string& directory);

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

struct ColorProduct {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t row_pitch = 0;
  std::string pixel_format;
  std::string origin;
  std::string color_space;
  std::vector<std::uint8_t> payload;
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
  std::uint64_t frames_rendered = 0;
  // The last completed frame's timeline value.
  std::uint64_t completion = 0;
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

  // Render `draws` at `width` x `height` into `color` and `depth`, reusing
  // their storage. Both products have their origin at the top left.
  [[nodiscard]] virtual bool Render(const DrawList& draws, std::uint32_t width,
      std::uint32_t height, ColorProduct& color, DepthProduct& depth,
      std::string& error) = 0;

  [[nodiscard]] virtual const OffscreenStatistics& statistics() const = 0;
};

[[nodiscard]] BackendCapability ProbeVulkanBackend();

// Returns nullptr with `status` Skip when this environment cannot render (no
// Vulkan build, loader, or a 1.3 device with the features the renderer
// needs) and Fail on a real error; `detail` says which.
[[nodiscard]] std::unique_ptr<OffscreenRenderer> CreateOffscreenRenderer(
    const SceneShaders& shaders, FrameStatus& status, std::string& detail,
    const RenderOptions& options = {});

// Render `frame_count` frames of `draws` at 64 x 64 on one persistent
// renderer and return the last frame's products: the headless evidence run.
[[nodiscard]] GpuFrameEvidence RenderOffscreen(const DrawList& draws,
    const SceneShaders& shaders, std::uint32_t frame_count);

} // namespace Toon
