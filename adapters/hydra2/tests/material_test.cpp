// SPDX-License-Identifier: Apache-2.0
//
// hdToon's material Sprim, through scene index emulation as a host drives it.
//
// Without arguments: a retained scene index stands in for vrmImaging and
// spells its locators literally (its imaging policy §28.1), so the check
// needs no format plugin and no GPU. With --stage, a stage runs through
// UsdImaging instead and each material's selection is printed at the start
// and end time codes; what it shows depends on the schema and imaging
// plugins the session registers.
#include "adapter.hpp"

#include <pxr/pxr.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hio/image.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
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

// A leaf whose value the test changes without replacing the prim, as a time
// move changes what a time-sampled attribute's data source returns.
class SharedFloatDataSource final : public HdTypedSampledDataSource<float> {
public:
  HD_DECLARE_DATASOURCE(SharedFloatDataSource);

  VtValue GetValue(Time shutter_offset) override {
    return VtValue(GetTypedValue(shutter_offset));
  }

  float GetTypedValue(Time shutter_offset) override {
    (void)shutter_offset;
    return *value_;
  }

  bool GetContributingSampleTimesForInterval(Time start_time, Time end_time,
      std::vector<Time>* sample_times) override {
    (void)start_time;
    (void)end_time;
    (void)sample_times;
    return false;
  }

private:
  explicit SharedFloatDataSource(std::shared_ptr<const float> value)
      : value_(std::move(value)) {
  }

  std::shared_ptr<const float> value_;
};

HdContainerDataSourceHandle Prim(const std::vector<Field>& vrm_groups) {
  return Container({{"vrm", Container(vrm_groups)}});
}

