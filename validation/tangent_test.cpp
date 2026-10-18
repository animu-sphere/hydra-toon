// SPDX-License-Identifier: Apache-2.0
// Authored frames compared with independently CPU-evaluated surface normals.
#include <toon/vulkan_backend.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}
Toon::Float3 Scale(Toon::Float3 v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}
Toon::Float3 Add(Toon::Float3 a, Toon::Float3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
float Dot(Toon::Float3 a, Toon::Float3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
Toon::Float3 Cross(Toon::Float3 a, Toon::Float3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Toon::Float3 Unit(Toon::Float3 v) {
  return Scale(v, 1 / std::sqrt(Dot(v, v)));
}
Toon::Float3 Direction(const Toon::Matrix4& m, Toon::Float3 v) {
  return {m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
      m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
      m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z};
}
float Sign(const Toon::Matrix4& m) {
  return Dot({m.m[0], m.m[1], m.m[2]}, Cross({m.m[4], m.m[5], m.m[6]},
                                           {m.m[8], m.m[9], m.m[10]})) < 0
             ? -1.0F
             : 1.0F;
}
void State() {
  Toon::RenderWorld world;
  world.SetBootstrapTriangle();
  const auto mesh = world.Commit().meshes[0].id;
  const std::vector<Toon::Float4> tangents(3, {0, 1, 0, -1});
  world.SetMeshTangents(mesh, tangents);
  const auto first = world.Commit();
  Require(*first.meshes[0].tangents == tangents, "authored tangent/sign lost");
  world.SetMeshTangents(mesh, tangents);
  Require(world.Commit().revision == first.revision, "equal tangents changed revisions");
  auto draws = Toon::ExtractDrawList(first);
  auto bad = first;
  ++bad.meshes[0].tangents_revision;
  std::string error;
  Require(!Toon::ApplyFastSnapshot(bad, draws, error), "late tangent revision accepted");
  bad = first;
  bad.meshes[0].tangents = std::make_shared<const std::vector<Toon::Float4>>(tangents);
  Require(!Toon::ApplyFastSnapshot(bad, draws, error), "late tangent storage accepted");
  world.SetMeshTransform(mesh, Toon::Matrix4{});
  world.SetMeshPoints(mesh, *first.meshes[0].points);
  auto next = world.Commit();
  Require(next.meshes[0].tangents == first.meshes[0].tangents &&
              next.meshes[0].tangents_revision == first.meshes[0].tangents_revision,
      "point edits replaced authored tangents");
  for (const auto invalid : {Toon::Float4{0, 0, 0, 1}, Toon::Float4{1, 0, 0, 0},
           Toon::Float4{1, 0, 0, 2}, Toon::Float4{std::numeric_limits<float>::infinity(), 0, 0, 1}}) {
    world.SetMeshTangents(mesh, tangents);
    world.SetMeshTangents(mesh, {invalid});
    Require(world.Commit().meshes[0].tangents->empty(), "invalid tangent did not clear stale data");
  }
  Require(*first.meshes[0].tangents == tangents, "old snapshot mutated");
}

bool Gpu(const Toon::SceneShaders& shaders, unsigned samples) {
  Toon::FrameStatus status;
  std::string error;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, error, {samples});
  if (status == Toon::FrameStatus::Skip) {
    std::cout << error << '\n';
    return false;
  }
  auto reference = Toon::CreateOffscreenRenderer(shaders, status, error, {samples});
  Require(renderer && reference, error.c_str());
  Toon::RenderWorld world, oracle;
  const auto mesh = world.CreateMesh(), ref = oracle.CreateMesh();
  const std::vector<Toon::Float3> rest{{-0.6F, -0.6F, 0}, {0.6F, -0.6F, 0}, {0, 0.6F, 0}};
  world.SetMeshPoints(mesh, rest);
  world.SetMeshTopology(mesh, {0, 1, 2});
  oracle.SetMeshTopology(ref, {0, 1, 2});
  world.SetMeshNormals(mesh, std::vector<Toon::Float3>(3, {0, 0, 1}));
  // Constant UVs deliberately cannot supply a derivative frame. The authored
  // direction also differs from a position/UV-derived tangent.
  world.SetMeshUVs(mesh, std::vector<Toon::Float2>(3, {0.5F, 0.5F}));
  Toon::ToonSkin skin;
  skin.influences_per_point = 2;
  skin.constant = true;
  skin.influences = {{0, 0.25F}, {1, 0.75F}};
  world.SetMeshSkin(mesh, skin);
  world.SetMeshMorph(mesh, {{{{0.125F, 0, 0}, 0, {0.25F, 0, 0}, 0}}, {{0, 1}, {0, 1}, {0, 1}}});
  Toon::ToonTexture texture;
  texture.width = texture.height = 1;
  texture.encoding = Toon::ToonTextureEncoding::Linear;
  texture.pixels = std::make_shared<const std::vector<std::uint8_t>>(
      std::vector<std::uint8_t>{191, 230, 204, 255});
  const auto tex = world.CreateTexture();
  world.SetTexture(tex, texture);
  Toon::ToonMaterial material;
  material.model = Toon::ToonShadingModel::MToon;
  material.base_color = {0.8F, 0.6F, 0.4F};
  material.mtoon.shade_color = {0.05F, 0.1F, 0.15F};
  material.mtoon.shading_toony = 0;
  material.normal_scale = 0.75F;
  const auto mat = world.CreateMaterial(), refmat = oracle.CreateMaterial();
  world.SetMeshMaterial(mesh, mat);
  oracle.SetMeshMaterial(ref, refmat);
  for (int mode = 0; mode < 3; ++mode) {
    material.alpha_mode = static_cast<Toon::ToonAlphaMode>(mode);
    material.alpha = mode == 2 ? 0.65F : 1.0F;
    for (int frame = 0; frame < 8; ++frame) {
      const float handedness = frame % 2 ? -1.0F : 1.0F;
      world.SetMeshTangents(mesh, std::vector<Toon::Float4>(3, {0, 1, 0, handedness}));
      Toon::Matrix4 joint0, joint1, model;
      // Rotate +90 degrees, with nonuniform scales and optional reflection.
      joint0.m[0] = joint0.m[5] = 0;
      joint0.m[1] = frame & 2 ? -0.75F : 0.75F;
      joint0.m[4] = -0.5F;
      joint1 = joint0;
      joint1.m[4] = -0.75F;
      Toon::Matrix4 blended = joint0;
      blended.m[4] = -0.6875F;
      world.SetMeshSkinPose(mesh, {{joint0, joint1}, {}});
      model.m[0] = frame & 4 ? -0.8F : 0.8F;
      model.m[5] = 1.25F;
      world.SetMeshTransform(mesh, model);
      const float weight = frame % 3 * 0.5F;
      world.SetMeshMorphWeights(mesh, {weight});
      // Independent CPU reference: classicLinear normal deformation, then
      // model inverse transpose; directions take the forward linear map.
      auto n = Direction(blended, {0.25F * weight, 0, 1});
      n = Unit({n.x / model.m[0], n.y / model.m[5], n.z});
      auto t = Direction(model, Direction(blended, {0, 1, 0}));
      t = Unit(Add(t, Scale(n, -Dot(n, t))));
      const float sign = handedness * Sign(blended) * Sign(model);
      auto mapped = Unit(Add(Add(Scale(t, (191 / 255.0F * 2 - 1) * material.normal_scale),
                                 Scale(Cross(n, t), -sign * (230 / 255.0F * 2 - 1) * material.normal_scale)),
          Scale(n, 204 / 255.0F * 2 - 1)));
      auto points = rest;
      for (auto& point : points)
        point = Direction(model,
            Direction(blended, Add(point, {0.125F * weight, 0, 0})));
      oracle.SetMeshPoints(ref, points);
      oracle.SetMeshNormals(ref, std::vector<Toon::Float3>(3, mapped));
      material.double_sided = true;
      material.normal_texture.texture = tex;
      world.SetMaterial(mat, material);
      material.normal_texture.texture = 0;
      oracle.SetMaterial(refmat, material);
      auto actual = Toon::ExtractDrawList(world.Commit());
      auto expected = Toon::ExtractDrawList(oracle.Commit());
      // The oracle bakes the model into points; compensate the front-face
      // setting the actual draw applies for that model's reflection.
      oracle.SetMeshTopology(ref, Sign(model) < 0 ? std::vector<std::uint32_t>{0, 2, 1} : std::vector<std::uint32_t>{0, 1, 2});
      expected = Toon::ExtractDrawList(oracle.Commit());
      for (auto* draws : {&actual, &expected}) {
        draws->lighting.key_direction = {0.2F, -0.75F, 0.6F};
        draws->lighting.direct_scale = 0.7F;
        draws->lighting.ambient_scale = 0;
      }
      Toon::ColorProduct color, target;
      Toon::DepthProduct depth, target_depth;
      Require(renderer->Render(actual, 96, 96, color, depth, error), error.c_str());
      Require(reference->Render(expected, 96, 96, target, target_depth, error), error.c_str());
      int maximum = 0;
      for (std::size_t i = 0; i < color.payload.size(); ++i)
        maximum = std::max(maximum, std::abs(int(color.payload[i]) - int(target.payload[i])));
      if (maximum > 2)
        std::cerr << "samples=" << samples << " mode=" << mode << " frame=" << frame << " max=" << maximum << '\n';
      Require(maximum <= 2, "authored tangent shading differs from CPU normal oracle");
      Require(renderer->statistics().validation_message_count == 0, "tangent Vulkan validation error");
      if (frame != 0)
        Require(renderer->statistics().point_uploads == 1 &&
                    renderer->statistics().topology_uploads == 1 && renderer->statistics().skin_uploads == 1 &&
                    renderer->statistics().morph_uploads == 1,
            "motion/tangent edit rebuilt other static buffers");
      if (mode == 2 && frame == 7) {
        oracle.SetMeshNormals(ref, std::vector<Toon::Float3>(3, n));
        expected = Toon::ExtractDrawList(oracle.Commit());
        expected.lighting = actual.lighting;
        Require(reference->Render(expected, 96, 96, target, target_depth, error), error.c_str());
        for (int missing = 0; missing < 3; ++missing) {
          world.SetMeshTangents(mesh, missing == 0 ? std::vector<Toon::Float4>{} : std::vector<Toon::Float4>(missing == 1 ? 1 : 3, {0, 1, 0, 1}));
          if (missing == 2)
            world.SetMeshUVs(mesh, {});
          actual = Toon::ExtractDrawList(world.Commit());
          actual.lighting = expected.lighting;
          Require(renderer->Render(actual, 96, 96, color, depth, error), error.c_str());
          for (std::size_t i = 0; i < color.payload.size(); ++i)
            Require(std::abs(int(color.payload[i]) - int(target.payload[i])) <= 2,
                "missing/short tangent or absent UV did not retain the vertex normal");
        }
      }
    }
  }
  // Draw only a back-facing hull. Normal debug gives an analytic colour
  // oracle without changing the rest normal that controls hull geometry.
  world.SetMeshSkin(mesh, {});
  world.SetMeshMorph(mesh, {});
  world.SetMeshMorphWeights(mesh, {});
  world.SetMeshTransform(mesh, {});
  world.SetMeshPoints(mesh, rest);
  world.SetMeshTopology(mesh, {0, 2, 1});
  world.SetMeshNormals(mesh, std::vector<Toon::Float3>(3, {0, 0, 1}));
  world.SetMeshUVs(mesh, std::vector<Toon::Float2>(3, {0.5F, 0.5F}));
  material.alpha_mode = Toon::ToonAlphaMode::Opaque;
  material.alpha = 1;
  material.double_sided = false;
  material.normal_texture.texture = tex;
  material.outline = true;
  material.outline_width = 0.05F;
  material.outline_color = {1, 1, 1};
  material.mtoon.outline_lighting_mix = 1;
  material.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
  world.SetMaterial(mat, material);
  for (float sign : {-1.0F, 1.0F}) {
    world.SetMeshTangents(mesh, std::vector<Toon::Float4>(3, {0, 1, 0, sign}));
    auto draws = Toon::ExtractDrawList(world.Commit());
    draws.lighting.material = Toon::MaterialDebug::Normal;
    Toon::ColorProduct color;
    Toon::DepthProduct depth;
    Require(renderer->Render(draws, 96, 96, color, depth, error), error.c_str());
    const auto mapped = Unit({sign * (230 / 255.0F * 2 - 1) * material.normal_scale,
        (191 / 255.0F * 2 - 1) * material.normal_scale, 204 / 255.0F * 2 - 1});
    const auto pixel = color.row_pitch * 48 + 48 * 4;
    const float normal[] = {mapped.x, mapped.y, mapped.z};
    for (int channel = 0; channel < 3; ++channel)
      Require(std::abs(color.payload[pixel + channel] - (0.5F - normal[channel] * 0.5F) * 255) <= 2,
          "outline did not use the authored tangent frame on its back face");
    Require(renderer->statistics().validation_message_count == 0, "outline tangent validation error");
  }
  std::cout << "authored tangents: " << samples << "x, 24 CPU-reference comparisons passed\n";
  return true;
}
} // namespace
int main(int argc, char** argv) {
  try {
    State();
    if (argc == 1)
      return 0;
    return Gpu(Toon::SceneShadersIn(argv[1]), 1) && Gpu(Toon::SceneShadersIn(argv[1]), 4) ? 0 : 77;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
