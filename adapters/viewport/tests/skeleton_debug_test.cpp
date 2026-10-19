// SPDX-License-Identifier: Apache-2.0
#include "hydra_scene.hpp"
#include <toon/vulkan_backend.hpp>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

PXR_NAMESPACE_USING_DIRECTIVE
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool Near(Toon::Float3 p, Toon::Float3 expected) {
  return std::abs(p.x - expected.x) < 1e-5F && std::abs(p.y - expected.y) < 1e-5F && std::abs(p.z - expected.z) < 1e-5F;
}
const Toon::MeshSnapshot& Skinned(const Toon::FrameSnapshot& frame) {
  const auto found = std::find_if(frame.meshes.begin(), frame.meshes.end(), Toon::IsSkinned);
  Require(found != frame.meshes.end(), "fixture not GPU skinned");
  return *found;
}
}
int main(int argc, char** argv) {
  try {
    Require(argc == 2 || argc == 3, "expected fixture and optional shaders");
    auto scene = Toon::viewport::HydraScene::Open(argv[1]);
    scene->SetTime(1);
    Toon::FrameSnapshot frame;
    scene->Update(frame);
    (void)Skinned(frame);
    std::unique_ptr<Toon::OffscreenRenderer> renderer;
    std::string error;
    if (argc == 3) {
      Toon::FrameStatus status;
      renderer = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(argv[2]), status, error, {4});
      if (status == Toon::FrameStatus::Skip) { std::cout << error << '\n'; return 77; }
      Require(renderer != nullptr, error.c_str());
    }
    Toon::ColorProduct color; Toon::DepthProduct depth;
    std::vector<std::uint8_t> previous_pixels;
    for (int step = 0; step < 3; ++step) {
      const double time = step == 1 ? 2 : 1;
      scene->SetTime(time); scene->Update(frame);
      // Frame the evaluated geometry, rather than the distant bind transforms.
      frame.view.view.m[12] = -2;
      frame.view.view.m[13] = -3.5F;
      const auto before = frame;
      const auto syncs = scene->sync_count();
      if (renderer) Require(renderer->Render(Toon::ExtractDrawList(frame), 96,96,color,depth,error), error.c_str());
      const auto pixels = color.payload; const auto depths = depth.payload;
      if (renderer) {
        Require(std::any_of(depths.begin(), depths.end(), [](float value) { return value < 1; }), "GPU oracle drew no geometry");
        if (step > 0) Require(pixels != previous_pixels, "animated GPU oracle unchanged");
        previous_pixels = pixels;
      }
      const auto statistics = renderer ? renderer->statistics() : Toon::OffscreenStatistics{};
      for (int repeat = 0; repeat < 4; ++repeat) {
        const auto skeletons = scene->ReadSkeletons();
        Require(skeletons.size() == 1 && skeletons[0].error.empty() && skeletons[0].joints.size() == 3, "diagnostic hierarchy");
        const auto& joints = skeletons[0].joints;
        Require(skeletons[0].path == "/Root/Skel" && joints[0].name == "root" && joints[1].name == "root/arm" &&
            joints[2].name == "root/arm/tip" && joints[0].parent == -1 && joints[1].parent == 0 && joints[2].parent == 1,
            "joint names/order/parents");
        const float y = time == 1 ? 3.25F : 3.75F, z = time == 1 ? 0.5F : 1.0F;
        Require(Near(joints[0].world, {2,y,0}) && Near(joints[1].world,{1,y,0}) && Near(joints[2].world,{1,y,z}),
            "world transforms, animation remap or rest fallback incorrect");
        scene->ReadFast(frame);
        Require(scene->sync_count() == syncs && frame.revision == before.revision &&
            Skinned(frame).points == Skinned(before).points && Skinned(frame).joints == Skinned(before).joints &&
            Skinned(frame).indices == Skinned(before).indices && Skinned(frame).influences == Skinned(before).influences,
            "diagnostic read mutated render state or synced Hydra");
      }
      if (renderer) {
        frame.view = before.view; // ReadFast leaves the viewport's camera to its host.
        Require(renderer->Render(Toon::ExtractDrawList(frame),96,96,color,depth,error), error.c_str());
        Require(color.payload == pixels && depth.payload == depths, "diagnostics changed scene pixels");
        const auto& after = renderer->statistics();
        Require(after.point_uploads == statistics.point_uploads && after.topology_uploads == statistics.topology_uploads &&
            after.skin_uploads == statistics.skin_uploads && after.pose_writes == statistics.pose_writes &&
            after.material_writes == statistics.material_writes && after.pipelines_created == statistics.pipelines_created &&
            after.texture_uploads == statistics.texture_uploads && after.morph_uploads == statistics.morph_uploads &&
            after.morph_weight_writes == statistics.morph_weight_writes && after.target_allocations == statistics.target_allocations &&
            after.validation_message_count == 0, "diagnostics uploaded scene or rebuilt pipelines");
      }
    }
    // Edit the shared USD layer in memory; no fixture is written to disk.
    auto owner = UsdStage::Open(argv[1]);
    Require(owner->GetPrimAtPath(SdfPath("/Root")).GetAttribute(TfToken("xformOp:translate")).Set(GfVec3d(4,5,0)), "world edit");
    scene->Update(frame);
    const auto edited = scene->ReadSkeletons();
    Require(edited.size() == 1 && Near(edited[0].joints[0].world,{4,5.25F,0}), "USD edit kept stale diagnostics");
    owner->RemovePrim(SdfPath("/Root/Skel"));
    scene->Update(frame);
    Require(scene->ReadSkeletons().empty(), "removed skeleton retained");
    std::cout << "Skeleton diagnostics PASS; no diagnostic Hydra sync or scene writes\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