HdContainerDataSourceHandle ToonPrim(
    const HdDataSourceBaseHandle& shading_shift) {
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
          {"shadingShiftFactor", shading_shift},
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

HdContainerDataSourceHandle TexturedPrim(
    const std::vector<Field>& texture_roles) {
  return Prim({
      {"mtoon", Container({})},
      {"textureInfo", Container(texture_roles)},
  });
}

// vrm/textureInfo/<role> (vrmImaging §29): a 2x2 image written here, named
// by a resolved asset path, is decoded top row first and shared by every
// role and material that names it in the same encoding; its sampling
// fields are read; a path
// that does not resolve, or another TEXCOORD set, samples nothing; and the
// texture goes when its last material does.
int RunTextures(HdRetainedSceneIndex& scene, HdRenderIndex& index) {
  const std::filesystem::path image_path =
      std::filesystem::temp_directory_path() / "toon-material-test.png";
  const std::string image = image_path.generic_string();
  std::vector<std::uint8_t> written{255, 0, 0, 255, 0, 255, 0, 255, 0, 0,
      255, 255, 255, 255, 255, 128};
  HioImage::StorageSpec storage;
  storage.width = 2;
  storage.height = 2;
  storage.depth = 1;
  storage.format = HioFormatUNorm8Vec4;
  storage.data = written.data();
  const HioImageSharedPtr writer = HioImage::OpenForWriting(image);
  if (!Check(writer != nullptr && writer->Write(storage),
          "the test image must be written")) {
    return 1;
  }
  Toon::ToonTexture decoded;
  if (!Check(HdToonLoadTexture(image, Toon::ToonTextureEncoding::Srgb,
                 decoded) &&
                 decoded.width == 2 && decoded.height == 2 &&
                 decoded.pixels != nullptr && *decoded.pixels == written,
          "an image must decode to RGBA8, top row first")) {
    return 1;
  }

  const SdfAssetPath file(image, image);
  const auto role = [&file](const std::vector<Field>& fields) {
    std::vector<Field> all{{"file", Value(file)}, {"texCoord", Value(0)}};
    all.insert(all.end(), fields.begin(), fields.end());
    return Container(all);
  };
  const SdfPath a_id("/Looks/TexturedA");
  const SdfPath b_id("/Looks/TexturedB");
  const SdfPath unresolved_id("/Looks/Unresolved");
  const SdfPath other_set_id("/Looks/OtherSet");
  const auto normal_scale = std::make_shared<float>(0.4F);
  scene.AddPrims({
      {a_id, HdPrimTypeTokens->material, TexturedPrim({
          {"baseColor", role({
              {"wrapS", Value(TfToken("clampToEdge"))},
              {"wrapT", Value(TfToken("mirroredRepeat"))},
              {"transform", Container({
                  {"offset", Value(GfVec2f(0.5F, 0.25F))},
                  {"rotation", Value(0.5F)},
                  {"scale", Value(GfVec2f(2.0F, 3.0F))},
              })},
          })},
          {"shadeMultiply", role({})},
          {"outlineWidthMultiply", role({})},
          {"matcap", role({})},
          {"rimMultiply", role({})},
          {"emissive", role({})},
          {"normal", role({{"scale", SharedFloatDataSource::New(normal_scale)}})},
          {"shadingShift", role({{"scale", Value(0.7F)}})},
          {"uvAnimationMask", role({})},
      })},
      {b_id, HdPrimTypeTokens->material,
          TexturedPrim({{"baseColor", role({})}})},
      {unresolved_id, HdPrimTypeTokens->material,
          TexturedPrim({{"baseColor", Container({
              {"file", Value(SdfAssetPath("missing.png"))},
          })}})},
      {other_set_id, HdPrimTypeTokens->material,
          TexturedPrim({
              {"baseColor", Container({
                  {"file", Value(file)},
                  {"texCoord", Value(1)},
              })},
              {"matcap", Container({
                  {"file", Value(file)},
                  {"texCoord", Value(1)},
              })},
          })},
  });
  HdTaskSharedPtrVector tasks;
  HdTaskContext context;
  index.SyncAll(&tasks, &context);

  const HdToonMaterial* a = FindMaterial(index, a_id);
  const HdToonMaterial* b = FindMaterial(index, b_id);
  const HdToonMaterial* unresolved = FindMaterial(index, unresolved_id);
  const HdToonMaterial* other_set = FindMaterial(index, other_set_id);
  if (!Check(a != nullptr && b != nullptr && unresolved != nullptr &&
                 other_set != nullptr,
          "textured material prims must become Toon materials")) {
    return 1;
  }
  const Toon::ToonTextureRef& base = a->GetToonMaterial().base_texture;
  const Toon::TextureId shared = base.texture;
  const Toon::TextureId outline_width =
      a->GetToonMaterial().mtoon.outline_width_texture.texture;
  if (!Check(shared != 0, "a resolved base colour texture must be sampled") ||
      !Check(a->GetToonMaterial().mtoon.shade_texture.texture == shared &&
                 a->GetToonMaterial().mtoon.matcap_texture.texture ==
                     shared &&
                 a->GetToonMaterial().mtoon.rim_multiply_texture.texture ==
                     shared &&
                 a->GetToonMaterial().emissive_texture.texture == shared &&
                 b->GetToonMaterial().base_texture.texture == shared,
          "one image must be one texture across roles and materials") ||
      !Check(outline_width != 0 && outline_width != shared,
          "the outline width role must read the same image as data, a "
          "texture of its own") ||
      !Check(a->GetToonMaterial().normal_texture.texture == outline_width &&
                 a->GetToonMaterial().mtoon.shading_shift_texture.texture == outline_width &&
                 a->GetToonMaterial().mtoon.uv_animation_mask_texture.texture == outline_width &&
                 a->GetToonMaterial().normal_scale == 0.4F &&
                 a->GetToonMaterial().mtoon.shading_shift_texture_scale == 0.7F,
          "normal, shift and mask must share linear data and read contribution scales") ||
      !Check(base.wrap_s == Toon::ToonWrap::ClampToEdge &&
                 base.wrap_t == Toon::ToonWrap::MirroredRepeat &&
                 base.offset == Toon::Float2{0.5F, 0.25F} &&
                 base.rotation == 0.5F &&
                 base.scale == Toon::Float2{2.0F, 3.0F},
          "a texture's wrap and transform must be read") ||
      !Check(unresolved->GetToonMaterial().base_texture.texture == 0,
          "a path that does not resolve must sample nothing") ||
      !Check(other_set->GetToonMaterial().base_texture.texture == 0,
          "another TEXCOORD set must sample nothing") ||
      !Check(other_set->GetToonMaterial().mtoon.matcap_texture.texture ==
                 shared,
          "MatCap samples no TEXCOORD set, so any set must be read")) {
    return 1;
  }

  // Every MToon role owns a separate transform, including the unanimated
  // mask and view-mapped MatCap. Contribution scale stays outside it.
  const std::array<const char*, kHdToonTextureRoles> role_names{
      "baseColor", "shadeMultiply", "outlineWidthMultiply", "matcap",
      "rimMultiply", "emissive", "normal", "shadingShift", "uvAnimationMask"};
  for (std::size_t slot = 0; slot < role_names.size(); ++slot) {
    auto source = HdToonReadMaterial(TexturedPrim({{role_names[slot], role({
        {"transform", Container({
            {"offset", Value(GfVec2f(0.2F, 0.3F))},
            {"rotation", Value(0.7F)},
            {"scale", Value(GfVec2f(2.0F, 0.5F))},
        })},
    })}}));
    const auto refs = HdToonTextureRefs(source.values);
    if (!Check(refs[slot]->offset == Toon::Float2{0.2F, 0.3F} &&
                   refs[slot]->rotation == 0.7F &&
                   refs[slot]->scale == Toon::Float2{2.0F, 0.5F},
            "every role must read its own texture transform")) {
      return 1;
    }
  }
  *normal_scale = 0.6F;
  scene.DirtyPrims({{a_id, HdDataSourceLocatorSet(
      HdDataSourceLocator(TfToken("vrm"), TfToken("textureInfo"),
          TfToken("normal"), TfToken("scale")))}});
  index.GetRenderDelegate()->Update();
  if (!Check(a->GetToonMaterial().normal_scale == 0.6F &&
                 a->GetToonMaterial().normal_texture.texture == outline_width,
          "a contribution-scale value edit must keep its texture identity")) {
    return 1;
  }

  // Once no material names the image, its texture is gone, and naming it
  // again decodes a new one.
  scene.RemovePrims({{a_id}, {b_id}, {other_set_id}});
  scene.AddPrims({{a_id, HdPrimTypeTokens->material,
      TexturedPrim({{"baseColor", role({})}})}});
  index.SyncAll(&tasks, &context);
  a = FindMaterial(index, a_id);
  if (!Check(a != nullptr && a->GetToonMaterial().base_texture.texture != 0 &&
                 a->GetToonMaterial().base_texture.texture != shared,
          "a texture no material samples must be released")) {
    return 1;
  }
  scene.RemovePrims({{a_id}, {unresolved_id}});
  std::error_code ignored;
  std::filesystem::remove(image_path, ignored);
  return 0;
}

int RunRetained() {
  if (!Check(HdToonReadMaterial(nullptr).values == Toon::ToonMaterial{},
          "a material with nothing readable must be the fallback material")) {
    return 1;
  }

  const SdfPath toon_id("/Looks/Toon");
  const SdfPath mtoon_only_id("/Looks/MToonOnly");
  const SdfPath gltf_only_id("/Looks/GltfOnly");
  const SdfPath plain_id("/Looks/Plain");
  const auto shading_shift = std::make_shared<float>(-0.1F);
  const HdRetainedSceneIndexRefPtr scene = HdRetainedSceneIndex::New();
  scene->AddPrims({
      {toon_id, HdPrimTypeTokens->material,
          ToonPrim(SharedFloatDataSource::New(shading_shift))},
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

  // A value-only change dirties its `vrm` locator alone, as a time move
  // across a sample does (renderer report 02). Emulation leaves the Sprim
  // clean; the delegate's observer carries the value.
  const auto base_snapshot = delegate.CommitScene();
  Toon::MaterialId expression_id = 0;
  for (const auto& material : base_snapshot.materials) {
    if (material.material == expected) expression_id = material.id;
  }
  auto expression_values = expected;
  expression_values.mtoon.shading_shift = -0.4F;
  if (!Check(expression_id != 0 && delegate.SetMaterialParametersOverride(expression_id, expression_values),
          "direct material expression rejected")) return 1;
  const auto expression_snapshot = delegate.CommitScene();
  const auto expression_matches = [&](const Toon::FrameSnapshot& snapshot, const Toon::ToonMaterial& values) {
    for (const auto& material : snapshot.materials) {
      if (material.id == expression_id) return material.material == values;
    }
    return false;
  };
  if (!Check(expression_matches(expression_snapshot, expression_values),
          "material expression must reach a commit without Hydra sync")) return 1;
  *shading_shift = 0.3F;
  scene->DirtyPrims({{toon_id,
      HdDataSourceLocatorSet{HdDataSourceLocator(TfToken("vrm"),
          TfToken("mtoon"), TfToken("shadingShiftFactor"))}}});
  if (!Check(index->GetChangeTracker().GetSprimDirtyBits(toon_id) ==
                 HdChangeTracker::Clean,
          "emulation must leave a value-only change's material clean")) {
    return 1;
  }
  index->SyncAll(&tasks, &context);
  toon = FindMaterial(*index, toon_id);
  expected.mtoon.shading_shift = 0.3F;
  if (!Check(toon != nullptr && toon->GetToonMaterial() == expected,
          "a value-only change must reach the material's values")) {
    return 1;
  }
  if (!Check(expression_matches(delegate.CommitScene(), expression_values),
          "Hydra material value sync must preserve a host expression")) return 1;
  delegate.ClearMaterialParametersOverride(expression_id);
  if (!Check(expression_matches(delegate.CommitScene(), expected),
          "clearing a host material expression must restore latest Hydra values")) return 1;

  // Re-adding a prim of the same type dirties all of it, as an authored
  // edit dirties the whole `material` locator.
  scene->AddPrims(
      {{toon_id, HdPrimTypeTokens->material, ToonPrim(Value(0.2F))}});
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
  if (RunTextures(*scene, *index) != 0) {
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

void PrintMaterials(HdRenderIndex& index, double time) {
  std::size_t mtoon = 0;
  const SdfPathVector ids = index.GetSprimSubtree(HdPrimTypeTokens->material,
      SdfPath::AbsoluteRootPath());
  for (const SdfPath& id : ids) {
    const HdToonMaterial* material = FindMaterial(index, id);
    if (material == nullptr) {
      continue;
    }
    const Toon::ToonMaterial& values = material->GetToonMaterial();
    mtoon += values.model == Toon::ToonShadingModel::MToon ? 1U : 0U;
    std::cout << "time=" << time << ' ' << id
              << " model=" << ModelName(values.model)
              << " base=(" << values.base_color.x << ", "
              << values.base_color.y << ", " << values.base_color.z << ")"
              << " alpha=" << values.alpha
              << " alphaMode=" << AlphaModeName(values.alpha_mode)
              << " doubleSided=" << values.double_sided
              << " outline=" << values.outline
              << " shadingShift=" << values.mtoon.shading_shift
              << " shadingToony=" << values.mtoon.shading_toony << '\n';
  }
  std::cout << "time=" << time << " materials=" << ids.size()
            << " mtoon=" << mtoon << '\n';
}

// Populates at the start time code, then moves to the end one: a
// time-sampled canonical value changes by the value-only route alone.
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
  HdTaskSharedPtrVector tasks;
  HdTaskContext context;
  for (const double time :
      {stage->GetStartTimeCode(), stage->GetEndTimeCode()}) {
    indices.stageSceneIndex->SetTime(UsdTimeCode(time));
    index->SyncAll(&tasks, &context);
    PrintMaterials(*index, time);
  }
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
