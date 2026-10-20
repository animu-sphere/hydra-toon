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
[[vk::binding(7, 0)]] StructuredBuffer<PreviewParameters> preview_materials;

float4 PreviewSample(PreviewTextureParameters input, float2 st) {
  float4 texel = input.fallback;
  if (input.info.x != 0) {
    const float2 uv = (draw.flags & kDrawHasUVs) != 0 ? st
        : float2(input.uv_rows[0].w, input.uv_rows[1].w);
    texel = textures[input.info.x].Sample(samplers[input.info.y], TransformUV(input.uv_rows, uv));
  }
  return texel * input.scale + input.bias;
}

float PreviewScalar(PreviewTextureParameters input, float2 st, float value) {
  return (input.info.w & 1) != 0 ? PreviewSample(input, st)[min(input.info.z, 3)] : value;
}

float3 PreviewColor(PreviewTextureParameters input, float2 st, float3 value) {
  return (input.info.w & 1) != 0 ? PreviewSample(input, st).rgb : value;
}
