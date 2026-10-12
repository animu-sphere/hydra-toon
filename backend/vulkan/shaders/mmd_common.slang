// SPDX-License-Identifier: Apache-2.0
// What mmd_opaque and mmd_transparent share (material policy §7): the MMD
// parameter slot, the vertex stage that carries the sub-texture UV, and
// MMD's surface shading, over what every surface pipeline shares. Included, not compiled on its own.
//
// MMD computes its terms on its colours and texels as stored and writes
// the result as it is (material policy MAT-Q3). So the textures are
// sampled undecoded, every term below works on stored values, and the
// result is converted from sRGB to linear once, at the end, so that the
// host's encoding of the linear target writes MMD's values.

#include "surface_common.slang"

// Must match vulkan_internal::MmdParameters.
struct MmdParameters {
  // MMD's diffuse RGBA, as stored.
  float4 diffuse;
  // rgb the specular colour, as stored; a the specular power.
  float4 specular;
  // rgb the ambient colour, as stored; a unused.
  float4 ambient;
  // x base texture entry, y its sampler; z sphere texture entry, w its
  // sampler. Entry 0 is white.
  uint4 textures;
  // x the toon ramp's entry, 0 for a material without one; y the sphere
  // mode, kSphereNone, kSphereMultiply, kSphereAdd or kSphereSubTexture; zw
  // unused.
  uint4 modes;
  float4 base_uv[2];
  // rgb the edge colour, as stored; a its alpha.
  float4 edge_color;
  // x the edge size, in pixels at an edge scale of 1; yzw unused.
  float4 edge;
};

// Must match vulkan_internal::kSphere*.
static const uint kSphereNone = 0;
static const uint kSphereMultiply = 1;
static const uint kSphereAdd = 2;
static const uint kSphereSubTexture = 3;
// The sampler that clamps both axes: ToonWrap::ClampToEdge is 1, and a
// sampler's index is wrap_s * 3 + wrap_t.
static const uint kClampSampler = 4;

[[vk::binding(4, 0)]] StructuredBuffer<MmdParameters> mmd_materials;

// VertexInput's streams at locations 0-2, in its order, and a fourth.
struct MmdVertexInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float2 st : TEXCOORD0;
  // primvars:mmd:uv1's x and y, at location 3: raw, origin at the image's
  // top left. (0, 0) when the mesh has none, bound with stride 0.
  float2 sub_uv : TEXCOORD1;
  uint vertex : SV_VulkanVertexID;
};

// VertexStageOutput's varyings, in its order, and the sub-texture UV.
struct MmdVertexOutput {
  float4 position : SV_Position;
  float3 view_normal : NORMAL;
  float2 st : TEXCOORD0;
  float3 view_position : TEXCOORD1;
  float2 sub_uv : TEXCOORD2;
};

// The surface's vertex stage, SurfaceVertex's, passing the sub-texture UV
// on.
MmdVertexOutput MmdSurfaceVertex(MmdVertexInput input) {
  VertexInput surface_input;
  surface_input.position = input.position;
  surface_input.normal = input.normal;
  surface_input.st = input.st;
  surface_input.vertex = input.vertex;
  const VertexStageOutput surface = SurfaceVertex(surface_input);
  MmdVertexOutput output;
  output.position = surface.position;
  output.view_normal = surface.view_normal;
  output.st = surface.st;
  output.view_position = surface.view_position;
  output.sub_uv = input.sub_uv;
  return output;
}

struct MmdSurface {
  // Linear.
  float3 color;
  float alpha;
};

// sRGB's decoding, which takes a value MMD would write to the value whose
// encoding writes it.
float3 SrgbToLinear(float3 value) {
  const float3 low = value / 12.92;
  const float3 high = pow((value + 0.055) / 1.055, float3(2.4));
  return lerp(high, low, step(value, float3(0.04045)));
}

// MMD's shading, as MikuMikuEffect's full.fx reproduces MMD's own, under
// one light: the scene's first direct light. Its colour, attenuated as
// MToon's lights are, is MMD's light colour, so a light of 0.302 is MMD's
// 77; an ambient light adds nothing, as MMD has none, and without a direct
// light the material shows its ambient term alone. A back face is shaded
// with its front's normal, as MMD shades it. Every sample comes first, in
// uniform control flow.
MmdSurface ShadeMmd(MmdParameters material, float3 view_normal,
    float3 view_position, float2 st, float2 sub_uv) {
  const float3 normal = normalize(view_normal);
  float3 light_direction = float3(0.0, 0.0, 1.0);
  const float3 light = uint(frame[0].y) == 0
      ? float3(0.0)
      : DirectLight(0, view_position, light_direction);
  const float lambert = dot(normal, light_direction);
  const float4 base_texel = SampleTexture(material.textures.x,
      material.textures.y, material.base_uv, st);
  // The sphere map is sampled where the view-space normal points, x right
  // and y up, in the image's top-left UV space; it needs no UVs. A
  // sub-texture is sampled at the mesh's additional UV 1 instead, which is
  // already in that space, as full.fx's use_subtexture samples it.
  const uint sphere = material.modes.y;
  const float2 sphere_uv = sphere == kSphereSubTexture
      ? sub_uv
      : normal.xy * float2(0.5, -0.5) + 0.5;
  const float4 sphere_texel = textures[material.textures.z].Sample(
      samplers[material.textures.w], sphere_uv);
  // The ramp's top row is the lit side, its bottom row the far side.
  const float4 toon_texel = textures[material.modes.x].Sample(
      samplers[kClampSampler], float2(0.0, 0.5 - 0.5 * lambert));

  // MMD's light reaches the diffuse colour, the ambient colour stands in
  // for its emission, and the sum saturates before any texture applies.
  float4 color = float4(
      saturate(material.diffuse.rgb * light + material.ambient.rgb),
      material.diffuse.a);
  color *= base_texel;
  // A sub-texture multiplies, as a sphere that does not add does in
  // full.fx.
  if (sphere == kSphereMultiply || sphere == kSphereSubTexture) {
    color.rgb *= sphere_texel.rgb;
  } else if (sphere == kSphereAdd) {
    color.rgb += sphere_texel.rgb;
  }
  if (sphere != kSphereNone) {
    color.a *= sphere_texel.a;
  }
  color *= toon_texel;
  // Blinn-Phong under the light's colour, added last. A power of 0 adds
  // the specular colour everywhere, as MMD's does.
  const float3 halfway = ViewDirection(view_position) + light_direction;
  const float3 half_vector =
      dot(halfway, halfway) > 1e-12 ? normalize(halfway) : normal;
  const float power = material.specular.a;
  const float highlight =
      power > 0.0 ? pow(max(dot(half_vector, normal), 0.0), power) : 1.0;
  color.rgb += highlight * material.specular.rgb * light;

  // The material debug views, as MToon's: base colour, normal, direct
  // light, and the ambient colour that stands in for MMD's ambient light.
  const uint debug = uint(frame[0].z);
  if (debug == 1) color.rgb = material.diffuse.rgb * base_texel.rgb;
  if (debug == 2) color.rgb = normal * 0.5 + 0.5;
  if (debug == 3) color.rgb = light;
  if (debug == 4) color.rgb = material.ambient.rgb;

  MmdSurface surface;
  surface.color = SrgbToLinear(saturate(color.rgb));
  surface.alpha = saturate(color.a);
  return surface;
}
