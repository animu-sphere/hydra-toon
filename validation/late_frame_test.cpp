// SPDX-License-Identifier: Apache-2.0
#include <toon/vulkan_backend.hpp>
#if defined(TOON_HAS_WEBGPU)
#include <toon/webgpu_backend.hpp>
#endif
#include "../adapters/viewport/src/telemetry.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
// A backend's offscreen renderer, so the same checks run on Vulkan and on
// WebGPU (WebGPU policy §9).
using Factory = std::function<std::unique_ptr<Toon::OffscreenRenderer>(
    Toon::FrameStatus&, std::string&, const Toon::RenderOptions&)>;

void Require(bool condition, const char* why) { if (!condition) throw std::runtime_error(why); }

struct Scene {
  Toon::RenderWorld world;
  Toon::MeshId mesh;
  Toon::MaterialId material;
  Toon::ToonMaterial values;
  Scene() {
    mesh = world.CreateMesh();
    world.SetMeshPoints(mesh, {{-0.4F,-0.4F,0}, {0.4F,-0.4F,0}, {0,0.4F,0}});
    world.SetMeshTopology(mesh, {0,1,2,2,1,0});
    world.SetMeshMorph(mesh, {{{{0.4F,0,0},0,{},0}},{{0,1},{0,1},{0,1}}});
    world.SetMeshMorphWeights(mesh, {0});
    Toon::ToonSkin skin;
    skin.influences_per_point = 1;
    skin.constant = true;
    skin.influences = {{0,1}};
    world.SetMeshSkin(mesh, skin);
    world.SetMeshSkinPose(mesh, {{Toon::Matrix4{}}, {}});
    material = world.CreateMaterial();
    values.model = Toon::ToonShadingModel::MToon;
    values.base_color = {0.7F,0.2F,0.1F};
    world.SetMaterial(material, values);
    world.SetMeshMaterial(mesh, material);
  }
};

