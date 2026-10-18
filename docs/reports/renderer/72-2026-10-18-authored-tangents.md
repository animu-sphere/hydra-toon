# Authored tangent frames

- Date: 2026-10-18
- Machine: Windows x86_64, NVIDIA RTX A5000, driver 597.16
- Build: MSVC 14.51; canonical CY2026 OpenUSD 26.08 `lookdev`, combined
  `viewport-usd`; plain CMake without OpenUSD; optional avatar state without Vulkan
- Scope: v0.5.0's authored tangent input and MToon consumption

## Behaviour

`RenderWorld::SetMeshTangents` retains immutable float4 rest tangents with
an independent revision. xyz points along +U; w is +/-1 such that
`w * cross(N,T)` points along +V in the mesh's bottom-left `st` convention.
Invalid values clear the input; absent or insufficient arrays select the
previous derivative fallback. Equal values retain both storage and revision.
Tangent edits invalidate late-snapshot and optional avatar-state bindings.

The Hydra adapter consumes float4 `primvars:tangents` with vertex, varying,
face-varying, uniform or constant interpolation, including indexed values
resolved by Hydra. Tangent seams use the existing corner map for geometry,
UVs, normals, skin influences and morph ranges. This is a bounded adapter
input convention, not an additional source-format schema. Top-left source
frames require a sign conversion at their adapter boundary.

MToon's opaque/Mask, Blend and outline shaders read a fourth vertex stream.
Skinning transforms tangent directions with the blended joints and preserves
the sign through reflections; model-view directions use the forward matrix,
normals its inverse transpose. Fragment shading orthogonalizes the tangent
against the morphed/skinned normal and converts its sign to top-left normal
sampling. Texture transforms and animation affect sampling alone. Pose and
weight changes retain the resident rest frame. Other material pipelines keep
their existing vertex inputs and no new pipeline variant is introduced.

## Controlled evidence

`toon-tangent-state` checks value retention, unchanged revisions, immutable
old snapshots, invalid directions/signs and atomic rejection of changed
tangent storage/revisions through `ApplyFastSnapshot`.

`toon-tangent-gpu` compares 24 deformed surfaces at each of 1x and 4x MSAA
with independently CPU-evaluated geometry and mapped normals. Cases combine
both handedness signs, joint and model reflections, nonuniform scales,
two weighted joints, morph position/normal offsets and Opaque/Mask/Blend.
The tangent direction deliberately differs from the geometry-derived frame,
and constant UVs cannot supply a derivative frame. Every RGBA byte agrees
within two levels. Missing/short tangents and absent UVs retain the vertex
normal on that degenerate-UV fixture. A back-facing hull at each sign has
its own analytic normal-debug colour check, exercising authored tangent
shading in the outline shader. Vulkan validation reports no messages.
Motion and tangent edits retain point, topology, skin and morph uploads.

`toon-renderer-hydra-primvars` checks indexed face-varying tangent seams,
uniform and constant interpolation, vertex/varying values, index edits,
point-count changes, malformed signs and removal through real UsdImaging.
The skin/morph fixture retains expanded tangent values and immutable arrays
through pose and weight changes. Its GPU counterpart compares the tangent
seam fixture with independent explicit vertices at 1x and 4x, and checks
steady resource reuse and zero validation messages.

The dual quaternion fixture checks that CPU-deformed points select the
derivative fallback instead of consuming undeformed authored tangents.

## Verification

Rebuilt the CMake trees and ran `ctest --test-dir <tree> --output-on-failure`:

- Combined `viewport-usd`: 61/61 CTests passed, including Hydra primvars,
  skin/morph/material checks, viewport presentation, `testusdview` and install.
- Plain CMake without OpenUSD: 18/18 CTests passed.
- Release Ninja, optional avatar state, no Vulkan or OpenUSD: 14/14 CTests
  passed, including structural binding rejection and installed consumers.

The final combined run also included the analytic hull assertions and the
CPU-deformation fallback check. Builds used the explicit MSVC developer
environment; direct viewport tests included the lookdev runtime's
`bin` and `lib` on `PATH`.

The local Debug compiler probes hit MSVC C1902 (PDB manager mismatch).
The supplemental build used `CMAKE_TRY_COMPILE_CONFIGURATION=Release`;
its installed-consumer checks used process-local `_CL_=/Z7` to embed debug
information during compiler probes. No compiler installation was changed.

## Limits

These are controlled fixtures and CPU-reference comparisons, not evidence
of equivalence to an external renderer or a glTF tangent generator.
MikkTSpace generation, shared PreviewSurface normal mapping and external
authored/generated asset comparisons remain in the v0.5.0 roadmap. Tangent
morph deltas are not represented; normal-offset morphs re-orthogonalize the
retained rest tangent. An unusable frame without a derivative fallback
retains the vertex normal. CPU point-computation fallbacks have no matching
deformed tangent output and keep the derivative route. No restricted asset
or image is committed and no package or milestone release was published.
