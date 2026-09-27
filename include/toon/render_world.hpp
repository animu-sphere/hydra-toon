// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace Toon {

struct Float2 {
  float x = 0.0F;
  float y = 0.0F;

  friend bool operator==(const Float2&, const Float2&) = default;
};

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

  friend bool operator==(const Matrix4&, const Matrix4&) = default;
};

[[nodiscard]] Matrix4 Multiply(const Matrix4& left, const Matrix4& right);

// The camera. `projection` follows the OpenGL / USD clip convention (y up,
// z in [-1, 1]); each backend maps it onto its own.
struct ToonView {
  Matrix4 view;
  Matrix4 projection;
};

using MeshId = std::uint32_t;
using MaterialId = std::uint32_t;
using TextureId = std::uint32_t;

// One joint's pull on a point: an index into the mesh's own joint order and
// its weight.
struct ToonJointInfluence {
  std::uint32_t joint = 0;
  float weight = 0.0F;

  friend bool operator==(const ToonJointInfluence&,
      const ToonJointInfluence&) = default;
};

// Geometry is immutable once committed, so a snapshot shares it with the
// world instead of copying it.
using PointArray = std::shared_ptr<const std::vector<Float3>>;
using IndexArray = std::shared_ptr<const std::vector<std::uint32_t>>;
using TexCoordArray = std::shared_ptr<const std::vector<Float2>>;
using PixelArray = std::shared_ptr<const std::vector<std::uint8_t>>;
using InfluenceArray = std::shared_ptr<const std::vector<ToonJointInfluence>>;
using MatrixArray = std::shared_ptr<const std::vector<Matrix4>>;

// How a mesh's points follow a skeleton, as UsdSkel's linear blend skinning
// states it. Structural: it changes when the binding does, not with a pose.
struct ToonSkin {
  // 0 when the mesh is not skinned.
  std::uint32_t influences_per_point = 0;
  // One set of influences for every point, a rigid binding, rather than one
  // set per point.
  bool constant = false;
  // `influences_per_point` per point, or once when `constant`.
  std::vector<ToonJointInfluence> influences;
  // Takes the mesh's points into the skeleton's bind space.
  Matrix4 geom_bind;
};

// A skeleton's pose as one skinned mesh sees it. Changes every frame an
// avatar moves, and moves nothing but the joint buffer (design policy §11).
struct ToonSkinPose {
  // Each joint's skinning transform, bind space to skeleton space, in the
  // mesh's joint order.
  std::vector<Matrix4> joints;
  // Skeleton space to the mesh's own space, where its transform applies.
  Matrix4 skeleton_to_mesh;
};

// One mesh as of a commit. Each revision changes only when its own data
// does, so a consumer re-uploads points without rebuilding topology
// (design policy §14).
struct MeshSnapshot {
  MeshId id = 0;
  PointArray points;
  std::uint64_t points_revision = 0;
  // Triangle list, three indices per triangle, counter-clockwise seen from
  // the front.
  IndexArray indices;
  std::uint64_t topology_revision = 0;
  // One past the largest index; a draw needs at least this many points.
  std::uint32_t index_bound = 0;
  // Smooth vertex normals, one per point, derived from the points and
  // topology at commit, as Storm derives them for a mesh that authors none.
  // A skinned mesh's are its rest pose's, skinned with its points. Empty
  // while the topology indexes past the points.
  PointArray normals;
  std::uint64_t normals_revision = 0;
  // Texture coordinates, one per point, as USD's `st`: origin at the
  // image's bottom left. Empty when the mesh has none; a draw then samples
  // no texture.
  TexCoordArray uvs;
  std::uint64_t uvs_revision = 0;
  // The skin, when the mesh is skinned: `points` and `normals` are then its
  // rest pose, and a draw moves them by `joints` (see IsSkinned). Empty
  // influences when it is not.
  std::uint32_t influences_per_point = 0;
  bool constant_influences = false;
  InfluenceArray influences;
  Matrix4 geom_bind;
  // One past the largest joint the influences name; a draw skins only with
  // at least this many joints.
  std::uint32_t joint_bound = 0;
  std::uint64_t skin_revision = 0;
  MatrixArray joints;
  Matrix4 skeleton_to_mesh;
  std::uint64_t pose_revision = 0;
  Matrix4 transform;
  Float3 color{0.5F, 0.5F, 0.5F};
  // 0 when the mesh binds no material; it then draws its colour, unlit.
  MaterialId material = 0;
  bool visible = true;
};

