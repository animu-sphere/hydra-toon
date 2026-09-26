// SPDX-License-Identifier: Apache-2.0
#include <toon/render_world.hpp>

#include <algorithm>
#include <utility>

namespace Toon {

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
  MeshSnapshot& mesh = meshes_[id];
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
  if (MeshSnapshot* record = Find(mesh)) {
    triangles.resize(triangles.size() - triangles.size() % 3U);
    const auto largest = std::max_element(triangles.begin(), triangles.end());
    record->index_bound = largest == triangles.end() ? 0U : *largest + 1U;
    record->indices =
        std::make_shared<const std::vector<std::uint32_t>>(std::move(triangles));
    record->topology_revision = Stamp();
    dirty_ = true;
  }
}

void RenderWorld::SetMeshPoints(MeshId mesh, std::vector<Float3> points) {
  if (MeshSnapshot* record = Find(mesh)) {
    record->points =
        std::make_shared<const std::vector<Float3>>(std::move(points));
    record->points_revision = Stamp();
    dirty_ = true;
  }
}

void RenderWorld::SetMeshTransform(MeshId mesh, const Matrix4& transform) {
  if (MeshSnapshot* record = Find(mesh)) {
    record->transform = transform;
    dirty_ = true;
  }
}

void RenderWorld::SetMeshColor(MeshId mesh, Float3 color) {
  if (MeshSnapshot* record = Find(mesh)) {
    record->color = color;
    dirty_ = true;
  }
}

void RenderWorld::SetMeshVisible(MeshId mesh, bool visible) {
  MeshSnapshot* record = Find(mesh);
  if (record != nullptr && record->visible != visible) {
    record->visible = visible;
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
  for (const auto& entry : meshes_) {
    snapshot.meshes.push_back(entry.second);
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

MeshSnapshot* RenderWorld::Find(MeshId mesh) {
  const auto found = meshes_.find(mesh);
  return found == meshes_.end() ? nullptr : &found->second;
}

std::uint64_t RenderWorld::Stamp() {
  return ++stamp_;
}

} // namespace Toon
