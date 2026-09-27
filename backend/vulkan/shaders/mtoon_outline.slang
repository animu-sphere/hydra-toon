// SPDX-License-Identifier: Apache-2.0
// The mtoon_outline pipeline (material policy §5, design policy §10): the
// inverted hull. Each skinned vertex moves out along its normal by MToon's
// outline width, and the hull draws with its front faces culled, so only
// the rim around the surface shows. The width, its mode and texture, and
// the colour are the material slot's, so an outline edit rewrites a slot
// and never selects a shader.

#include "mtoon_common.slang"

[shader("vertex")]
VertexStageOutput vertex_main(VertexInput input) {
  VertexStageOutput output;
  float3 position = input.position;
  float3 normal = input.normal;
  Skin(draw.flags, input.vertex, position, normal);
  const MToonParameters material = materials[draw.material_slot];
  const float3x3 normal_matrix = NormalMatrix();
  const float3 view_normal = mul(normal_matrix, normal);

  // outlineWidthFactor times the G channel of outlineWidthMultiplyTexture,
  // which is white when the material has none. Mip 0: the vertex stage has
  // no derivatives to pick another.
  const float4 width_texel =
      textures[TextureEntry(material.outline_texture.x)].SampleLevel(
          samplers[material.outline_texture.y],
          TransformUV(material.outline_uv, input.st), 0.0);
  float width = material.outline.x * width_texel.g;
  // worldCoordinates is a length in metres; in the scene's units it is a
  // length in view space, which a rigid view keeps from world space.
  // screenCoordinates is a ratio of the screen height, which spans 2 in
  // clip space: that ratio of it at this vertex's w is
  // 2 * width * w / |projection[1][1]| in view space, perspective or not.
  if (material.outline_texture.z == kOutlineScreen) {
    const float w = mul(draw.clip_from_object, float4(position, 1.0)).w;
    width *= 2.0 * w / max(draw.projection_scale, 1e-6);
  } else {
    width *= draw.units_per_meter;
  }
  // The object-space move that is `width` along the view-space normal: the
  // inverse of the model-view 3x3 is the normal matrix transposed, so it
  // holds under any scale the mesh's transform or skin has.
  const float view_length = length(view_normal);
  if (view_length > 0.0) {
    const float3 direction = view_normal / view_length;
    position += mul(direction, normal_matrix) * width;
  }

  output.position = mul(draw.clip_from_object, float4(position, 1.0));
  output.view_normal = view_normal;
  output.st = input.st;
  output.view_position = ViewPosition(position);
  return output;
}

// outlineColorFactor, multiplied by the surface's shading as far as
// outlineLightingMixFactor asks. The hull's visible faces are its back
// faces, lit from their own side. A transparent material's hull takes its
// surface's alpha, which the pipeline blends by; any other's returns 1.
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
  const float3 color = material.outline_color.rgb *
                       lerp(float3(1.0), surface.color,
                           material.outline_color.a);
  const float alpha =
      (draw.flags & kDrawBlend) != 0 ? saturate(surface.alpha) : 1.0;
  return float4(color, alpha);
}
