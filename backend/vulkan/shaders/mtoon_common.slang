// SPDX-License-Identifier: Apache-2.0
// What mtoon_opaque, mtoon_transparent and mtoon_outline share (material
// policy §7): the draw constants, the material parameter slot and texture
// table, scene lighting, the surface's vertex stage and MToon's
// surface shading, rim included. Included, not compiled on its own.

#include "skinning.slang"

// Must match vulkan_internal::MToonDrawConstants.
struct DrawConstants {
  float4x4 clip_from_object;
  // The top three rows of view-from-object: its 3x3 in xyz, the
  // translation in w.
  float4 view_from_object_rows[3];
  uint material_slot;
  // kDrawHasUVs when the mesh has texture coordinates, kDrawOrthographic
  // under an orthographic camera, kDrawBlend when the material is
  // transparent, kDrawAlphaToCoverage when mtoon_opaque is multisampled,
  // and the skinning flags (skinning.slang).
  uint flags;
  // |projection[1][1]|: clip-space y per unit of view-space height at w = 1.
  // Read by mtoon_outline's screen-coordinates width alone. Scalars, not a
  // uint2, which could align past 128 bytes.
  float projection_scale;
  // The scene's units per metre: 1 / its metersPerUnit. Read by
  // mtoon_outline's world-coordinates width alone.
  float units_per_meter;
};

static const uint kDrawHasUVs = 1;
// Must match vulkan_internal::kDrawOrthographic.
static const uint kDrawOrthographic = 16;
// Must match vulkan_internal::kDrawBlend.
static const uint kDrawBlend = 32;
// Must match vulkan_internal::kDrawAlphaToCoverage.
static const uint kDrawAlphaToCoverage = 64;
// Must match vulkan_internal::kTextureCapacity and kSamplerCount.
static const uint kTextureCapacity = 128;
static const uint kSamplerCount = 9;
// Must match vulkan_internal::kOutlineWorld and kOutlineScreen.
static const uint kOutlineWorld = 1;
static const uint kOutlineScreen = 2;

// Must match vulkan_internal::MToonParameters.
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

[[vk::push_constant]] ConstantBuffer<DrawConstants> draw;
[[vk::binding(0, 0)]] StructuredBuffer<MToonParameters> materials;
// Indexed by a value from the material slot, so uniform across a draw.
[[vk::binding(1, 0)]] Texture2D<float4> textures[kTextureCapacity];
[[vk::binding(2, 0)]] SamplerState samplers[kSamplerCount];
// Header: time, direct-light count, material debug mode, metres per unit;
// uniform ambient RGB. Then four float4 rows per light, in view space:
// colour/type, position/radius (metres), direction/outer cosine, inner cosine.
[[vk::binding(3, 0)]] StructuredBuffer<float4> frame;

struct VertexInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  // USD's st: origin at the image's bottom left. (0, 0) when the mesh has
  // none, bound with stride 0.
  float2 st : TEXCOORD0;
  uint vertex : SV_VulkanVertexID;
};

struct VertexStageOutput {
  float4 position : SV_Position;
  float3 view_normal : NORMAL;
  float2 st : TEXCOORD0;
  float3 view_position : TEXCOORD1;
};

float3 ViewPosition(float3 position) {
  const float4 point = float4(position, 1.0);
  return float3(dot(draw.view_from_object_rows[0], point),
      dot(draw.view_from_object_rows[1], point),
      dot(draw.view_from_object_rows[2], point));
}

// The inverse transpose of view-from-object's 3x3, which takes normals to
// view space. Its rows are the cross products of the 3x3's rows over its
// determinant, and they are the inverse's columns.
float3x3 NormalMatrix() {
  const float3 row0 = draw.view_from_object_rows[0].xyz;
  const float3 row1 = draw.view_from_object_rows[1].xyz;
  const float3 row2 = draw.view_from_object_rows[2].xyz;
  const float3 cross0 = cross(row1, row2);
  const float determinant = dot(row0, cross0);
  const float scale = determinant == 0.0 ? 1.0 : 1.0 / determinant;
  return float3x3(cross0 * scale, cross(row2, row0) * scale,
      cross(row0, row1) * scale);
}

float3 ViewNormal(float3 normal) {
  return mul(NormalMatrix(), normal);
}

// The surface's vertex stage, mtoon_opaque's and mtoon_transparent's: the
// point and normal skinned, then seen through the camera.
VertexStageOutput SurfaceVertex(VertexInput input) {
  VertexStageOutput output;
  float3 position = input.position;
  float3 normal = input.normal;
  Skin(draw.flags, input.vertex, position, normal);
  output.position = mul(draw.clip_from_object, float4(position, 1.0));
  output.view_normal = ViewNormal(normal);
  output.st = input.st;
  output.view_position = ViewPosition(position);
  return output;
}

// The unit vector from a view-space point toward the eye.
float3 ViewDirection(float3 view_position) {
  return (draw.flags & kDrawOrthographic) != 0 ? float3(0.0, 0.0, 1.0)
                                                : normalize(-view_position);
}

// st in glTF's UV space, through a texture's KHR_texture_transform. glTF's
// UV space has its origin at the image's top left, as the image's first row
// is, so st is flipped into it and the transform applies as glTF states it.
float2 TransformUV(float4 uv_rows[2], float2 st) {
  const float3 uv = float3(st.x, 1.0 - st.y, 1.0);
  return float2(dot(uv_rows[0].xyz, uv), dot(uv_rows[1].xyz, uv));
}