void State() {
  Scene s;
  const auto baseline = s.world.Commit();
  auto extracted = Toon::ExtractDrawList(baseline);
  const auto* storage = extracted.draws.data();
  Toon::ToonSkinPose pose{{Toon::Matrix4{}}, {}};
  pose.joints[0].m[13] = 0.2F;
  s.world.SetMeshSkinPose(s.mesh, pose);
  s.world.SetMeshMorphWeights(s.mesh, {-0.5F});
  s.values.base_color = {0.1F,0.6F,0.2F};
  s.world.SetMaterial(s.material, s.values);
  auto latest = s.world.Commit();
  latest.view.view.m[12] = 0.1F;
  latest.inputs.pose = 100;
  std::string error;
  Require(Toon::ApplyFastSnapshot(latest, extracted, error), error.c_str());
  Require(extracted.draws.data() == storage && extracted.draws[0].points == baseline.meshes[0].points &&
      extracted.draws[0].joints == latest.meshes[0].joints &&
      extracted.materials[0].material == s.values && extracted.inputs.pose == 100 &&
      extracted.view.view.m[12] == 0.1F, "late snapshot rebuilt packets or lost values");
  const auto good = extracted;
  const auto reject = [&](Toon::FrameSnapshot bad) {
    Require(!Toon::ApplyFastSnapshot(bad, extracted, error) && !error.empty(), "structural late frame accepted");
    Require(extracted.source_revision == good.source_revision && extracted.view.view == good.view.view &&
        extracted.draws[0].points == good.draws[0].points &&
        extracted.draws[0].joints == good.draws[0].joints &&
        extracted.materials[0].material == good.materials[0].material,
        "rejection partially changed frame");
  };
  auto bad = latest;
  bad.meshes[0].visible = false; reject(bad);
  bad = latest; ++bad.meshes[0].skin_revision; reject(bad);
  bad = latest; ++bad.meshes[0].morph_revision; reject(bad);
  bad = latest; bad.meshes[0].morph_weights = std::make_shared<const std::vector<float>>(2U, 0.0F); reject(bad);
  bad = latest; bad.materials[0].material.double_sided = true; reject(bad);
  bad = latest; bad.view.view.m[0] = std::numeric_limits<float>::infinity(); reject(bad);
  bad = latest; bad.materials[0].material.mtoon.rim_lift = std::numeric_limits<float>::quiet_NaN(); reject(bad);
  bad = latest; bad.materials[0].material.preview.roughness = std::numeric_limits<float>::quiet_NaN(); reject(bad);
  bad = latest; bad.materials[0].material.preview.metallic = std::numeric_limits<float>::infinity(); reject(bad);
  bad = latest; bad.materials[0].material.preview.inputs[0].scale.x = std::numeric_limits<float>::quiet_NaN(); reject(bad);
  bad = latest; bad.materials[0].material.preview.inputs[4].channel = 4; reject(bad);
  bad = latest; bad.materials[0].material.preview.normal.y = std::numeric_limits<float>::quiet_NaN(); reject(bad);
  bad = latest; bad.materials[0].material.preview.inputs[5].bias.x = std::numeric_limits<float>::infinity(); reject(bad);
  bad = latest; bad.materials[0].material.preview.opacity_texture.texture = 10; reject(bad);
  bad = latest; bad.materials[0].material.preview.enabled = true; reject(bad);
  bad = latest; bad.meshes[0].color.y = std::numeric_limits<float>::infinity(); reject(bad);
  bad = latest; bad.lights.push_back({});
  bad.lights[0].light.direction.z = std::numeric_limits<float>::infinity(); reject(bad);
  bad = latest; bad.meshes[0].points = std::make_shared<const std::vector<Toon::Float3>>(*latest.meshes[0].points); reject(bad);

  Toon::viewport::Series values(3);
  values.Push(1); values.Push(2); values.Push(3); values.Push(4);
  values.Push(std::numeric_limits<double>::quiet_NaN());
  const auto summary = values.Summarize();
  Require(summary.count == 3 && summary.mean == 3 && std::abs(summary.variance - 2.0/3.0) < 1e-12,
      "rolling variance wrong");
  Toon::viewport::FrameTelemetry telemetry;
  Toon::FrameLatency sample;
  sample.inputs.pose = sample.inputs.expression = sample.inputs.look_at = sample.inputs.camera = 1'000'000;
  sample.latched = 1'500'000;
  sample.buffers_written = 2'000'000;
  sample.submitted = 3'000'000;
  sample.present_returned = 4'000'000;
  telemetry.PushLatency(sample);
  const auto rows = telemetry.Latency();
  const auto row = [&](std::string_view name) -> const Toon::viewport::Series& {
    for (const auto& named : rows) {
      if (name == named.name) return *named.series;
    }
    throw std::runtime_error("missing latency series " + std::string(name));
  };
  // Every input's age at the late read, buffer write, submit and present.
  for (const char* input : {"pose", "expression", "look_at"}) {
    const std::string prefix(input);
    Require(row(prefix + "_latch").Summarize().mean == 0.5 &&
            row(prefix + "_buffer").Summarize().mean == 1 &&
            row(prefix + "_submit").Summarize().mean == 2 &&
            row(prefix + "_present_api").Summarize().mean == 3,
        "latency clock conversion wrong");
  }
  Require(row("camera_present_api").Summarize().mean == 3, "latency clock conversion wrong");
  telemetry.PushLatency(sample);
  Require(row("pose_buffer").size() == 1 && row("expression_present_api").size() == 1 &&
              row("camera_present_api").size() == 1,
      "unchanged input age counted as another response latency");
  sample.inputs = {}; // Absent samples must not be reported as zero latency.
  telemetry.PushLatency(sample);
  Require(row("pose_buffer").size() == 1, "absent input counted as measured latency");
  Require(telemetry.Json().find("\"display_time_measured\":false") != std::string::npos,
      "telemetry misrepresents display endpoint");
}

