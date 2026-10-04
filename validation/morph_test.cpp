// SPDX-License-Identifier: Apache-2.0
// Sparse GPU morphs against independently CPU-deformed rest geometry.
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
  return {{{{0.25F, 0, 0}, 0, {0.25F, 0, 0}, 0},
              {{0, 0.25F, 0}, 1, {0, 0.25F, 0}, 0},
              {{0.25F, 0, 0}, 0, {0.25F, 0, 0}, 0},
              {{0, 0.25F, 0}, 1, {0, 0.25F, 0}, 0}},
      {{0, 2}, {2, 1}, {3, 1}}};
}
const std::vector<Toon::Float3> kRest{{-0.4F, -0.4F, 0}, {0.4F, -0.4F, 0}, {0, 0.4F, 0}};
const std::vector<Toon::Float3> kNormals(3, {0.5F, 0, 1});

Toon::MeshId Setup(Toon::RenderWorld& world, bool skin) {
  const auto mesh = world.CreateMesh();
  world.SetMeshPoints(mesh, kRest);
  world.SetMeshNormals(mesh, kNormals);
  // Front and back faces exercise the inverted hull as well as the surface.
  world.SetMeshTopology(mesh, {0, 1, 2, 2, 1, 0});
  world.SetMeshColor(mesh, {0.8F, 0.4F, 0.2F});
  if (skin) {
    Toon::ToonSkin binding;
    binding.influences_per_point = 1;
    binding.constant = true;
    binding.influences = {{0, 1}};
    binding.geom_bind.m[0] = 0.75F;
    world.SetMeshSkin(mesh, binding);
    Toon::ToonSkinPose pose;
    Toon::Matrix4 joint;
    joint.m[0] = 0; joint.m[1] = 1;
    joint.m[4] = -1; joint.m[5] = 0;
    pose.joints = {joint};
    world.SetMeshSkinPose(mesh, pose);
  }
  return mesh;
}

void Core() {
  Toon::RenderWorld world;
  const auto mesh = Setup(world, true);
  world.SetMeshMorph(mesh, Targets());
  world.SetMeshMorphWeights(mesh, {0, 0});
  const auto before = world.Commit();
  world.SetMeshMorphWeights(mesh, {0.5F, -0.25F});
  const auto after = world.Commit();
  const auto& a = before.meshes[0]; const auto& b = after.meshes[0];
  Require(Toon::IsMorphed(b) && b.morph_weights_revision != a.morph_weights_revision,
      "weight change did not reach the snapshot");
  Require(a.points == b.points && a.normals == b.normals &&
      a.points_revision == b.points_revision && a.topology_revision == b.topology_revision &&
      a.normals_revision == b.normals_revision && a.skin_revision == b.skin_revision &&
      a.pose_revision == b.pose_revision && a.morph_revision == b.morph_revision &&
      a.morph_offsets == b.morph_offsets && a.morph_ranges == b.morph_ranges,
      "weights dirtied slow state");
  world.SetMeshMorphWeights(mesh, {0.5F, -0.25F});
  Require(world.Commit().revision == after.revision, "identical weights changed the scene");
  world.SetMeshMorphWeights(mesh, {std::numeric_limits<float>::quiet_NaN()});
  auto invalid = Targets(); invalid.ranges[0] = {4, 1};
  world.SetMeshMorph(mesh, invalid);
  Require(world.Commit().revision == after.revision, "invalid morph input changed the scene");
  invalid = Targets(); invalid.offsets[0].normal.x = std::numeric_limits<float>::infinity();
  world.SetMeshMorph(mesh, invalid);
  Require(world.Commit().revision == after.revision, "non-finite target changed the scene");
  Toon::OutlineBounds bounds; bounds.Update(b);
  auto outside = b; outside.transform.m[12] = 10;
  Require(!bounds.OutsideView(outside, {}, 0.01F, Toon::ToonOutlineWidthMode::World, 1),
      "rest bounds culled a morphed hull");
  world.SetMeshMorphWeights(mesh, {});
  Require(!Toon::IsMorphed(world.Commit().meshes[0]), "empty weights did not disable morphs");
  world.SetMeshMorphWeights(mesh, {1});
  world.SetMeshTopology(mesh, {0, 1, 3});
  Require(!Toon::IsMorphed(world.Commit().meshes[0]), "short ranges enabled an invalid draw");
}

