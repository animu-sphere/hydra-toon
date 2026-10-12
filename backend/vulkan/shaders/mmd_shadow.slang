// SPDX-License-Identifier: Apache-2.0
// The mmd_shadow pipeline (material policy §10): MMD's self-shadow map.
// Every MMD draw whose material casts the self-shadow draws its skinned
// points here, seen from the light, before the scene; only their depth is
// kept. The draw constants' clip_from_object is the light's, so nothing
// here is the camera's. As MikuMikuEffect's full.fx draws its shadow map,
// no texel's alpha cuts a caster away.

#include "surface_common.slang"

struct ShadowInput {
  float3 position : POSITION;
  uint vertex : SV_VulkanVertexID;
};

[shader("vertex")]
float4 vertex_main(ShadowInput input) : SV_Position {
  float3 position = input.position;
  float3 normal = float3(0.0, 0.0, 1.0);
  Skin(draw.flags, input.vertex, position, normal);
  return mul(draw.clip_from_object, float4(position, 1.0));
}

// Depth alone: the pipeline has no colour attachment.
[shader("fragment")]
void fragment_main() {
}
