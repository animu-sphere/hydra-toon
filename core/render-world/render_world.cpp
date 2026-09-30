// SPDX-License-Identifier: Apache-2.0
#include <toon/render_world.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Toon {

namespace {

// Area-weighted face normals summed at each corner's point, the smooth
// normals Storm computes for a mesh without authored ones. A point no
// triangle reaches, or whose triangles cancel, gets +z.
std::vector<Float3> SmoothNormals(const std::vector<Float3>& points,
    const std::vector<std::uint32_t>& indices) {
  std::vector<Float3> normals(points.size());
  for (std::size_t corner = 0; corner + 2U < indices.size(); corner += 3U) {
    const Float3& a = points[indices[corner]];
    const Float3& b = points[indices[corner + 1U]];
    const Float3& c = points[indices[corner + 2U]];
    const Float3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
    const Float3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
    // Twice the triangle's area, along its counter-clockwise normal.
    const Float3 face{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z,
        ab.x * ac.y - ab.y * ac.x};
    for (std::size_t offset = 0; offset < 3U; ++offset) {
      Float3& normal = normals[indices[corner + offset]];
      normal.x += face.x;
      normal.y += face.y;
      normal.z += face.z;
    }
  }
  for (Float3& normal : normals) {
    const float length = std::sqrt(
        normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (length > 0.0F && std::isfinite(length)) {
      normal = {normal.x / length, normal.y / length, normal.z / length};
    } else {
      normal = {0.0F, 0.0F, 1.0F};
    }
  }
  return normals;
}

} // namespace

Matrix4 Multiply(const Matrix4& left, const Matrix4& right) {
  Matrix4 result;
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0.0F;
      for (int k = 0; k < 4; ++k) {
        sum += left.m[k * 4 + row] * right.m[column * 4 + k];
      }
      result.m[column * 4 + row] = sum;
    }
  }
  return result;
}

MeshId RenderWorld::CreateMesh() {
  const MeshId id = next_mesh_++;
  MeshSnapshot& mesh = meshes_[id].snapshot;
  mesh.id = id;
  mesh.points = std::make_shared<const std::vector<Float3>>();
  mesh.points_revision = Stamp();
  mesh.indices = std::make_shared<const std::vector<std::uint32_t>>();
  mesh.topology_revision = Stamp();
  mesh.uvs = std::make_shared<const std::vector<Float2>>();
  mesh.uvs_revision = Stamp();
  mesh.influences = std::make_shared<const std::vector<ToonJointInfluence>>();
  mesh.skin_revision = Stamp();
  mesh.joints = std::make_shared<const std::vector<Matrix4>>();
  mesh.pose_revision = Stamp();
  dirty_ = true;
  return id;
}

void RenderWorld::RemoveMesh(MeshId mesh) {
  if (meshes_.erase(mesh) != 0) {
    dirty_ = true;
  }
}

void RenderWorld::SetMeshTopology(MeshId mesh,
    std::vector<std::uint32_t> triangles) {
  if (MeshRecord* record = Find(mesh)) {
    triangles.resize(triangles.size() - triangles.size() % 3U);
    const auto largest = std::max_element(triangles.begin(), triangles.end());
    MeshSnapshot& snapshot = record->snapshot;
    snapshot.index_bound = largest == triangles.end() ? 0U : *largest + 1U;
    snapshot.indices =
        std::make_shared<const std::vector<std::uint32_t>>(std::move(triangles));
    snapshot.topology_revision = Stamp();
    record->normals_stale = true;
    dirty_ = true;
  }
}

void RenderWorld::SetMeshPoints(MeshId mesh, std::vector<Float3> points) {
  if (MeshRecord* record = Find(mesh)) {
    record->snapshot.points =
        std::make_shared<const std::vector<Float3>>(std::move(points));
    record->snapshot.points_revision = Stamp();
    record->normals_stale = true;
    dirty_ = true;
  }
}

