// SPDX-License-Identifier: Apache-2.0
//
// hdToon's material Sprim, through scene index emulation as a host drives it.
//
// Without arguments: a retained scene index stands in for vrmImaging and
// spells its locators literally (its imaging policy §28.1), so the check
// needs no format plugin and no GPU. With --stage, a stage runs through
// UsdImaging instead and each material's selection is printed; what it shows
// depends on the schema and imaging plugins the session registers.
#include "adapter.hpp"

#include <pxr/pxr.h>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

using Field = std::pair<const char*, HdDataSourceBaseHandle>;

HdContainerDataSourceHandle Container(const std::vector<Field>& fields) {
  std::vector<TfToken> names;
  std::vector<HdDataSourceBaseHandle> values;
  for (const auto& [name, value] : fields) {
    names.emplace_back(name);
    values.push_back(value);
  }
  return HdRetainedContainerDataSource::New(names.size(), names.data(),
      values.data());
}

template <typename T>
HdDataSourceBaseHandle Value(const T& value) {
  return HdRetainedTypedSampledDataSource<T>::New(value);
}

HdContainerDataSourceHandle Prim(const std::vector<Field>& vrm_groups) {
  return Container({{"vrm", Container(vrm_groups)}});
}

HdContainerDataSourceHandle ToonPrim(float shading_shift) {
  return Prim({
      {"material", Container({
          {"baseColorFactor", Value(GfVec3f(0.8F, 0.6F, 0.4F))},
          {"baseColorAlphaFactor", Value(0.5F)},
          {"alphaMode", Value(TfToken("MASK"))},
          {"alphaCutoff", Value(0.25F)},
          {"doubleSided", Value(true)},
          {"emissiveFactor", Value(GfVec3f(1.0F, 0.5F, 0.0F))},
          {"emissiveStrength", Value(2.0F)},
      })},
      {"mtoon", Container({
          {"shadeColorFactor", Value(GfVec3f(0.5F, 0.4F, 0.3F))},
          {"shadingShiftFactor", Value(shading_shift)},
          {"outlineWidthMode", Value(TfToken("screenCoordinates"))},
          {"outlineWidthFactor", Value(0.02F)},
          {"outlineColorFactor", Value(GfVec3f(0.1F, 0.1F, 0.2F))},
          {"renderQueueOffsetNumber", Value(3)},
          {"transparentWithZWrite", Value(true)},
      })},
  });
}

const HdToonMaterial* FindMaterial(HdRenderIndex& index, const SdfPath& id) {
  return dynamic_cast<const HdToonMaterial*>(
      index.GetSprim(HdPrimTypeTokens->material, id));
}

int RunRetained() {
  if (!Check(HdToonReadMaterial(nullptr) == Toon::ToonMaterial{},
          "a material with nothing readable must be the fallback material")) {
    return 1;
  }

  const SdfPath toon_id("/Looks/Toon");
  const SdfPath mtoon_only_id("/Looks/MToonOnly");
  const SdfPath gltf_only_id("/Looks/GltfOnly");
  const SdfPath plain_id("/Looks/Plain");
  const HdRetainedSceneIndexRefPtr scene = HdRetainedSceneIndex::New();
  scene->AddPrims({
      {toon_id, HdPrimTypeTokens->material, ToonPrim(-0.1F)},
      // A leaf of the wrong type keeps the default.
      {mtoon_only_id, HdPrimTypeTokens->material,
          Prim({{"mtoon", Container({
              {"shadingToonyFactor", Value(0.5F)},
              {"shadingShiftFactor", Value(0.3)},
          })}})},
      {gltf_only_id, HdPrimTypeTokens->material,
          Prim({{"material", Container({
              {"baseColorFactor", Value(GfVec3f(0.2F, 0.2F, 0.2F))},
          })}})},
      {plain_id, HdPrimTypeTokens->material, Container({})},
  });

  HdToonRenderDelegate delegate;
  std::unique_ptr<HdRenderIndex> index(HdRenderIndex::New(&delegate, {}));
  if (!Check(index != nullptr && index->GetTerminalSceneIndex(),
          "the render index must emulate scene indices")) {
    return 1;
  }
  index->InsertSceneIndex(scene, SdfPath::AbsoluteRootPath());
  HdTaskSharedPtrVector tasks;
  HdTaskContext context;
  index->SyncAll(&tasks, &context);

  const HdToonMaterial* toon = FindMaterial(*index, toon_id);
  if (!Check(toon != nullptr, "a material prim must become a Toon material")) {
    return 1;
  }
  Toon::ToonMaterial expected;
  expected.model = Toon::ToonShadingModel::MToon;
  expected.base_color = {0.8F, 0.6F, 0.4F};
  expected.alpha = 0.5F;
  expected.alpha_mode = Toon::ToonAlphaMode::Mask;
  expected.alpha_cutoff = 0.25F;
  expected.double_sided = true;
  expected.emissive = {2.0F, 1.0F, 0.0F};
  expected.outline = true;
  expected.outline_width = 0.02F;
  expected.outline_color = {0.1F, 0.1F, 0.2F};
  expected.mtoon.shade_color = {0.5F, 0.4F, 0.3F};
  expected.mtoon.shading_shift = -0.1F;
  expected.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::Screen;
  expected.mtoon.render_queue_offset = 3;
  expected.mtoon.transparent_with_z_write = true;
  if (!Check(toon->GetToonMaterial() == expected,
          "vrm/mtoon must select MToon and read both groups")) {
    return 1;
  }

  Toon::ToonMaterial mtoon_only;
  mtoon_only.model = Toon::ToonShadingModel::MToon;
  mtoon_only.mtoon.shading_toony = 0.5F;
  const HdToonMaterial* gltf_only = FindMaterial(*index, gltf_only_id);
  const HdToonMaterial* plain = FindMaterial(*index, plain_id);
  if (!Check(FindMaterial(*index, mtoon_only_id) != nullptr &&
                 FindMaterial(*index, mtoon_only_id)->GetToonMaterial() ==
                     mtoon_only,
          "vrm/mtoon alone must be MToon with the common defaults") ||
      !Check(gltf_only != nullptr &&
                 gltf_only->GetToonMaterial() == Toon::ToonMaterial{},
          "vrm/material without vrm/mtoon must be PreviewSurface") ||
      !Check(plain != nullptr &&
                 plain->GetToonMaterial() == Toon::ToonMaterial{},
          "a material without vrm must be PreviewSurface")) {
    return 1;
  }

  // Re-adding a prim of the same type dirties all of it, as an authored
  // edit dirties the whole `material` locator.
  scene->AddPrims({{toon_id, HdPrimTypeTokens->material, ToonPrim(0.2F)}});
  index->SyncAll(&tasks, &context);
  toon = FindMaterial(*index, toon_id);
  expected.mtoon.shading_shift = 0.2F;
  if (!Check(toon != nullptr && toon->GetToonMaterial() == expected,
          "a re-synced material must read its new values")) {
    return 1;
  }

  scene->RemovePrims({{plain_id}});
  if (!Check(FindMaterial(*index, plain_id) == nullptr,
          "a removed material prim must be gone")) {
    return 1;
  }
  index.reset();
  return 0;
}

