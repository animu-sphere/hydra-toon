# Explicit avatar primary UV bindings

Date: 2026-10-26. Source base `e54022f` plus this change. Windows x64,
MSVC 19.51 Release, runtime ABI-5 headers from base `7da1356`, NVIDIA
RTX A5000 / driver 597.16. Generated resources only.

## Boundary

`Toon::AvatarState` accepts complete absolute primary `st` records through
explicit `AvatarUvBinding` vertex-to-runtime-slot maps. Reordered slots and
duplicated split vertices are allowed; all runtime outputs must be mapped.
The adapter validates dense source mesh identities, complete resident vertex
arrays, topology bounds, finite narrowing and stable scene/layout identity.
Failures preserve output and frame history. Identical values reuse immutable
arrays/revisions, and Reset/Release restore through the ordinary baseline route.

UV changes take ordinary extraction and geometry upload. The late-frame
structural guard is unchanged. Resident tangent arrays refuse pending coherent
regeneration, and the automatic Hydra matcher reports UV outputs as unmatched
until its resident description supplies source vertex mapping. No format
evaluation, V conversion or base-plus-delta arithmetic enters the renderer.
ABI-4 compilation retains its prior scope. The ABI-5 experimental C++ binding
layout changes; install/rebuild the library and consumers together.

## Checks

All 17 CTests pass in `build/avatar-state-no-vulkan`, including installed
component/core consumers. The focused Vulkan CPU/GPU checks also pass.
CPU cases cover source-slot reordering, split vertices, omitted/overlapping
bindings, sparse identities, mixed source meshes, missing arrays, invalid
topology bounds, stale UV baseline/revisions, resident tangents, identity/count
changes, nonfinite and float-overflow values, reset, retained output and release.
Failed bindings leave the prior binding usable; failed updates publish nothing.

The GPU check uses explicit MMD and MToon triangles with a generated four-colour
texture at 96 by 96 pixels, default 4x MSAA. Signed and above-one weights
`0 -> 1.5 -> -0.5 -> 0` produce absolute UVs from an independent analytic
scene oracle. Colour/depth arrays agree exactly for every frame; nonzero UVs
change texture pixels and zero restores the original pixels. Vulkan validation
message counts remain zero. Existing pose/morph/material late-frame GPU checks
also pass in that CTest.

The rebuilt Hydra skinning CTest passes, including the automatic matcher's
explicit unmatched-UV diagnostic beside valid pose/morph/material bindings.
Documentation checks and their seven checker tests pass.

Runtime's separate installed-Toon MMD check validates owner-resolved UVs for
two instances and three reset generations with pose/morph/visibility, duplicate
array reuse and snapshot lifetime after producer destruction; see the
[MMD report](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/reports/2026-10-26-phase8-toon-primary-uv.md).
This is CPU transport plus a separate renderer GPU check, not composed
MMD-to-GPU playback or runtime/Hydra parity. Automatic resident mapping,
tangent regeneration and representative avatar acceptance remain open.

## Reproduction

```powershell
cmake -S . -B build/avatar-state-no-vulkan -A x64 `
  -DTOON_ENABLE_AVATAR_STATE=ON -DTOON_ENABLE_VULKAN=OFF `
  -DTOON_AVATAR_RUNTIME_INCLUDE_DIR=<runtime-checkout>/include
cmake --build build/avatar-state-no-vulkan --config Release -- /m:1 /nr:false
ctest --test-dir build/avatar-state-no-vulkan -C Release --output-on-failure
cmake -S . -B build/avatar-state-vulkan -A x64 `
  -DTOON_ENABLE_AVATAR_STATE=ON -DTOON_ENABLE_VULKAN=ON `
  -DTOON_AVATAR_RUNTIME_INCLUDE_DIR=<runtime-checkout>/include
cmake --build build/avatar-state-vulkan --config Release `
  --target toon-avatar-state-test -- /m:1 /nr:false
ctest --test-dir build/avatar-state-vulkan -C Release `
  -R '^toon-avatar-state(-gpu)?$' --output-on-failure
```

Supply the installed Vulkan SDK and shader compiler as in the
[build guide](../../guides/BUILDING.md). These commands reuse the existing
local configurations; they are local installed-package evidence, not a
published renderer/runtime release claim.
