// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <toon/render_world.hpp>

namespace Toon {

// Shadow: what reaches a receiver past the shared shadow views, the mean
// over the lights that have one, or past MMD's self-shadow; 1 elsewhere
// (shadow policy §8).
enum class MaterialDebug { Surface,
  BaseColor,
  Normal,
  DirectLight,
  Ambient,
  Shadow };

// Evaluation controls: per-frame values, never edits to resident materials.
struct LightingDebug {
  bool scene_lights = true;
  float direct_scale = 1.0F;
  float ambient_scale = 1.0F;
  Float3 key_direction{0.25F, 0.5F, 1.0F}; // toward fallback key, view space
  MaterialDebug material = MaterialDebug::Surface;
};

// The shared shadow views' render settings (shadow policy §3): per-frame
// values, never material or geometry edits. Bias and filter apply to the
// receivers of every family but MMD, whose comparison and edge are its
// own (material policy §10). The defaults were measured in renderer
// report 86: no acne at 1,024 to 4,096 texels on its scenes and avatar,
// few thin casters' shadows lost, and edges neither stepped nor grainy.
struct ShadowSettings {
  // `toon:shadowResolution`: texels on a side of every view, a power of
  // two from 512 to 4096 (ShadowResolution).
  std::uint32_t resolution = 2048;
  // `toon:shadowDepthBias`: how far behind what a view saw a receiver
  // must lie before it is shadowed, in the view's texels, before slope.
  float depth_bias = 1.5F;
  // `toon:shadowNormalBias`: how far a receiver moves along its normal
  // as it turns from the light, at most, in the view's texels.
  float normal_bias = 1.0F;
  // `toon:shadowFilterRadius`: the radius of the PCF disc a receiver
  // reads, in the view's texels; 0 reads one bilinear tap.
  float filter_radius = 1.5F;

  friend bool operator==(const ShadowSettings&, const ShadowSettings&) = default;
};

// The resolution a view is drawn at for a requested one: the power of two
// at or below it, from 512 to 4096.
[[nodiscard]] std::uint32_t ShadowResolution(std::uint32_t requested);

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
  // MMD's self-shadow, as MMD's own switch turns it on or off: whether MMD
  // materials that cast it draw into a shadow map and those that receive
  // it shade by it (material policy §10). A per-frame value, like
  // `outlines`.
  bool mmd_self_shadow = true;
  // Shared shadows, the render setting `toon:shadows` (shadow policy §3):
  // whether the casters of every family but MMD draw into the shared
  // views and every family but MMD receives them. MMD's casters draw
  // whenever a view is drawn, and its receivers follow `mmd_self_shadow`.
  // A per-frame value, like `outlines`.
  bool shadows = true;
  ShadowSettings shadow_settings;
  // Whether MMD materials' texture, sphere and toon tints, which material
  // morphs set, draw as MikuMikuEffect's full.fx draws them. Off, as MMD
  // itself draws none (material policy MAT-Q6); the tints stay in the
  // material slots either way. A per-frame value, like `outlines`.
  bool mmd_morph_tints = false;
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
  // The mesh's points as its pose and transform place them, in the scene's
  // space, from the same envelopes; false when they cannot be bounded.
  // Unlike OutsideView it bounds a morphed mesh, without its offsets.
  [[nodiscard]] bool SceneBounds(const MeshSnapshot& mesh,
      std::array<double, 3>& low, std::array<double, 3>& high) const;
  // Where the mesh's parts lie as its pose and transform place them, in
  // the scene's space: each joint's box around the points it moves, or the
  // mesh's one box when it is not skinned. A skinned point lies among its
  // joints' boxes, not always within one, so they place its parts rather
  // than bound it. False, with no boxes, when they cannot be placed.
  struct SceneBox {
    std::array<double, 3> low{};
    std::array<double, 3> high{};
  };
  [[nodiscard]] bool ScenePieces(const MeshSnapshot& mesh,
      std::vector<SceneBox>& boxes) const;

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
  // The posed points in the mesh's space.
  [[nodiscard]] bool ObjectBounds(const MeshSnapshot& mesh, Box& bounds) const;
  [[nodiscard]] bool ViewBounds(const MeshSnapshot& mesh, const ToonView& view,
      float width, ToonOutlineWidthMode mode, float meters_per_unit,
      Box& bounds, double& radius) const;
  std::uint64_t points_revision_ = 0;
  std::uint64_t topology_revision_ = 0;
  std::uint64_t skin_revision_ = 0;
};

