// SPDX-License-Identifier: Apache-2.0
#include "avatar_runtime.hpp"
#include "hydra_scene.hpp"
#include "hydra_scene_parts.hpp"
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/usdPhysics/joint.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace Toon;
using namespace Toon::viewport;
using namespace PXR_NS;
namespace {
void require(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}
std::string text(const SdfLayerHandle& layer) {
  std::string result;
  require(layer->ExportToString(&result), "export layer");
  return result;
}
} // namespace
int main(int argc, char** argv) {
  try {
    require(argc == 3, "generated morph PMX and mixed VMD required");
    auto scene = HydraScene::Open(argv[1]);
    auto stage = scene->parts().stage;
    const auto root = text(stage->GetRootLayer());
    stage->SetEditTarget(stage->GetSessionLayer());
    const SdfPath anchor("/Asset/physics/rigidBodies/anchor");
    const SdfPath other("/Asset/physics/rigidBodies/otherAnchor");
    SdfCreatePrimInLayer(stage->GetSessionLayer(), other.GetParentPath());
    require(SdfCopySpec(stage->GetRootLayer(), anchor, stage->GetSessionLayer(), other), "copy anchor");
    const SdfPath path("/Asset/physics/joints/inert");
    auto joint = UsdPhysicsJoint::Define(stage, path);
    joint.CreateBody0Rel().SetTargets({anchor});
    joint.CreateBody1Rel().SetTargets({other});
    joint.CreateJointEnabledAttr().Set(true);
    auto prim = joint.GetPrim();
    prim.CreateAttribute(TfToken("mmd:physics:type"), SdfValueTypeNames->Token, true, SdfVariabilityUniform)
        .Set(TfToken("spring6Dof"));
    for (const char* name : {"mmd:physics:translationSpring", "mmd:physics:rotationSpring"})
      prim.CreateAttribute(TfToken(name), SdfValueTypeNames->Vector3f, true, SdfVariabilityUniform).Set(GfVec3f(1));
    auto damping = stage->GetPrimAtPath(anchor).GetAttribute(TfToken("mmd:physics:linearDamping"));
    auto angular = stage->GetPrimAtPath(anchor).GetAttribute(TfToken("mmd:physics:angularDamping"));
    require(damping.Set(-2.0F) && angular.Set(2.0F), "set source damping");
    const auto authored = text(stage->GetSessionLayer());
    FrameSnapshot base;
    scene->Update(base);
    AvatarRuntimeOptions options;
    options.motion = argv[2];
    auto host = AvatarRuntime::Start(*scene, base, 1, options);
    auto check = [&] {
      const auto& status = host->status();
      require(status.inert_joints == std::vector<std::string>{path.GetString()}, "inert path delivery");
      require(status.damping_normalizations.size() == 2, "normalization count");
      for (const auto& expected : {MmdDampingNormalization{damping.GetPath().GetString(), -2, 0},
               MmdDampingNormalization{angular.GetPath().GetString(), 2, 1}}) {
        require(std::any_of(status.damping_normalizations.begin(), status.damping_normalizations.end(),
                    [&](const auto& record) { return record.attribute == expected.attribute && record.original == expected.original &&
                                                     record.coefficient == expected.coefficient; }),
            "normalization path and original/effective values");
      }
    };
    check();
    const auto retained = host->status();
    std::vector<FrameSnapshot> replay;
    std::string error;
    for (int generation = 0; generation < 2; ++generation) {
      host->Control(AvatarRuntime::Action::Play);
      for (int tick = 0; tick < 120; ++tick) {
        host->Tick(.0083334);
        require(host->status().physics_steps == static_cast<std::uint64_t>(tick + 1), "one fixed tick");
        auto frame = base;
        require(host->Apply(frame, false, error), error.c_str());
        if (!generation)
          replay.push_back(frame);
        else {
          require(frame.meshes.size() == replay[tick].meshes.size(), "replay mesh count");
          for (std::size_t i = 0; i < frame.meshes.size(); ++i) {
            const auto& a = frame.meshes[i];
            const auto& b = replay[tick].meshes[i];
            require(a.joints && b.joints && *a.joints == *b.joints, "resident joint replay");
            require(bool(a.uvs) == bool(b.uvs) && (!a.uvs || *a.uvs == *b.uvs), "resident UV replay");
            require(bool(a.morph_weights) == bool(b.morph_weights) &&
                        (!a.morph_weights || *a.morph_weights == *b.morph_weights),
                "resident morph replay");
          }
        }
      }
      host->Control(AvatarRuntime::Action::Pause);
      host->Tick(1);
      require(host->status().physics_steps == 120, "pause");
      host->Control(AvatarRuntime::Action::Reset);
      check();
    }
    require(host->Report().find("normalized_damping=2 inert_joints=1") != std::string::npos, "admission run log");
    require(host->status().refused == 0 && host->status().mismatches.empty(), "resident admission refused");
    // Successful Reset replaces records instead of appending stale entries.
    require(damping.Set(0.0F) && angular.Set(1.0F) && joint.GetJointEnabledAttr().Set(false), "change admission");
    host->Control(AvatarRuntime::Action::Reset);
    require(host->status().refused == 0 && host->status().damping_normalizations.empty() &&
                host->status().inert_joints.empty(),
        "Reset did not refresh admission records");
    require(retained.inert_joints.size() == 1 && retained.damping_normalizations.size() == 2 &&
                std::any_of(retained.damping_normalizations.begin(), retained.damping_normalizations.end(),
                    [](const auto& record) { return record.original == -2; }),
        "retained status changed");
    require(damping.Set(-2.0F) && angular.Set(2.0F) && joint.GetJointEnabledAttr().Set(true), "restore source opinions");
    host->Control(AvatarRuntime::Action::Reset);
    check();
    host->Control(AvatarRuntime::Action::Stop);
    require(host->status().bodies == 0 && host->status().inert_joints.empty() &&
                host->status().damping_normalizations.empty(),
        "Stop retained admission records");
    host->Control(AvatarRuntime::Action::Play);
    check();
    host->Tick(.0083334);
    require(host->status().physics_steps == 1 && host->status().refused == 0, "restart");
    host->Control(AvatarRuntime::Action::Stop);
    require(text(stage->GetRootLayer()) == root && text(stage->GetSessionLayer()) == authored, "host changed source layers");
    std::cout << "MMD admission: owned diagnostics, 240 resident ticks, exact reset replay, refresh, Stop/restart and unchanged USD passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
