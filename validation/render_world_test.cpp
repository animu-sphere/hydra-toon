// SPDX-License-Identifier: Apache-2.0
// The core's dirty routing (design policy §14): each kind of change advances
// only its own revision, and extraction drops what cannot be drawn.
#include <toon/extraction.hpp>
#include <toon/render_world.hpp>

#include <iostream>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main() {
  Toon::RenderWorld world;
  const Toon::MeshId mesh = world.CreateMesh();
  world.SetMeshTopology(mesh, {0, 1, 2, 0, 2, 3});
  world.SetMeshPoints(mesh, {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}});
  const Toon::FrameSnapshot first = world.Commit();
  const Toon::DrawList first_draws = Toon::ExtractDrawList(first);
  if (!Check(first.meshes.size() == 1 && first.meshes[0].index_bound == 4,
          "a committed mesh must carry its topology") ||
      !Check(first_draws.draws.size() == 1 &&
                 first_draws.triangle_count == 2,
          "a complete mesh must be drawn")) {
    return 1;
  }

  if (!Check(world.Commit().revision == first.revision,
          "a commit without changes must keep the revision")) {
    return 1;
  }

  world.SetMeshPoints(mesh, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}});
  const Toon::FrameSnapshot moved = world.Commit();
  if (!Check(moved.revision > first.revision, "a points edit must commit") ||
      !Check(moved.meshes[0].points_revision != first.meshes[0].points_revision,
          "a points edit must advance the points revision") ||
      !Check(moved.meshes[0].topology_revision ==
                 first.meshes[0].topology_revision,
          "a points edit must not advance the topology revision") ||
      !Check(moved.meshes[0].indices == first.meshes[0].indices,
          "a points edit must share the unchanged topology")) {
    return 1;
  }

  Toon::ToonView view;
  view.view.m[14] = -5.0F;
  world.SetView(view);
  const Toon::FrameSnapshot looked = world.Commit();
  if (!Check(looked.view_revision != moved.view_revision,
          "a camera edit must advance the view revision") ||
      !Check(looked.meshes[0].points_revision == moved.meshes[0].points_revision,
          "a camera edit must not touch geometry")) {
    return 1;
  }

  world.SetMeshTopology(mesh, {0, 1, 4});
  if (!Check(Toon::ExtractDrawList(world.Commit()).draws.empty(),
          "an index past the last point must not be drawn")) {
    return 1;
  }
  world.SetMeshTopology(mesh, {0, 1, 2});
  world.SetMeshVisible(mesh, false);
  if (!Check(Toon::ExtractDrawList(world.Commit()).draws.empty(),
          "an invisible mesh must not be drawn")) {
    return 1;
  }

  world.RemoveMesh(mesh);
  if (!Check(world.Commit().meshes.empty(), "a removed mesh must be gone")) {
    return 1;
  }
  return 0;
}
