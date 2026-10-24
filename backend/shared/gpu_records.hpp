// SPDX-License-Identifier: Apache-2.0
// What both backends fill the same way below their GPU APIs (layout §3): the
// C++ side of the shader contract — draw constants, the frame buffer's rows,
// each model's parameter record, shadow view records, the draw and material
// flags — and the writers that fill them from a DrawList. Each record says
// which Slang declaration it must match. No GPU API type appears here;
// resources, bindings, texture entries and recording stay each backend's.
// Private to backend/.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <toon/extraction.hpp>

namespace Toon::backend_shared {

// Must match DrawConstants in shaders/scene/mesh.slang.
struct DrawConstants {
  float clip_from_object[16];
  float color[4];
  // The kDraw* skinning flags and influence count, as mtoon_opaque's.
  std::uint32_t flags;
  std::uint32_t padding[3];
};

// Must match DrawConstants in shaders/common/surface_common.slang, which every
// MToon, MMD and PreviewSurface pipeline reads: 128 bytes, the push constant
// size every Vulkan device offers, and WebGPU's draw ring binding.
struct SurfaceDrawConstants {
  float clip_from_object[16];
  // The top three rows of view-from-object: its 3x3 in xyz and the
  // translation in w. The shaders derive the normal matrix from it.
  float view_from_object_rows[12];
  std::uint32_t material_slot;
  // kDrawHasUVs when the mesh has texture coordinates; kDrawOrthographic
  // under an orthographic camera; kDrawBlend when its material is
  // transparent; kDrawAlphaToCoverage when mtoon_opaque is multisampled;
  // kDrawSkinned, kDrawConstantInfluences and the influences
  // per point from kInfluenceCountShift up when it is skinned.
  std::uint32_t flags;
  // |projection[1][1]|, for a screen-space outline width.
  float projection_scale;
  // What mtoon_outline's world-coordinates width, which MToon gives in
  // metres, is multiplied by: the scene's units per metre.
  float outline_scale;
  // For kDrawDisplayColor, material_slot holds the float bits of red and
  // projection_scale/outline_scale hold green/blue. No material slot or
  // outline is used; this keeps the shared push constants within 128 bytes.
};
static_assert(sizeof(SurfaceDrawConstants) == 128);

constexpr std::uint32_t kDrawHasUVs = 1U;
constexpr std::uint32_t kDrawSkinned = 2U;
constexpr std::uint32_t kDrawConstantInfluences = 4U;
constexpr std::uint32_t kDrawDisplayColor = 8U;
// The camera is orthographic: every fragment is seen along view-space +z.
constexpr std::uint32_t kDrawOrthographic = 16U;
// The material is transparent: mtoon_outline's hull returns the surface's
// alpha rather than 1.
constexpr std::uint32_t kDrawBlend = 32U;
// mtoon_opaque turns its alpha into samples: a Mask fragment returns its
// coverage of the cutoff rather than being kept or cut away whole.
constexpr std::uint32_t kDrawAlphaToCoverage = 64U;
constexpr std::uint32_t kDrawMorphed = 128U;
// The shadow pipeline cuts a Mask caster at its cutoff, by its MToon or
// PreviewSurface slot's alpha.
constexpr std::uint32_t kDrawCutMToon = 256U;
constexpr std::uint32_t kDrawCutPreview = 512U;
constexpr std::uint32_t kInfluenceCountShift = 16U;
static_assert(sizeof(ToonMorphOffset) == 32);
static_assert(sizeof(ToonMorphRange) == 8);

// An MToon slot's outline width mode, as mtoon_outline reads it; 0 draws
// no outline.
constexpr std::uint32_t kOutlineWorld = 1U;
constexpr std::uint32_t kOutlineScreen = 2U;

// One sampler per glTF wrap pair: index wrap_s * 3 + wrap_t, in ToonWrap
// order.
constexpr std::uint32_t kSamplerCount = 9;

// Two float4 header rows and four rows per direct light. Ambient lights
// accumulate in the header and do not consume direct-light slots.
constexpr std::uint32_t kLightCapacity = 32;
constexpr std::uint32_t kFrameBytes = (2 + 4 * kLightCapacity) * 16;

// One material's slot in the parameter buffer. Must match MToonParameters
// in shaders/mtoon/mtoon_common.slang.
struct MToonParameters {
  float base_color[4];
  // a: the alpha cutoff, negative when the material is not Mask.
  float shade_color[4];
  float emissive[4];
  // x shading shift, y shading toony, z GI equalization.
  float shading[4];
  // x base texture entry, y its sampler; z shade texture entry, w its
  // sampler.
  std::uint32_t textures[4];
  // Each texture's KHR_texture_transform as two rows of a 2x3 affine map
  // in glTF's UV space, each padded to four floats.
  float base_uv[8];
  float shade_uv[8];
  // rgb linear, a the outline lighting mix.
  float outline_color[4];
  // x the outline width factor.
  float outline[4];
  // x outline width texture entry, y its sampler; z kOutlineWorld,
  // kOutlineScreen or 0.
  std::uint32_t outline_texture[4];
  float outline_uv[8];
  // rgb the parametric rim colour, a the rim lighting mix.
  float rim_color[4];
  // x the parametric rim's fresnel power, y its lift.
  float rim[4];
  // rgb matcapFactor.
  float matcap[4];
  // x MatCap texture entry, y its sampler; z rim multiply texture entry,
  // w its sampler.
  std::uint32_t rim_textures[4];
  float matcap_uv[8];
  float rim_uv[8];
  // x emissive entry, y sampler; z normal entry, w sampler.
  std::uint32_t surface_textures[4];
  float emissive_uv[8];
  float normal_uv[8];
  // x shift entry, y sampler; z animation mask entry, w sampler.
  std::uint32_t animation_textures[4];
  float shift_uv[8];
  float mask_uv[8];
  // x normal scale, y shading shift texture scale.
  float surface[4];
  // xy scroll UV/s, z rotation rad/s.
  float animation[4];
};
static_assert(sizeof(MToonParameters) == 544);

// Must match PreviewParameters in shaders/preview/preview_common.slang.
struct PreviewTextureParameters {
  float uv_rows[8];
  float scale[4];
  float bias[4];
  float fallback[4];
  // Texture entry, sampler, scalar channel, connected.
  std::uint32_t info[4];
};
static_assert(sizeof(PreviewTextureParameters) == 96);
struct PreviewParameters {
  float base_color[4];
  float emissive[4];
  // metallic, roughness, Mask cutoff (negative for Opaque/Blend), unused.
  float surface[4];
  float normal[4];
  PreviewTextureParameters inputs[6];
};
static_assert(sizeof(PreviewParameters) == 640);

// An MMD slot's sphere mode, as shaders/mmd/mmd_common.slang reads it.
constexpr std::uint32_t kSphereNone = 0U;
constexpr std::uint32_t kSphereMultiply = 1U;
constexpr std::uint32_t kSphereAdd = 2U;
constexpr std::uint32_t kSphereSubTexture = 3U;

// One material's slot in the MMD parameter buffer. Must match
// MmdParameters in shaders/mmd/mmd_common.slang. Colours are MMD's as stored.
struct MmdParameters {
  float diffuse[4];
  // rgb the specular colour, a the specular power.
  float specular[4];
  float ambient[4];
  // x base texture entry, y its sampler; z sphere texture entry, w its
  // sampler.
  std::uint32_t textures[4];
  // x the toon ramp's entry, 0 without one; y the sphere mode; z 1 when
  // the material receives MMD's self-shadow.
  std::uint32_t modes[4];
  float base_uv[8];
  // rgb the edge colour, a its alpha.
  float edge_color[4];
  // x the edge size, in pixels at an edge scale of 1.
  float edge[4];
  // What MMD's material morphs make of the base, sphere and toon texels:
  // each a multiply RGBA and an add RGBA.
  float texture_tint[8];
  float sphere_tint[8];
  float toon_tint[8];
};
static_assert(sizeof(MmdParameters) == 240);

// Where MMD's self-shadow map was drawn from this frame, and whether MMD
// materials' tints draw. Must match MmdShadowParameters in
// shaders/mmd/mmd_common.slang.
struct MmdShadowParameters {
  // The map's Vulkan clip space from view space, column-major.
  float clip_from_view[16];
  // x 1 when the map was drawn this frame; y MMD units per unit of the
  // map's depth; z the map's size in texels; w 1 when material morphs'
  // tints draw (DrawList::mmd_morph_tints).
  float parameters[4];
  // x how far a receiver's point moves along its normal before the map
  // is read, at most, in view-space units: three of the map's texels; y a
  // texel's width in MMD units; z the map's layer of the shared views'
  // image.
  float texel[4];
};
static_assert(sizeof(MmdShadowParameters) == 96);

// Where a shared shadow view was drawn from this frame, for the receivers
// of every family but MMD. Must match ShadowViewParameters in
// shaders/common/surface_common.slang.
struct ShadowViewParameters {
  // The view's clip space from view space, column-major.
  float clip_from_view[16];
  // x its size in texels, y its layer of the image, z 1 when it is
  // perspective (a spot light's) and 0 when orthographic; w unused.
  float extent[4];
  // In view-space units: x its depth range, y its near plane's distance
  // when perspective, z a texel's width, at a distance of 1 when
  // perspective; w unused.
  float depth[4];
  // The receivers' comparison, in the view's texels (ShadowSettings): x
  // the depth bias, y the normal bias, z the filter radius; w unused.
  float receiver[4];
};
static_assert(sizeof(ShadowViewParameters) == 112);

// Each texture a slot samples, as the slot records it: 0 for a texture the
// material has not got or that is not resident, which samples white. On
// Vulkan the nonzero value is its texture table entry; on WebGPU, which binds
// textures per role, it is 1.
struct MaterialEntries {
  std::uint32_t base = 0;
  std::uint32_t shade = 0;
  std::uint32_t outline = 0;
  std::uint32_t matcap = 0;
  std::uint32_t rim = 0;
  std::uint32_t emissive = 0;
  std::uint32_t normal = 0;
  std::uint32_t shift = 0;
  std::uint32_t mask = 0;
  std::uint32_t sphere = 0;
  std::uint32_t toon = 0;
  std::uint32_t metallic = 0;
  std::uint32_t roughness = 0;
  std::uint32_t opacity = 0;