void RenderWorld::SetMeshUVs(MeshId mesh, std::vector<Float2> uvs) {
  if (MeshRecord* record = Find(mesh)) {
    record->snapshot.uvs =
        std::make_shared<const std::vector<Float2>>(std::move(uvs));
    record->snapshot.uvs_revision = Stamp();
    dirty_ = true;
  }
}

void RenderWorld::SetMeshNormals(MeshId mesh, std::vector<Float3> normals) {
  MeshRecord* record = Find(mesh);
  if (record == nullptr || *record->authored_normals == normals) {
    return;
  }
  record->authored_normals =
      std::make_shared<const std::vector<Float3>>(std::move(normals));
  record->normals_stale = true;
  dirty_ = true;
}

void RenderWorld::SetMeshTransform(MeshId mesh, const Matrix4& transform) {
  if (MeshRecord* record = Find(mesh)) {
    record->snapshot.transform = transform;
    dirty_ = true;
  }
}

void RenderWorld::SetMeshColor(MeshId mesh, Float3 color) {
  if (MeshRecord* record = Find(mesh)) {
    record->snapshot.color = color;
    dirty_ = true;
  }
}

void RenderWorld::SetMeshVisible(MeshId mesh, bool visible) {
  MeshRecord* record = Find(mesh);
  if (record != nullptr && record->snapshot.visible != visible) {
    record->snapshot.visible = visible;
    dirty_ = true;
  }
}

void RenderWorld::SetMeshMaterial(MeshId mesh, MaterialId material) {
  MeshRecord* record = Find(mesh);
  if (record != nullptr && record->snapshot.material != material) {
    record->snapshot.material = material;
    dirty_ = true;
  }
}

// A skin without influences per point, or without influences, is none.
void RenderWorld::SetMeshSkin(MeshId mesh, ToonSkin skin) {
  MeshRecord* record = Find(mesh);
  if (record == nullptr) {
    return;
  }
  if (skin.influences_per_point == 0 || skin.influences.empty()) {
    skin = {};
  }
  MeshSnapshot& snapshot = record->snapshot;
  if (snapshot.influences_per_point == skin.influences_per_point &&
      snapshot.constant_influences == skin.constant &&
      snapshot.geom_bind == skin.geom_bind &&
      *snapshot.influences == skin.influences) {
    return;
  }
  // Wide, so an index at the type's limit still bounds past itself and no
  // pose can satisfy it.
  std::uint64_t joint_bound = 0;
  for (const ToonJointInfluence& influence : skin.influences) {
    joint_bound = std::max<std::uint64_t>(joint_bound,
        std::uint64_t{influence.joint} + 1U);
  }
  snapshot.influences_per_point = skin.influences_per_point;
  snapshot.constant_influences = skin.constant;
  snapshot.geom_bind = skin.geom_bind;
  snapshot.joint_bound = static_cast<std::uint32_t>(std::min<std::uint64_t>(
      joint_bound, std::numeric_limits<std::uint32_t>::max()));
  snapshot.influences =
      std::make_shared<const std::vector<ToonJointInfluence>>(
          std::move(skin.influences));
  snapshot.skin_revision = Stamp();
  dirty_ = true;
}

void RenderWorld::SetMeshSkinPose(MeshId mesh, ToonSkinPose pose) {
  MeshRecord* record = Find(mesh);
  if (record == nullptr) {
    return;
  }
  MeshSnapshot& snapshot = record->snapshot;
  if (snapshot.skeleton_to_mesh == pose.skeleton_to_mesh &&
      *snapshot.joints == pose.joints) {
    return;
  }
  snapshot.skeleton_to_mesh = pose.skeleton_to_mesh;
  snapshot.joints =
      std::make_shared<const std::vector<Matrix4>>(std::move(pose.joints));
  snapshot.pose_revision = Stamp();
  dirty_ = true;
}

