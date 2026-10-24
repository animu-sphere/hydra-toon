// SPDX-License-Identifier: Apache-2.0
// What mtoon_opaque, mtoon_transparent and mtoon_outline share (material
// policy §7): the material parameter slot and MToon's surface shading, rim
// included, over what every surface pipeline shares. Included, not
// compiled on its own.

#include "common/surface_common.slang"

// Must match backend_shared::MToonParameters.
struct MToonParameters {
  // rgb linear, a alpha.
  float4 base_color;
  // rgb linear, a the alpha cutoff; negative when the material is not Mask.
  float4 shade_color;
  // rgb linear, strength applied; a unused.
  float4 emissive;
  // x shading shift, y shading toony, z GI equalization, w unused.
  float4 shading;
  // x base texture entry, y its sampler; z shade texture entry, w its
  // sampler. Entry 0 is white.
  uint4 textures;
  // Each texture's KHR_texture_transform: two rows of a 2x3 affine map in
  // glTF's UV space; w unused.
  float4 base_uv[2];
  float4 shade_uv[2];
  // rgb linear, a the outline lighting mix.
  float4 outline_color;
  // x the outline width factor; yzw unused.
  float4 outline;
  // x outline width texture entry, y its sampler; z the width mode,
  // kOutlineWorld or kOutlineScreen (0 draws no outline); w unused.
  uint4 outline_texture;
  float4 outline_uv[2];
  // rgb the parametric rim colour, linear; a the rim lighting mix.
  float4 rim_color;
  // x the parametric rim's fresnel power, y its lift; zw unused.
  float4 rim;
  // rgb matcapFactor, linear; a unused.
  float4 matcap;
  // x MatCap texture entry, y its sampler; z rim multiply texture entry,
  // w its sampler. A MatCap entry of 0 adds no MatCap.
  uint4 rim_textures;
  float4 matcap_uv[2];
  float4 rim_uv[2];
  uint4 surface_textures;
  float4 emissive_uv[2];
  float4 normal_uv[2];
  uint4 animation_textures;
  float4 shift_uv[2];
  float4 mask_uv[2];
  float4 surface;
  float4 animation;
};

TOON_MTOON_MATERIALS_BINDING StructuredBuffer<MToonParameters> materials;

// MToon's texture roles (WebGPU policy §4), the bindings' role numbers.
// Must match webgpu_internal::MToonRole.
static const uint kMToonBase = 0;
static const uint kMToonShade = 1;
static const uint kMToonNormal = 2;
static const uint kMToonEmissive = 3;
static const uint kMToonRim = 4;
static const uint kMToonMatcap = 5;
static const uint kMToonOutlineWidth = 6;
static const uint kMToonMask = 7;
static const uint kMToonShift = 8;

float2 AnimatedST(MToonParameters material, float2 st, float mask) {
  const float time = frame[0].x * mask;
  const float angle = material.animation.z * time;
  const float c = cos(angle);
  const float s = sin(angle);
  const float2 centered = float2(st.x, 1.0 - st.y) - 0.5;
  // MToon's U-right, V-down rotation, then scroll, before each role's
  // KHR_texture_transform. The mask itself stays at unanimated UVs.
  const float2 uv = float2(c * centered.x + s * centered.y,
      -s * centered.x + c * centered.y) + 0.5 + material.animation.xy * time;
  return float2(uv.x, 1.0 - uv.y);
}

float LinearStep(float low, float high, float value) {
  return saturate((value - low) / max(high - low, 1e-5));
}

struct MToonSurface {
  float3 color;
  float alpha;
};

// Where MToon's MatCap samples, as st: the normal seen in a frame whose x
// is horizontal across the view direction and whose y is up across it.
// Up is the camera's, as three-vrm takes it; MToon's pseudocode takes the
// world's, and the two agree while the camera is level. The texture's
// transform applies after, as for any other.
float2 MatcapST(float3 normal, float3 view) {
  const float3 across = cross(float3(0.0, 1.0, 0.0), view);
  const float3 x = dot(across, across) > 1e-12 ? normalize(across)
                                                : float3(1.0, 0.0, 0.0);
  const float3 y = cross(view, x);
  return float2(dot(x, normal), dot(y, normal)) * 0.495 + 0.5;
}

