// SPDX-License-Identifier: Apache-2.0
#include <toon/extraction.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <random>

namespace {
bool Check(bool condition, const char* message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}
} // namespace

int main() {
  Toon::RenderWorld world;
  const auto id = world.CreateMesh();
  world.SetMeshPoints(id, {{-0.1F, -0.1F, 0}, {0.1F, -0.1F, 0}, {0, 0.1F, 0}});
  world.SetMeshTopology(id, {0, 1, 2});
  auto mesh = world.Commit().meshes[0];
  Toon::OutlineBounds bounds;
  bounds.Update(mesh);
  Toon::ToonView view;
  const auto outside = [&](float width = 0.01F,
                           Toon::ToonOutlineWidthMode mode = Toon::ToonOutlineWidthMode::World,
                           float unit = 1) {
    return bounds.OutsideView(mesh, view, width, mode, unit);
  };
  if (!Check(!outside(), "visible mesh must keep its hull"))
    return 1;
  for (const auto axis : {12U, 13U, 14U}) {
    for (const float sign : {-1.0F, 1.0F}) {
      mesh.transform = {};
      mesh.transform.m[axis] = sign * 2;
      if (!Check(outside(), "every clip plane must omit a distant hull"))
        return 1;
      mesh.transform.m[axis] = sign * (axis == 14U ? 1.05F : 1.15F);
      if (!Check(outside() && !outside(0.1F) &&
                     !outside(0.01F, Toon::ToonOutlineWidthMode::World, 0.1F) &&
                     !outside(0.05F, Toon::ToonOutlineWidthMode::Screen),
              "expanded world/screen/unit widths must keep boundary silhouettes"))
        return 1;
    }
  }
  mesh.transform = {};
  mesh.transform.m[14] = -10;
  if (!Check(outside(), "a hull beyond the depth volume must be omitted"))
    return 1;
  mesh.transform.m[12] = 2;
  mesh.transform.m[0] = 0;
  if (!Check(!outside(), "singular transforms must retain hulls"))
    return 1;
  mesh.transform = {};
  mesh.transform.m[12] = std::numeric_limits<float>::infinity();
  if (!Check(!outside(), "nonfinite transforms must retain hulls"))
    return 1;

  // Explicit finite/infinite perspective depth ranges. A hull reaching
  // back across a plane must survive even when every rest point is outside.
  mesh.transform = {};
  view.projection.m[10] = -4.5F / 3.5F;
  view.projection.m[14] = -4.0F / 3.5F;
  view.projection.m[11] = -1;
  view.projection.m[15] = 0;
  for (const bool reversed : {false, true}) {
    if (reversed) {
      view.projection.m[10] *= -1;
      view.projection.m[14] *= -1;
    }
    for (const float z : {-0.2F, -4.2F, 2.0F}) {
      mesh.transform.m[14] = z;
      if (!Check(outside(0.1F), "fully clipped perspective hulls must be omitted"))
        return 1;
    }
    for (const float z : {-0.45F, -4.05F}) {
      mesh.transform.m[14] = z;
      if (!Check(!outside(0.1F), "depth-plane extrusion must keep a boundary hull"))
        return 1;
    }
    mesh.transform.m[14] = -4.3F;
    if (!Check(!outside(0.05F, Toon::ToonOutlineWidthMode::Screen) &&
                   !outside(0.01F, Toon::ToonOutlineWidthMode::World, 0.01F),
            "screen depth scaling and stage units must expand depth bounds"))
      return 1;
    mesh.transform.m[14] = -5;
    if (!Check(outside(0.05F, Toon::ToonOutlineWidthMode::Screen),
            "distant screen-width hulls must still be omitted"))
      return 1;
  }
  view.projection.m[10] = -1;
  view.projection.m[14] = -1; // near 0.5, infinite far
  mesh.transform.m[14] = -100;
  if (!Check(!outside(), "infinite-far projections must retain distant hulls"))
    return 1;
  mesh.transform.m[14] = -0.2F;
  if (!Check(outside(), "infinite-far projections still clip the near plane"))
    return 1;
  view = {};

  Toon::ToonSkin skin;
  skin.influences_per_point = 2;
  skin.influences = {{0, 0.25F}, {1, 0.75F}, {0, 0.6F}, {1, 0.4F}, {0, 0.0F}, {1, 1.1F}};
  skin.geom_bind.m[12] = 0.2F;
  world.SetMeshSkin(id, skin);
  // Build before a pose exists: its arrival must enable culling without
  // rebuilding envelopes or depending on the previous frame's pose.
  mesh = world.Commit().meshes[0];
  bounds.Update(mesh);
  Toon::ToonSkinPose pose;
  pose.joints.resize(2);
  pose.joints[0].m[12] = 3;
  pose.joints[1].m[12] = 4;
  world.SetMeshSkinPose(id, pose);
  mesh = world.Commit().meshes[0];
  bounds.Update(mesh);
  if (!Check(outside(), "joint envelopes must omit an offscreen pose"))
    return 1;
  pose.skeleton_to_mesh.m[12] = -3.5F;
  world.SetMeshSkinPose(id, pose);
  mesh = world.Commit().meshes[0];
  bounds.Update(mesh);
  if (!Check(!outside(), "skeleton-space motion must restore a visible hull"))
    return 1;
  pose.joints.clear();
  world.SetMeshSkinPose(id, pose);
  mesh = world.Commit().meshes[0];
  if (!Check(!outside(), "incomplete skinning must use rest geometry"))
    return 1;

  // Independent point-by-point oracle: full-width extrusion samples inside
  // all six planes must survive arbitrary affine/positive blend poses.
  std::mt19937 random(42);
  std::uniform_real_distribution<float> position(-4, 4);
  std::uniform_real_distribution<float> scale(0.2F, 2);
  int omissions = 0;
  int visible = 0;
  for (int trial = 0; trial < 1000; ++trial) {
    pose.joints.resize(2);
    for (auto& joint : pose.joints) {
      joint = {};
      joint.m[0] = scale(random);
      joint.m[5] = scale(random);
      joint.m[12] = position(random);
      joint.m[13] = position(random);
      joint.m[14] = position(random);
    }
    pose.skeleton_to_mesh = {};
    pose.skeleton_to_mesh.m[14] = -scale(random);
    world.SetMeshSkinPose(id, pose);
    mesh = world.Commit().meshes[0];
    mesh.transform.m[0] = -scale(random);
    mesh.transform.m[4] = 0.3F;
    mesh.transform.m[5] = scale(random);
    view.projection = {};
    if (trial % 2) {
      view.projection.m[0] = 1.3F;
      view.projection.m[5] = 1.7F;
      // OpenGL perspective, near 0.1 and far 10.
      view.projection.m[10] = -10.1F / 9.9F;
      view.projection.m[14] = -2.0F / 9.9F;
      view.projection.m[11] = -1;
      view.projection.m[15] = 0;
    }
    const auto mode = trial % 3 ? Toon::ToonOutlineWidthMode::Screen : Toon::ToonOutlineWidthMode::World;
    const bool omitted = outside(0.08F, mode, 0.5F);
    omissions += omitted ? 1 : 0;
    const auto model_view = Toon::Multiply(view.view, mesh.transform);
    for (std::size_t vertex = 0; vertex < mesh.points->size(); ++vertex) {
      const auto point = (*mesh.points)[vertex];
      double skinned[3]{};
      for (std::size_t influence = 0; influence < 2; ++influence) {
        const auto weight = skin.influences[vertex * 2 + influence];
        const auto matrix = Toon::Multiply(pose.joints[weight.joint], skin.geom_bind);
        for (std::size_t row = 0; row < 3; ++row)
          skinned[row] += weight.weight * (matrix.m[row] * point.x + matrix.m[4 + row] * point.y + matrix.m[8 + row] * point.z + matrix.m[12 + row]);
      }
      double object[3]{};
      double center[3]{};
      for (std::size_t row = 0; row < 3; ++row) {
        object[row] = pose.skeleton_to_mesh.m[12 + row];
        for (std::size_t axis = 0; axis < 3; ++axis)
          object[row] += pose.skeleton_to_mesh.m[4 * axis + row] * skinned[axis];
      }
      for (std::size_t row = 0; row < 3; ++row) {
        center[row] = model_view.m[12 + row];
        for (std::size_t axis = 0; axis < 3; ++axis)
          center[row] += model_view.m[4 * axis + row] * object[axis];
      }
      const double w = trial % 2 ? -center[2] : 1;
      const double width = mode == Toon::ToonOutlineWidthMode::World ? 0.16 : 2 * 0.08 * w / view.projection.m[5];
      for (int latitude = -4; latitude <= 4; ++latitude) {
        const double z_direction = latitude / 4.0;
        const double ring = std::sqrt(1 - z_direction * z_direction);
        for (int direction = 0; direction < 32; ++direction) {
          const double angle = direction * 6.283185307 / 32;
          const double x = (center[0] + width * ring * std::cos(angle)) * view.projection.m[0];
          const double y = (center[1] + width * ring * std::sin(angle)) * view.projection.m[5];
          const double z_view = center[2] + width * z_direction;
          const double z = z_view * view.projection.m[10] + view.projection.m[14];
          const double clip_w = trial % 2 ? -z_view : 1;
          if (std::abs(x) <= clip_w && std::abs(y) <= clip_w && std::abs(z) <= clip_w) {
            ++visible;
            if (!Check(!omitted, "joint bounds culled a visible extrusion sample"))
              return 1;
          }
        }
      }
    }
  }
  if (!Check(omissions > 100 && visible > 100, "oracle must exercise visible and omitted poses"))
    return 1;
  skin.influences[0].weight = -0.25F;
  world.SetMeshSkin(id, skin);
  mesh = world.Commit().meshes[0];
  mesh.transform.m[12] = 100;
  bounds.Update(mesh);
  if (!Check(!outside(), "negative weights must conservatively retain hulls"))
    return 1;
  world.SetMeshPoints(id, {{0, 0, 0}, {100, 0, 0}, {0, 1, 0}});
  world.SetMeshSkin(id, {});
  mesh = world.Commit().meshes[0];
  mesh.transform.m[12] = -100;
  view = {};
  bounds.Update(mesh);
  if (!Check(!outside(), "points edits must refresh the envelope"))
    return 1;
  world.SetMeshTopology(id, {0, 0, 0});
  mesh = world.Commit().meshes[0];
  mesh.transform.m[12] = -100;
  bounds.Update(mesh);
  if (!Check(outside(), "topology edits must remove unused points"))
    return 1;
  return 0;
}