bool IsSkinned(const MeshSnapshot& mesh) {
  if (mesh.influences_per_point == 0 || mesh.influences == nullptr ||
      mesh.joints == nullptr || mesh.joints->size() < mesh.joint_bound) {
    return false;
  }
  const std::size_t sets = mesh.constant_influences ? 1U : mesh.index_bound;
  return mesh.influences->size() >=
         sets * static_cast<std::size_t>(mesh.influences_per_point);
}

void RenderWorld::SetView(const ToonView& view) {
  if (view.view != view_.view || view.projection != view_.projection) {
    view_ = view;
    view_revision_ = Stamp();
    dirty_ = true;
  }
}

void RenderWorld::SetMetersPerUnit(float meters) {
  if (std::isfinite(meters) && meters > 0.0F && meters != meters_per_unit_) {
    meters_per_unit_ = meters;
    dirty_ = true;
  }
}

bool HasOutline(const ToonMaterial& material) {
  return material.model == ToonShadingModel::MToon && material.outline &&
         material.mtoon.outline_width_mode != ToonOutlineWidthMode::None &&
         material.outline_width > 0.0F && std::isfinite(material.outline_width);
}

bool IsTransparent(const ToonMaterial& material) {
  return material.alpha_mode == ToonAlphaMode::Blend;
}

std::int32_t RenderQueue(const ToonMaterial& material) {
  switch (material.alpha_mode) {
  case ToonAlphaMode::Opaque:
    return 2000;
  case ToonAlphaMode::Mask:
    return 2450;
  case ToonAlphaMode::Blend:
    break;
  }
  const bool mtoon = material.model == ToonShadingModel::MToon;
  const std::int32_t offset = mtoon ? material.mtoon.render_queue_offset : 0;
  return mtoon && material.mtoon.transparent_with_z_write
             ? 2501 + std::clamp(offset, 0, 9)
             : 3000 + std::clamp(offset, -9, 0);
}

bool WritesDepth(const ToonMaterial& material) {
  return !IsTransparent(material) ||
         (material.model == ToonShadingModel::MToon &&
             material.mtoon.transparent_with_z_write);
}

bool IsStructuralChange(const ToonMaterial& before,
    const ToonMaterial& after) {
  return before.model != after.model ||
         before.alpha_mode != after.alpha_mode ||
         before.double_sided != after.double_sided ||
         before.emissive_texture.texture != after.emissive_texture.texture ||
         before.normal_texture.texture != after.normal_texture.texture ||
         before.mtoon.shading_shift_texture.texture !=
             after.mtoon.shading_shift_texture.texture ||
         before.mtoon.uv_animation_mask_texture.texture !=
             after.mtoon.uv_animation_mask_texture.texture ||
         before.base_texture.texture != after.base_texture.texture ||
         before.mtoon.shade_texture.texture !=
             after.mtoon.shade_texture.texture ||
         before.mtoon.outline_width_texture.texture !=
             after.mtoon.outline_width_texture.texture ||
         before.mtoon.matcap_texture.texture !=
             after.mtoon.matcap_texture.texture ||
         before.mtoon.rim_multiply_texture.texture !=
             after.mtoon.rim_multiply_texture.texture;
}

MaterialId RenderWorld::CreateMaterial() {
  const MaterialId id = next_material_++;
  MaterialSnapshot& material = materials_[id];
  material.id = id;
  material.parameters_revision = Stamp();
  material.structure_revision = material.parameters_revision;
  dirty_ = true;
  return id;
}

void RenderWorld::SetTimeSeconds(double seconds) {
  if (std::isfinite(seconds) && time_seconds_ != seconds) {
    time_seconds_ = seconds;
    dirty_ = true;
  }
}

void RenderWorld::RemoveMaterial(MaterialId material) {
  if (materials_.erase(material) != 0) {
    dirty_ = true;
  }
}

void RenderWorld::SetMaterial(MaterialId material, const ToonMaterial& values) {
  const auto found = materials_.find(material);
  if (found == materials_.end() || found->second.material == values) {
    return;
  }
  MaterialSnapshot& record = found->second;
  const bool structural = IsStructuralChange(record.material, values);
  record.material = values;
  record.parameters_revision = Stamp();
  if (structural) {
    record.structure_revision = record.parameters_revision;
  }
  dirty_ = true;
}