// Whether a draw skins `mesh`: it has influences for every point its
// topology reaches and a joint for every index they name. Otherwise it draws
// its points as they are.
[[nodiscard]] bool IsSkinned(const MeshSnapshot& mesh);

// The material model a material's source selected (material policy §3).
enum class ToonShadingModel { PreviewSurface, MToon, MMD };

enum class ToonAlphaMode { Opaque, Mask, Blend };

enum class ToonOutlineWidthMode { None, World, Screen };

// How a texture's bytes encode their values: colour is sRGB-encoded, data
// is linear. The role a material samples a texture in decides which.
enum class ToonTextureEncoding { Srgb, Linear };

// One image, decoded to RGBA8 with its first row at the top, as glTF's UV
// space and a GPU image both lay it out.
struct ToonTexture {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  ToonTextureEncoding encoding = ToonTextureEncoding::Srgb;
  // width * height * 4 bytes; a texture without them samples as white.
  PixelArray pixels;
};

// One texture as of a commit. `revision` advances when its pixels do.
struct TextureSnapshot {
  TextureId id = 0;
  ToonTexture texture;
  std::uint64_t revision = 0;
};

// glTF's sampler wrap modes.
enum class ToonWrap { Repeat, ClampToEdge, MirroredRepeat };

// How a material samples one texture: which, with what wrap, through what
// KHR_texture_transform. The transform is glTF's, in glTF's UV space (origin
// top left): uv' = translate(offset) * rotate(rotation) * scale(scale) * uv.
struct ToonTextureRef {
  // 0 when the material samples none; the factor is then used alone.
  TextureId texture = 0;
  ToonWrap wrap_s = ToonWrap::Repeat;
  ToonWrap wrap_t = ToonWrap::Repeat;
  Float2 offset;
  // Radians, counter-clockwise in glTF's UV space.
  float rotation = 0.0F;
  Float2 scale{1.0F, 1.0F};

  friend bool operator==(const ToonTextureRef&,
      const ToonTextureRef&) = default;
};

// One material, normalized from whichever model its source selected
// (material policy §4). Renderer-private: never a USD schema. A texture is
// referenced by id; the world owns it.
struct ToonMaterial {
  ToonShadingModel model = ToonShadingModel::PreviewSurface;

  // The common part, which every pipeline reads the same way.
  Float3 base_color{1.0F, 1.0F, 1.0F};
  float alpha = 1.0F;
  // Multiplies base colour and alpha.
  ToonTextureRef base_texture;
  ToonAlphaMode alpha_mode = ToonAlphaMode::Opaque;
  float alpha_cutoff = 0.5F;
  bool double_sided = false;
  // Linear, with any strength multiplier already applied.
  Float3 emissive;
  // Whether the source asks for an outline, its width in the unit the
  // model states (MToon: `outline_width_mode`), and its linear colour.
  bool outline = false;
  float outline_width = 0.0F;
  Float3 outline_color;

