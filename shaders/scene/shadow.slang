// SPDX-License-Identifier: Apache-2.0
// The shadow pipeline (shadow policy §4): the shared caster pass. Every
// draw that casts draws its skinned and morphed points here, seen from a
// shadowing light, before the scene; only their depth is kept. The draw
// constants' clip_from_object is the light view's, so nothing here is the
// camera's. A Mask caster of MToon or PreviewSurface is cut at its cutoff
// by its slot's alpha; as MikuMikuEffect's full.fx draws MMD's map, no
// texel's alpha cuts an MMD caster away.

#include "mtoon/mtoon_common.slang"
#include "preview/preview_common.slang"

// Must match backend_shared::kDrawCutMToon and kDrawCutPreview.
static const uint kDrawCutMToon = 256;
static const uint kDrawCutPreview = 512;

// The points and, as surface_common's VertexInput, USD's st, (0, 0) when
// the mesh has none; no normals.
struct CasterInput {
  float3 position : POSITION;
  float2 st : TEXCOORD0;
  uint vertex : SV_VulkanVertexID;
};

struct CasterOutput {
  float4 position : SV_Position;
  float2 st : TEXCOORD0;
};

[shader("vertex")]
CasterOutput vertex_main(CasterInput input) {
  float3 position = input.position;
  float3 normal = float3(0.0, 0.0, 1.0);
  Skin(draw.flags, input.vertex, position, normal);
  CasterOutput output;
  output.position = mul(draw.clip_from_object, float4(position, 1.0));
  output.st = input.st;
  return output;
}

// Depth alone: the pipeline has no colour attachment. The cut's flags are
// the draw's, so its samples are taken in uniform control flow.
[shader("fragment")]
void fragment_main(float2 st : TEXCOORD0) {
  if ((draw.flags & kDrawCutMToon) != 0) {
    const MToonParameters material = materials[draw.material_slot];
    if (CutAway(material, SurfaceAlpha(material, st))) {
      discard;
    }
  } else if ((draw.flags & kDrawCutPreview) != 0) {
    const PreviewParameters material = preview_materials[draw.material_slot];
    const float opacity = saturate(
        PreviewScalar(material, kPreviewOpacity, st, material.base_color.a));
    if (material.surface.z >= 0.0 && opacity < material.surface.z) {
      discard;
    }
  }
}
