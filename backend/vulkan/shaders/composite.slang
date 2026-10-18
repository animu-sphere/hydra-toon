// SPDX-License-Identifier: Apache-2.0
// Scene-linear premultiplied foreground -> linear source-over -> display.
struct CompositeConstants {
  float4 color; // w: checker size in framebuffer pixels
  float4 alternate; // w: checkerboard enabled
};
[[vk::push_constant]] ConstantBuffer<CompositeConstants> background;
[[vk::binding(0, 0)]] Sampler2D<float4> foreground;

[shader("vertex")]
float4 vertex_main(uint vertex : SV_VulkanVertexID) : SV_Position {
  return float4(vertex == 1 ? 3.0 : -1.0, vertex == 2 ? 3.0 : -1.0, 0, 1);
}

[shader("fragment")]
float4 fragment_main(float4 position : SV_Position) : SV_Target {
  const int2 pixel = int2(position.xy);
  const float4 source = foreground.Load(int3(pixel, 0));
  const uint2 cell = uint2(position.xy / background.color.w);
  const bool alternate = background.alternate.w != 0 && ((cell.x + cell.y) & 1) != 0;
  const float3 rgb = alternate ? background.alternate.rgb : background.color.rgb;
  // The sRGB swapchain attachment encodes this once, after composition.
  return float4(source.rgb + rgb * (1.0 - source.a), 1);
}
