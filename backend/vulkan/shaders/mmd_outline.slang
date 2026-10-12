// SPDX-License-Identifier: Apache-2.0
// The mmd_outline pipeline (material policy §5, design policy §10): MMD's
// edge as an inverted hull. Each skinned vertex moves out along its normal
// by the material's edge size times the vertex's edge scale, and the hull
// draws with its front faces culled, so only the rim around the surface
// shows, in the edge colour. The size and colour are the material slot's,
// so an edge edit rewrites a slot and never selects a shader.

#include "mmd_common.slang"

// VertexInput's streams at locations 0-2, in its order, and a fourth.
struct MmdOutlineInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float2 st : TEXCOORD0;
  // primvars:mmd:edgeScale, at location 3; 1 when the mesh has none, bound
  // with stride 0.
  float edge_scale : TEXCOORD1;
  uint vertex : SV_VulkanVertexID;
};

// MMD's edge width at a vertex whose clip w is `w` and whose view-space
// depth is `depth`, as a length in view space, for an edge size times edge
// scale of `size`. On MMD's own frames the edge is a share of the target's
// height: a narrower field of view or a taller target leaves that share as
// it is, and it falls with the vertex's depth. Per unit of size it is
// kEdgeShare plus kEdgeShareMmdUnits over the depth in MMD units, fitted so
// that hdToon's edge is as wide as MMD's on its frames from five cameras
// (material policy MAT-Q4, renderer report 58). MMD units are
// 0.08 m on the stage (usd-mmd-plugins stage contract §6). The depth is
// taken as at least one MMD unit, which bounds the width at the camera and
// gives a vertex at an orthographic camera's depth one.
static const float kMetersPerMmdUnit = 0.08;
static const float kEdgeShare = 0.00108;
static const float kEdgeShareMmdUnits = 0.0203;

float EdgeWidth(float size, float w, float depth) {
  const float mmd_depth = max(depth * frame[0].w / kMetersPerMmdUnit, 1.0);
  return ScreenWidth(
      size * (kEdgeShare + kEdgeShareMmdUnits / mmd_depth), w);
}

[shader("vertex")]
VertexStageOutput vertex_main(MmdOutlineInput input) {
  VertexStageOutput output;
  float3 position = input.position;
  float3 normal = input.normal;
  Skin(draw.flags, input.vertex, position, normal);
  const MmdParameters material = mmd_materials[draw.material_slot];
  const float3x3 normal_matrix = NormalMatrix();
  const float3 view_normal = mul(normal_matrix, normal);
  const float w = mul(draw.clip_from_object, float4(position, 1.0)).w;
  const float depth = -ViewPosition(position).z;
  position = HullPosition(position, view_normal, normal_matrix,
      EdgeWidth(material.edge.x * input.edge_scale, w, depth));

  output.position = mul(draw.clip_from_object, float4(position, 1.0));
  output.view_normal = view_normal;
  output.st = input.st;
  output.view_position = ViewPosition(position);
  return output;
}

// The edge colour, unlit, as MMD draws it whatever its light: stored, so
// decoded to linear once (material policy MAT-Q3), with its alpha, which
// the pipeline blends by.
[shader("fragment")]
float4 fragment_main() : SV_Target {
  const MmdParameters material = mmd_materials[draw.material_slot];
  return float4(SrgbToLinear(saturate(material.edge_color.rgb)),
      saturate(material.edge_color.a));
}
