// SPDX-License-Identifier: Apache-2.0
// WebGPU's mipmaps (WebGPU policy §6): WebGPU has no blit, so each level of
// a texture is drawn from the one above it by one fullscreen triangle,
// filtered linearly, as the Vulkan backend's linear blits filter. An sRGB
// texture's levels are drawn through sRGB views, so the filter averages
// linear values, as a blit between sRGB images does. A backend-specific
// module: only the WebGPU backend compiles it.

[[vk::binding(0, 0)]] Texture2D<float4> source;
[[vk::binding(1, 0)]] SamplerState source_sampler;

struct VertexStageOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

// One triangle covering the target: uv (0, 0), (2, 0) and (0, 2), the
// image's top left at uv (0, 0).
[shader("vertex")]
VertexStageOutput vertex_main(uint vertex : SV_VertexID) {
  VertexStageOutput output;
  const float2 uv = float2(float((vertex << 1) & 2), float(vertex & 2));
  output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
  output.uv = uv;
  return output;
}

[shader("fragment")]
float4 fragment_main(float2 uv : TEXCOORD0) : SV_Target {
  return source.SampleLevel(source_sampler, uv, 0.0);
}
