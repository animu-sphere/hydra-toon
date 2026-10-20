// SPDX-License-Identifier: Apache-2.0
// Basic metallic/roughness fallback. Uniform ambient approximates diffuse
// irradiance; environment reflections and arbitrary graph evaluation are
// outside this realization. All shading takes place in linear colour.
#include "surface_common.slang"
#include "preview_common.slang"

[shader("vertex")]
TangentVertexStageOutput vertex_main(TangentVertexInput input) {
  return TangentSurfaceVertex(input);
}

[shader("fragment")]
float4 fragment_main(float3 view_normal : NORMAL, float2 st : TEXCOORD0,
    float3 view_position : TEXCOORD1, float4 view_tangent : TEXCOORD2,
    bool front : SV_IsFrontFace) : SV_Target {
  PreviewParameters material;
  if ((draw.flags & kDrawDisplayColor) != 0) {
    material = (PreviewParameters)0;
    material.base_color = float4(asfloat(draw.material_slot), draw.projection_scale, draw.outline_scale, 1);
    // Opaque, rough dielectric, with the geometry's normal and no textures.
    material.surface = float4(0, 1, -1, 0);
    material.normal = float4(0, 0, 1, 0);
  } else {
    material = preview_materials[draw.material_slot];
  }
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
  const float3 position_dx = ddx(view_position);
  const float3 position_dy = ddy(view_position);
  // All implicit samples and frame derivatives precede divergent discard.
  if (material.surface.z >= 0.0 && opacity < material.surface.z) discard;
  const float3 v = ViewDirection(view_position);
  const float nv = max(dot(n, v), 1e-5);
  const float3 f0 = lerp(float3(0.04), base, metallic);
  const float3 f90 = lerp(float3(1.0), base, metallic);
  const float a2 = pow(roughness, 4.0);
  const float gv = 2.0 * nv / (nv + sqrt(a2 + (1.0 - a2) * nv * nv));
  const float pi = 3.14159265359;
  float3 color = base * (1.0 - metallic) * (1.0 - f0) * frame[1].rgb;
  // The material debug view of the shadows: the mean visibility of the
  // lights that have a shared view, 1 without, linear. It reads a light
  // the surface turns from too, which the shading skips.
  const bool shadow_debug = uint(frame[0].z) == kDebugShadow;
  float shadowed = 0.0;
  float viewed = 0.0;
  for (uint index = 0; index < uint(frame[0].y); ++index) {
    float3 l;
    const float3 light = DirectLight(index, view_position, l);
    const float nl = saturate(dot(n, l));
    if (nl <= 0.0 && !shadow_debug) continue;
    const float visibility = ShadowVisibility(index, view_position,
        position_dx, position_dy, n, nl);
    if (ShadowSlot(index) != 0) {
      shadowed += visibility;
      viewed += 1.0;
    }
    if (nl <= 0.0) continue;
    const float3 half_vector = v + l;
    const float3 h = half_vector * rsqrt(max(dot(half_vector, half_vector), 1e-8));
    const float nh = saturate(dot(n, h));
    const float vh = saturate(dot(v, h));
    const float denominator = nh * nh * (a2 - 1.0) + 1.0;
    const float distribution = a2 / max(pi * denominator * denominator, 1e-8);
    const float gl = 2.0 * nl / (nl + sqrt(a2 + (1.0 - a2) * nl * nl));
    const float3 fresnel = f0 + (f90 - f0) * pow(1.0 - vh, 5.0);
    const float3 specular = distribution * gv * gl * fresnel / max(4.0 * nv * nl, 1e-6);
    const float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base / pi;
    color += (diffuse + specular) * light * nl * visibility;
  }
  if (shadow_debug) {
    return float4(float3(viewed > 0.0 ? shadowed / viewed : 1.0), alpha);
  }
  return float4(color + emission, alpha);
}
