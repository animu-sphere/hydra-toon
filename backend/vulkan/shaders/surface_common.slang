// SPDX-License-Identifier: Apache-2.0
// What every surface pipeline shares, MToon's, MMD's and PreviewSurface's (material policy
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
  // Read by a screen-space outline width alone. Scalars, not a uint2, which
  // could align past 128 bytes.
  float projection_scale;
  // What mtoon_outline's world-coordinates width is multiplied by: the
  // scene's units per metre, 1 / its metersPerUnit.
  float outline_scale;
  // kDrawDisplayColor uses asfloat(material_slot), projection_scale and
  // outline_scale as RGB, without a material buffer lookup or an outline.
};

static const uint kDrawHasUVs = 1;
static const uint kDrawDisplayColor = 8;
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
// colour/type, position/radius (metres), direction/outer cosine, inner
// cosine and the shared shadow view it casts into, plus one, or 0.
[[vk::binding(3, 0)]] StructuredBuffer<float4> frame;
// The sampler that clamps both axes: ToonWrap::ClampToEdge is 1, and a
// sampler's index is wrap_s * 3 + wrap_t.
static const uint kClampSampler = 4;

// The shared shadow views' depth, a layer each (shadow policy §5), and
// where each was drawn from this frame, for the receivers of every family
// but MMD, whose own reading of the first direct light's view is
// mmd_common's.
[[vk::binding(5, 0)]] Texture2DArray<float> shadow_map;
// Must match vulkan_internal::ShadowViewParameters.
struct ShadowViewParameters {
  // The view's clip space from view space: x and y in [-1, 1] across it,
  // z its depth in [0, 1] from the light, w 1 or, when the view is
  // perspective, the distance along the light's axis.
  float4x4 clip_from_view;
  // x its size in texels, y its layer, z 1 when it is perspective; w
  // unused.
  float4 extent;
  // In view-space units: x its depth range, y its near plane's distance
  // when perspective, z a texel's width, at a distance of 1 when
  // perspective; w unused.
  float4 depth;
  // The comparison, in the view's texels: x the depth bias, y the normal
  // bias, z the filter radius; w unused.
  float4 receiver;
};
[[vk::binding(8, 0)]] StructuredBuffer<ShadowViewParameters> shadow_views;

// The steepest slope a receiver's comparison allows for, and the bilinear
// taps its filter reads (shadow policy §3).
static const float kReceiverSlopeLimit = 4.0;
static const int kReceiverFilterTaps = 16;

// Must match MaterialDebug::Shadow.
static const uint kDebugShadow = 5;

// The shared shadow view direct light `index` casts into, plus one, or 0.
uint ShadowSlot(uint index) {
  return uint(frame[2 + index * 4 + 3].y + 0.5);
}

// How a view-space change from a point moves it across a view, in texels,
// and in depth, scaled by `depth_scale`: a perspective view divides by w.
float3 ShadowAcross(float4 clip, float4 change, bool perspective, float size,
    float depth_scale) {
  const float2 across = (change.xy * clip.w - clip.xy * change.w) /
                        (clip.w * clip.w) * (0.5 * size);
  return float3(across, (perspective ? change.w : change.z) * depth_scale);
}

