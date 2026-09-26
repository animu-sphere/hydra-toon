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

struct FrameSnapshot {
  // Advances once per commit that follows any change.
  std::uint64_t revision = 0;
  ToonView view;
  std::uint64_t view_revision = 0;
  // Ordered by id.
  std::vector<MeshSnapshot> meshes;
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
};

} // namespace Toon
