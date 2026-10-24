// SPDX-License-Identifier: Apache-2.0
// WebGPU's depth resolve (WebGPU policy §7): WebGPU resolves colour as a
// pass ends but not depth, so the multisampled scene's depth is resolved by
// one fullscreen triangle that writes each pixel's sample 0, as the Vulkan
// backend's sample-zero resolve does. A backend-specific module: only the
// WebGPU backend compiles it.

[[vk::binding(0, 0)]] Texture2DMS<float> scene_depth;

// One triangle covering the target.
[shader("vertex")]
float4 vertex_main(uint vertex : SV_VertexID) : SV_Position {
  const float2 uv = float2(float((vertex << 1) & 2), float(vertex & 2));
  return float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
}

// A member, not the result: WGSL generation writes a result's SV_Depth as a
// colour location, a member's as the fragment depth.
struct DepthOutput {
  float depth : SV_Depth;
};

[shader("fragment")]
DepthOutput fragment_main(float4 position : SV_Position) {
  DepthOutput output;
  output.depth = scene_depth.Load(int2(position.xy), 0);
  return output;
}
