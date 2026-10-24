// SPDX-License-Identifier: Apache-2.0
#include "gpu_records.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Toon::backend_shared {
namespace {

// MMD's unit on the stage (usd-mmd-plugins stage contract §6), as
// shaders/mmd/mmd_outline.slang states it.
constexpr double kMetersPerMmdUnit = 0.08;

// A tint's multiply RGBA, then its add RGBA.
void WriteTint(const ToonMaterial::MMD::TextureTint& tint, float (&row)[8]) {
  row[0] = tint.multiply.x;
  row[1] = tint.multiply.y;
  row[2] = tint.multiply.z;
  row[3] = tint.multiply.w;
  row[4] = tint.add.x;
  row[5] = tint.add.y;
  row[6] = tint.add.z;
  row[7] = tint.add.w;
}

} // namespace

bool ViewRows(const Matrix4& matrix, float rows[12]) {
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 4; ++column) {
      rows[row * 4 + column] = matrix.m[column * 4 + row];
    }
  }
  const float* m = matrix.m.data();
  const float determinant = m[0] * (m[5] * m[10] - m[9] * m[6]) -
                            m[4] * (m[1] * m[10] - m[9] * m[2]) +
                            m[8] * (m[1] * m[6] - m[5] * m[2]);
  return determinant < 0.0F;
}

void WriteUvRows(const ToonTextureRef& texture, float rows[8]) {
  const float c = std::cos(texture.rotation);
  const float s = std::sin(texture.rotation);
  const float values[8] = {texture.scale.x * c, texture.scale.y * s,
      texture.offset.x, 0.0F, -texture.scale.x * s, texture.scale.y * c,
      texture.offset.y, 0.0F};
  std::copy(values, values + 8, rows);
}

std::uint32_t SamplerIndex(const ToonTextureRef& texture) {
  return static_cast<std::uint32_t>(texture.wrap_s) * 3U +
         static_cast<std::uint32_t>(texture.wrap_t);
}

std::uint32_t OutlineMode(const ToonMaterial& material) {
  switch (material.mtoon.outline_width_mode) {
  case ToonOutlineWidthMode::World:
    return kOutlineWorld;
  case ToonOutlineWidthMode::Screen:
    return kOutlineScreen;
  case ToonOutlineWidthMode::None:
    break;
  }
  return 0U;
}

