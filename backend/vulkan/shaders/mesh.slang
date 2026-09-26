// SPDX-License-Identifier: Apache-2.0
// Scene meshes, unlit: one colour per draw. slangc compiles one entry point
// per SPIR-V artifact from this module, so the vertex/fragment interface
// lives in one file.

// Must match vulkan_internal::DrawConstants.
struct DrawConstants {
  float4x4 clip_from_object;
  float4 color;
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> draw;

struct VertexInput {
  float3 position : POSITION;
};

struct VertexStageOutput {
  float4 position : SV_Position;
};

[shader("vertex")]
VertexStageOutput vertex_main(VertexInput input) {
  VertexStageOutput output;
  output.position = mul(draw.clip_from_object, float4(input.position, 1.0));
  return output;
}

[shader("fragment")]
float4 fragment_main() : SV_Target {
  return draw.color;
}
