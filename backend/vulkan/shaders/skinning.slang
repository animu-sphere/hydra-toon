// SPDX-License-Identifier: Apache-2.0
// Morphs then linear blend skinning, shared by every scene vertex pipeline
// (design policy §11): a pose change rewrites only joints, a morph weight
// change only weights. Included, not compiled on its own; the including module's
// DrawConstants carry `flags`.

// Must match vulkan_internal::kDrawSkinned, kDrawConstantInfluences and
// kInfluenceCountShift.
static const uint kDrawSkinned = 2;
static const uint kDrawConstantInfluences = 4;
static const uint kInfluenceCountShift = 8;

// Must match Toon::ToonJointInfluence.
struct JointInfluence {
  uint joint;
  float weight;
};

// The mesh's influences, per point or once when constant.
[[vk::binding(0, 1)]] StructuredBuffer<JointInfluence> influences;
// Entry 0 takes skeleton space to the mesh's own; entry 1 + j is joint j's
// skinning transform composed with the geometry bind transform, so it takes
// a rest point straight to skeleton space.
[[vk::binding(1, 1)]] StructuredBuffer<float4x4> joints;

struct MorphOffset {
  float3 position;
  uint target;
  float3 normal;
  uint padding;
};
[[vk::binding(2, 1)]] StructuredBuffer<MorphOffset> morphOffsets;
[[vk::binding(3, 1)]] StructuredBuffer<uint2> morphRanges;
[[vk::binding(4, 1)]] StructuredBuffer<float> morphWeights;
static const uint kDrawMorphed = 128;

// Moves a rest point and its normal as UsdSkel's classicLinear skinning
// does: the weighted joints in skeleton space, then into the mesh's space.
// A draw without either deformation leaves both as they are.
void Skin(uint flags, uint vertex, inout float3 position,
    inout float3 normal) {
  if ((flags & kDrawMorphed) != 0) {
    uint weightCount, stride;
    morphWeights.GetDimensions(weightCount, stride);
    const uint2 range = morphRanges[vertex];
    for (uint index = 0; index < range.y; ++index) {
      const MorphOffset offset = morphOffsets[range.x + index];
      if (offset.target < weightCount) {
        const float weight = morphWeights[offset.target];
        position += offset.position * weight;
        normal += offset.normal * weight;
      }
    }
  }
  if ((flags & kDrawSkinned) == 0) {
    return;
  }
  const uint count = flags >> kInfluenceCountShift;
  const uint first =
      (flags & kDrawConstantInfluences) != 0 ? 0 : vertex * count;
  float4x4 blended = float4x4(0.0);
  for (uint index = 0; index < count; ++index) {
    const JointInfluence influence = influences[first + index];
    blended += joints[1 + influence.joint] * influence.weight;
  }
  const float3 skeleton_position = mul(blended, float4(position, 1.0)).xyz;
  const float3 skeleton_normal = mul(blended, float4(normal, 0.0)).xyz;
  position = mul(joints[0], float4(skeleton_position, 1.0)).xyz;
  normal = mul(joints[0], float4(skeleton_normal, 0.0)).xyz;
}
