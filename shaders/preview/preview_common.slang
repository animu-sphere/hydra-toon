// SPDX-License-Identifier: Apache-2.0
// What preview_surface and the shadow pipeline share (material policy §7):
// the PreviewSurface parameter slot and how its inputs sample. Included
// after surface_common.slang, not compiled on its own.

// Must match vulkan_internal::PreviewParameters.
struct PreviewTextureParameters {
  float4 uv_rows[2];
  float4 scale;
  float4 bias;
  float4 fallback;
  uint4 info;
};
struct PreviewParameters {
  float4 base_color;
  float4 emissive;
  float4 surface;
  float4 normal;
  PreviewTextureParameters inputs[6];
};
TOON_PREVIEW_MATERIALS_BINDING StructuredBuffer<PreviewParameters> preview_materials;

// PreviewSurface's inputs, in the slot's order, which are its texture roles
// (WebGPU policy §4). Must match webgpu_internal::PreviewRole.
static const uint kPreviewBase = 0;
static const uint kPreviewEmissive = 1;
static const uint kPreviewMetallic = 2;
static const uint kPreviewRoughness = 3;
static const uint kPreviewOpacity = 4;
static const uint kPreviewNormal = 5;

float4 PreviewSample(PreviewParameters material, uint role, float2 st) {
  const PreviewTextureParameters input = material.inputs[role];
  float4 texel = input.fallback;
  if (input.info.x != 0) {
    const float2 uv = (draw.flags & kDrawHasUVs) != 0 ? st
        : float2(input.uv_rows[0].w, input.uv_rows[1].w);
    texel = SampleRole(role, input.info.x, input.info.y, TransformUV(input.uv_rows, uv));
  }
  return texel * input.scale + input.bias;
}

float PreviewScalar(PreviewParameters material, uint role, float2 st, float value) {
  const PreviewTextureParameters input = material.inputs[role];
  return (input.info.w & 1) != 0 ? PreviewSample(material, role, st)[min(input.info.z, 3)] : value;
}

float3 PreviewColor(PreviewParameters material, uint role, float2 st, float3 value) {
  return (material.inputs[role].info.w & 1) != 0 ? PreviewSample(material, role, st).rgb : value;
}