  friend bool operator==(const MaterialEntries&,
      const MaterialEntries&) = default;
};

// The top three rows of `matrix`, its 3x3 in xyz and translation in w, as
// SurfaceDrawConstants::view_from_object_rows holds them; returns whether
// the 3x3 mirrors, which turns a counter-clockwise triangle clockwise.
bool ViewRows(const Matrix4& matrix, float rows[12]);

// KHR_texture_transform's translate * rotate * scale, as the two rows of a
// 2x3 affine map in glTF's UV space, each padded to four floats.
void WriteUvRows(const ToonTextureRef& texture, float rows[8]);

// The sampler of a texture's glTF wrap pair: wrap_s * 3 + wrap_t.
[[nodiscard]] std::uint32_t SamplerIndex(const ToonTextureRef& texture);

// An MToon material's outline width mode, kOutlineWorld, kOutlineScreen or
// 0, and an MMD material's sphere mode, kSphere*.
[[nodiscard]] std::uint32_t OutlineMode(const ToonMaterial& material);
[[nodiscard]] std::uint32_t SphereMode(const ToonMaterial& material);

// Each model's slot from a material and the entries of the textures it
// samples.
void WriteParameters(const ToonMaterial& material, const MaterialEntries& entries,
    MToonParameters& slot);
void WriteParameters(const ToonMaterial& material, const MaterialEntries& entries,
    MmdParameters& slot);
void WriteParameters(const ToonMaterial& material, const MaterialEntries& entries,
    PreviewParameters& slot);

// The frame buffer's kFrameBytes: time, direct-light count, material debug
// mode and metres per unit; uniform ambient; then each visible direct light
// in view space, four rows each, or the camera's fallback key and ambient
// when the draws use no scene lights. A light's entry names the view of
// `views` it casts into, plus one, while `draws.shadows` has every family
// but MMD receive; without `views`, none.
void WriteFrame(const DrawList& draws, const std::vector<ShadowView>* views,
    std::span<float, kFrameBytes / sizeof(float)> frame);

// The parts a mesh draws in: its committed parts, or, for a mesh a
// RenderWorld did not commit, `whole`, the mesh with its own material.
[[nodiscard]] std::span<const MeshPart> PartsOf(const MeshSnapshot& mesh,
    std::uint32_t index_count, MeshPart& whole);

// Whether a part lies within the triangle list a backend uploaded.
[[nodiscard]] bool Fits(const MeshPart& part, std::uint32_t index_count);

} // namespace Toon::backend_shared
