// SPDX-License-Identifier: Apache-2.0
// The mtoon_transparent pipeline (material policy §6, §7): MToon's Blend
// alpha mode. The surface shades as mtoon_opaque's does and returns its
// alpha, base alpha times the base texture's, which the pipeline blends
// over what is behind it. Whether it writes depth, and when it draws, are
// the draw's, from the material's transparentWithZWrite and render queue.

#include "mtoon_common.slang"

[shader("vertex")]
VertexStageOutput vertex_main(VertexInput input) {
  return SurfaceVertex(input);
}

[shader("fragment")]
float4 fragment_main(float3 view_normal : NORMAL, float2 st : TEXCOORD0,
    float3 view_position : TEXCOORD1, bool front : SV_IsFrontFace)
    : SV_Target {
  const MToonParameters material = materials[draw.material_slot];
  const MToonSurface surface =
      ShadeSurface(material, view_normal, view_position, st, front);
  return float4(surface.color, saturate(surface.alpha));
}
