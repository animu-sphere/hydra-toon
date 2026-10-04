// SPDX-License-Identifier: Apache-2.0
// Transient expressions, with ordinary scene setters as the GPU oracle.
#include <toon/vulkan_backend.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

Toon::ToonMorph Targets() {
  return {{{{0.3F, 0, 0}, 0, {0.2F, 0, 0}, 0}}, {{0, 1}, {0, 1}, {0, 1}}};
}

struct Scene {
  Toon::RenderWorld world;
  Toon::MeshId mesh;
  Toon::MaterialId material;
  Toon::ToonMaterial values;

  explicit Scene(Toon::ToonAlphaMode mode = Toon::ToonAlphaMode::Opaque) {
    mesh = world.CreateMesh();
    world.SetMeshPoints(mesh, {{-0.4F, -0.4F, 0}, {0.4F, -0.4F, 0}, {0, 0.4F, 0}});
    world.SetMeshNormals(mesh, std::vector<Toon::Float3>(3, {0.3F, 0, 1}));
    world.SetMeshTopology(mesh, {0, 1, 2, 2, 1, 0});
    world.SetMeshMorph(mesh, Targets());
    world.SetMeshMorphWeights(mesh, {0});
    Toon::ToonSkin skin;
    skin.influences_per_point = 1;
    skin.constant = true;
    skin.influences = {{0, 1}};
    world.SetMeshSkin(mesh, skin);
    Toon::ToonSkinPose pose;
    Toon::Matrix4 joint;
    joint.m[13] = 0.1F;
    pose.joints = {joint};
    world.SetMeshSkinPose(mesh, pose);
    material = world.CreateMaterial();
    values.model = Toon::ToonShadingModel::MToon;
    values.alpha_mode = mode;
    values.base_color = {0.7F, 0.3F, 0.2F};
    values.mtoon.shade_color = {0.2F, 0.1F, 0.05F};
    values.outline = true;
    values.outline_width = 0.03F;
    values.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
    if (mode == Toon::ToonAlphaMode::Blend) values.alpha = 0.6F;
    world.SetMaterial(material, values);
    world.SetMeshMaterial(mesh, material);
  }
};

void SlowState(const Toon::FrameSnapshot& before, const Toon::FrameSnapshot& after) {
  const auto& a = before.meshes[0];
  const auto& b = after.meshes[0];
  Require(a.points == b.points && a.normals == b.normals && a.indices == b.indices &&
      a.uvs == b.uvs && a.influences == b.influences && a.joints == b.joints &&
      a.morph_offsets == b.morph_offsets && a.morph_ranges == b.morph_ranges &&
      a.points_revision == b.points_revision && a.normals_revision == b.normals_revision &&
      a.topology_revision == b.topology_revision && a.uvs_revision == b.uvs_revision &&
      a.skin_revision == b.skin_revision && a.pose_revision == b.pose_revision &&
      a.morph_revision == b.morph_revision &&
      before.materials[0].structure_revision == after.materials[0].structure_revision,
      "expression changed slow scene state");
}

