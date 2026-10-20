// SPDX-License-Identifier: Apache-2.0
// The core's shared shadow decisions (shadow policy §2, §4, §5): which
// materials cast and receive, which lights are given a view, and how a
// view is fitted, without a GPU.
#include <toon/extraction.hpp>

#include <cmath>
#include <iostream>
#include <vector>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

bool Near(double value, double expected, double tolerance = 1e-4) {
  return std::abs(value - expected) <= tolerance;
}

// A square of half-width `half` at height z, facing +z.
Toon::MeshId Square(Toon::RenderWorld& world, float half, float z,
    Toon::MaterialId material) {
  const Toon::MeshId id = world.CreateMesh();
  world.SetMeshPoints(id, {{-half, -half, z}, {half, -half, z},
                              {half, half, z}, {-half, half, z}});
  world.SetMeshTopology(id, {0, 1, 2, 0, 2, 3});
  world.SetMeshMaterial(id, material);
  return id;
}

// Where `view` puts a point: x and y across it, z its depth.
std::array<double, 3> Project(const Toon::ShadowView& view, double x, double y,
    double z) {
  const auto& m = view.clip_from_world.m;
  std::array<double, 3> result{};
  for (std::size_t row = 0; row < 3; ++row) {
    result[row] = m[row] * x + m[4 + row] * y + m[8 + row] * z + m[12 + row];
  }
  return result;
}

} // namespace

