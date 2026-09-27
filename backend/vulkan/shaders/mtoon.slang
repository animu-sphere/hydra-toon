// SPDX-License-Identifier: Apache-2.0
// The mtoon_opaque pipeline (material policy §7): MToon's lit / shade split
// with shading shift and toony, GI equalization and emission, with the base
// and shade colour textures. A material is a slot in the parameter buffer
// plus texture table entries, so a new or changed material never compiles
// or selects a shader.

#include "skinning.slang"

// Must match vulkan_internal::MToonDrawConstants.
struct DrawConstants {
  float4x4 clip_from_object;
  // Rows of the view-space normal matrix; w is unused.
  float4 view_normal_rows[3];
  uint material_slot;
  // kDrawHasUVs when the mesh has texture coordinates, and the skinning
  // flags (skinning.slang).
  uint flags;
  // Scalars, not a uint2, which could align past 128 bytes.
  uint padding0;
  uint padding1;
};

static const uint kDrawHasUVs = 1;
// Must match vulkan_internal::kTextureCapacity and kSamplerCount.
static const uint kTextureCapacity = 128;
static const uint kSamplerCount = 9;

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
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> draw;
[[vk::binding(0, 0)]] StructuredBuffer<MToonParameters> materials;
// Indexed by a value from the material slot, so uniform across a draw.
[[vk::binding(1, 0)]] Texture2D<float4> textures[kTextureCapacity];
[[vk::binding(2, 0)]] SamplerState samplers[kSamplerCount];

// A stand-in for the scene's lights until hdToon reads them: one white key
// light fixed to the camera, above and to the right of it, in view space
// (toward the light), and a uniform ambient. With uniform ambient, GI
// equalization has nothing to equalize, but the term is kept in MToon's form.
static const float3 kViewLightDirection = float3(0.25, 0.5, 1.0);
static const float3 kLightColor = float3(1.0, 1.0, 1.0);
static const float3 kAmbientColor = float3(0.25, 0.25, 0.25);

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
};

[shader("vertex")]
VertexStageOutput vertex_main(VertexInput input) {
  VertexStageOutput output;
  float3 position = input.position;
  float3 normal = input.normal;
  Skin(draw.flags, input.vertex, position, normal);
  output.position = mul(draw.clip_from_object, float4(position, 1.0));
  output.view_normal = float3(dot(draw.view_normal_rows[0].xyz, normal),
      dot(draw.view_normal_rows[1].xyz, normal),
      dot(draw.view_normal_rows[2].xyz, normal));
  output.st = input.st;
  return output;
}

// A texture sampled through its KHR_texture_transform. glTF's UV space has
// its origin at the image's top left, as the image's first row is, so st
// is flipped into it and the transform applies as glTF states it. A mesh
// without UVs samples the white entry.
float4 SampleTexture(uint entry, uint sampler, float4 uv_rows[2], float2 st) {
  if ((draw.flags & kDrawHasUVs) == 0) {
    entry = 0;
  }
  const float3 uv = float3(st.x, 1.0 - st.y, 1.0);
  const float2 transformed = float2(dot(uv_rows[0].xyz, uv),
      dot(uv_rows[1].xyz, uv));
  return textures[entry].Sample(samplers[sampler], transformed);
}

float LinearStep(float low, float high, float value) {
  return saturate((value - low) / max(high - low, 1e-5));
}

[shader("fragment")]
float4 fragment_main(float3 view_normal : NORMAL, float2 st : TEXCOORD0,
    bool front : SV_IsFrontFace) : SV_Target {
  const MToonParameters material = materials[draw.material_slot];
  // Sampled before any discard, in uniform control flow, so the implicit
  // derivatives that pick a mip level are defined.
  const float4 base_texel = SampleTexture(material.textures.x,
      material.textures.y, material.base_uv, st);
  const float3 shade_texel = SampleTexture(material.textures.z,
      material.textures.w, material.shade_uv, st).rgb;
  const float alpha = material.base_color.a * base_texel.a;
  if (material.shade_color.a >= 0.0 && alpha < material.shade_color.a) {
    discard;
  }
  // A back face is drawn only when the material is double-sided, and then
  // is lit from its own side.
  float3 normal = normalize(view_normal);
  if (!front) {
    normal = -normal;
  }
  const float3 light = normalize(kViewLightDirection);

  // MToon 1.0: the shading term, shifted, then made toony.
  const float toony = material.shading.y;
  const float shading = LinearStep(-1.0 + toony, 1.0 - toony,
      dot(normal, light) + material.shading.x);
  const float3 lit = material.base_color.rgb * base_texel.rgb;
  const float3 shade = material.shade_color.rgb * shade_texel;
  float3 color = lerp(shade, lit, shading) * kLightColor;

  // Raw GI would be the ambient seen along the normal; uniformed GI its
  // average over up and down.
  const float3 raw_gi = kAmbientColor;
  const float3 uniformed_gi = kAmbientColor;
  color += lerp(raw_gi, uniformed_gi, material.shading.z) * lit;
  color += material.emissive.rgb;
  return float4(color, 1.0);
}
