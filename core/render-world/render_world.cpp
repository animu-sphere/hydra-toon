// SPDX-License-Identifier: Apache-2.0
#include <toon/render_world.hpp>

#include <algorithm>
#include <cmath>
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

void RenderWorld::SetView(const ToonView& view) {
  if (view.view.m != view_.view.m ||
      view.projection.m != view_.projection.m) {
    view_ = view;
    view_revision_ = Stamp();
    dirty_ = true;
  }
}

bool IsStructuralChange(const ToonMaterial& before,
    const ToonMaterial& after) {
  return before.model != after.model ||
         before.alpha_mode != after.alpha_mode ||
         before.double_sided != after.double_sided;
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
  snapshot.meshes.clear();
  for (auto& entry : meshes_) {
    MeshRecord& record = entry.second;
    if (record.normals_stale) {
      MeshSnapshot& mesh = record.snapshot;
      mesh.normals = mesh.points->size() < mesh.index_bound
                         ? std::make_shared<const std::vector<Float3>>()
                         : std::make_shared<const std::vector<Float3>>(
                               SmoothNormals(*mesh.points, *mesh.indices));
      mesh.normals_revision = Stamp();
      record.normals_stale = false;
    }
    snapshot.meshes.push_back(record.snapshot);
  }
  snapshot.materials.clear();
  for (const auto& entry : materials_) {
    snapshot.materials.push_back(entry.second);
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
