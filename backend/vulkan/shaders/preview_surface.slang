// SPDX-License-Identifier: Apache-2.0
// Basic metallic/roughness fallback. Uniform ambient approximates diffuse
// irradiance; environment reflections and arbitrary graph evaluation are
// outside this realization. All shading takes place in linear colour.
#include "surface_common.slang"

struct PreviewTextureParameters {
  float4 uv_rows[2];
  float4 scale;
  float4 bias;
  float4 fallback;
  uint4 info;
};
struct PreviewParameters {
  float4 base_color;
  float4 emissive;
  float4 surface;
  float4 normal;
  PreviewTextureParameters inputs[6];
};
[[vk::binding(7, 0)]] StructuredBuffer<PreviewParameters> materials;

float4 PreviewSample(PreviewTextureParameters input, float2 st) {
  float4 texel = input.fallback;
  if (input.info.x != 0) {
    const float2 uv = (draw.flags & kDrawHasUVs) != 0 ? st
        : float2(input.uv_rows[0].w, input.uv_rows[1].w);
    texel = textures[input.info.x].Sample(samplers[input.info.y], TransformUV(input.uv_rows, uv));
  }
  return texel * input.scale + input.bias;
}

float PreviewScalar(PreviewTextureParameters input, float2 st, float value) {
  return (input.info.w & 1) != 0 ? PreviewSample(input, st)[min(input.info.z, 3)] : value;
}

float3 PreviewColor(PreviewTextureParameters input, float2 st, float3 value) {
  return (input.info.w & 1) != 0 ? PreviewSample(input, st).rgb : value;
}

[shader("vertex")]
TangentVertexStageOutput vertex_main(TangentVertexInput input) {
  return TangentSurfaceVertex(input);
}

[shader("fragment")]
float4 fragment_main(float3 view_normal : NORMAL, float2 st : TEXCOORD0,
    float3 view_position : TEXCOORD1, float4 view_tangent : TEXCOORD2,
    bool front : SV_IsFrontFace) : SV_Target {
  const PreviewParameters material = materials[draw.material_slot];
  const float opacity = saturate(PreviewScalar(material.inputs[4], st, material.base_color.a));
  const float alpha = (draw.flags & kDrawBlend) != 0 ? opacity : 1.0;
  const float3 base = max(PreviewColor(material.inputs[0], st, material.base_color.rgb), 0.0);
  const float metallic = saturate(PreviewScalar(material.inputs[2], st, material.surface.x));
  const float roughness = clamp(PreviewScalar(material.inputs[3], st, material.surface.y), 0.04, 1.0);
  // UsdPreviewSurface consumes an already signed tangent-space vector.
  // Texture scale/bias supplies any [0,1] to [-1,1] decoding; +Y is +st.V.
  const float3 map = PreviewColor(material.inputs[5], st, material.normal.xyz);
  const float3 n = TangentMappedNormal(normalize(view_normal), view_tangent,
      view_position, st, map, 1.0, true) * (front ? 1.0 : -1.0);
  const float3 emission = max(PreviewColor(material.inputs[1], st, material.emissive.rgb), 0.0);
  // All implicit samples and frame derivatives precede divergent discard.
  if (material.surface.z >= 0.0 && opacity < material.surface.z) discard;
  const float3 v = ViewDirection(view_position);
  const float nv = max(dot(n, v), 1e-5);
  const float3 f0 = lerp(float3(0.04), base, metallic);
  const float a2 = pow(roughness, 4.0);
  const float gv = 2.0 * nv / (nv + sqrt(a2 + (1.0 - a2) * nv * nv));
  const float pi = 3.14159265359;
  float3 color = base * (1.0 - metallic) * (1.0 - f0) * frame[1].rgb;
  for (uint index = 0; index < uint(frame[0].y); ++index) {
    float3 l;
    const float3 light = DirectLight(index, view_position, l);
    const float nl = saturate(dot(n, l));
    if (nl <= 0.0) continue;
    const float3 half_vector = v + l;
    const float3 h = half_vector * rsqrt(max(dot(half_vector, half_vector), 1e-8));
    const float nh = saturate(dot(n, h));
    const float vh = saturate(dot(v, h));
    const float denominator = nh * nh * (a2 - 1.0) + 1.0;
    const float distribution = a2 / max(pi * denominator * denominator, 1e-8);
    const float gl = 2.0 * nl / (nl + sqrt(a2 + (1.0 - a2) * nl * nl));
    const float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
    const float3 specular = distribution * gv * gl * fresnel / max(4.0 * nv * nl, 1e-6);
    const float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base / pi;
    color += (diffuse + specular) * light * nl;
  }
  return float4(color + emission, alpha);
}