void WriteParameters(const ToonMaterial& material,
    const MaterialEntries& entries, MToonParameters& slot) {
  slot = {};
  slot.base_color[0] = material.base_color.x;
  slot.base_color[1] = material.base_color.y;
  slot.base_color[2] = material.base_color.z;
  slot.base_color[3] = material.alpha;
  slot.shade_color[0] = material.mtoon.shade_color.x;
  slot.shade_color[1] = material.mtoon.shade_color.y;
  slot.shade_color[2] = material.mtoon.shade_color.z;
  slot.shade_color[3] = material.alpha_mode == ToonAlphaMode::Mask
                            ? material.alpha_cutoff
                            : -1.0F;
  slot.emissive[0] = material.emissive.x;
  slot.emissive[1] = material.emissive.y;
  slot.emissive[2] = material.emissive.z;
  slot.shading[0] = material.mtoon.shading_shift;
  slot.shading[1] = material.mtoon.shading_toony;
  slot.shading[2] = material.mtoon.gi_equalization;
  slot.textures[0] = entries.base;
  slot.textures[1] = SamplerIndex(material.base_texture);
  slot.textures[2] = entries.shade;
  slot.textures[3] = SamplerIndex(material.mtoon.shade_texture);
  WriteUvRows(material.base_texture, slot.base_uv);
  WriteUvRows(material.mtoon.shade_texture, slot.shade_uv);
  slot.outline_color[0] = material.outline_color.x;
  slot.outline_color[1] = material.outline_color.y;
  slot.outline_color[2] = material.outline_color.z;
  slot.outline_color[3] = material.mtoon.outline_lighting_mix;
  slot.outline[0] = material.outline_width;
  slot.outline_texture[0] = entries.outline;
  slot.outline_texture[1] = SamplerIndex(material.mtoon.outline_width_texture);
  slot.outline_texture[2] = OutlineMode(material);
  WriteUvRows(material.mtoon.outline_width_texture, slot.outline_uv);
  slot.rim_color[0] = material.mtoon.rim_color.x;
  slot.rim_color[1] = material.mtoon.rim_color.y;
  slot.rim_color[2] = material.mtoon.rim_color.z;
  slot.rim_color[3] = material.mtoon.rim_lighting_mix;
  slot.rim[0] = material.mtoon.rim_fresnel_power;
  slot.rim[1] = material.mtoon.rim_lift;
  slot.matcap[0] = material.mtoon.matcap.x;
  slot.matcap[1] = material.mtoon.matcap.y;
  slot.matcap[2] = material.mtoon.matcap.z;
  slot.rim_textures[0] = entries.matcap;
  slot.rim_textures[1] = SamplerIndex(material.mtoon.matcap_texture);
  slot.rim_textures[2] = entries.rim;
  slot.rim_textures[3] = SamplerIndex(material.mtoon.rim_multiply_texture);
  WriteUvRows(material.mtoon.matcap_texture, slot.matcap_uv);
  WriteUvRows(material.mtoon.rim_multiply_texture, slot.rim_uv);
  slot.surface_textures[0] = entries.emissive;
  slot.surface_textures[1] = SamplerIndex(material.emissive_texture);
  slot.surface_textures[2] = entries.normal;
  slot.surface_textures[3] = SamplerIndex(material.normal_texture);
  WriteUvRows(material.emissive_texture, slot.emissive_uv);
  WriteUvRows(material.normal_texture, slot.normal_uv);
  slot.animation_textures[0] = entries.shift;
  slot.animation_textures[1] = SamplerIndex(material.mtoon.shading_shift_texture);
  slot.animation_textures[2] = entries.mask;
  slot.animation_textures[3] = SamplerIndex(material.mtoon.uv_animation_mask_texture);
  WriteUvRows(material.mtoon.shading_shift_texture, slot.shift_uv);
  WriteUvRows(material.mtoon.uv_animation_mask_texture, slot.mask_uv);
  slot.surface[0] = material.normal_scale;
  slot.surface[1] = material.mtoon.shading_shift_texture_scale;
  slot.animation[0] = material.mtoon.uv_scroll_x_speed;
  slot.animation[1] = material.mtoon.uv_scroll_y_speed;
  slot.animation[2] = material.mtoon.uv_rotation_speed;
}

std::uint32_t SphereMode(const ToonMaterial& material) {
  // A mode with no sphere texture changes nothing in MMD; sampled, the
  // missing texture would read white, which an added sphere would add.
  if (material.mmd.sphere_texture.texture == 0) {
    return kSphereNone;
  }
  switch (material.mmd.sphere_mode) {
  case ToonSphereMode::Multiply:
    return kSphereMultiply;
  case ToonSphereMode::Add:
    return kSphereAdd;
  case ToonSphereMode::SubTexture:
    return kSphereSubTexture;
  case ToonSphereMode::Disabled:
    break;
  }
  return kSphereNone;
}

void WriteParameters(const ToonMaterial& material,
    const MaterialEntries& entries, MmdParameters& slot) {
  slot = {};
  slot.diffuse[0] = material.base_color.x;
  slot.diffuse[1] = material.base_color.y;
  slot.diffuse[2] = material.base_color.z;
  slot.diffuse[3] = material.alpha;
  slot.specular[0] = material.mmd.specular_color.x;
  slot.specular[1] = material.mmd.specular_color.y;
  slot.specular[2] = material.mmd.specular_color.z;
  slot.specular[3] = material.mmd.specular_power;
  slot.ambient[0] = material.mmd.ambient_color.x;
  slot.ambient[1] = material.mmd.ambient_color.y;
  slot.ambient[2] = material.mmd.ambient_color.z;
  slot.textures[0] = entries.base;
  slot.textures[1] = SamplerIndex(material.base_texture);
  const std::uint32_t sphere = SphereMode(material);
  slot.textures[2] = sphere == kSphereNone ? 0U : entries.sphere;
  slot.textures[3] = SamplerIndex(material.mmd.sphere_texture);
  slot.modes[0] = entries.toon;
  slot.modes[1] = sphere;
  slot.modes[2] = material.mmd.receive_self_shadow ? 1U : 0U;
  WriteUvRows(material.base_texture, slot.base_uv);
  slot.edge_color[0] = material.outline_color.x;
  slot.edge_color[1] = material.outline_color.y;
  slot.edge_color[2] = material.outline_color.z;
  slot.edge_color[3] = material.mmd.edge_alpha;
  slot.edge[0] = material.outline_width;
  WriteTint(material.mmd.texture_tint, slot.texture_tint);
  WriteTint(material.mmd.sphere_tint, slot.sphere_tint);
  WriteTint(material.mmd.toon_tint, slot.toon_tint);
}

