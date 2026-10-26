# Generated MMD resident UV composition

Date: 2026-10-26. Renderer base `ccae76b` plus this change; MMD checkout
`8c1e1eb`, runtime checkout `a76aaf4` and their local installed packages.
Windows x64, MSVC 19.51 Release, OpenUSD 26.08, runtime ABI 5 and Vulkan.
Generated resources only. Follows [report 104](104-2026-10-26-avatar-uv-tangents.md).

## Boundary

`TOON_MMD_UV_PROVIDER_TEST` adds a test-only executable linked to installed
`AvatarRuntime` component `mmd`. Its generated canonical model and bound motion
run the installed MMD owner evaluator through runtime pre/post callbacks and
real retained snapshots. An empty physics world supplies the runtime barrier;
there are no bones or simulated bodies. No owner values are manufactured in
the renderer's snapshot interface.

The Hydra delegate supplies the resident baseline and source-point mapping.
Automatic matching binds reordered runtime slots; the existing state adapter
regenerates tangents and new seams. The check compares the owner/runtime UVs
against independently specified arrays, then compares all resident geometry,
UV, tangent and auxiliary streams against those arrays authored through
USD/Hydra. USD authoring is an oracle in the test, not a renderer requirement.

## Evidence

- CPU: six frames over three reset generations, nested group resolution,
  weights `0 -> 1 -> 1.5 -> -0.5 -> 0`, a separate collapsed-UV morph and
  return to immutable bases. Repeated application reuses UV/tangent arrays
  and revisions. Retained snapshots and renderer arrays survive producer
  destruction; Release restores the baseline UVs and tangents.
- GPU: the same owner-produced snapshots drive a generated four-colour
  texture with MMD and normal-mapped MToon materials. At 128 by 128, 1x and
  4x MSAA, colour and depth agree exactly with the USD/Hydra oracle on every
  frame. Geometry must cover pixels, morphs must change sampled pixels in
  both material families, and zero/reset frames restore the original pixels.
  Steady rendering reuses geometry and pipelines with no validation messages.
- All four focused CTests pass: `toon-renderer-mmd-uv-cpu`,
  `toon-renderer-mmd-uv-gpu`, `toon-renderer-hydra-primvars` and
  `toon-renderer-hydra-primvars-gpu`.

This is generated UV-only owner/runtime-to-GPU composition, not full PMX/VMD
playback. Actual importer intake, bone/vertex deformation with physics,
application resident-host lifecycle, additional runtime UV channels, material
morph realization and representative avatar L3 acceptance remain open. The
renderer and runtime production APIs, algorithms and dependency pins did not
change; format dependencies remain confined to the opt-in test host.

## Reproduction

Reuse report 104's Hydra build and matching ABI-5 headers. Install the runtime
with its MMD adapter and the compatible MMD owner packages, then run from an
initialized x64 MSVC developer shell:

```powershell
cmake -S . -B build/avatar-state-hydra -DTOON_MMD_UV_PROVIDER_TEST=ON `
  "-DCMAKE_PREFIX_PATH=<runtime-install>;<mmd-install>;<physics-install>"
cmake --build build/avatar-state-hydra --target toon-hydra2-mmd-uv-test toon-hydra2-primvar-test toon-renderer-shaders --parallel 2
ctest --test-dir build/avatar-state-hydra `
  -R '^toon-renderer-(mmd-uv-(cpu|gpu)|hydra-primvars(-gpu)?)$' --output-on-failure
```

The provider executable is never installed. Default builds retain their
existing dependency scope. The GPU test uses the existing provider launcher
to locate runtime DLLs; a missing Vulkan device is reported as a skip.
