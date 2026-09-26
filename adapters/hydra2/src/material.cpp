// SPDX-License-Identifier: Apache-2.0
//
// Material selection and normalization from a Hydra material prim's data
// sources. Kept apart from Windows.h, whose OPAQUE macro would collide.
#include "adapter.hpp"

#include <pxr/pxr.h>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/imaging/hd/materialSchema.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// `vrmImaging`'s locators, spelled here because it has no public header and
// nothing of it is linked (its imaging policy §28.1).
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (vrm)
    (material)
    (mtoon)

    (baseColorFactor)
    (baseColorAlphaFactor)
    (alphaMode)
    (alphaCutoff)
    (doubleSided)
    (emissiveFactor)
    (emissiveStrength)
    ((alphaModeMask, "MASK"))
    ((alphaModeBlend, "BLEND"))

    (shadeColorFactor)
    (shadingShiftFactor)
    (shadingToonyFactor)
    (giEqualizationFactor)
    (matcapFactor)
    (parametricRimColorFactor)
    (parametricRimFresnelPowerFactor)
    (parametricRimLiftFactor)
    (rimLightingMixFactor)
    (outlineWidthMode)
    (outlineWidthFactor)
    (outlineColorFactor)
    (outlineLightingMixFactor)
    ((outlineWorldCoordinates, "worldCoordinates"))
    ((outlineScreenCoordinates, "screenCoordinates"))
    (uvAnimationScrollXSpeedFactor)
    (uvAnimationScrollYSpeedFactor)
    (uvAnimationRotationSpeedFactor)
    (renderQueueOffsetNumber)
    (transparentWithZWrite));

HdContainerDataSourceHandle Group(const HdContainerDataSourceHandle& parent,
    const TfToken& name) {
  return parent ? HdContainerDataSource::Cast(parent->Get(name)) : nullptr;
}

// Every leaf of a present group has a value of the attribute's type, at the
// scene index's current time; a leaf of another type keeps `value`.
template <typename T>
bool Read(const HdContainerDataSourceHandle& group, const TfToken& name,
    T& value) {
  const auto leaf = HdTypedSampledDataSource<T>::Cast(group->Get(name));
  if (!leaf) {
    return false;
  }
  value = leaf->GetTypedValue(0.0F);
  return true;
}

void Read(const HdContainerDataSourceHandle& group, const TfToken& name,
    Toon::Float3& value) {
  GfVec3f color;
  if (Read(group, name, color)) {
    value = {color[0], color[1], color[2]};
  }
}

// VrmMaterialAPI: the glTF core MToon builds on.
void ReadCommon(const HdContainerDataSourceHandle& group,
    Toon::ToonMaterial& result) {
  Read(group, _tokens->baseColorFactor, result.base_color);
  Read(group, _tokens->baseColorAlphaFactor, result.alpha);
  TfToken alpha_mode;
  if (Read(group, _tokens->alphaMode, alpha_mode)) {
    result.alpha_mode = alpha_mode == _tokens->alphaModeMask
        ? Toon::ToonAlphaMode::Mask
        : alpha_mode == _tokens->alphaModeBlend ? Toon::ToonAlphaMode::Blend
                                                : Toon::ToonAlphaMode::Opaque;
  }
  Read(group, _tokens->alphaCutoff, result.alpha_cutoff);
  Read(group, _tokens->doubleSided, result.double_sided);
  Read(group, _tokens->emissiveFactor, result.emissive);
  float strength = 1.0F;
  Read(group, _tokens->emissiveStrength, strength);
  result.emissive = {result.emissive.x * strength,
      result.emissive.y * strength, result.emissive.z * strength};
}

void ReadMToon(const HdContainerDataSourceHandle& group,
    Toon::ToonMaterial& result) {
  Toon::ToonMaterial::MToon& mtoon = result.mtoon;
  Read(group, _tokens->shadeColorFactor, mtoon.shade_color);
  Read(group, _tokens->shadingShiftFactor, mtoon.shading_shift);
  Read(group, _tokens->shadingToonyFactor, mtoon.shading_toony);
  Read(group, _tokens->giEqualizationFactor, mtoon.gi_equalization);
  Read(group, _tokens->matcapFactor, mtoon.matcap);
  Read(group, _tokens->parametricRimColorFactor, mtoon.rim_color);
  Read(group, _tokens->parametricRimFresnelPowerFactor,
      mtoon.rim_fresnel_power);
  Read(group, _tokens->parametricRimLiftFactor, mtoon.rim_lift);
  Read(group, _tokens->rimLightingMixFactor, mtoon.rim_lighting_mix);
  TfToken width_mode;
  if (Read(group, _tokens->outlineWidthMode, width_mode)) {
    mtoon.outline_width_mode = width_mode == _tokens->outlineWorldCoordinates
        ? Toon::ToonOutlineWidthMode::World
        : width_mode == _tokens->outlineScreenCoordinates
            ? Toon::ToonOutlineWidthMode::Screen
            : Toon::ToonOutlineWidthMode::None;
  }
  Read(group, _tokens->outlineLightingMixFactor, mtoon.outline_lighting_mix);
  Read(group, _tokens->uvAnimationScrollXSpeedFactor,
      mtoon.uv_scroll_x_speed);
  Read(group, _tokens->uvAnimationScrollYSpeedFactor,
      mtoon.uv_scroll_y_speed);
  Read(group, _tokens->uvAnimationRotationSpeedFactor,
      mtoon.uv_rotation_speed);
  int queue_offset = 0;
  if (Read(group, _tokens->renderQueueOffsetNumber, queue_offset)) {
    mtoon.render_queue_offset = queue_offset;
  }
  Read(group, _tokens->transparentWithZWrite, mtoon.transparent_with_z_write);

  // The outline request is common; how it is drawn is the renderer's
  // (material policy §5).
  result.outline = mtoon.outline_width_mode != Toon::ToonOutlineWidthMode::None;
  Read(group, _tokens->outlineWidthFactor, result.outline_width);
  Read(group, _tokens->outlineColorFactor, result.outline_color);
}

} // namespace

bool HdToonIsValueOnlyChange(const HdDataSourceLocatorSet& locators) {
  static const HdDataSourceLocator vrm(_tokens->vrm);
  return locators.Intersects(vrm) &&
         !locators.Intersects(HdMaterialSchema::GetDefaultLocator());
}

// Rule 2 of the selection, MMD, waits for `mmdImaging`'s Hydra view
// (MAT-Q1). Rule 3's surface network is read in Renderer Phase 5; until then
// a PreviewSurface material has the fallback material's values.
Toon::ToonMaterial HdToonReadMaterial(const HdContainerDataSourceHandle& prim) {
  Toon::ToonMaterial result;
  const HdContainerDataSourceHandle vrm = Group(prim, _tokens->vrm);
  const HdContainerDataSourceHandle mtoon = Group(vrm, _tokens->mtoon);
  if (!mtoon) {
    return result;
  }
  result.model = Toon::ToonShadingModel::MToon;
  if (const HdContainerDataSourceHandle material =
          Group(vrm, _tokens->material)) {
    ReadCommon(material, result);
  }
  ReadMToon(mtoon, result);
  return result;
}

PXR_NAMESPACE_CLOSE_SCOPE