bool Gpu(const Factory& create) {
  Toon::FrameStatus status;
  std::string error;
  auto renderer = create(status, error, {4});
  if (status == Toon::FrameStatus::Skip) { std::cout << error << '\n'; return false; }
  Require(renderer != nullptr, error.c_str());
  auto oracle = create(status, error, {4});
  Require(oracle != nullptr, error.c_str());
  Scene s;
  const auto stale = Toon::ExtractDrawList(s.world.Commit());
  struct Source { Scene* scene; Toon::FrameSnapshot latest; unsigned int reads = 0; } source{&s, {}};
  renderer->SetLateFrameSource({[](void* context, Toon::FrameSnapshot& out, std::string&) {
    auto& input = *static_cast<Source*>(context);
    ++input.reads;
    // The input is produced in the callback, after extraction and GPU waits.
    const float weight = (input.reads % 3 == 0) ? -0.5F : (input.reads % 3 == 1) ? 0.75F : 0;
    input.scene->world.SetMeshMorphWeights(input.scene->mesh, {weight});
    Toon::ToonSkinPose pose{{Toon::Matrix4{}}, {}};
    pose.joints[0].m[13] = weight * 0.2F;
    input.scene->world.SetMeshSkinPose(input.scene->mesh, pose);
    auto values = input.scene->values;
    values.base_color.y = 0.2F + std::abs(weight) * 0.5F;
    input.scene->world.SetMaterial(input.scene->material, values);
    input.scene->world.Commit(out);
    out.inputs.pose = out.inputs.expression = Toon::SteadyNanoseconds();
    out.view.view.m[12] = weight * 0.1F;
    input.latest = out;
    return true;
  }, &source});
  Toon::ColorProduct color, expected;
  Toon::DepthProduct depth, expected_depth;
  for (int i = 0; i < 18; ++i) {
    const auto before = renderer->statistics();
    Require(renderer->Render(stale, 96, 96, color, depth, error), error.c_str());
    Require(oracle->Render(Toon::ExtractDrawList(source.latest), 96, 96, expected, expected_depth, error), error.c_str());
    Require(color.payload == expected.payload && depth.payload == expected_depth.payload,
        "late image does not match latest pose/expression/camera/material");
    const auto& after = renderer->statistics();
    Require(after.latency.inputs.pose <= after.latency.latched &&
        after.latency.latched <= after.latency.buffers_written &&
        after.latency.buffers_written <= after.latency.submitted && after.latency.present_returned == 0,
        "late telemetry endpoints out of order");
    Require(after.validation_message_count == 0, "late validation message");
    if (i == 0) continue;
    Require(after.point_uploads == before.point_uploads && after.topology_uploads == before.topology_uploads &&
        after.skin_uploads == before.skin_uploads && after.morph_uploads == before.morph_uploads &&
        after.texture_uploads == before.texture_uploads && after.pipelines_created == before.pipelines_created &&
        after.target_allocations == before.target_allocations &&
        after.pose_writes == before.pose_writes + 1 && after.morph_weight_writes == before.morph_weight_writes + 1 &&
        after.material_writes == before.material_writes + 1, "late frame wrote stale fast values or static buffers");
  }
  Require(source.reads == 18 && renderer->statistics().late_samples_applied == 18, "late source read count wrong");
  // Reject structural late data, retaining the ordinary extracted frame.
  s.world.SetMeshPoints(s.mesh, {{-0.3F,-0.3F,0}, {0.3F,-0.3F,0}, {0,0.3F,0}});
  Require(renderer->Render(stale, 96, 96, color, depth, error), error.c_str());
  Require(renderer->statistics().late_samples_rejected == 1 &&
      renderer->statistics().point_uploads == 1, "structural late sample uploaded geometry");
  Require(oracle->Render(stale, 96, 96, expected, expected_depth, error), error.c_str());
  Require(color.payload == expected.payload && depth.payload == expected_depth.payload, "rejected late frame changed image");
  std::cout << "late GPU: 18 latest-value comparisons; weight/pose/material writes only; structural fallback verified\n";
  return true;
}
}

int main(int argc, char** argv) {
  try {
    State();
    if (argc == 4 && std::string_view(argv[2]) == "--backend" &&
        std::string_view(argv[3]) == "webgpu") {
#if defined(TOON_HAS_WEBGPU)
      const auto shaders = Toon::WebGpuShadersIn(argv[1]);
      return Gpu([&](Toon::FrameStatus& status, std::string& error,
                     const Toon::RenderOptions& options) {
        return Toon::CreateWebGpuOffscreenRenderer(shaders, status, error, options);
      }) ? 0 : 77;
#else
      return 2;
#endif
    }
    if (argc == 2) {
      const auto shaders = Toon::SceneShadersIn(argv[1]);
      if (!Gpu([&](Toon::FrameStatus& status, std::string& error,
                   const Toon::RenderOptions& options) {
            return Toon::CreateOffscreenRenderer(shaders, status, error, options);
          })) {
        return 77;
      }
    }
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