  // Read only when `model` is MToon.
  struct MToon {
    Float3 shade_color{1.0F, 1.0F, 1.0F};
    // MToon's shadeMultiplyTexture: multiplies the shade colour.
    ToonTextureRef shade_texture;
    float shading_shift = 0.0F;
    float shading_toony = 0.9F;
    float gi_equalization = 0.9F;
    // matcapFactor, which multiplies the MatCap texture. A material without
    // the texture adds no MatCap, whatever the factor.
    Float3 matcap{1.0F, 1.0F, 1.0F};
    // MToon's matcapTexture, colour, sampled where the view-space normal
    // points rather than at the mesh's UVs.
    ToonTextureRef matcap_texture;
    Float3 rim_color;
    float rim_fresnel_power = 5.0F;
    float rim_lift = 0.0F;
    // MToon's rimMultiplyTexture, colour: multiplies MatCap and the
    // parametric rim together.
    ToonTextureRef rim_multiply_texture;
    float rim_lighting_mix = 1.0F;
    ToonOutlineWidthMode outline_width_mode = ToonOutlineWidthMode::None;
    // MToon's outlineWidthMultiplyTexture: its G channel multiplies the
    // outline width. Data, not colour.
    ToonTextureRef outline_width_texture;
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

// Whether a material's draws add an outline (material policy §5): an MToon
// material whose width mode is not None, with a width above zero.
[[nodiscard]] bool HasOutline(const ToonMaterial& material);

// Whether a material's draws blend over what is behind them: its alpha mode
// is Blend. Opaque and Mask draw first, and blend nothing.
[[nodiscard]] bool IsTransparent(const ToonMaterial& material);

// A material's sort key (material policy §6), as MToon's render queue
// states it: Opaque 2000 and Mask 2450; Blend with transparentWithZWrite
// 2501 plus renderQueueOffsetNumber clamped to [0, 9], and without it 3000
// plus the offset clamped to [-9, 0]. A transparent draw with a lower key
// draws first; the offset is read for MToon alone.
[[nodiscard]] std::int32_t RenderQueue(const ToonMaterial& material);

// Whether a material's draws write depth: every one but a transparent one,
// which writes it only when MToon's transparentWithZWrite asks.
[[nodiscard]] bool WritesDepth(const ToonMaterial& material);

// Whether moving from one material to the other changes how it is drawn —
// its model, alpha mode, double-sidedness or which textures it samples —
// rather than only the values in its parameter slot (material policy §8).
[[nodiscard]] bool IsStructuralChange(const ToonMaterial& before,
    const ToonMaterial& after);

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
  // The scene's linear unit in metres, as UsdGeom's metersPerUnit states
  // it: what a length MToon gives in metres, a world-coordinates outline
  // width, is divided by to become a length in the scene.
  float meters_per_unit = 1.0F;
  // Ordered by id.
  std::vector<MeshSnapshot> meshes;
  // Ordered by id.
  std::vector<MaterialSnapshot> materials;
  // Ordered by id.
  std::vector<TextureSnapshot> textures;
};

// Host-neutral scene state. Not thread-safe: a host that syncs in parallel
// serializes its calls.
class RenderWorld {
public:
  [[nodiscard]] MeshId CreateMesh();
  void RemoveMesh(MeshId mesh);
  void SetMeshTopology(MeshId mesh, std::vector<std::uint32_t> triangles);
  void SetMeshPoints(MeshId mesh, std::vector<Float3> points);
  // One per point; any other count, or none, draws without textures.
  void SetMeshUVs(MeshId mesh, std::vector<Float2> uvs);
  void SetMeshTransform(MeshId mesh, const Matrix4& transform);
  void SetMeshColor(MeshId mesh, Float3 color);
  void SetMeshVisible(MeshId mesh, bool visible);
  // Binds a material by id; 0, or an id no material has, draws unlit.
  void SetMeshMaterial(MeshId mesh, MaterialId material);
  // A skin without influences per point unskins the mesh. Setting the skin
  // or pose a mesh already has changes nothing.
  void SetMeshSkin(MeshId mesh, ToonSkin skin);
  void SetMeshSkinPose(MeshId mesh, ToonSkinPose pose);
  void SetView(const ToonView& view);
  // The scene's linear unit in metres, 1 until set. A value that is not
  // positive and finite changes nothing.
  void SetMetersPerUnit(float meters);

  // A new material is the default `ToonMaterial`: the fallback material.
  [[nodiscard]] MaterialId CreateMaterial();
  void RemoveMaterial(MaterialId material);
  // Setting the values a material already has changes nothing.
  void SetMaterial(MaterialId material, const ToonMaterial& values);

  // A new texture has no pixels, and samples as white until it has.
  [[nodiscard]] TextureId CreateTexture();
  void RemoveTexture(TextureId texture);
  void SetTexture(TextureId texture, ToonTexture values);

  // The template's bootstrap triangle as one mesh, seen through an identity
  // camera, so its clip-space placement is the mesh's own.
  void SetBootstrapTriangle();

  // Fills `snapshot`, reusing its storage, so a steady frame allocates nothing.
  void Commit(FrameSnapshot& snapshot);
  [[nodiscard]] FrameSnapshot Commit();

private:
  struct MeshRecord {
    MeshSnapshot snapshot;
    // Set by a points or topology edit; the next commit recomputes normals.
    bool normals_stale = true;
  };

  MeshRecord* Find(MeshId mesh);
  std::uint64_t Stamp();

  std::uint64_t revision_ = 0;
  std::uint64_t stamp_ = 0;
  bool dirty_ = false;
  MeshId next_mesh_ = 1;
  ToonView view_;
  std::uint64_t view_revision_ = 0;
  float meters_per_unit_ = 1.0F;
  std::map<MeshId, MeshRecord> meshes_;
  MaterialId next_material_ = 1;
  std::map<MaterialId, MaterialSnapshot> materials_;
  TextureId next_texture_ = 1;
  std::map<TextureId, TextureSnapshot> textures_;
};

} // namespace Toon