std::span<const MeshPart> PartsOf(const MeshSnapshot& mesh,
    std::uint32_t index_count, MeshPart& whole) {
  if (mesh.parts != nullptr && !mesh.parts->empty()) {
    return *mesh.parts;
  }
  whole = {0, index_count, mesh.material};
  return {&whole, 1};
}

bool Fits(const MeshPart& part, std::uint32_t index_count) {
  return part.index_count != 0 && part.first_index <= index_count &&
         part.index_count <= index_count - part.first_index;
}

void WriteParameters(const ToonMaterial& material, const MaterialEntries& entries,
    PreviewParameters& slot) {
  slot = {};
  slot.base_color[0] = material.base_color.x;
  slot.base_color[1] = material.base_color.y;
  slot.base_color[2] = material.base_color.z;
  slot.base_color[3] = material.alpha;
  slot.emissive[0] = material.emissive.x;
  slot.emissive[1] = material.emissive.y;
  slot.emissive[2] = material.emissive.z;
  slot.surface[0] = material.preview.metallic;
  slot.surface[1] = material.preview.roughness;
  slot.surface[2] = material.alpha_mode == ToonAlphaMode::Mask ? material.alpha_cutoff : -1.0F;
  slot.normal[0] = material.preview.normal.x;
  slot.normal[1] = material.preview.normal.y;
  slot.normal[2] = material.preview.normal.z;
  const ToonTextureRef* refs[] = {&material.base_texture, &material.emissive_texture,
      &material.preview.metallic_texture, &material.preview.roughness_texture,
      &material.preview.opacity_texture, &material.normal_texture};
  const std::uint32_t images[] = {entries.base, entries.emissive,
      entries.metallic, entries.roughness, entries.opacity, entries.normal};
  for (std::size_t i = 0; i < material.preview.inputs.size(); ++i) {
    const auto& input = material.preview.inputs[i];
    auto& target = slot.inputs[i];
    WriteUvRows(*refs[i], target.uv_rows);
    target.uv_rows[3] = input.uv_fallback.x;
    target.uv_rows[7] = input.uv_fallback.y;
    const auto copy = [](const Float4& value, float* out) {
      out[0] = value.x; out[1] = value.y; out[2] = value.z; out[3] = value.w;
    };
    copy(input.scale, target.scale);
    copy(input.bias, target.bias);
    copy(input.fallback, target.fallback);
    target.info[0] = images[i];
    target.info[1] = SamplerIndex(*refs[i]);
    target.info[2] = input.channel;
    target.info[3] = input.connected ? 1U : 0U;
  }
}