// How much of direct light `index` reaches a view-space point past its
// shared shadow view: 1 when the light has none, or outside the view.
// `lambert` is N·L, toward the light; `position_dx` and `position_dy` are
// the point's screen derivatives, taken by the caller in uniform control
// flow, from which the point's plane across the view follows. Gather takes
// no derivatives of its own.
//
// Every length is in texels of the view where the point is, a perspective
// view's growing with the distance: the point moves along its normal by
// up to the normal bias as it turns from the light, must lie the depth
// bias behind what the view saw, more as it slopes, and reads 16 bilinear
// taps over a disc of the filter radius, each compared with the depth the
// point's own plane has there, so that the texels around it, which a
// sloping surface or the far side of a ridge show nearer the light, do
// not shadow it.
float ShadowVisibility(uint index, float3 view_position, float3 position_dx,
    float3 position_dy, float3 normal, float lambert) {
  const uint slot = ShadowSlot(index);
  if (slot == 0) {
    return 1.0;
  }
  const ShadowViewParameters view = shadow_views[slot - 1];
  const bool perspective = view.extent.z != 0.0;
  const float size = view.extent.x;
  const float facing = saturate(lambert);
  const float sine = sqrt(saturate(1.0 - facing * facing));
  const float distance =
      mul(view.clip_from_view, float4(view_position, 1.0)).w;
  const float texel =
      view.depth.z * (perspective ? max(distance, view.depth.y) : 1.0);
  const float4 clip = mul(view.clip_from_view,
      float4(view_position + normal * texel * view.receiver.y * sine, 1.0));
  if (clip.w <= 0.0) {
    return 1.0;
  }
  const float3 map = clip.xyz / clip.w;
  const float2 uv = map.xy * 0.5 + 0.5;
  if (any(uv < 0.0) || any(uv > 1.0) || map.z > 1.0 || map.z < 0.0) {
    return 1.0;
  }
  // Depth in texels, so the comparison is the view's own: an orthographic
  // view's z spans its depth range, a perspective view's w is the
  // distance.
  const float depth_scale = (perspective ? 1.0 : view.depth.x) / max(texel, 1e-12);
  const float depth = (perspective ? clip.w : map.z) * depth_scale;
  const float near = view.depth.y;
  const float far = near + view.depth.x;
  const float slope = min(sine / max(facing, 0.05), kReceiverSlopeLimit);
  const float allowance = view.receiver.x * (1.0 + slope);
  const float2 at = uv * size - 0.5;
  // The point's plane across the view, in texels of depth per texel.
  const float3 across_x = ShadowAcross(clip,
      mul(view.clip_from_view, float4(position_dx, 0.0)), perspective, size,
      depth_scale);
  const float3 across_y = ShadowAcross(clip,
      mul(view.clip_from_view, float4(position_dy, 0.0)), perspective, size,
      depth_scale);
  const float determinant = across_x.x * across_y.y - across_y.x * across_x.y;
  float2 plane = float2(0.0);
  if (abs(determinant) > 1e-8) {
    plane = float2(across_x.z * across_y.y - across_y.z * across_x.y,
                across_y.z * across_x.x - across_x.z * across_y.x) /
            determinant;
    plane *= min(1.0, kReceiverSlopeLimit / max(length(plane), 1e-12));
  }
  const float radius = view.receiver.z;
  float light = 0.0;
  [unroll] for (int i = 0; i < kReceiverFilterTaps; ++i) {
    // Vogel's spiral: taps spread evenly over the disc.
    const float angle = float(i) * 2.39996323;
    const float2 apart = radius *
        sqrt((float(i) + 0.5) / float(kReceiverFilterTaps)) *
        float2(cos(angle), sin(angle));
    const float2 tap = at + apart;
    const float2 base = floor(tap);
    const float2 f = tap - base;
    // The four texels around the tap: w its own, z the next across, x the
    // next down, y both.
    const float4 seen = shadow_map.Gather(samplers[kClampSampler],
        float3((base + 1.0) / size, view.extent.y));
    // A perspective view's depth back to the distance it was drawn at.
    const float4 seen_depth = perspective
        ? near * far / (far - seen * view.depth.x) * depth_scale
        : seen * depth_scale;
    const float4 lit = step(depth + dot(plane, apart) - allowance, seen_depth);
    light += lerp(lerp(lit.w, lit.z, f.x), lerp(lit.x, lit.y, f.x), f.y);
  }
  return light / float(kReceiverFilterTaps);
}

struct VertexInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  // USD's st: origin at the image's bottom left. (0, 0) when the mesh has
  // none, bound with stride 0.
  float2 st : TEXCOORD0;
  uint vertex : SV_VulkanVertexID;
};

