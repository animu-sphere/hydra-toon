// SPDX-License-Identifier: Apache-2.0
// mtoon_opaque as the WebGPU backend draws it multisampled (WebGPU policy
// §7). WebGPU has alpha to coverage but not alpha to one, which the Vulkan
// pipeline pairs with it so a Mask sample's alpha is not counted twice: so
// this fragment turns MaskCoverage's share into samples itself, through the
// sample mask, and every sample it keeps is opaque. One sample of four kept
// over a transparent clear resolves to alpha 0.25, as on Vulkan. Opaque
// keeps every sample. A backend-specific module: only the WebGPU backend
// compiles it.

#include "mtoon/mtoon_common.slang"

// The only multisampled count core WebGPU renders.
static const uint kCoverageSamples = 4;

[shader("vertex")]
TangentVertexStageOutput vertex_main(TangentVertexInput input) {
  return TangentSurfaceVertex(input);
}

struct CoverageOutput {
  float4 color : SV_Target;
  uint mask : SV_Coverage;
};

// How many samples a share keeps, and which, is the implementation's under
// alpha to coverage. This keeps those the Vulkan backend's reference driver
// keeps, measured sample by sample and pixel by pixel (renderer report 92):
// the share in 255ths, less an ordered dither of 0, 16, 32 or 48 by the
// pixel's place in a 2 x 2 block, keeps one sample from 7.5 and one more
// each 64 after; the kept samples nest in the order 3, 0, 1, 2 of the
// standard 4x positions, so one kept sample is not sample 0, which the depth
// product resolves.
static const uint kCoverageMasks[kCoverageSamples + 1] = {0x0, 0x8, 0x9, 0xB, 0xF};
static const float kCoverageDither[4] = {0.0, 32.0, 48.0, 16.0};

[shader("fragment")]
CoverageOutput fragment_main(float4 position : SV_Position, float3 view_normal : NORMAL,
    float2 st : TEXCOORD1, float3 view_position : TEXCOORD2, float4 view_tangent : TEXCOORD3,
    bool front : SV_IsFrontFace) {
  const MToonParameters material = materials[draw.material_slot];
  const MToonSurface surface =
      ShadeSurface(material, view_normal, view_tangent, view_position, st, front);
  const float coverage = MaskCoverage(material, surface.alpha);
  const uint2 pixel = uint2(position.xy) & 1u;
  const float steps = coverage * 255.0 - 7.5 - kCoverageDither[pixel.x + 2u * pixel.y];
  const uint kept = steps < 0.0 ? 0u : min(uint(steps / 64.0) + 1u, kCoverageSamples);
  if (kept == 0) {
    discard;
  }
  CoverageOutput output;
  output.color = float4(surface.color, 1.0);
  output.mask = kCoverageMasks[kept];
  return output;
}
