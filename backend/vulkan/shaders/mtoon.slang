// SPDX-License-Identifier: Apache-2.0
// The mtoon_opaque pipeline (material policy §7): MToon's lit / shade split
// with shading shift and toony, GI equalization, emission and the rim, with
// the base and shade colour, MatCap and rim multiply textures, for the
// Opaque and Mask alpha modes. A material is a slot in the parameter buffer
// plus texture table entries, so a new or changed material never compiles
// or selects a shader.

#include "mtoon_common.slang"

[shader("vertex")]
VertexStageOutput vertex_main(VertexInput input) {
  return SurfaceVertex(input);
}

// A back face is drawn only when the material is double-sided. Opaque and
// Mask draw no alpha: Mask cuts a fragment away below its cutoff and draws
// the rest whole.
[shader("fragment")]
float4 fragment_main(float3 view_normal : NORMAL, float2 st : TEXCOORD0,
    float3 view_position : TEXCOORD1, bool front : SV_IsFrontFace)
    : SV_Target {
  const MToonParameters material = materials[draw.material_slot];
  const MToonSurface surface =
      ShadeSurface(material, view_normal, view_position, st, front);
  if (CutAway(material, surface.alpha)) {
    discard;
  }
  return float4(surface.color, 1.0);
}