TextureId RenderWorld::CreateTexture() {
  const TextureId id = next_texture_++;
  TextureSnapshot& texture = textures_[id];
  texture.id = id;
  texture.texture.pixels = std::make_shared<const std::vector<std::uint8_t>>();
  texture.revision = Stamp();
  dirty_ = true;
  return id;
}

void RenderWorld::RemoveTexture(TextureId texture) {
  if (textures_.erase(texture) != 0) {
    dirty_ = true;
  }
}

// Pixels that do not fill width x height are dropped, so a consumer can
// trust the size.
void RenderWorld::SetTexture(TextureId texture, ToonTexture values) {
  const auto found = textures_.find(texture);
  if (found == textures_.end()) {
    return;
  }
  if (values.pixels == nullptr ||
      values.pixels->size() !=
          static_cast<std::size_t>(values.width) * values.height * 4U) {
    values.width = 0;
    values.height = 0;
    values.pixels = std::make_shared<const std::vector<std::uint8_t>>();
  }
  found->second.texture = std::move(values);
  found->second.revision = Stamp();
  dirty_ = true;
}

void RenderWorld::SetBootstrapTriangle() {
  // z = -0.5 in OpenGL clip space is depth 0.25 once a backend maps it.
  const MeshId mesh = CreateMesh();
  SetMeshPoints(mesh, {{-0.70F, -0.65F, -0.5F},
                          {0.70F, -0.65F, -0.5F},
                          {0.00F, 0.70F, -0.5F}});
  SetMeshTopology(mesh, {0, 1, 2});
  SetMeshColor(mesh, {0.80F, 0.20F, 0.10F});
  SetView(ToonView{});
}

void RenderWorld::Commit(FrameSnapshot& snapshot) {
  if (dirty_) {
    ++revision_;
    dirty_ = false;
  }
  snapshot.revision = revision_;
  snapshot.view = view_;
  snapshot.view_revision = view_revision_;
  snapshot.meters_per_unit = meters_per_unit_;
  snapshot.time_seconds = time_seconds_;
  snapshot.meshes.clear();
  for (auto& entry : meshes_) {
    MeshRecord& record = entry.second;
    if (record.normals_stale) {
      // Authored normals do not follow the points, so a points edit that
      // keeps them serving re-uploads none.
      MeshSnapshot& mesh = record.snapshot;
      const bool authored = !record.authored_normals->empty() &&
                            record.authored_normals->size() >= mesh.index_bound;
      if (authored) {
        if (mesh.normals != record.authored_normals) {
          mesh.normals = record.authored_normals;
          mesh.normals_revision = Stamp();
        }
      } else {
        mesh.normals = mesh.points->size() < mesh.index_bound
                           ? std::make_shared<const std::vector<Float3>>()
                           : std::make_shared<const std::vector<Float3>>(
                                 SmoothNormals(*mesh.points, *mesh.indices));
        mesh.normals_revision = Stamp();
      }
      mesh.authored_normals = authored;
      record.normals_stale = false;
    }
    snapshot.meshes.push_back(record.snapshot);
  }
  snapshot.materials.clear();
  for (const auto& entry : materials_) {
    snapshot.materials.push_back(entry.second);
  }
  snapshot.textures.clear();
  for (const auto& entry : textures_) {
    snapshot.textures.push_back(entry.second);
  }
}

FrameSnapshot RenderWorld::Commit() {
  FrameSnapshot snapshot;
  Commit(snapshot);
  return snapshot;
}

RenderWorld::MeshRecord* RenderWorld::Find(MeshId mesh) {
  const auto found = meshes_.find(mesh);
  return found == meshes_.end() ? nullptr : &found->second;
}

std::uint64_t RenderWorld::Stamp() {
  return ++stamp_;
}

} // namespace Toon
