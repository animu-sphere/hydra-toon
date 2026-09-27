// SPDX-License-Identifier: Apache-2.0
//
// Material selection and normalization from a Hydra material prim's data
// sources, and the decoding of the images a material samples. Kept apart
// from Windows.h, whose OPAQUE macro would collide.
#include "adapter.hpp"

#include <pxr/pxr.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/imaging/hd/materialSchema.h>
#include <pxr/imaging/hio/image.h>
#include <pxr/imaging/hio/types.h>
#include <pxr/usd/sdf/assetPath.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

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
    (transparentWithZWrite)

    (textureInfo)
    (baseColor)
    (shadeMultiply)
    (file)
    (texCoord)
    (wrapS)
    (wrapT)
    (transform)
    (offset)
    (rotation)
    (scale)
    ((wrapClampToEdge, "clampToEdge"))
    ((wrapMirroredRepeat, "mirroredRepeat")));

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

void Read(const HdContainerDataSourceHandle& group, const TfToken& name,
    Toon::Float2& value) {
  GfVec2f pair;
  if (Read(group, name, pair)) {
    value = {pair[0], pair[1]};
  }
}

Toon::ToonWrap ToWrap(const TfToken& wrap) {
  return wrap == _tokens->wrapClampToEdge ? Toon::ToonWrap::ClampToEdge
         : wrap == _tokens->wrapMirroredRepeat
             ? Toon::ToonWrap::MirroredRepeat
             : Toon::ToonWrap::Repeat;
}

// Once per authored path, so a material that syncs again stays quiet.
void WarnUnresolved(const std::string& authored) {
  static std::mutex mutex;
  static std::set<std::string> warned;
  std::scoped_lock lock(mutex);
  if (warned.insert(authored).second) {
    TF_WARN("Toon: texture '%s' does not resolve; the material samples "
            "without it", authored.c_str());
  }
}

// VrmTextureInfoAPI:<role>: the image and how it is sampled. `file` is
// present only when authored, and is the path the session's resolver gave
// it (vrmImaging §29). Only TEXCOORD_0 reaches a mesh, as `st`, so a role
// that samples another set is read as sampling nothing.
void ReadTexture(const HdContainerDataSourceHandle& group,
    Toon::ToonTextureEncoding encoding, HdToonTextureKey& key,
    Toon::ToonTextureRef& texture) {
  if (!group) {
    return;
  }
  SdfAssetPath file;
  int tex_coord = 0;
  Read(group, _tokens->texCoord, tex_coord);
  if (!Read(group, _tokens->file, file) || tex_coord != 0) {
    return;
  }
  if (file.GetResolvedPath().empty()) {
    WarnUnresolved(file.GetAssetPath());
    return;
  }
  key.path = file.GetResolvedPath();
  key.encoding = encoding;
  TfToken wrap;
  if (Read(group, _tokens->wrapS, wrap)) {
    texture.wrap_s = ToWrap(wrap);
  }
  if (Read(group, _tokens->wrapT, wrap)) {
    texture.wrap_t = ToWrap(wrap);
  }
  if (const HdContainerDataSourceHandle transform =
          Group(group, _tokens->transform)) {
    Read(transform, _tokens->offset, texture.offset);
    Read(transform, _tokens->rotation, texture.rotation);
    Read(transform, _tokens->scale, texture.scale);
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
HdToonMaterialSource HdToonReadMaterial(
    const HdContainerDataSourceHandle& prim) {
  HdToonMaterialSource source;
  Toon::ToonMaterial& result = source.values;
  const HdContainerDataSourceHandle vrm = Group(prim, _tokens->vrm);
  const HdContainerDataSourceHandle mtoon = Group(vrm, _tokens->mtoon);
  if (!mtoon) {
    return source;
  }
  result.model = Toon::ToonShadingModel::MToon;
  if (const HdContainerDataSourceHandle material =
          Group(vrm, _tokens->material)) {
    ReadCommon(material, result);
  }
  ReadMToon(mtoon, result);
  // Both roles are colour (the schema fixes colour or data by role).
  const HdContainerDataSourceHandle textures =
      Group(vrm, _tokens->textureInfo);
  ReadTexture(Group(textures, _tokens->baseColor),
      Toon::ToonTextureEncoding::Srgb, source.base_texture,
      result.base_texture);
  ReadTexture(Group(textures, _tokens->shadeMultiply),
      Toon::ToonTextureEncoding::Srgb, source.shade_texture,
      result.mtoon.shade_texture);
  return source;
}

// HioImage opens the path through Ar, so a path inside a package (a .usdz)
// reads as a file does. 8- and 16-bit images of one to four channels are
// read; a 16-bit channel keeps its high byte.
bool HdToonLoadTexture(const std::string& path,
    Toon::ToonTextureEncoding encoding, Toon::ToonTexture& texture) {
  const HioImageSharedPtr image = HioImage::OpenForReading(path, 0, 0,
      HioImage::SourceColorSpace::Raw, /*suppressErrors=*/true);
  if (!image || image->GetWidth() <= 0 || image->GetHeight() <= 0) {
    return false;
  }
  const HioFormat format = image->GetFormat();
  const HioType type = HioGetHioType(format);
  const int channels = HioGetComponentCount(format);
  const std::size_t channel_size = HioGetDataSizeOfType(type);
  if ((type != HioTypeUnsignedByte && type != HioTypeUnsignedByteSRGB &&
          type != HioTypeUnsignedShort) ||
      channels < 1 || channels > 4) {
    return false;
  }
  const auto width = static_cast<std::uint32_t>(image->GetWidth());
  const auto height = static_cast<std::uint32_t>(image->GetHeight());
  const std::size_t pixels = static_cast<std::size_t>(width) * height;
  std::vector<std::uint8_t> source(pixels * channels * channel_size);
  HioImage::StorageSpec storage;
  storage.width = static_cast<int>(width);
  storage.height = static_cast<int>(height);
  storage.depth = 1;
  storage.format = format;
  storage.flipped = false;
  storage.data = source.data();
  if (!image->Read(storage)) {
    return false;
  }
  // One channel is grey, two grey and alpha, as glTF reads PNG.
  std::vector<std::uint8_t> rgba(pixels * 4U);
  for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
    std::uint8_t values[4] = {0, 0, 0, 255};
    for (int channel = 0; channel < channels; ++channel) {
      const std::size_t offset =
          (pixel * channels + channel) * channel_size + (channel_size - 1U);
      values[channel] = source[offset];
    }
    std::uint8_t* target = rgba.data() + pixel * 4U;
    if (channels <= 2) {
      target[0] = target[1] = target[2] = values[0];
      target[3] = channels == 2 ? values[1] : std::uint8_t{255};
    } else {
      std::memcpy(target, values, 4);
    }
  }
  texture.width = width;
  texture.height = height;
  texture.encoding = encoding;
  texture.pixels =
      std::make_shared<const std::vector<std::uint8_t>>(std::move(rgba));
  return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
