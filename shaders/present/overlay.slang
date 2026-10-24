// SPDX-License-Identifier: Apache-2.0
// The present session's 2D overlay: textured, vertex-coloured triangles in
// framebuffer pixels, blended over the presented image (OverlayDrawList).

// Must match vulkan_internal::OverlayConstants and the WebGPU backend's.
struct OverlayConstants {
  // Framebuffer pixels to clip space: clip = pixel * scale + translate.
  float2 scale;
  float2 translate;
  // kOverlayLinearize when the target encodes to sRGB as it is written,
  // and kOverlayDepth when the texture is a shadow view's depth.
  uint flags;
  uint padding0;
  uint padding1;
  uint padding2;
};

static const uint kOverlayLinearize = 1;
static const uint kOverlayDepth = 2;

#if defined(TOON_WEBGPU)
// WebGPU has no push constants or combined samplers: the constants are a
// uniform at a dynamic offset, group 1, and each texture binds with its own
// sampler, group 0, a filtering one or, for a shadow view's depth, not.
[[vk::binding(0, 1)]] ConstantBuffer<OverlayConstants> overlay;
[[vk::binding(0, 0)]] Texture2D<float4> atlas_texture;
[[vk::binding(1, 0)]] SamplerState atlas_sampler;
#define SampleAtlas(uv) atlas_texture.Sample(atlas_sampler, uv)
#else
[[vk::push_constant]] ConstantBuffer<OverlayConstants> overlay;

[[vk::binding(0, 0)]] Sampler2D<float4> atlas;
// A name, so the SPIR-V is what sampling the combined image gives.
#define SampleAtlas(uv) atlas.Sample(uv)
#endif

struct VertexInput {
  float2 position : POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
};

struct VertexStageOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
};

// The sRGB transfer function's inverse.
float3 Linearize(float3 encoded) {
  const float3 low = encoded / 12.92;
  const float3 high = pow((encoded + 0.055) / 1.055, float3(2.4));
  return select(encoded <= 0.04045, low, high);
}

[shader("vertex")]
VertexStageOutput vertex_main(VertexInput input) {
  VertexStageOutput output;
  output.position =
      float4(input.position * overlay.scale + overlay.translate, 0.0, 1.0);
  output.uv = input.uv;
  // The colours are sRGB-encoded; an sRGB target would encode them again
  // as it writes them, so they are decoded here and blend in linear.
  output.color = input.color;
  if ((overlay.flags & kOverlayLinearize) != 0) {
    output.color.rgb = Linearize(input.color.rgb);
  }
  return output;
}

[shader("fragment")]
float4 fragment_main(VertexStageOutput input) : SV_Target {
  const float4 texel = SampleAtlas(input.uv);
  // A depth image's red is its depth; grey, opaque.
  if ((overlay.flags & kOverlayDepth) != 0) {
    return input.color * float4(texel.rrr, 1.0);
  }
  return input.color * texel;
}
