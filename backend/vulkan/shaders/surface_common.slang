// SPDX-License-Identifier: Apache-2.0
// What every surface pipeline shares, MToon's and MMD's (material policy
// §7): the draw constants, the texture table and samplers, scene lighting,
// and the surface's vertex stage. Each model's own module declares its
// parameter buffer. Included, not compiled on its own.

#include "skinning.slang"

// Must match vulkan_internal::SurfaceDrawConstants.
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

[[vk::push_constant]] ConstantBuffer<DrawConstants> draw;
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

// The surface's vertex stage, every surface pipeline's but an outline's:
// the point and normal skinned, then seen through the camera.
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

// Direct light `index` as a surface at `view_position` receives it: its
// colour times its attenuation, returned, and the unit direction toward
// it. A point or spot light falls off with the square of its distance in
// metres, no faster than at its radius, and a spot light by its cone.
float3 DirectLight(uint index, float3 view_position, out float3 direction) {
  const uint row = 2 + index * 4;
  const float4 source = frame[row];
  const float4 point = frame[row + 1];
  const float4 axis = frame[row + 2];
  direction = -normalize(axis.xyz);
  float attenuation = 1.0;
  if (source.w != 0.0) {
    const float3 delta = point.xyz - view_position;
    const float distance_squared = dot(delta, delta);
    direction = distance_squared > 1e-12 ? delta * rsqrt(distance_squared) : -normalize(axis.xyz);
    attenuation = 1.0 / max(distance_squared * frame[0].w * frame[0].w, point.w * point.w);
    if (source.w == 2.0) {
      const float cosine = dot(-direction, normalize(axis.xyz));
      const float inner = frame[row + 3].x;
      attenuation *= inner > axis.w + 1e-6
          ? smoothstep(axis.w, inner, cosine) : (cosine >= axis.w ? 1.0 : 0.0);
    }
  }
  return source.rgb * attenuation;
}
