// SPDX-License-Identifier: Apache-2.0
// Exercise the same HydraScene route the morph panel uses, without a window.
#include "hydra_scene.hpp"
#include <toon/vulkan_backend.hpp>

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

const Toon::MeshSnapshot& Morphed(const Toon::FrameSnapshot& frame) {
  const auto found = std::find_if(frame.meshes.begin(), frame.meshes.end(), Toon::IsMorphed);
  Require(found != frame.meshes.end(), "fixture has no GPU morph mesh");
  return *found;
}

void SlowState(const Toon::FrameSnapshot& before, const Toon::FrameSnapshot& after) {
  const auto& a = Morphed(before);
  const auto& b = Morphed(after);
  Require(a.points == b.points && a.normals == b.normals && a.indices == b.indices &&
      a.influences == b.influences && a.joints == b.joints &&
      a.morph_offsets == b.morph_offsets && a.morph_ranges == b.morph_ranges &&
      a.points_revision == b.points_revision && a.normals_revision == b.normals_revision &&
      a.topology_revision == b.topology_revision && a.skin_revision == b.skin_revision &&
      a.pose_revision == b.pose_revision && a.morph_revision == b.morph_revision,
      "morph debugging changed resident scene state");
}
}

int main(int argc, char** argv) {
  try {
    Require(argc == 2 || argc == 3, "expected scene and optional shader directory");
    auto scene = Toon::viewport::HydraScene::Open(argv[1]);
    scene->SetTime(1);
    Toon::FrameSnapshot snapshot;
    scene->Update(snapshot);
    Require(Toon::IsMorphed(Morphed(snapshot)),
        "fixture has no GPU morph mesh");
    const auto baseline = snapshot;
    const auto mesh = Morphed(snapshot).id;
    const auto count = Morphed(snapshot).morph_weights->size();
    Require(!Morphed(snapshot).morph_weights_overridden, "initial override active");

    // Equal effective values still indicate an active override, without writes.
    Require(scene->SetMorphWeightsOverride(mesh, *Morphed(snapshot).morph_weights),
        "equal override rejected");
    scene->Update(snapshot);
    Require(snapshot.revision == baseline.revision && Morphed(snapshot).morph_weights_overridden,
        "equal override caused a write or lost diagnostic state");
    scene->ClearMorphWeightsOverride(mesh);
    scene->Update(snapshot);
    Require(snapshot.revision == baseline.revision && !Morphed(snapshot).morph_weights_overridden,
        "equal clear caused a write or retained diagnostic state");
    Require(!scene->SetMorphWeightsOverride(0, std::vector<float>(count, 1)) &&
        !scene->SetMorphWeightsOverride(mesh, {}) &&
        !scene->SetMorphWeightsOverride(mesh, std::vector<float>(count + 1, 1)) &&
        !scene->SetMorphWeightsOverride(mesh,
            std::vector<float>(count, std::numeric_limits<float>::infinity())),
        "invalid debug edit accepted");
    scene->Update(snapshot);
    Require(snapshot.revision == baseline.revision && scene->sync_count() == 1,
        "debug controls triggered Hydra sync or changed scene");

    Toon::FrameStatus status = Toon::FrameStatus::Pass;
    std::string error;
    std::unique_ptr<Toon::OffscreenRenderer> renderer;
    if (argc == 3) {
      renderer = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(argv[2]), status, error, {4});
      if (status == Toon::FrameStatus::Skip) { std::cout << error << '\n'; return 77; }
      Require(renderer != nullptr, error.c_str());
    }
    Toon::ColorProduct color;
    Toon::DepthProduct depth;
    std::vector<std::uint8_t> initial, positive;
    std::vector<float> positive_depth;
    for (int frame = 0; frame < 7; ++frame) {
      if (frame == 1 || frame == 2)
        Require(scene->SetMorphWeightsOverride(mesh, std::vector<float>(count, 1)), "override rejected");
      if (frame == 3) scene->SetTime(2); // Continue evaluating beneath the override.
      if (frame == 4 || frame == 6) scene->ClearMorphWeightsOverride(mesh);
      if (frame == 5)
        Require(scene->SetMorphWeightsOverride(mesh, std::vector<float>(count, -0.5F)), "signed override rejected");
      scene->Update(snapshot);
      SlowState(baseline, snapshot);
      Require(scene->sync_count() == (frame < 3 ? 1U : 2U), "debug edit requested Hydra sync");
      const bool active = frame == 1 || frame == 2 || frame == 3 || frame == 5;
      Require(Morphed(snapshot).morph_weights_overridden == active, "wrong override diagnostic state");
      const float expected = frame == 0 ? 0 : frame == 5 ? -0.5F : 1;
      for (float weight : *Morphed(snapshot).morph_weights)
        Require(weight == expected, "clear did not resume latest scene evaluation");
      if (!renderer) continue;
      const auto before = renderer->statistics();
      Require(renderer->Render(Toon::ExtractDrawList(snapshot), 96, 96, color, depth, error), error.c_str());
      if (frame == 0) initial = color.payload;
      else if (frame == 1) {
        positive = color.payload;
        positive_depth = depth.payload;
        Require(positive != initial, "weight edit did not change image");
      } else if (frame == 5) Require(color.payload != positive && color.payload != initial,
          "signed weight edit did not change image");
      else Require(color.payload == positive && depth.payload == positive_depth,
          "duplicate edit, underlying evaluation or clear changed expected image");
      const auto& after = renderer->statistics();
      Require(after.validation_message_count == 0, "Vulkan validation message");
      if (frame == 0) continue;
      Require(after.point_uploads == before.point_uploads && after.topology_uploads == before.topology_uploads &&
          after.skin_uploads == before.skin_uploads && after.pose_writes == before.pose_writes &&
          after.morph_uploads == before.morph_uploads && after.material_writes == before.material_writes &&
          after.texture_uploads == before.texture_uploads && after.pipelines_created == before.pipelines_created &&
          after.target_allocations == before.target_allocations, "debug edit changed static GPU state");
      Require(after.morph_weight_writes == before.morph_weight_writes +
          (frame == 1 || frame == 5 || frame == 6 ? 1U : 0U), "unexpected weight writes");
    }
    // Overrides are owned by a scene, even when a replacement reuses mesh ids.
    Require(scene->SetMorphWeightsOverride(mesh, std::vector<float>(count, -0.5F)),
        "replacement setup override rejected");
    scene->Update(snapshot);
    Require(Morphed(snapshot).morph_weights_overridden, "replacement setup override absent");
    auto replacement = Toon::viewport::HydraScene::Open(argv[1]);
    replacement->SetTime(1);
    replacement->Update(snapshot);
    Require(!Morphed(snapshot).morph_weights_overridden &&
        *Morphed(snapshot).morph_weights == *Morphed(baseline).morph_weights,
        "replacement inherited override");
    std::cout << "morph debug: seven frames, two Hydra syncs; clear resumes latest evaluation\n";
    if (renderer) std::cout << "GPU: four weight writes, resident static state, colour/depth return equality\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
