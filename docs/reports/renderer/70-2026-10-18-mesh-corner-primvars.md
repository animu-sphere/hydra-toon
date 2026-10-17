# Generic USD mesh corner primvars

- Date: 2026-10-18
- Machine: Windows x86_64, NVIDIA RTX A5000, driver 597.16
- Build: MSVC 14.51; canonical CY2026 OpenUSD 26.08 `lookdev`, combined
  `viewport-usd`; plain CMake without OpenUSD
- Scope: v0.5.0's ordinary textured mesh inputs

## Normalization

`HdToonMesh` reads `st` and normals with vertex, varying, face-varying,
uniform or constant interpolation. Indexed values arrive resolved by Hydra.
Face-varying and uniform inputs use polygon-corner vertices: a second
`HdMeshUtil` triangulation, with corner numbers instead of point numbers,
produces the same face/subset ordering, holes and winding as the source
topology. Only corners the triangles reach become output vertices.

The adapter retains source-point geometry in a private `RenderWorld` and
publishes normalized per-vertex arrays to the existing shared render world.
Each corner takes its source point, skin influences, sparse morph range,
MMD edge scale and additional UV 1. Morph offsets retain their weight-slot
identities; a duplicated vertex references the same source range. Constant
skin influences remain constant. No backend vertex format changes.

Missing or unusable normals derive on the original shared points before
UV seams split them, so those seams do not introduce a lighting crease.
The optional `authored` argument of `RenderWorld::SetMeshNormals`, true by
default, lets the adapter supply these derived values without labelling
them authored. Empty or insufficient arrays still derive in core.

Source revisions retain static arrays on pose and morph-weight changes.
UV or normal value edits retain topology and points. Removing corner
inputs restores source-point numbering. Equal topology and source points
are ignored when Hydra dirties them along with a primvar change. Constant
UVs expand again when the source point count changes.

The winding check also exposed an existing double reversal: `HdMeshUtil`
already converts left-handed topology, and the adapter reversed it again.
The adapter now uses its result directly. OpenUSD's
[mesh utility implementation](https://github.com/PixarAnimationStudios/OpenUSD/blob/release/pxr/imaging/hd/meshUtil.cpp)
owns that conversion, including triangle rotations for polygon edge flags.

## Controlled evidence

`toon-renderer-hydra-primvars` imports a redistributable in-memory USD stage
through UsdImaging and tests:

- Indexed face-varying UVs on two quads sharing points, with a UV seam.
- Uniform and face-varying normals; generated smooth normals that agree
  across the seam.
- Subset face ranges, per-point MMD edge scales and additional UVs using
  the same corner map.
- UV index edits, normal value/interpolation edits, removal, constant UVs,
  point-count edits and malformed short corner arrays.
- Hole faces and left-handed polygon triangulation.

The check also reuses `skinning.usda`, with two triangles and a corner UV
seam. It verifies duplicated influences, shared morph ranges, preserved
rest normals, unchanged static revisions through pose and weight changes,
and skinned corner-normal value edits. Face-varying rest normals follow
the normals aggregator's `hasFaceVaryingNormals` input.

`toon-renderer-hydra-primvars-gpu` compares the textured ordinary mesh to
independently specified explicit vertices, UVs, normals and triangles. It
also compares a skinned/morphed frame to independently specified deformed
points and normals. Both comparisons give identical colour and depth at
1x and 4x MSAA. Repeated frames retain geometry, skin, morph buffers and
pipelines, without Vulkan validation messages. The core state test checks
the provenance and equal-value behaviour of adapter-derived normals.

## Verification

The combined `viewport-usd` build passed all 58 CTests, including the two
new primvar checks, existing skinning/morph/subset checks, material GPU
checks, presentation, `testusdview` and the install-tree check. The
plain-CMake build without OpenUSD passed all 16 CTests, including the
expanded core state check and the install-tree check. Both trees were
rebuilt with `cmake --build`, then checked with
`ctest --test-dir <tree> --output-on-failure`.

The first combined run omitted the lookdev runtime's `bin` and `lib` from
the process `PATH`; six direct viewport launches failed with Windows
missing-DLL status `0xc0000135`. Re-running with those runtime directories
on `PATH` passed all 58 checks. The Hydra-specific CTests already add the
runtime directories themselves. Changed documents' relative links and
`git diff --check` also passed.

## Limits

The corner path expands emitted polygon corners rather than welding equal
attribute tuples, and retains source geometry in addition to normalized
arrays. It does not refine subdivision surfaces, add more UV sets or shade
per-vertex display colours. Authored/generated tangents and PreviewSurface
normal mapping remain in [v0.5.0](../../roadmap/v0.5.0.md).

OpenUSD 26.08 retains the normals computation's `hasFaceVaryingNormals`
flag after changing the input interpolation in an already formed scene.
The test observed new normal values with the old false flag. Forming a
fresh host over the same edited stage supplies the true flag and the
expected corner normals; later value-only edits work on that host. The
adapter follows the supplied computation contract, so changing a skinned
normal's interpolation requires the host to reform that computation.

These controlled comparisons establish normalization and renderer
consistency, not reference-renderer PBR or tangent equivalence. No
restricted avatar, texture or capture was added, and no package or
milestone release was published.
