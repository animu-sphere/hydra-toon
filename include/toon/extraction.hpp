// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <toon/render_world.hpp>

namespace Toon {

enum class MaterialDebug { Surface,
  BaseColor,
  Normal,
  DirectLight,
  Ambient };

// Evaluation controls: per-frame values, never edits to resident materials.
struct LightingDebug {
  bool scene_lights = true;
  float direct_scale = 1.0F;
  float ambient_scale = 1.0F;
  Float3 key_direction{0.25F, 0.5F, 1.0F}; // toward fallback key, view space
  MaterialDebug material = MaterialDebug::Surface;
};

// What a frame draws: every visible mesh whose topology its points can
// satisfy, the materials they bind, and the camera they are seen through.
struct DrawList {
  FrameSnapshot::InputTimes inputs;
  std::uint64_t source_revision = 0;
  ToonView view;
  std::uint64_t view_revision = 0;
  // The snapshot's unit in metres.
  float meters_per_unit = 1.0F;
  double time_seconds = 0.0;
  std::vector<LightSnapshot> lights;
  LightingDebug lighting;
  // Evaluation switch: omit hulls without changing material slots or uploads.
  bool outlines = true;
  // Evaluation reference: disable conservative six-plane frustum omission.
  bool outline_frustum_culling = true;
  // Current-frame opaque-triangle occlusion; no history or GPU readback.
  bool outline_occlusion_culling = true;
  std::vector<MeshSnapshot> draws;
  // Every material of the snapshot, ordered by id, so a consumer keeps one
  // parameter slot per material whether or not a draw binds it this frame.
  std::vector<MaterialSnapshot> materials;
  // Every texture of the snapshot, ordered by id, for the same reason.
  std::vector<TextureSnapshot> textures;
  std::uint64_t triangle_count = 0;
};

// Fills `draws`, reusing its storage.
void ExtractDrawList(const FrameSnapshot& snapshot, DrawList& draws);
[[nodiscard]] DrawList ExtractDrawList(const FrameSnapshot& snapshot);

// Apply the latest evaluated frame after extraction, retaining resident draw
// membership and every structural resource. Rejects a changed binding, target,
// weight count, material structure or texture before changing anything.
// Rejection means a host must take the change through ordinary extraction.
[[nodiscard]] bool ApplyFastSnapshot(const FrameSnapshot& latest,
    DrawList& draws, std::string& error);

// Rest-point envelopes, rebuilt only on points, topology or skin changes.
// Pose evaluation transforms joint boxes, never skins the vertex array.
class OutlineBounds {
public:
  void Update(const MeshSnapshot& mesh);
  // True only when the expanded hull is wholly outside a clip plane.
  // Unusable/non-finite data conservatively retains the draw. Consumers
  // must use ToonView's -w..w depth range and clip before rasterization
  // (no depth clamp); raster depth bias does not change this decision.
  [[nodiscard]] bool OutsideView(const MeshSnapshot& mesh, const ToonView& view,
      float width, ToonOutlineWidthMode mode, float meters_per_unit) const;

private:
  friend class OutlineOcclusion;
  struct Box {
    std::array<double, 3> low{};
    std::array<double, 3> high{};
    bool valid = false;
  };
  Box rest_;
  std::vector<Box> joints_;
  double min_weight_ = 0;
  double max_weight_ = 0;
  bool valid_skin_ = false;
  [[nodiscard]] bool ViewBounds(const MeshSnapshot& mesh, const ToonView& view,
      float width, ToonOutlineWidthMode mode, float meters_per_unit,
      Box& bounds, double& radius) const;
  std::uint64_t points_revision_ = 0;
  std::uint64_t topology_revision_ = 0;
  std::uint64_t skin_revision_ = 0;
};

// A bounded set of small, rigid opaque occluders. Mask, Blend, skinned and
// single-sided MToon meshes cannot establish full coverage here. A single
// triangle must cover the entire expanded hull; triangle unions are not used.
class OutlineOcclusion {
public:
  void Update(const DrawList& draws, std::uint32_t width, std::uint32_t height);
  [[nodiscard]] bool Occludes(const MeshSnapshot& mesh, const OutlineBounds& bounds,
      const ToonView& view, float width, ToonOutlineWidthMode mode,
      float meters_per_unit) const;

private:
  struct Triangle {
    MeshId mesh = 0;
    std::array<std::array<double, 3>, 3> points{};
    double error = 0;
  };
  // Fixed storage and work limit, independent of avatar vertex counts.
  std::array<Triangle, 128> triangles_{};
  std::size_t count_ = 0;
  double pixel_margin_ = 0;
  ToonView view_;
};

} // namespace Toon