// A mesh without UVs samples the white entry.
uint TextureEntry(uint entry) {
  return (draw.flags & kDrawHasUVs) == 0 ? 0 : entry;
}

float4 SampleTexture(uint entry, uint sampler, float4 uv_rows[2], float2 st) {
  return textures[TextureEntry(entry)].Sample(samplers[sampler],
      TransformUV(uv_rows, st));
}

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

// A tangent frame from the mesh's original UVs and deformed positions.
// Texture transforms/animation move the sample, not the tangent frame.
// Degenerate or absent UVs retain the authored (or derived) vertex normal.
float3 MappedNormal(float3 normal, float3 position, float2 st,
    float3 texel, float scale, bool enabled) {
  const float3 dx = ddx(position);
  const float3 dy = ddy(position);
  const float2 ux = ddx(float2(st.x, 1.0 - st.y));
  const float2 uy = ddy(float2(st.x, 1.0 - st.y));
  const float determinant = ux.x * uy.y - ux.y * uy.x;
  if (!enabled || abs(determinant) < 1e-10) {
    return normal;
  }
  float3 tangent = (dx * uy.y - dy * ux.y) / determinant;
  tangent -= normal * dot(normal, tangent);
  const float3 bitangent = (dy * ux.x - dx * uy.x) / determinant;
  if (dot(tangent, tangent) < 1e-12 || dot(bitangent, bitangent) < 1e-12) {
    return normal;
  }
  tangent = normalize(tangent);
  const float handedness = dot(cross(normal, tangent), bitangent) < 0.0 ? -1.0 : 1.0;
  const float3 map = float3((texel.xy * 2.0 - 1.0) * scale, texel.z * 2.0 - 1.0);
  const float3 mapped = tangent * map.x + cross(normal, tangent) * handedness * map.y + normal * map.z;
  return dot(mapped, mapped) > 1e-12 ? normalize(mapped) : normal;
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
    float3 view_position, float2 st, bool front) {
  float3 normal = normalize(view_normal);
  const float mask = SampleTexture(material.animation_textures.z,
      material.animation_textures.w, material.mask_uv, st).b;
  const float2 animated_st = AnimatedST(material, st, mask);
  const float3 normal_texel = SampleTexture(material.surface_textures.z,
      material.surface_textures.w, material.normal_uv, animated_st).rgb;
  normal = MappedNormal(normal, view_position, st, normal_texel,
      material.surface.x, TextureEntry(material.surface_textures.z) != 0);
  if (!front) {
    normal = -normal;
  }
  const float3 view = ViewDirection(view_position);
  const float4 base_texel = SampleTexture(material.textures.x,
      material.textures.y, material.base_uv, animated_st);
  const float3 shade_texel = SampleTexture(material.textures.z,
      material.textures.w, material.shade_uv, animated_st).rgb;
  // The MatCap texture needs no UVs, and a material without one adds none,
  // so its entry is not replaced by white.
  const float3 matcap_texel = textures[material.rim_textures.x].Sample(
      samplers[material.rim_textures.y],
      TransformUV(material.matcap_uv, MatcapST(normal, view))).rgb;
  const float3 rim_texel = SampleTexture(material.rim_textures.z,
      material.rim_textures.w, material.rim_uv, animated_st).rgb;
  const float3 emissive_texel = SampleTexture(material.surface_textures.x,
      material.surface_textures.y, material.emissive_uv, animated_st).rgb;
  const float shift_texel = SampleTexture(material.animation_textures.x,
      material.animation_textures.y, material.shift_uv, animated_st).r;
  const float shift = TextureEntry(material.animation_textures.x) == 0 ? 0.0
      : shift_texel * material.surface.y;
  const float toony = material.shading.y;
  const float3 lit = material.base_color.rgb * base_texel.rgb;
  const float3 shade = material.shade_color.rgb * shade_texel;
  float3 color = float3(0.0);
  float3 lighting = float3(0.0);
  for (uint index = 0; index < uint(frame[0].y); ++index) {
    const uint row = 2 + index * 4;
    const float4 source = frame[row];
    const float4 point = frame[row + 1];
    const float4 axis = frame[row + 2];
    float3 light = -normalize(axis.xyz);
    float attenuation = 1.0;
    if (source.w != 0.0) {
      const float3 delta = point.xyz - view_position;
      const float distance_squared = dot(delta, delta);
      light = distance_squared > 1e-12 ? delta * rsqrt(distance_squared) : -normalize(axis.xyz);
      attenuation = 1.0 / max(distance_squared * frame[0].w * frame[0].w, point.w * point.w);
      if (source.w == 2.0) {
        const float cosine = dot(-light, normalize(axis.xyz));
        const float inner = frame[row + 3].x;
        attenuation *= inner > axis.w + 1e-6
            ? smoothstep(axis.w, inner, cosine) : (cosine >= axis.w ? 1.0 : 0.0);
      }
    }
    const float3 incident = source.rgb * attenuation;
    const float shading = LinearStep(-1.0 + toony, 1.0 - toony,
        dot(normal, light) + material.shading.x + shift);
    color += lerp(shade, lit, shading) * incident;
    lighting += incident;
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
  // GI, as three-vrm sums them.
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

  MToonSurface surface;
  surface.color = color;
  surface.alpha = material.base_color.a * base_texel.a;
  return surface;
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
  return saturate((alpha - cutoff) / max(fwidth(alpha), 1e-4) + 0.5);
}
