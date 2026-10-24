// SPDX-License-Identifier: Apache-2.0
// Vulkan's bindings: the draw constants are push constants; set 0 is the
// scene's one material set, with every model's parameter buffer, the frame,
// the shared shadow views and the 128-entry texture table; set 1 is a mesh's
// deformation. Included through bindings.slang, not compiled on its own.

#define TOON_DRAW_BINDING [[vk::push_constant]]

// Must match the material set layout in vulkan_scene.cpp.
#define TOON_MTOON_MATERIALS_BINDING [[vk::binding(0, 0)]]
#define TOON_FRAME_BINDING [[vk::binding(3, 0)]]
#define TOON_MMD_MATERIALS_BINDING [[vk::binding(4, 0)]]
#define TOON_MMD_SHADOW_BINDING [[vk::binding(6, 0)]]
#define TOON_PREVIEW_MATERIALS_BINDING [[vk::binding(7, 0)]]
#define TOON_SHADOW_VIEWS_BINDING [[vk::binding(8, 0)]]

// Must match the skin set layout in vulkan_scene.cpp.
#define TOON_INFLUENCES_BINDING [[vk::binding(0, 1)]]
#define TOON_JOINTS_BINDING [[vk::binding(1, 1)]]
#define TOON_MORPH_OFFSETS_BINDING [[vk::binding(2, 1)]]
#define TOON_MORPH_RANGES_BINDING [[vk::binding(3, 1)]]
#define TOON_MORPH_WEIGHTS_BINDING [[vk::binding(4, 1)]]

// Must match vulkan_internal::kTextureCapacity and backend_shared::kSamplerCount.
static const uint kTextureCapacity = 128;
static const uint kSamplerCount = 9;

// Indexed by a value from the material slot, so uniform across a draw.
[[vk::binding(1, 0)]] Texture2D<float4> textures[kTextureCapacity];
[[vk::binding(2, 0)]] SamplerState samplers[kSamplerCount];

// The shared shadow views' depth, a layer each (shadow policy §5).
[[vk::binding(5, 0)]] Texture2DArray<float> shadow_map;

// A texture's texel at `uv`: table `entry` through wrap sampler `sampler`.
// The role names the texture on a backend that binds them per material;
// the table needs only the entry, 0 the white placeholder.
float4 SampleRole(uint role, uint entry, uint sampler, float2 uv) {
  return textures[entry].Sample(samplers[sampler], uv);
}

float4 SampleRoleLevel(uint role, uint entry, uint sampler, float2 uv,
    float level) {
  return textures[entry].SampleLevel(samplers[sampler], uv, level);
}

// The four shadow-map texels around `uv` on layer `layer`; gathering filters
// nothing, so the sampler is only the clamp to the layer's edge (entry 4,
// both axes ToonWrap::ClampToEdge).
float4 GatherShadow(float2 uv, float layer) {
  return shadow_map.Gather(samplers[4], float3(uv, layer));
}

// Screen-space derivatives, as the driver takes them (WebGPU policy §5);
// names, so the SPIR-V is what plain ddx, ddy and fwidth give.
#define DerivativeX ddx
#define DerivativeY ddy
#define DerivativeWidth fwidth
