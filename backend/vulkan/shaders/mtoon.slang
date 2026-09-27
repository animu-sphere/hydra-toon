// SPDX-License-Identifier: Apache-2.0
// The mtoon_opaque pipeline (material policy §7): MToon's lit / shade split
// with shading shift and toony, GI equalization and emission, untextured.
// A material is a slot in the parameter buffer, so a new or changed
// material never compiles or selects a shader.

// Must match vulkan_internal::MToonDrawConstants.
struct DrawConstants {
  float4x4 clip_from_object;
  // Rows of the view-space normal matrix; w is unused.
  float4 view_normal_rows[3];
  uint material_slot;
  // Scalars, not a uint3, which would align to 16 and overrun 128 bytes.
  uint padding0;
  uint padding1;
  uint padding2;
};

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
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> draw;
[[vk::binding(0, 0)]] StructuredBuffer<MToonParameters> materials;

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
};

struct VertexStageOutput {
  float4 position : SV_Position;
  float3 view_normal : NORMAL;
};

[shader("vertex")]
VertexStageOutput vertex_main(VertexInput input) {
  VertexStageOutput output;
  output.position = mul(draw.clip_from_object, float4(input.position, 1.0));
  output.view_normal = float3(dot(draw.view_normal_rows[0].xyz, input.normal),
      dot(draw.view_normal_rows[1].xyz, input.normal),
      dot(draw.view_normal_rows[2].xyz, input.normal));
  return output;
}

float LinearStep(float low, float high, float value) {
  return saturate((value - low) / max(high - low, 1e-5));
}

[shader("fragment")]
float4 fragment_main(float3 view_normal : NORMAL, bool front : SV_IsFrontFace)
    : SV_Target {
  const MToonParameters material = materials[draw.material_slot];
  const float alpha = material.base_color.a;
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
  const float3 lit = material.base_color.rgb;
  const float3 shade = material.shade_color.rgb;
  float3 color = lerp(shade, lit, shading) * kLightColor;

  // Raw GI would be the ambient seen along the normal; uniformed GI its
  // average over up and down.
  const float3 raw_gi = kAmbientColor;
  const float3 uniformed_gi = kAmbientColor;
  color += lerp(raw_gi, uniformed_gi, material.shading.z) * lit;
  color += material.emissive.rgb;
  return float4(color, 1.0);
}
