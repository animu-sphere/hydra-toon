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

// MMD's edge width at a vertex whose clip w is `w`, as a length in view
// space, for an edge size times edge scale of `size`. MMD units are 0.08 m
// on the stage (usd-mmd-plugins stage contract §6).
static const float kMetersPerMmdUnit = 0.08;
// MMD's edge is neither a constant length nor a constant number of pixels
// on its own frames: the width is taken as part a length in MMD units and
// part a width in pixels, per unit of size, fitted to two distances
// (material policy MAT-Q4, renderer report 57).
static const float kEdgeMmdUnits = 0.0135;
static const float kEdgePixels = 0.89;

float EdgeWidth(float size, float w) {
  const float units_per_meter = 1.0 / max(frame[0].w, 1e-12);
  const float world = kEdgeMmdUnits * kMetersPerMmdUnit * units_per_meter;
  const float screen = ScreenWidth(kEdgePixels * draw.outline_scale, w);
  return size * (world + screen);
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
  position = HullPosition(position, view_normal, normal_matrix,
      EdgeWidth(material.edge.x * input.edge_scale, w));

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
