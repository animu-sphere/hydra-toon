// SPDX-License-Identifier: Apache-2.0
// The mmd_transparent pipeline (material policy §6, §7): MMD's shading for
// a material that can blend, its diffuse alpha below 1 or its base or
// sphere texture not opaque. The surface shades as mmd_opaque's does and
// returns its alpha, which the pipeline blends over what is behind it.
// When it draws is the draw's: after every opaque draw, in MMD's material
// order. It writes depth, as MMD does, except where its alpha is 0.

#include "mmd_common.slang"

[shader("vertex")]
VertexStageOutput vertex_main(VertexInput input) {
  return SurfaceVertex(input);
}

[shader("fragment")]
float4 fragment_main(float3 view_normal : NORMAL, float2 st : TEXCOORD0,
    float3 view_position : TEXCOORD1)
    : SV_Target {
  const MmdSurface surface = ShadeMmd(mmd_materials[draw.material_slot],
      view_normal, view_position, st);
  if (surface.alpha <= 0.0) {
    discard;
  }
  return float4(surface.color, surface.alpha);
}