const char* ModelName(Toon::ToonShadingModel model) {
  switch (model) {
  case Toon::ToonShadingModel::MToon:
    return "MToon";
  case Toon::ToonShadingModel::MMD:
    return "MMD";
  case Toon::ToonShadingModel::PreviewSurface:
    break;
  }
  return "PreviewSurface";
}

const char* AlphaModeName(Toon::ToonAlphaMode mode) {
  switch (mode) {
  case Toon::ToonAlphaMode::Mask:
    return "mask";
  case Toon::ToonAlphaMode::Blend:
    return "blend";
  case Toon::ToonAlphaMode::Opaque:
    break;
  }
  return "opaque";
}

int RunStage(const char* path) {
  const UsdStageRefPtr stage = UsdStage::Open(path);
  if (!stage) {
    std::cerr << "cannot open " << path << '\n';
    return 1;
  }
  UsdImagingCreateSceneIndicesInfo info;
  info.stage = stage;
  const UsdImagingSceneIndices indices = UsdImagingCreateSceneIndices(info);

  HdToonRenderDelegate delegate;
  std::unique_ptr<HdRenderIndex> index(HdRenderIndex::New(&delegate, {}));
  index->InsertSceneIndex(indices.finalSceneIndex,
      SdfPath::AbsoluteRootPath());
  indices.stageSceneIndex->SetTime(stage->GetStartTimeCode());
  HdTaskSharedPtrVector tasks;
  HdTaskContext context;
  index->SyncAll(&tasks, &context);

  std::size_t mtoon = 0;
  const SdfPathVector ids = index->GetSprimSubtree(HdPrimTypeTokens->material,
      SdfPath::AbsoluteRootPath());
  for (const SdfPath& id : ids) {
    const HdToonMaterial* material = FindMaterial(*index, id);
    if (material == nullptr) {
      continue;
    }
    const Toon::ToonMaterial& values = material->GetToonMaterial();
    mtoon += values.model == Toon::ToonShadingModel::MToon ? 1U : 0U;
    std::cout << id << " model=" << ModelName(values.model)
              << " base=(" << values.base_color.x << ", "
              << values.base_color.y << ", " << values.base_color.z << ")"
              << " alpha=" << values.alpha
              << " alphaMode=" << AlphaModeName(values.alpha_mode)
              << " doubleSided=" << values.double_sided
              << " outline=" << values.outline
              << " shadingShift=" << values.mtoon.shading_shift
              << " shadingToony=" << values.mtoon.shading_toony << '\n';
  }
  std::cout << "materials=" << ids.size() << " mtoon=" << mtoon << '\n';
  index.reset();
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  if (argc == 1) {
    return RunRetained();
  }
  if (argc == 3 && std::strcmp(argv[1], "--stage") == 0) {
    return RunStage(argv[2]);
  }
  std::cerr << "usage: toon-hydra2-material-test [--stage <file>]\n";
  return 2;
}