bool Gpu(const Toon::SceneShaders& shaders, bool skin, int mode) {
  Toon::FrameStatus status;
  std::string error;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, error, {1});
  if (status == Toon::FrameStatus::Skip) {
    std::cout << error << '\n';
    return false;
  }
  Require(renderer != nullptr, error.c_str());
  auto reference = Toon::CreateOffscreenRenderer(shaders, status, error, {1});
  Require(reference != nullptr, error.c_str());
  Toon::RenderWorld world, oracle;
  const auto mesh = Setup(world, skin), ref = Setup(oracle, skin);
  auto targets = Targets();
  world.SetMeshMorph(mesh, targets);
  if (mode != 0) {
    Toon::ToonMaterial material;
    material.model = Toon::ToonShadingModel::MToon;
    material.base_color = {0.7F, 0.3F, 0.2F};
    material.mtoon.shade_color = {0.3F, 0.2F, 0.1F};
    material.outline = true; material.outline_width = 0.025F;
    material.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
    if (mode == 2) { material.alpha_mode = Toon::ToonAlphaMode::Blend; material.alpha = 0.6F; }
    if (mode == 3) material.alpha_mode = Toon::ToonAlphaMode::Mask;
    const auto mat = world.CreateMaterial(), refmat = oracle.CreateMaterial();
    world.SetMaterial(mat, material); oracle.SetMaterial(refmat, material);
    world.SetMeshMaterial(mesh, mat); oracle.SetMeshMaterial(ref, refmat);
  }
  Toon::ColorProduct color, expected;
  Toon::DepthProduct depth, expected_depth;
  const std::vector<std::vector<float>> samples{{0, 0}, {1, 0.5F}, {-0.5F, 1},
      {0.25F}, {}, {0.5F, -0.25F}};
  for (int frame = 0; frame < 36; ++frame) {
    const auto& weights = samples[static_cast<std::size_t>(frame) % samples.size()];
    world.SetMeshMorphWeights(mesh, weights);
    auto points = kRest, normals = kNormals;
    for (std::size_t point = 0; point < points.size(); ++point) {
      const auto range = targets.ranges[point];
      for (std::uint32_t i = 0; i < range.count; ++i) {
        const auto& offset = targets.offsets[range.first + i];
        if (offset.target >= weights.size()) continue;
        const float weight = weights[offset.target];
        points[point].x += offset.position.x * weight;
        points[point].y += offset.position.y * weight;
        points[point].z += offset.position.z * weight;
        normals[point].x += offset.normal.x * weight;
        normals[point].y += offset.normal.y * weight;
        normals[point].z += offset.normal.z * weight;
      }
    }
    oracle.SetMeshPoints(ref, points); oracle.SetMeshNormals(ref, normals);
    const auto before = renderer->statistics();
    Require(renderer->Render(Toon::ExtractDrawList(world.Commit()), 96, 96, color, depth, error), error.c_str());
    Require(reference->Render(Toon::ExtractDrawList(oracle.Commit()), 96, 96, expected, expected_depth, error), error.c_str());
    Require(color.payload == expected.payload, "GPU morph colour differs from CPU rest-geometry oracle");
    Require(depth.payload.size() == expected_depth.payload.size(), "depth extent mismatch");
    for (std::size_t i = 0; i < depth.payload.size(); ++i)
      Require(std::abs(depth.payload[i] - expected_depth.payload[i]) <= 1e-6F, "GPU morph depth differs from oracle");
    const auto& stats = renderer->statistics();
    Require(stats.validation_message_count == 0, "Vulkan validation reported an error");
    if (frame != 0) {
      Require(stats.point_uploads == before.point_uploads && stats.topology_uploads == before.topology_uploads &&
          stats.skin_uploads == before.skin_uploads && stats.pose_writes == before.pose_writes &&
          stats.material_writes == before.material_writes && stats.morph_uploads == before.morph_uploads &&
          stats.pipelines_created == before.pipelines_created && stats.target_allocations == before.target_allocations,
          "a morph weight change touched static GPU state");
      Require(stats.morph_weight_writes == before.morph_weight_writes + (weights.empty() ? 0U : 1U),
          "weight update wrote unexpected buffers");
    }
  }
  std::cout << "morph skin=" << skin << " mode=" << mode << " frames=36 targets="
      << renderer->statistics().morph_uploads << " weights=" << renderer->statistics().morph_weight_writes
      << " points=" << renderer->statistics().point_uploads << " pipelines="
      << renderer->statistics().pipelines_created << '\n';
  return true;
}
} // namespace

int main(int argc, char** argv) {
  try {
    Core();
    if (argc == 2) {
      const auto capability = Toon::ProbeVulkanBackend();
      if (!capability.available) { std::cout << capability.detail << '\n'; return 77; }
      for (bool skin : {false, true}) for (int mode = 0; mode < 4; ++mode)
        if (!Gpu(Toon::SceneShadersIn(argv[1]), skin, mode)) return 77;
    }
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
