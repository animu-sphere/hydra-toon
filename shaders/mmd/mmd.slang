// SPDX-License-Identifier: Apache-2.0
// The mmd_opaque pipeline (material policy §7): MMD's shading for a
// material that blends nothing, its diffuse alpha 1 and its base and
// sphere textures opaque. A material is a slot in the MMD parameter buffer
// plus texture table entries, so a new or changed material never compiles
// or selects a shader.

#include "mmd/mmd_common.slang"

[shader("vertex")]
MmdVertexOutput vertex_main(MmdVertexInput input) {
  return MmdSurfaceVertex(input);
}

[shader("fragment")]
float4 fragment_main(float3 view_normal : NORMAL, float2 st : TEXCOORD1,
    float3 view_position : TEXCOORD2, float2 sub_uv : TEXCOORD3)
    : SV_Target {
  const MmdSurface surface = ShadeMmd(mmd_materials[draw.material_slot],
      view_normal, view_position, st, sub_uv);
  return float4(surface.color, 1.0);
}
