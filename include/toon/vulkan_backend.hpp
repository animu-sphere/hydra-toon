// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <toon/extraction.hpp>

namespace Toon {

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
};

// The shaders as the build and install trees lay them out: `<module>.vert.spv`
// and `<module>.frag.spv` in one directory.
[[nodiscard]] SceneShaders SceneShadersIn(const std::string& directory);

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
  std::uint64_t frames_rendered = 0;
  // The last completed frame's timeline value.
  std::uint64_t completion = 0;
  std::uint32_t pipelines_created = 0;
  std::uint32_t target_allocations = 0;
  std::uint64_t topology_uploads = 0;
  std::uint64_t point_uploads = 0;
  // Material parameter slots written: one per new or changed material.
  std::uint64_t material_writes = 0;
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
    const SceneShaders& shaders, FrameStatus& status, std::string& detail);

// Render `frame_count` frames of `draws` at 64 x 64 on one persistent
// renderer and return the last frame's products: the headless evidence run.
[[nodiscard]] GpuFrameEvidence RenderOffscreen(const DrawList& draws,
    const SceneShaders& shaders, std::uint32_t frame_count);

} // namespace Toon
