// SPDX-License-Identifier: Apache-2.0
#include "adapter.hpp"
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/lightSchema.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/glf/simpleLight.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usdLux/blackbody.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>
#include <cmath>
#include <iostream>
#include <memory>

PXR_NAMESPACE_USING_DIRECTIVE

bool Check(bool condition, const char* message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

const Toon::LightSnapshot* Find(const Toon::FrameSnapshot& scene, Toon::ToonLightType type) {
  for (const auto& light : scene.lights)
    if (light.light.type == type)
      return &light;
  return nullptr;
}

int main(int argc, char** argv) {
  if (argc != 2)
    return 2;
  const auto stage = UsdStage::Open(argv[1]);
  if (!stage)
    return 1;
  UsdImagingCreateSceneIndicesInfo info;
  info.stage = stage;
  const auto indices = UsdImagingCreateSceneIndices(info);
  HdToonRenderDelegate delegate;
  std::unique_ptr<HdRenderIndex> index(HdRenderIndex::New(&delegate, {}));
  index->InsertSceneIndex(indices.finalSceneIndex, SdfPath::AbsoluteRootPath());
  HdTaskSharedPtrVector tasks;
  HdTaskContext context;
  const HdRprimCollection collection(HdTokens->geometry, HdReprSelector(HdReprTokens->smoothHull));
  const auto sync = [&](double time) {
    indices.stageSceneIndex->ApplyPendingUpdates();
    indices.stageSceneIndex->SetTime(UsdTimeCode(time));
    index->EnqueueCollectionToSync(collection);
    index->SyncAll(&tasks, &context);
    return delegate.CommitScene();
  };
  const auto first = sync(1);
  using Type = Toon::ToonLightType;
  const auto* key = Find(first, Type::Directional);
  const auto* point = Find(first, Type::Point);
  const auto* spot = Find(first, Type::Spot);
  const auto* ambient = Find(first, Type::Ambient);
  if (!Check(first.lights.size() == 4 && key && point && spot && ambient,
          "UsdLux must produce directional, point, spot and ambient lights"))
    return 1;
  if (!Check(key->light.color == Toon::Float3{2, 1, 4} &&
                 key->light.direction == Toon::Float3{0, 0, -1} &&
                 point->light.position == Toon::Float3{1, 2, 3} &&
                 spot->light.cone_angle == 30 && spot->light.cone_softness == 0.5F && spot->light.visible &&
                 ambient->light.color == Toon::Float3{0.05F, 0.1F, 0.15F},
          "colour, exposure, transform and shaping must normalize"))
    return 1;
  const auto warm = UsdLuxBlackbodyTemperatureAsRgb(3000);
  if (!Check(point->light.color == Toon::Float3{warm[0] * 1.5F, warm[1] * 1.5F, warm[2] * 1.5F},
          "temperature and diffuse multiplier must reach the light colour"))
    return 1;
  const auto second = sync(2);
  const auto* moved = Find(second, Type::Directional);
  const auto* hidden = Find(second, Type::Spot);
  if (!Check(moved && hidden && moved->light.color == Toon::Float3{4, 2, 8} &&
                 std::abs(moved->light.direction.x + 1) < 1e-5F && !hidden->light.visible &&
                 moved->revision != key->revision &&
                 Find(second, Type::Point)->revision == point->revision &&
                 second.meshes[0].points_revision == first.meshes[0].points_revision &&
                 second.materials[0].parameters_revision == first.materials[0].parameters_revision,
          "time changes must update only changed lights"))
    return 1;
  if (!Check(sync(2).revision == second.revision, "steady light sync must change nothing"))
    return 1;
  stage->GetAttributeAtPath(SdfPath("/Point.inputs:intensity")).Set(6.0F);
  const auto edited = sync(2);
  if (!Check(Find(edited, Type::Point)->light.color ==
                     Toon::Float3{warm[0] * 3, warm[1] * 3, warm[2] * 3} &&
                 edited.meshes[0].points_revision == second.meshes[0].points_revision,
          "a USD light parameter edit must propagate at unchanged time"))
    return 1;
  stage->RemovePrim(SdfPath("/Point"));
  if (!Check(sync(2).lights.size() == 3, "removed light must leave the render world"))
    return 1;
  // Re-add after removal: the new sprim must own a new core light, not a stale id.
  stage->DefinePrim(SdfPath("/Point"), TfToken("SphereLight"));
  const auto restored = sync(2);
  if (!Check(restored.lights.size() == 4 && Find(restored, Type::Point)->id != point->id,
          "re-added light must receive a fresh identity"))
    return 1;
  // Hdx's built-in application lights carry a typed GlfSimpleLight payload,
  // even when advertised as distant/dome. They must not become scene lights
  // with the converted 15,000 intensity or suppress the camera fallback.
  const auto host = HdRetainedSceneIndex::New();
  index->InsertSceneIndex(host, SdfPath::AbsoluteRootPath());
  const auto helper = HdRetainedContainerDataSource::New(
      HdLightSchema::GetSchemaToken(), HdRetainedContainerDataSource::New(
          HdLightTokens->params,
          HdRetainedTypedSampledDataSource<GlfSimpleLight>::New(GlfSimpleLight()),
          HdLightTokens->intensity,
          HdRetainedTypedSampledDataSource<float>::New(15000.0F)));
  host->AddPrims({{SdfPath("/HostKey"), HdPrimTypeTokens->distantLight, helper},
      {SdfPath("/HostDome"), HdPrimTypeTokens->domeLight, helper}});
  if (!Check(sync(2).lights.size() == 4,
          "host GlfSimpleLights must not alter the authored USD rig"))
    return 1;
  for (const char* path : {"/Key", "/Point", "/Spot", "/Ambient"})
    stage->RemovePrim(SdfPath(path));
  if (!Check(sync(2).lights.empty(),
          "host helpers alone must retain the camera-light fallback"))
    return 1;
  return 0;
}
