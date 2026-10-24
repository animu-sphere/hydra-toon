// SPDX-License-Identifier: Apache-2.0
// WebGPU's bindings (WebGPU policy §4). Core WebGPU has no push constants
// and no arrays of texture bindings, so the resources are grouped by how
// often they change:
//
//   group 0, frame: the frame buffer, the shadow views' image, their
//     parameters, the gather sampler and MMD's reading of the views;
//   group 1, material: the model's parameter buffer and one texture and
//     sampler per role, a role without a texture bound to a 1 x 1 white;
//   group 2, deformation: a mesh's influences, joints and morphs;
//   group 3, draw: the draw constants, at a dynamic offset in a ring.
//
// Included through bindings.slang, not compiled on its own.

#define TOON_DRAW_BINDING [[vk::binding(0, 3)]]

// Must match the frame group layout in webgpu_scene.cpp.
#define TOON_FRAME_BINDING [[vk::binding(0, 0)]]
#define TOON_SHADOW_VIEWS_BINDING [[vk::binding(2, 0)]]
#define TOON_MMD_SHADOW_BINDING [[vk::binding(4, 0)]]

// Must match the material group layouts in webgpu_scene.cpp: one buffer
// binding per model, so the caster pass can read two models' slots.
#define TOON_MTOON_MATERIALS_BINDING [[vk::binding(0, 1)]]
#define TOON_MMD_MATERIALS_BINDING [[vk::binding(1, 1)]]
#define TOON_PREVIEW_MATERIALS_BINDING [[vk::binding(2, 1)]]

// Must match the deformation group layout in webgpu_scene.cpp.
#define TOON_INFLUENCES_BINDING [[vk::binding(0, 2)]]
#define TOON_JOINTS_BINDING [[vk::binding(1, 2)]]
#define TOON_MORPH_OFFSETS_BINDING [[vk::binding(2, 2)]]
#define TOON_MORPH_RANGES_BINDING [[vk::binding(3, 2)]]
#define TOON_MORPH_WEIGHTS_BINDING [[vk::binding(4, 2)]]

[[vk::binding(1, 0)]] Texture2DArray<float> shadow_map;
// Non-filtering: the map is depth32float, which only such a sampler reads.
[[vk::binding(3, 0)]] SamplerState shadow_sampler;

// Must match webgpu_internal::kTextureRoles: the most any model declares,
// MToon's nine. Role r's texture is at binding 3 + r, its sampler at
// 12 + r.
[[vk::binding(3, 1)]] Texture2D<float4> role_texture0;
[[vk::binding(4, 1)]] Texture2D<float4> role_texture1;
[[vk::binding(5, 1)]] Texture2D<float4> role_texture2;
[[vk::binding(6, 1)]] Texture2D<float4> role_texture3;
[[vk::binding(7, 1)]] Texture2D<float4> role_texture4;
[[vk::binding(8, 1)]] Texture2D<float4> role_texture5;
[[vk::binding(9, 1)]] Texture2D<float4> role_texture6;
[[vk::binding(10, 1)]] Texture2D<float4> role_texture7;
[[vk::binding(11, 1)]] Texture2D<float4> role_texture8;
[[vk::binding(12, 1)]] SamplerState role_sampler0;
[[vk::binding(13, 1)]] SamplerState role_sampler1;
[[vk::binding(14, 1)]] SamplerState role_sampler2;
[[vk::binding(15, 1)]] SamplerState role_sampler3;
[[vk::binding(16, 1)]] SamplerState role_sampler4;
[[vk::binding(17, 1)]] SamplerState role_sampler5;
[[vk::binding(18, 1)]] SamplerState role_sampler6;
[[vk::binding(19, 1)]] SamplerState role_sampler7;
[[vk::binding(20, 1)]] SamplerState role_sampler8;

// A texture's texel at `uv`: role `role`'s texture through its own sampler,
// which the material's bind group chose from the texture's wrap pair. The
// entry is the slot's flag, 0 for a texture the draw does not have, which
// samples white as the Vulkan table's entry 0 does; the sampler index is
// Vulkan's and unused here. Callers pass a constant role and take every
// sample in uniform control flow.
float4 SampleRole(uint role, uint entry, uint sampler, float2 uv) {
  if (entry == 0) {
    return float4(1.0);
  }
  switch (role) {
  case 0: return role_texture0.Sample(role_sampler0, uv);
  case 1: return role_texture1.Sample(role_sampler1, uv);
  case 2: return role_texture2.Sample(role_sampler2, uv);
  case 3: return role_texture3.Sample(role_sampler3, uv);
  case 4: return role_texture4.Sample(role_sampler4, uv);
  case 5: return role_texture5.Sample(role_sampler5, uv);
  case 6: return role_texture6.Sample(role_sampler6, uv);
  case 7: return role_texture7.Sample(role_sampler7, uv);
  default: return role_texture8.Sample(role_sampler8, uv);
  }
}

float4 SampleRoleLevel(uint role, uint entry, uint sampler, float2 uv,
    float level) {
  if (entry == 0) {
    return float4(1.0);
  }
  switch (role) {
  case 0: return role_texture0.SampleLevel(role_sampler0, uv, level);
  case 1: return role_texture1.SampleLevel(role_sampler1, uv, level);
  case 2: return role_texture2.SampleLevel(role_sampler2, uv, level);
  case 3: return role_texture3.SampleLevel(role_sampler3, uv, level);
  case 4: return role_texture4.SampleLevel(role_sampler4, uv, level);
  case 5: return role_texture5.SampleLevel(role_sampler5, uv, level);
  case 6: return role_texture6.SampleLevel(role_sampler6, uv, level);
  case 7: return role_texture7.SampleLevel(role_sampler7, uv, level);
  default: return role_texture8.SampleLevel(role_sampler8, uv, level);
  }
}

// The four shadow-map texels around `uv` on layer `layer`.
float4 GatherShadow(float2 uv, float layer) {
  return shadow_map.Gather(shadow_sampler, float3(uv, layer));
}

// Screen-space derivatives, fine: WGSL's plain ones are coarse on Dawn's
// Direct3D 12 device, and the Vulkan backend's reference driver takes them
// fine, which a multisampled Mask edge and a derivative tangent frame show
// (renderer report 92). Slang has no WGSL spelling of its own for them.
float DerivativeX(float value) {
  __target_switch { case wgsl: __intrinsic_asm "dpdxFine($0)"; default: return ddx(value); }
}
float2 DerivativeX(float2 value) {
  __target_switch { case wgsl: __intrinsic_asm "dpdxFine($0)"; default: return ddx(value); }
}
float3 DerivativeX(float3 value) {
  __target_switch { case wgsl: __intrinsic_asm "dpdxFine($0)"; default: return ddx(value); }
}
float DerivativeY(float value) {
  __target_switch { case wgsl: __intrinsic_asm "dpdyFine($0)"; default: return ddy(value); }
}
float2 DerivativeY(float2 value) {
  __target_switch { case wgsl: __intrinsic_asm "dpdyFine($0)"; default: return ddy(value); }
}
float3 DerivativeY(float3 value) {
  __target_switch { case wgsl: __intrinsic_asm "dpdyFine($0)"; default: return ddy(value); }
}
float DerivativeWidth(float value) {
  __target_switch { case wgsl: __intrinsic_asm "fwidthFine($0)"; default: return fwidth(value); }
}
