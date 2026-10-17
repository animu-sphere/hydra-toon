// SPDX-License-Identifier: Apache-2.0
// Basic metallic/roughness fallback. Uniform ambient approximates diffuse
// irradiance; environment reflections and arbitrary graph evaluation are
// outside this realization. All shading takes place in linear colour.
#include "surface_common.slang"

struct PreviewParameters {
  float4 base_color;
  float4 emissive;
  float4 surface;
};
[[vk::binding(7, 0)]] StructuredBuffer<PreviewParameters> materials;

[shader("vertex")]
VertexStageOutput vertex_main(VertexInput input) {
  return SurfaceVertex(input);
}

[shader("fragment")]
float4 fragment_main(float3 view_normal : NORMAL, float2 st : TEXCOORD0,
    float3 view_position : TEXCOORD1, bool front : SV_IsFrontFace) : SV_Target {
  const PreviewParameters material = materials[draw.material_slot];
  const float opacity = saturate(material.base_color.a);
  if (material.surface.z >= 0.0 && opacity < material.surface.z) discard;
  const float alpha = (draw.flags & kDrawBlend) != 0 ? opacity : 1.0;
  const float3 base = max(material.base_color.rgb, 0.0);
  const float metallic = saturate(material.surface.x);
  const float roughness = clamp(material.surface.y, 0.04, 1.0);
  const float3 n = normalize(view_normal) * (front ? 1.0 : -1.0);
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
  return float4(color + max(material.emissive.rgb, 0.0), alpha);
}