void Core() {
  Scene s;
  const auto scene = s.world.Commit();
  Require(s.world.SetMeshMorphWeightsOverride(s.mesh, {0}) &&
      s.world.SetMaterialParametersOverride(s.material, s.values), "baseline override rejected");
  Require(s.world.Commit().revision == scene.revision, "baseline override caused redundant writes");
  s.world.ClearMeshMorphWeightsOverride(s.mesh);
  s.world.ClearMaterialParametersOverride(s.material);
  Require(s.world.Commit().revision == scene.revision, "baseline clear caused redundant writes");
  auto values = s.values;
  values.base_color = {0.1F, 0.6F, 0.2F};
  values.base_texture.offset = {0.2F, 0.3F};
  Require(s.world.SetMeshMorphWeightsOverride(s.mesh, {-0.5F}) &&
      s.world.SetMaterialParametersOverride(s.material, values), "valid overrides rejected");
  const auto overridden = s.world.Commit();
  SlowState(scene, overridden);
  Require(*overridden.meshes[0].morph_weights == std::vector<float>{-0.5F} &&
      overridden.materials[0].material == values, "overrides absent from snapshot");
  Require(*scene.meshes[0].morph_weights == std::vector<float>{0} &&
      scene.materials[0].material == s.values, "override mutated an old snapshot");
  Require(s.world.SetMeshMorphWeightsOverride(s.mesh, {-0.5F}) &&
      s.world.SetMaterialParametersOverride(s.material, values), "identical overrides rejected");
  Require(s.world.Commit().revision == overridden.revision, "identical override dirtied scene");

  auto structural = values;
  structural.alpha_mode = Toon::ToonAlphaMode::Blend;
  Require(!s.world.SetMaterialParametersOverride(s.material, structural), "alpha-mode edit accepted");
  structural = values;
  structural.base_texture.texture = 42;
  Require(!s.world.SetMaterialParametersOverride(s.material, structural), "texture identity edit accepted");
  Require(!s.world.SetMeshMorphWeightsOverride(999, {1}) &&
      !s.world.SetMaterialParametersOverride(999, values) &&
      !s.world.SetMeshMorphWeightsOverride(s.mesh, {}) &&
      !s.world.SetMeshMorphWeightsOverride(s.mesh, {1, 2}) &&
      !s.world.SetMeshMorphWeightsOverride(s.mesh, {std::numeric_limits<float>::infinity()}),
      "invalid override accepted");
  Require(s.world.Commit().revision == overridden.revision, "rejected input changed scene");

  // The scene can keep evaluating underneath a persistent expression.
  s.world.SetMeshMorphWeights(s.mesh, {0.25F});
  s.values.emissive = {0.1F, 0.2F, 0.3F};
  s.world.SetMaterial(s.material, s.values);
  const auto synced = s.world.Commit();
  Require(synced.meshes[0].morph_weights == overridden.meshes[0].morph_weights &&
      synced.meshes[0].morph_weights_revision == overridden.meshes[0].morph_weights_revision &&
      synced.materials[0].material == values &&
      synced.materials[0].parameters_revision == overridden.materials[0].parameters_revision,
      "scene value edit displaced persistent override");
  s.world.ClearMeshMorphWeightsOverride(s.mesh);
  s.world.ClearMaterialParametersOverride(s.material);
  const auto restored = s.world.Commit();
  Require(*restored.meshes[0].morph_weights == std::vector<float>{0.25F} &&
      restored.materials[0].material == s.values, "clear did not restore latest scene values");
  SlowState(scene, restored);
  s.world.ClearMeshMorphWeightsOverride(s.mesh);
  s.world.ClearMaterialParametersOverride(s.material);
  Require(s.world.Commit().revision == restored.revision, "repeated clear changed scene");

  Require(s.world.SetMeshMorphWeightsOverride(s.mesh, {1}), "override rejected");
  auto targets = Targets();
  targets.offsets[0].position.x = 0.2F;
  s.world.SetMeshMorph(s.mesh, targets);
  Require(*s.world.Commit().meshes[0].morph_weights == std::vector<float>{0.25F},
      "target edit kept stale expression binding");
  Require(s.world.SetMeshMorphWeightsOverride(s.mesh, {1}), "override rejected");
  s.world.SetMeshMorphWeights(s.mesh, {0, 0});
  Require(*s.world.Commit().meshes[0].morph_weights == std::vector<float>({0, 0}),
      "weight count edit kept stale override");
  Require(s.world.SetMaterialParametersOverride(s.material, values), "override rejected");
  s.values.double_sided = true;
  s.world.SetMaterial(s.material, s.values);
  Require(s.world.Commit().materials[0].material == s.values, "structural edit kept stale override");
  s.world.RemoveMesh(s.mesh);
  s.world.RemoveMaterial(s.material);
  Require(!s.world.SetMeshMorphWeightsOverride(s.mesh, {1}) &&
      !s.world.SetMaterialParametersOverride(s.material, values), "removed ids accepted");
  s.world.ClearMeshMorphWeightsOverride(s.mesh);
  s.world.ClearMaterialParametersOverride(s.material);
}