void WriteFrame(const DrawList& draws, const std::vector<ShadowView>* views,
    std::span<float, kFrameBytes / sizeof(float)> frame) {
  std::fill(frame.begin(), frame.end(), 0.0F);
  frame[0] = static_cast<float>(draws.time_seconds);
  frame[2] = static_cast<float>(draws.lighting.material);
  frame[3] = draws.meters_per_unit;
  const auto transform = [&](Float3 value, float w) {
    const auto& m = draws.view.view.m;
    return Float3{m[0] * value.x + m[4] * value.y + m[8] * value.z + m[12] * w,
        m[1] * value.x + m[5] * value.y + m[9] * value.z + m[13] * w,
        m[2] * value.x + m[6] * value.y + m[10] * value.z + m[14] * w};
  };
  std::uint32_t count = 0;
  const auto append = [&](const ToonLight& light, bool view_space,
                          LightId id) {
    if (!light.visible) {
      return;
    }
    if (light.type == ToonLightType::Ambient) {
      frame[4] += light.color.x * draws.lighting.ambient_scale;
      frame[5] += light.color.y * draws.lighting.ambient_scale;
      frame[6] += light.color.z * draws.lighting.ambient_scale;
      return;
    }
    if (count == kLightCapacity) {
      return;
    }
    float* slot = frame.data() + 8 + count++ * 16;
    slot[0] = light.color.x * draws.lighting.direct_scale;
    slot[1] = light.color.y * draws.lighting.direct_scale;
    slot[2] = light.color.z * draws.lighting.direct_scale;
    slot[3] = static_cast<float>(light.type);
    const Float3 position = view_space ? light.position : transform(light.position, 1.0F);
    slot[4] = position.x;
    slot[5] = position.y;
    slot[6] = position.z;
    slot[7] = light.radius;
    const Float3 direction = view_space ? light.direction : transform(light.direction, 0.0F);
    slot[8] = direction.x;
    slot[9] = direction.y;
    slot[10] = direction.z;
    constexpr float radians = 0.017453292519943295F;
    slot[11] = std::cos(light.cone_angle * radians);
    slot[12] = std::cos(light.cone_angle * (1.0F - light.cone_softness) * radians);
    // The view it casts into, plus one, for the receivers that are not
    // MMD's; 0 when it has none or they do not receive.
    if (views != nullptr && draws.shadows) {
      for (std::size_t view = 0; view < views->size(); ++view) {
        if ((*views)[view].light == id) {
          slot[13] = static_cast<float>(view + 1U);
        }
      }
    }
  };
  if (!draws.lighting.scene_lights || draws.lights.empty()) {
    ToonLight key;
    key.direction = {-draws.lighting.key_direction.x,
        -draws.lighting.key_direction.y, -draws.lighting.key_direction.z};
    // A zero debug direction keeps the default key instead of producing NaNs.
    if (key.direction == Float3{}) {
      key.direction = {-0.25F, -0.5F, -1.0F};
    }
    append(key, true, 0);
    ToonLight ambient;
    ambient.type = ToonLightType::Ambient;
    ambient.color = {0.25F, 0.25F, 0.25F};
    append(ambient, true, 0);
  } else {
    for (const auto& light : draws.lights) {
      append(light.light, false, light.id);
    }
  }
  frame[1] = static_cast<float>(count);
}

void WriteShadowParameters(const DrawList& draws, const std::vector<ShadowView>& views,
    std::uint32_t resolution, std::span<ShadowViewParameters> written,
    MmdShadowParameters& mmd) {
  const ShadowSettings& settings = draws.shadow_settings;
  mmd = {};
  const Matrix4 world_from_view = AffineInverse(draws.view.view);
  for (std::size_t index = 0; index < views.size() && index < written.size(); ++index) {
    const ShadowView& view = views[index];
    const Matrix4 clip_from_view = Multiply(view.clip_from_world, world_from_view);
    ShadowViewParameters& parameters = written[index];
    std::memcpy(parameters.clip_from_view, clip_from_view.m.data(),
        sizeof(parameters.clip_from_view));
    parameters.extent[0] = static_cast<float>(resolution);
    parameters.extent[1] = static_cast<float>(index);
    parameters.extent[2] = view.perspective ? 1.0F : 0.0F;
    parameters.extent[3] = 0.0F;
    parameters.depth[0] = static_cast<float>(view.depth);
    parameters.depth[1] = static_cast<float>(view.near_plane);
    parameters.depth[2] = static_cast<float>(view.texel);
    parameters.depth[3] = 0.0F;
    parameters.receiver[0] = std::clamp(settings.depth_bias, 0.0F, 16.0F);
    parameters.receiver[1] = std::clamp(settings.normal_bias, 0.0F, 16.0F);
    parameters.receiver[2] = std::clamp(settings.filter_radius, 0.0F, 8.0F);
    parameters.receiver[3] = 0.0F;
    if (!view.mmd) continue;
    std::memcpy(mmd.clip_from_view, clip_from_view.m.data(), sizeof(mmd.clip_from_view));
    mmd.parameters[0] = 1.0F;
    mmd.parameters[1] =
        static_cast<float>(view.depth * draws.meters_per_unit / kMetersPerMmdUnit);
    mmd.parameters[2] = static_cast<float>(resolution);
    mmd.texel[0] = static_cast<float>(3.0 * view.texel);
    mmd.texel[1] = static_cast<float>(view.texel * draws.meters_per_unit / kMetersPerMmdUnit);
    mmd.texel[2] = static_cast<float>(index);
  }
  mmd.parameters[3] = draws.mmd_morph_tints ? 1.0F : 0.0F;
}

} // namespace Toon::backend_shared