// How a material takes part in shared shadows (shadow policy §4), before
// a frame's settings decide. A null material is an unbound draw's display
// colour, an opaque, double-sided dielectric.
struct ShadowRole {
  // MMD's: it casts by its castSelfShadow and receives by its
  // receiveSelfShadow, as MMD's self-shadow reads them.
  bool mmd = false;
  bool casts = false;
  // Whether it casts both faces, or only those its surface draws.
  bool both_faces = false;
  // Whether its texels' alpha cuts it at its Mask cutoff as it casts.
  bool cut = false;
  bool receives = false;
};
[[nodiscard]] ShadowRole ShadowRoleOf(const ToonMaterial* material);

// The material `id` names among `draws.materials`; null for 0 or an id
// the list does not have.
[[nodiscard]] const ToonMaterial* FindMaterial(const DrawList& draws,
    MaterialId id);

// The views a frame draws shared shadows into (shadow policy §2, §5): the
// shadowing directional and spot lights' in the light list's order, or the
// fallback key's alone.
constexpr std::uint32_t kShadowViewCapacity = 4;

// The widest spot cone a view covers, as a half-angle in degrees; a wider
// cone is a hemisphere's or a point light's, which a cube map would cover
// (shadow policy §10), and its light is given no view.
constexpr float kShadowSpotLimit = 80.0F;

// One view of a shadowing light, backend-neutral.
struct ShadowView {
  // The light's id; 0 for the fallback key.
  LightId light = 0;
  // From the scene's space to the view's clip space: x and y in [-1, 1]
  // across it, y up, and z, the depth a caster writes and a receiver
  // compares, in [0, 1] from its side nearest the light. A directional
  // light's view is orthographic; a spot light's is perspective, its w the
  // distance along the light's axis.
  Matrix4 clip_from_world;
  bool perspective = false;
  // Its depth range in scene units: orthographic, from its near side to
  // its far side; perspective, from `near_plane` to the far plane, both
  // distances from the light along its axis.
  double depth = 0.0;
  double near_plane = 0.0;
  // The width of one of its texels in scene units: orthographic, anywhere;
  // perspective, at a distance of one unit along the axis, growing in
  // proportion to the distance.
  double texel = 0.0;
  // Whether MMD's receivers read it: it is the view of the scene's first
  // direct light and MMD's self-shadow is on (shadow policy §6).
  bool mmd = false;
};

// Decides each frame which views are drawn and fits each to what it must
// cover, from posed bounds it keeps per mesh as OutlineBounds, so a pose
// moves a view without touching geometry. A directional view is fitted as
// material policy §10 fits MMD's map, over every family's casters and
// receivers: along the light, the box around them; across it, what the
// camera sees of the receivers, in steps of an eighth of an octave and
// whole texels. A spot view is the light's perspective over its cone, its
// near and far planes fitted to the same box. A light with nothing to
// receive its shadow has no view.
class ShadowFit {
public:
  // Fits views of `draws.shadow_settings`' resolution.
  void Update(const DrawList& draws);
  [[nodiscard]] const std::vector<ShadowView>& views() const { return views_; }
  // Shadowing lights given no view this frame: past the capacity, or spot
  // lights whose cone is wider than kShadowSpotLimit. Reported, not
  // silent.
  [[nodiscard]] std::uint32_t unviewed() const { return unviewed_; }

  // Whether a draw of `role` draws into the frame's views, and whether it
  // receives `view`, under the draw list's settings.
  [[nodiscard]] static bool Casts(const ShadowRole& role, const DrawList& draws);
  [[nodiscard]] static bool Receives(const ShadowRole& role,
      const DrawList& draws, const ShadowView& view);

private:
  struct Bounds {
    OutlineBounds bounds;
    std::uint64_t generation = 0;
  };
  // A drawn mesh as every view of the frame sees it, gathered once: the
  // box around its posed points in the scene's space, whether it casts,
  // whether it receives every view or only MMD's, and the corners of what
  // the camera sees of it, a range of `seen_`.
  struct Shadowed {
    std::array<double, 3> low{};
    std::array<double, 3> high{};
    bool casts = false;
    bool receives = false;
    bool receives_mmd = false;
    std::size_t first_seen = 0;
    std::size_t seen_count = 0;
    [[nodiscard]] bool ReceivesView(const ShadowView& view) const {
      return receives || (receives_mmd && view.mmd);
    }
  };
  void Gather(const DrawList& draws);
  bool Fit(const DrawList& draws, const Float3& travel, double size,
      ShadowView& view);
  bool FitSpot(const DrawList& draws, const ToonLight& light, double size,
      ShadowView& view) const;

  std::unordered_map<MeshId, Bounds> bounds_;
  std::vector<OutlineBounds::SceneBox> pieces_;
  std::vector<Shadowed> shadowed_;
  std::vector<std::array<double, 3>> seen_;
  std::vector<ShadowView> views_;
  std::uint64_t generation_ = 0;
  std::uint32_t unviewed_ = 0;
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