// MToon 1.0's surface under the scene lights. Every sample comes first,
// in uniform control flow, so the implicit derivatives that pick a mip
// level are defined before a caller discards. A back face is lit from its
// own side: the back of a double-sided surface, or the hull an outline
// draws with front faces culled.
MToonSurface ShadeSurface(MToonParameters material, float3 view_normal,
    float4 view_tangent, float3 view_position, float2 st, bool front) {
  float3 normal = normalize(view_normal);
  // For the shared shadow views' receiver plane, in uniform control flow.
  const float3 position_dx = DerivativeX(view_position);
  const float3 position_dy = DerivativeY(view_position);
  const float mask = SampleTexture(kMToonMask, material.animation_textures.z,
      material.animation_textures.w, material.mask_uv, st).b;
  const float2 animated_st = AnimatedST(material, st, mask);
  const float3 normal_texel = SampleTexture(kMToonNormal, material.surface_textures.z,
      material.surface_textures.w, material.normal_uv, animated_st).rgb;
  const float3 map = float3((normal_texel.xy * 2.0 - 1.0) * material.surface.x,
      normal_texel.z * 2.0 - 1.0);
  normal = TangentMappedNormal(normal, view_tangent, view_position, st, map,
      -1.0, TextureEntry(material.surface_textures.z) != 0);
  if (!front) {
    normal = -normal;
  }
  const float3 view = ViewDirection(view_position);
  const float4 base_texel = SampleTexture(kMToonBase, material.textures.x,
      material.textures.y, material.base_uv, animated_st);
  const float3 shade_texel = SampleTexture(kMToonShade, material.textures.z,
      material.textures.w, material.shade_uv, animated_st).rgb;
  // The MatCap texture needs no UVs, and a material without one adds none,
  // so its entry is not replaced by white.
  const float3 matcap_texel = SampleRole(kMToonMatcap,
      material.rim_textures.x, material.rim_textures.y,
      TransformUV(material.matcap_uv, MatcapST(normal, view))).rgb;
  const float3 rim_texel = SampleTexture(kMToonRim, material.rim_textures.z,
      material.rim_textures.w, material.rim_uv, animated_st).rgb;
  const float3 emissive_texel = SampleTexture(kMToonEmissive, material.surface_textures.x,
      material.surface_textures.y, material.emissive_uv, animated_st).rgb;
  const float shift_texel = SampleTexture(kMToonShift, material.animation_textures.x,
      material.animation_textures.y, material.shift_uv, animated_st).r;
  const float shift = TextureEntry(material.animation_textures.x) == 0 ? 0.0
      : shift_texel * material.surface.y;
  const float toony = material.shading.y;
  const float3 lit = material.base_color.rgb * base_texel.rgb;
  const float3 shade = material.shade_color.rgb * shade_texel;
  float3 color = float3(0.0);
  float3 lighting = float3(0.0);
  // The visibility of the lights that have a shared view, for the debug
  // view: their sum and count.
  float shadowed = 0.0;
  float viewed = 0.0;
  for (uint index = 0; index < uint(frame[0].y); ++index) {
    float3 light;
    const float3 incident = DirectLight(index, view_position, light);
    const float lambert = dot(normal, light);
    const float visibility = ShadowVisibility(index, view_position,
        position_dx, position_dy, normal, lambert);
    const float shading = LinearStep(-1.0 + toony, 1.0 - toony,
        lambert + material.shading.x + shift) * visibility;
    color += lerp(shade, lit, shading) * incident;
    lighting += incident;
    if (ShadowSlot(index) != 0) {
      shadowed += visibility;
      viewed += 1.0;
    }
  }

  // Raw GI would be the ambient seen along the normal; uniformed GI its
  // average over up and down.
  const float3 raw_gi = frame[1].rgb;
  const float3 uniformed_gi = frame[1].rgb;
  const float3 gi = lerp(raw_gi, uniformed_gi, material.shading.z);
  color += gi * lit;
  color += material.emissive.rgb * emissive_texel;

  // The rim: MatCap and the parametric rim, masked by the rim multiply
  // texture, then lit as far as rimLightingMixFactor asks. The light it
  // mixes in is what reaches the surface, the direct light's colour and the
  // GI, as three-vrm sums them, unshadowed: three-vrm's shadow scales the
  // shading term alone.
  float3 rim = material.rim_textures.x == 0
                   ? float3(0.0)
                   : material.matcap.rgb * matcap_texel;
  const float parametric = pow(
      saturate(1.0 - dot(normal, view) + material.rim.y),
      max(material.rim.x, 1e-5));
  rim += parametric * material.rim_color.rgb;
  rim *= rim_texel;
  color += rim * lerp(float3(1.0), lighting + gi, material.rim_color.a);

  const uint debug = uint(frame[0].z);
  if (debug == 1) color = lit;
  if (debug == 2) color = normal * 0.5 + 0.5;
  if (debug == 3) color = lighting;
  if (debug == 4) color = gi;
  // The mean visibility of the lights that have a shared view, 1 without.
  if (debug == kDebugShadow) color = float3(viewed > 0.0 ? shadowed / viewed : 1.0);

  MToonSurface surface;
  surface.color = color;
  surface.alpha = material.base_color.a * base_texel.a;
  return surface;
}

// The alpha MToon's surface has at st, as ShadeSurface takes it, for a
// shadow caster's cut: the base colour's and its texel's at the animated
// UVs.
float SurfaceAlpha(MToonParameters material, float2 st) {
  const float mask = SampleTexture(kMToonMask, material.animation_textures.z,
      material.animation_textures.w, material.mask_uv, st).b;
  const float2 animated_st = AnimatedST(material, st, mask);
  return material.base_color.a * SampleTexture(kMToonBase, material.textures.x,
      material.textures.y, material.base_uv, animated_st).a;
}

// Whether a Mask material cuts this fragment away.
bool CutAway(MToonParameters material, float alpha) {
  return material.shade_color.a >= 0.0 && alpha < material.shade_color.a;
}

// The share of a fragment's samples its material keeps: 1 unless it is
// Mask, which keeps a fragment at or above its cutoff whole and cuts one
// below it away. Under alpha to coverage, the cut is instead spread over
// the pixel the alpha takes to cross the cutoff, so the pipeline keeps as
// many samples as the fragment lies above it and a cutout's edge is
// anti-aliased like a silhouette. The cutoff and the flag are the draw's,
// so the derivative is taken in uniform control flow.
float MaskCoverage(MToonParameters material, float alpha) {
  const float cutoff = material.shade_color.a;
  if (cutoff < 0.0) {
    return 1.0;
  }
  if ((draw.flags & kDrawAlphaToCoverage) == 0) {
    return alpha < cutoff ? 0.0 : 1.0;
  }
  return saturate((alpha - cutoff) / max(DerivativeWidth(alpha), 1e-4) + 0.5);
}