bool Gpu(const Toon::SceneShaders& shaders, Toon::ToonAlphaMode mode, bool outlines) {
  Toon::FrameStatus status;
  std::string error;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, error, {1});
  if (status == Toon::FrameStatus::Skip) { std::cout << error << '\n'; return false; }
  Require(renderer != nullptr, error.c_str());
  auto reference = Toon::CreateOffscreenRenderer(shaders, status, error, {1});
  Require(reference != nullptr, error.c_str());
  Scene s(mode), oracle(mode);
  Toon::ColorProduct color, expected;
  Toon::DepthProduct depth, expected_depth;
  Toon::FrameSnapshot snapshot;
  Toon::DrawList draws;
  std::vector<std::uint8_t> initial;
  for (int frame = 0; frame < 30; ++frame) {
    const int phase = frame % 5;
    auto values = s.values;
    const float weight = phase == 1 || phase == 2 ? 0.75F : phase == 3 ? -0.25F : 0;
    if (phase == 2 || phase == 3) {
      values.base_color = {0.2F, 0.6F, 0.3F};
      values.mtoon.shading_shift = -0.2F;
      values.outline_width = 0.05F;
    }
    if (phase == 0 || phase == 4) {
      s.world.ClearMeshMorphWeightsOverride(s.mesh);
      s.world.ClearMaterialParametersOverride(s.material);
    } else {
      Require(s.world.SetMeshMorphWeightsOverride(s.mesh, {weight}) &&
          s.world.SetMaterialParametersOverride(s.material, values), "GPU override rejected");
    }
    oracle.world.SetMeshMorphWeights(oracle.mesh, {weight});
    oracle.world.SetMaterial(oracle.material, values);
    s.world.Commit(snapshot);
    Toon::ExtractDrawList(snapshot, draws);
    draws.outlines = outlines;
    auto reference_draws = Toon::ExtractDrawList(oracle.world.Commit());
    reference_draws.outlines = outlines;
    const auto before = renderer->statistics();
    Require(renderer->Render(draws, 96, 96, color, depth, error), error.c_str());
    Require(reference->Render(reference_draws, 96, 96, expected, expected_depth, error), error.c_str());
    Require(color.payload == expected.payload, "expression colour differs from scene-setter oracle");
    Require(depth.payload.size() == expected_depth.payload.size(), "depth extent mismatch");
    for (std::size_t i = 0; i < depth.payload.size(); ++i)
      Require(std::abs(depth.payload[i] - expected_depth.payload[i]) <= 1e-6F, "expression depth differs");
    if (frame == 0) initial = color.payload;
    if (phase == 1 || phase == 2 || phase == 3)
      Require(color.payload != initial, "image did not detect expression");
    else Require(color.payload == initial, "clear did not restore baseline image");
    const auto& after = renderer->statistics();
    Require(after.validation_message_count == 0, "Vulkan validation message");
    if (frame != 0) {
      Require(after.point_uploads == before.point_uploads && after.topology_uploads == before.topology_uploads &&
          after.skin_uploads == before.skin_uploads && after.pose_writes == before.pose_writes &&
          after.morph_uploads == before.morph_uploads && after.texture_uploads == before.texture_uploads &&
          after.pipelines_created == before.pipelines_created && after.target_allocations == before.target_allocations,
          "expression touched static GPU state");
      Require(after.morph_weight_writes == before.morph_weight_writes + (phase == 0 || phase == 2 ? 0U : 1U),
          "expression wrote unexpected weight buffers");
      Require(after.material_writes == before.material_writes + (phase == 2 || phase == 4 ? 1U : 0U),
          "expression wrote unexpected material slots");
    }
  }
  std::cout << "expression mode=" << static_cast<int>(mode) << " outlines=" << outlines
            << " frames=30 weights=" << renderer->statistics().morph_weight_writes
            << " materials=" << renderer->statistics().material_writes << '\n';
  return true;
}
} // namespace

int main(int argc, char** argv) {
  try {
    Core();
    if (argc == 2) {
      const auto capability = Toon::ProbeVulkanBackend();
      if (!capability.available) { std::cout << capability.detail << '\n'; return 77; }
      for (auto mode : {Toon::ToonAlphaMode::Opaque, Toon::ToonAlphaMode::Mask, Toon::ToonAlphaMode::Blend})
        for (bool outlines : {false, true})
          if (!Gpu(Toon::SceneShadersIn(argv[1]), mode, outlines)) return 77;
    }
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
