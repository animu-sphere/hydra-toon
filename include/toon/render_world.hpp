// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace Toon {

struct Float3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;

  friend bool operator==(const Float3&, const Float3&) = default;
};

// A 4x4 matrix stored column-major, for column vectors:
// clip = projection * view * transform * point.
struct Matrix4 {
  std::array<float, 16> m{
      1.0F, 0.0F, 0.0F, 0.0F,
      0.0F, 1.0F, 0.0F, 0.0F,
      0.0F, 0.0F, 1.0F, 0.0F,
      0.0F, 0.0F, 0.0F, 1.0F};
};

[[nodiscard]] Matrix4 Multiply(const Matrix4& left, const Matrix4& right);

// The camera. `projection` follows the OpenGL / USD clip convention (y up,
// z in [-1, 1]); each backend maps it onto its own.
struct ToonView {
  Matrix4 view;
  Matrix4 projection;
};

using MeshId = std::uint32_t;

// Geometry is immutable once committed, so a snapshot shares it with the
// world instead of copying it.
using PointArray = std::shared_ptr<const std::vector<Float3>>;
using IndexArray = std::shared_ptr<const std::vector<std::uint32_t>>;

// One mesh as of a commit. Each revision changes only when its own data
// does, so a consumer re-uploads points without rebuilding topology
// (design policy §14).
struct MeshSnapshot {
  MeshId id = 0;
  PointArray points;
  std::uint64_t points_revision = 0;
  // Triangle list, three indices per triangle.
  IndexArray indices;
  std::uint64_t topology_revision = 0;
  // One past the largest index; a draw needs at least this many points.
  std::uint32_t index_bound = 0;
  Matrix4 transform;
  Float3 color{0.5F, 0.5F, 0.5F};
  bool visible = true;
};

// The material model a material's source selected (material policy §3).
enum class ToonShadingModel { PreviewSurface, MToon, MMD };

enum class ToonAlphaMode { Opaque, Mask, Blend };

enum class ToonOutlineWidthMode { None, World, Screen };

// One material, normalized from whichever model its source selected
// (material policy §4). Renderer-private: never a USD schema. Textures are
// not held yet.
struct ToonMaterial {
  ToonShadingModel model = ToonShadingModel::PreviewSurface;

  // The common part, which every pipeline reads the same way.
  Float3 base_color{1.0F, 1.0F, 1.0F};
  float alpha = 1.0F;
  ToonAlphaMode alpha_mode = ToonAlphaMode::Opaque;
  float alpha_cutoff = 0.5F;
  bool double_sided = false;
  // Linear, with any strength multiplier already applied.
  Float3 emissive;
  bool outline = false;
  float outline_width = 0.0F;
  Float3 outline_color;

  // Read only when `model` is MToon.
  struct MToon {
    Float3 shade_color{1.0F, 1.0F, 1.0F};
    float shading_shift = 0.0F;
    float shading_toony = 0.9F;
    float gi_equalization = 0.9F;
    Float3 matcap{1.0F, 1.0F, 1.0F};
    Float3 rim_color;
    float rim_fresnel_power = 5.0F;
    float rim_lift = 0.0F;
    float rim_lighting_mix = 1.0F;
    ToonOutlineWidthMode outline_width_mode = ToonOutlineWidthMode::None;
    float outline_lighting_mix = 1.0F;
    float uv_scroll_x_speed = 0.0F;
    float uv_scroll_y_speed = 0.0F;
    float uv_rotation_speed = 0.0F;
    std::int32_t render_queue_offset = 0;
    bool transparent_with_z_write = false;

    friend bool operator==(const MToon&, const MToon&) = default;
  } mtoon;

  friend bool operator==(const ToonMaterial&, const ToonMaterial&) = default;
};

// Whether moving from one material to the other changes how it is drawn —
// its model, alpha mode or double-sidedness — rather than only the values
// in its parameter slot (material policy §8).
[[nodiscard]] bool IsStructuralChange(const ToonMaterial& before,
    const ToonMaterial& after);

using MaterialId = std::uint32_t;

// One material as of a commit. `parameters_revision` advances on any change
// and `structure_revision` only on a structural one, so a consumer rewrites
// a parameter slot without rebuilding what draws it (design policy §14).
struct MaterialSnapshot {
  MaterialId id = 0;
  ToonMaterial material;
  std::uint64_t parameters_revision = 0;
  std::uint64_t structure_revision = 0;
};

struct FrameSnapshot {
  // Advances once per commit that follows any change.
  std::uint64_t revision = 0;
  ToonView view;
  std::uint64_t view_revision = 0;
  // Ordered by id.
  std::vector<MeshSnapshot> meshes;
  // Ordered by id.
  std::vector<MaterialSnapshot> materials;
};

// Host-neutral scene state. Not thread-safe: a host that syncs in parallel
// serializes its calls.
class RenderWorld {
public:
  [[nodiscard]] MeshId CreateMesh();
  void RemoveMesh(MeshId mesh);
  void SetMeshTopology(MeshId mesh, std::vector<std::uint32_t> triangles);
  void SetMeshPoints(MeshId mesh, std::vector<Float3> points);
  void SetMeshTransform(MeshId mesh, const Matrix4& transform);
  void SetMeshColor(MeshId mesh, Float3 color);
  void SetMeshVisible(MeshId mesh, bool visible);
  void SetView(const ToonView& view);

  // A new material is the default `ToonMaterial`: the fallback material.
  [[nodiscard]] MaterialId CreateMaterial();
  void RemoveMaterial(MaterialId material);
  // Setting the values a material already has changes nothing.
  void SetMaterial(MaterialId material, const ToonMaterial& values);

  // The template's bootstrap triangle as one mesh, seen through an identity
  // camera, so its clip-space placement is the mesh's own.
  void SetBootstrapTriangle();

  // Fills `snapshot`, reusing its storage, so a steady frame allocates nothing.
  void Commit(FrameSnapshot& snapshot);
  [[nodiscard]] FrameSnapshot Commit();

private:
  MeshSnapshot* Find(MeshId mesh);
  std::uint64_t Stamp();

  std::uint64_t revision_ = 0;
  std::uint64_t stamp_ = 0;
  bool dirty_ = false;
  MeshId next_mesh_ = 1;
  ToonView view_;
  std::uint64_t view_revision_ = 0;
  std::map<MeshId, MeshSnapshot> meshes_;
  MaterialId next_material_ = 1;
  std::map<MaterialId, MaterialSnapshot> materials_;
};

} // namespace Toon