// The vertex stage's move of a skinned point out along its normal by
// `width` in view space, for an inverted hull: the object-space move that is
// `width` along the view-space normal. The inverse of the model-view 3x3 is
// the normal matrix transposed, so it holds under any scale the mesh's
// transform or skin has.
float3 HullPosition(float3 position, float3 view_normal,
    float3x3 normal_matrix, float width) {
  const float view_length = length(view_normal);
  if (view_length <= 0.0) {
    return position;
  }
  return position + mul(view_normal / view_length, normal_matrix) * width;
}

// A width given as a share of the screen's height, which spans 2 in clip
// space, as a length in view space at a point's clip w: 2 * share * w /
// |projection[1][1]|, perspective or not.
float ScreenWidth(float share, float w) {
  return 2.0 * share * w / max(draw.projection_scale, 1e-6);
}

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

// Shared authored-frame path. Directions use the forward transform while
// normals use its inverse transpose. Keep the sign through reflections;
// re-orthogonalization belongs after interpolation in the fragment stage.
float4 ViewTangent(float4 tangent) {
  const float3x3 linear = float3x3(draw.view_from_object_rows[0].xyz,
      draw.view_from_object_rows[1].xyz, draw.view_from_object_rows[2].xyz);
  return float4(mul(linear, tangent.xyz),
      tangent.w * (determinant(linear) < 0.0 ? -1.0 : 1.0));
}

struct TangentVertexInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float2 st : TEXCOORD0;
  float4 tangent : TANGENT;
  uint vertex : SV_VulkanVertexID;
};

struct TangentVertexStageOutput {
  float4 position : SV_Position;
  float3 view_normal : NORMAL;
  float2 st : TEXCOORD0;
  float3 view_position : TEXCOORD1;
  float4 view_tangent : TEXCOORD2;
};

TangentVertexStageOutput TangentSurfaceVertex(TangentVertexInput input) {
  TangentVertexStageOutput output;
  float3 position = input.position;
  float3 normal = input.normal;
  float4 tangent = input.tangent;
  Skin(draw.flags, input.vertex, position, normal, tangent);
  output.position = mul(draw.clip_from_object, float4(position, 1.0));
  output.view_normal = ViewNormal(normal);
  output.view_tangent = ViewTangent(tangent);
  output.st = input.st;
  output.view_position = ViewPosition(position);
  return output;
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

// Prefer the authored rest frame, deformed with the mesh. Otherwise retain
// the derivative frame from original UVs and deformed positions until the
// MikkTSpace generator is added. Sampling transforms never move the frame.
// An unusable frame retains the vertex normal. An authored frame can also
// map a constant/fallback vector without mesh UVs, as PreviewSurface allows.
float3 TangentMappedNormal(float3 normal, float4 authored_tangent, float3 position, float2 st,
    float3 map, float v_sign, bool enabled) {
  const float3 dx = ddx(position);
  const float3 dy = ddy(position);
  const float2 ux = ddx(float2(st.x, st.y * v_sign));
  const float2 uy = ddy(float2(st.x, st.y * v_sign));
  const float determinant = ux.x * uy.y - ux.y * uy.x;
  if (!enabled) {
    return normal;
  }
  const bool authored = abs(authored_tangent.w) > 0.5;
  if (!authored && abs(determinant) < 1e-10) return normal;
  float3 tangent = authored ? authored_tangent.xyz : (dx * uy.y - dy * ux.y) / determinant;
  tangent -= normal * dot(normal, tangent);
  // Authored w describes bottom-left st. Each model selects its +Y axis.
  const float3 bitangent = authored ?
      v_sign * authored_tangent.w * cross(normal, tangent) : (dy * ux.x - dx * uy.x) / determinant;
  if (dot(tangent, tangent) < 1e-12 || dot(bitangent, bitangent) < 1e-12) {
    return normal;
  }
  tangent = normalize(tangent);
  const float handedness = dot(cross(normal, tangent), bitangent) < 0.0 ? -1.0 : 1.0;
  const float3 mapped = tangent * map.x + cross(normal, tangent) * handedness * map.y + normal * map.z;
  return dot(mapped, mapped) > 1e-12 ? normalize(mapped) : normal;
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
