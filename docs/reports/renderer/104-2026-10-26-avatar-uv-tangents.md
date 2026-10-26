# Evaluated primary UV tangent regeneration

Date: 2026-10-26. Renderer base `0666297` plus this change. Windows x64,
MSVC 19.51 Release, OpenUSD 26.08 and experimental runtime ABI-5 headers.
GPU: NVIDIA RTX A5000, driver 597.16, Vulkan. Generated resources only.
Follows [report 103](103-2026-10-26-avatar-resident-uv.md).

## Boundary

The evaluated UV adapter admits complete resident tangent geometry. Coordinates
equal to the immutable baseline retain its authored frames. Changed absolute
UVs regenerate rest frames through the same pinned MikkTSpace wrapper Hydra
uses. The wrapper and unmodified upstream source compile once in render-world;
neither input adapter depends on the other or on OpenUSD through that helper.

New tangent seams expand points, normals, UVs, per-point influences, sparse
morph ranges, edge scales and additional UVs with one vertex map. Constant
influences, joint palettes, sparse offsets, triangle order and material parts
remain intact. Revisions and retained arrays change only with stream values,
including Release back to the authored baseline. Incomplete streams needed
by a seam fail without publishing output or advancing adapter history.

Entirely collapsed UVs and unusable generated frames select the derivative
fallback. Meshes without resident tangents keep that fallback. Existing
baseline splits are retained, rather than welded. Ordinary extraction handles
geometry uploads; the late pose/material path still refuses structural edits.
No runtime ABI, evaluator, importer, dependency pin or public core API changed.

## Evidence

- No-Vulkan build: all 17 CTests pass, including installed AvatarState/core
  consumers and the core boundary check.
- Direct adapter CPU/GPU checks: reordered runtime slots, new mirrored seams,
  reflected UVs, collapsed UVs, same-topology updates, unchanged authored frames,
  immutable retained frames, duplicate array/revision reuse, reset, Release and
  constant/per-point skinning. Incomplete seam streams refuse atomically.
  Independent expected directions/handedness and explicit stream expansion
  cover skin/morph/auxiliary data. Persistent Vulkan rendering at 96 by 96
  matches the independent expanded normal-mapped scene in colour and depth;
  validation-message counts remain zero.
- Rebuilt Hydra skinning/primvar CPU and primvar GPU tests pass. Automatically
  matched, reordered runtime UV slots over an immutable generated baseline
  produce the same points, normals, topology, UVs, tangents and auxiliary data
  as authoring the same values through USD/Hydra. Mirrored seam creation,
  reflection, collapsed fallback and return to base are covered. Normal-mapped
  MToon Vulkan colour/depth agree exactly at 128 by 128, 1x and 4x MSAA.

The renderer documentation check and all seven documentation-checker tests pass.

## Reproduction and limits

From an initialized x64 MSVC developer shell, reuse report 103's configured
Hydra build with avatar state enabled and current runtime headers:

```powershell
cmake --build build/avatar-state-hydra --config Release `
  --target toon-avatar-state-test toon-hydra2-skinning-test toon-hydra2-primvar-test toon-renderer-shaders --parallel 2
ctest --test-dir build/avatar-state-hydra -C Release `
  -R '^(toon-avatar-state(-gpu)?|toon-renderer-hydra-(skinning|primvars|primvars-gpu))$' --output-on-failure
```

Report 102's no-Vulkan configuration also builds and passes its complete
CTest suite. This is generated renderer-state and USD/Hydra parity, without
an MMD owner/evaluator in the new pixel checks. It does not establish composed
MMD-to-GPU playback, additional runtime UV channels, runtime material morph
realization, general authored-asset agreement or representative avatar L3
acceptance. Those remain separate integration work.