int main() {
  // Roles, before a frame's settings.
  Toon::ToonMaterial mmd;
  mmd.model = Toon::ToonShadingModel::MMD;
  mmd.mmd.cast_self_shadow = true;
  Toon::ToonMaterial mtoon;
  mtoon.model = Toon::ToonShadingModel::MToon;
  Toon::ToonMaterial preview;
  preview.preview.enabled = true;
  Toon::ToonMaterial unlit;
  {
    const Toon::ShadowRole display = Toon::ShadowRoleOf(nullptr);
    const Toon::ShadowRole mmd_role = Toon::ShadowRoleOf(&mmd);
    Toon::ToonMaterial blend = mtoon;
    blend.alpha_mode = Toon::ToonAlphaMode::Blend;
    Toon::ToonMaterial mask = preview;
    mask.alpha_mode = Toon::ToonAlphaMode::Mask;
    mask.double_sided = true;
    const Toon::ShadowRole mask_role = Toon::ShadowRoleOf(&mask);
    const Toon::ShadowRole unlit_role = Toon::ShadowRoleOf(&unlit);
    if (!Check(display.casts && display.receives && display.both_faces &&
                   !display.mmd && !display.cut,
            "an unbound draw must cast both faces and receive") ||
        !Check(mmd_role.mmd && mmd_role.casts && !mmd_role.receives &&
                   mmd_role.both_faces && !mmd_role.cut,
            "an MMD material must cast by castSelfShadow, both faces, uncut") ||
        !Check(Toon::ShadowRoleOf(&mtoon).casts &&
                   !Toon::ShadowRoleOf(&mtoon).both_faces &&
                   Toon::ShadowRoleOf(&mtoon).receives,
            "an opaque single-sided MToon material must cast its front faces") ||
        !Check(!Toon::ShadowRoleOf(&blend).casts &&
                   Toon::ShadowRoleOf(&blend).receives,
            "a Blend surface must receive and not cast") ||
        !Check(mask_role.casts && mask_role.cut && mask_role.both_faces,
            "a double-sided Mask PreviewSurface must cast cut, both faces") ||
        !Check(unlit_role.casts && !unlit_role.receives,
            "an unlit diagnostic material must cast and not receive"))
      return 1;
  }

  // An MMD wall that receives, and an MMD square above it that casts,
  // under a light straight down -z, through an identity camera.
  Toon::RenderWorld world;
  const Toon::MaterialId wall_material = world.CreateMaterial();
  Toon::ToonMaterial wall_values = mmd;
  wall_values.mmd.receive_self_shadow = true;
  world.SetMaterial(wall_material, wall_values);
  const Toon::MaterialId square_material = world.CreateMaterial();
  world.SetMaterial(square_material, mmd);
  const Toon::MeshId wall = Square(world, 1.0F, 0.0F, wall_material);
  Square(world, 0.2F, 0.5F, square_material);
  const Toon::LightId sun = world.CreateLight();
  Toon::ToonLight sun_values;
  sun_values.direction = {0.0F, 0.0F, -1.0F};
  world.SetLight(sun, sun_values);
  Toon::DrawList draws = Toon::ExtractDrawList(world.Commit());
  if (!Check(Toon::FindMaterial(draws, wall_material) != nullptr &&
                 Toon::FindMaterial(draws, 0) == nullptr &&
                 Toon::FindMaterial(draws, 999) == nullptr,
          "FindMaterial must find listed ids alone"))
    return 1;

  Toon::ShadowFit fit;
  fit.Update(draws, 2048);
  if (!Check(fit.views().size() == 1 && fit.views()[0].light == sun &&
                 fit.views()[0].mmd && fit.unviewed() == 0,
          "an MMD scene must draw one view of its directional light for MMD"))
    return 1;
  {
    const Toon::ShadowView& view = fit.views()[0];
    const auto center = Project(view, 0, 0, 0);
    const auto caster = Project(view, 0, 0, 0.5);
    const auto corner = Project(view, 1, 1, 0);
    if (!Check(Near(center[0], 0) && Near(center[1], 0) && center[2] > 0 &&
                   center[2] < 1 && caster[2] >= 0 && caster[2] < center[2],
            "the view must look down the light, the caster nearer than the wall") ||
        !Check(corner[0] > 0.9 && corner[0] < 1 && corner[1] > 0.9 &&
                   corner[1] < 1,
            "a view whose camera sees the whole box must span the box") ||
        !Check(Near(view.texel, 2.0 * 1.02 / 2048, 1e-6) &&
                   Near(view.depth, 0.5 * 1.04, 1e-6),
            "the view must state its texel and depth in scene units"))
      return 1;
  }

  draws.mmd_self_shadow = false;
  fit.Update(draws, 2048);
  if (!Check(fit.views().empty(),
          "with MMD's self-shadow off an MMD scene has nothing to receive"))
    return 1;
  draws.mmd_self_shadow = true;

  // A close camera spends the view's texels on what it sees, in steps of
  // an eighth of an octave and whole texels.
  draws.view.projection.m[0] = 4.0F;
  draws.view.projection.m[5] = 4.0F;
  draws.view.projection.m[12] = -0.4F;
  fit.Update(draws, 2048);
  {
    const Toon::ShadowView& view = fit.views()[0];
    const double across = 2.0 / view.clip_from_world.m[0];
    const double steps = std::log2(across) * 8.0;
    const double texels = -view.clip_from_world.m[12] * 1024.0;
    if (!Check(across < 1.0 && Near(steps, std::round(steps), 1e-3) &&
                   Near(texels, std::round(texels), 1e-2) &&
                   Near(view.texel, across / 2048, 1e-6),
            "a close-up view must follow the camera in octave steps and texels"))
      return 1;
  }
  draws.view = {};

  // MToon receives every family's casters while shared shadows are on.
  Toon::ToonMaterial toon_wall = mtoon;
  world.SetMaterial(wall_material, toon_wall);
  draws = Toon::ExtractDrawList(world.Commit());
  fit.Update(draws, 2048);
  if (!Check(fit.views().size() == 1, "an MToon receiver must have a view"))
    return 1;
  draws.shadows = false;
  fit.Update(draws, 2048);
  if (!Check(fit.views().empty(),
          "with shared shadows off an MToon receiver has no view"))
    return 1;
  draws.shadows = true;
  world.SetMaterial(wall_material, unlit);
  draws = Toon::ExtractDrawList(world.Commit());
  fit.Update(draws, 2048);
  if (!Check(fit.views().empty(), "an unlit scene receives nothing"))
    return 1;
  world.SetMaterial(wall_material, toon_wall);

  // Lights: the first shadowing directional light has the view; MMD reads
  // it only when it is the first direct light.
  const Toon::LightId spot = world.CreateLight();
  Toon::ToonLight spot_values;
  spot_values.type = Toon::ToonLightType::Spot;
  spot_values.position = {0.0F, 0.0F, 3.0F};
  world.SetLight(spot, spot_values);
  world.RemoveLight(sun);
  const Toon::LightId second = world.CreateLight();
  world.SetLight(second, sun_values);
  draws = Toon::ExtractDrawList(world.Commit());
  fit.Update(draws, 2048);
  if (!Check(fit.views().size() == 1 && fit.views()[0].light == second &&
                 !fit.views()[0].mmd && fit.unviewed() == 1,
          "a spot light first must leave MMD unshadowed and be reported"))
    return 1;
  Toon::ToonLight off = sun_values;
  off.shadow = false;
  world.SetLight(second, off);
  world.SetLight(spot, [&] {
    Toon::ToonLight point = spot_values;
    point.type = Toon::ToonLightType::Point;
    return point;
  }());
  draws = Toon::ExtractDrawList(world.Commit());
  fit.Update(draws, 2048);
  if (!Check(fit.views().empty() && fit.unviewed() == 0,
          "a light whose shadow is off, or a point light, draws no view"))
    return 1;
  world.SetLight(second, sun_values);
  const Toon::LightId third = world.CreateLight();
  world.SetLight(third, sun_values);
  draws = Toon::ExtractDrawList(world.Commit());
  fit.Update(draws, 2048);
  if (!Check(fit.views().size() == 1 && fit.views()[0].light == second &&
                 fit.unviewed() == 1,
          "lights past the capacity must be reported"))
    return 1;

  // The fallback key, with no rig or with scene lights off, casts and is
  // MMD's light.
  draws.lighting.scene_lights = false;
  fit.Update(draws, 2048);
  if (!Check(fit.views().size() == 1 && fit.views()[0].light == 0 &&
                 fit.views()[0].mmd,
          "the fallback key must cast as MMD's light"))
    return 1;
  draws.lighting.scene_lights = true;

  // A pose moves the view, through the joint boxes alone.
  world.RemoveLight(spot);
  world.RemoveLight(third);
  Toon::ToonSkin skin;
  skin.influences_per_point = 1;
  skin.constant = true;
  skin.influences = {{0, 1.0F}};
  world.SetMeshSkin(wall, skin);
  Toon::ToonSkinPose pose;
  pose.joints.resize(1);
  world.SetMeshSkinPose(wall, pose);
  draws = Toon::ExtractDrawList(world.Commit());
  fit.Update(draws, 2048);
  const double before = Project(fit.views()[0], 0, 0, 0)[0];
  pose.joints[0].m[12] = 0.5F;
  world.SetMeshSkinPose(wall, pose);
  const Toon::FrameSnapshot posed = world.Commit();
  if (!Check(posed.meshes[0].points_revision == draws.draws[0].points_revision,
          "a pose must not change the rest points"))
    return 1;
  draws = Toon::ExtractDrawList(posed);
  fit.Update(draws, 2048);
  if (!Check(Project(fit.views()[0], 0, 0, 0)[0] < before - 0.1,
          "a pose must move the view with the posed receiver"))
    return 1;

  std::cout << "shadow fit: roles, lights, fit and pose pass\n";
  return 0;
}
