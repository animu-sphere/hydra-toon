// SPDX-License-Identifier: Apache-2.0
// The Materials panel's actual Hydra host route, including late application.
#include "hydra_scene.hpp"
#include <toon/vulkan_backend.hpp>

#include <pxr/usd/usd/stage.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

const Toon::MaterialSnapshot& Bound(const Toon::FrameSnapshot& frame) {
  Require(frame.meshes.size() == 1, "expected one fixture mesh");
  const auto found = std::find_if(frame.materials.begin(), frame.materials.end(),
      [&](const auto& material) { return material.id == frame.meshes[0].material; });
  Require(found != frame.materials.end(), "fixture material binding missing");
  return *found;
}

void SlowState(const Toon::FrameSnapshot& before, const Toon::FrameSnapshot& after) {
  const auto& a = before.meshes[0];
  const auto& b = after.meshes[0];
  Require(a.points == b.points && a.normals == b.normals && a.indices == b.indices &&
      a.uvs == b.uvs && a.influences == b.influences && a.joints == b.joints &&
      a.morph_offsets == b.morph_offsets && a.morph_ranges == b.morph_ranges &&
      a.morph_weights == b.morph_weights && a.points_revision == b.points_revision &&
      a.normals_revision == b.normals_revision && a.topology_revision == b.topology_revision &&
      a.uvs_revision == b.uvs_revision && a.skin_revision == b.skin_revision &&
      a.pose_revision == b.pose_revision && a.morph_revision == b.morph_revision &&
      a.morph_weights_revision == b.morph_weights_revision &&
      Bound(before).structure_revision == Bound(after).structure_revision,
      "material debugging changed resident scene state");
}
}

int main(int argc, char** argv) {
  try {
    Require(argc == 2 || argc == 3, "expected fixture and optional shader directory");
    auto scene = Toon::viewport::HydraScene::Open(argv[1]);
    Toon::FrameSnapshot snapshot;
    scene->Update(snapshot);
    const auto baseline = snapshot;
    const auto material = Bound(baseline).id;
    Require(!Bound(baseline).parameters_overridden, "initial material override active");
    Require(scene->SetMaterialParametersOverride(material, Bound(baseline).material), "equal override rejected");
    scene->ReadFast(snapshot);
    Require(snapshot.revision == baseline.revision && Bound(snapshot).parameters_overridden &&
        !Bound(baseline).parameters_overridden, "equal override lost diagnostics or changed revision");
    scene->ClearMaterialParametersOverride(material);
    scene->ReadFast(snapshot);
    Require(snapshot.revision == baseline.revision && !Bound(snapshot).parameters_overridden,
        "equal clear advanced revision or kept diagnostic state");
    auto structural = Bound(baseline).material;
    structural.double_sided = !structural.double_sided;
    Require(!scene->SetMaterialParametersOverride(material, structural) &&
        !scene->SetMaterialParametersOverride(0, Bound(baseline).material), "invalid override accepted");
    scene->ReadFast(snapshot);
    Require(snapshot.revision == baseline.revision && scene->sync_count() == 1,
        "rejected override changed scene or synced Hydra");

    std::string error;
    Toon::FrameStatus status;
    std::unique_ptr<Toon::OffscreenRenderer> renderer;
    if (argc == 3) {
      renderer = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(argv[2]), status, error, {4});
      if (status == Toon::FrameStatus::Skip) { std::cout << error << '\n'; return 77; }
      Require(renderer != nullptr, error.c_str());
    }
    Toon::ColorProduct color;
    Toon::DepthProduct depth;
    std::vector<std::uint8_t> initial;
    std::vector<float> initial_depth;
    auto values = Bound(baseline).material;
    values.base_color = {0.1F, 0.8F, 0.3F};
    values.emissive = {0.2F, 0.1F, 0.4F};
    for (int frame = 0; frame < 6; ++frame) {
      // Extract first; the same late read used by the viewport sees debug edits.
      scene->Update(snapshot);
      auto draws = Toon::ExtractDrawList(snapshot);
      if (frame == 1 || frame == 2)
        Require(scene->SetMaterialParametersOverride(material, values), "value override rejected");
      if (frame == 3 || frame == 4) scene->ClearMaterialParametersOverride(material);
      if (frame == 5)
        Require(scene->SetMaterialParametersOverride(material, Bound(baseline).material), "equal override rejected");
      scene->ReadFast(snapshot);
      Require(Toon::ApplyFastSnapshot(snapshot, draws, error), error.c_str());
      const bool active = frame == 1 || frame == 2 || frame == 5;
      Require(Bound(snapshot).parameters_overridden == active, "wrong override diagnostic state");
      const auto found = std::find_if(draws.materials.begin(), draws.materials.end(),
          [&](const auto& item) { return item.id == material; });
      Require(found != draws.materials.end() && found->parameters_overridden == active &&
          found->material == (frame == 1 || frame == 2 ? values : Bound(baseline).material),
          "late application lost values or diagnostics");
      SlowState(baseline, snapshot);
      Require(scene->sync_count() == 1, "material debugging synced Hydra");
      if (!renderer) continue;
      const auto before = renderer->statistics();
      Require(renderer->Render(draws, 96, 96, color, depth, error), error.c_str());
      if (frame == 0) {
        initial = color.payload;
        initial_depth = depth.payload;
        Require(std::any_of(depth.payload.begin(), depth.payload.end(), [](float z) { return z < 1; }),
            "empty render cannot establish unchanged geometry");
      } else {
        // PreviewSurface currently draws mesh display colour; MToon image
        // effects are established independently by toon-expression-gpu.
        Require(color.payload == initial && depth.payload == initial_depth, "fallback surface or depth changed");
        const auto& after = renderer->statistics();
        Require(after.point_uploads == before.point_uploads && after.topology_uploads == before.topology_uploads &&
            after.skin_uploads == before.skin_uploads && after.pose_writes == before.pose_writes &&
            after.morph_uploads == before.morph_uploads && after.morph_weight_writes == before.morph_weight_writes &&
            after.texture_uploads == before.texture_uploads && after.pipelines_created == before.pipelines_created &&
            after.target_allocations == before.target_allocations, "debug edit changed static GPU state");
        Require(after.material_writes == before.material_writes + (frame == 1 || frame == 3 ? 1U : 0U),
            "unexpected material writes");
      }
      Require(renderer->statistics().validation_message_count == 0, "Vulkan validation message");
    }
    // A scene replacement can reuse numeric ids, but must not inherit overrides.
    auto replacement = Toon::viewport::HydraScene::Open(argv[1]);
    replacement->Update(snapshot);
    Require(Bound(snapshot).id == material && !Bound(snapshot).parameters_overridden &&
        Bound(snapshot).material == Bound(baseline).material, "replacement inherited material override");
    // Shared-layer edits invalidate the actual host binding; the file is never saved.
    auto stage = pxr::UsdStage::Open(argv[1]);
    Require(stage->RemovePrim(pxr::SdfPath("/Material")), "material removal failed");
    scene->Update(snapshot);
    Require(!scene->SetMaterialParametersOverride(material, values) &&
        std::none_of(snapshot.materials.begin(), snapshot.materials.end(),
            [&](const auto& item) { return item.id == material; }), "removed material retained override");
    scene->ClearMaterialParametersOverride(material);
    std::cout << "material debug: six late frames, one Hydra sync; equal/duplicate/release/replacement/removal verified\n";
    if (renderer) std::cout << "GPU: two value writes; unchanged resident resources and fallback colour/depth\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
