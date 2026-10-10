# Hydra resident target identities

- Date: 2026-10-10
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Runtime: canonical CY2026 OpenUSD 26.08 lookdev
- Scope: the renderer side of preparing runtime-to-resident bindings from an
  actual avatar's static asset in
  [v0.3.0](../../roadmap/v0.3.0.md#evaluated-state-adapter-and-representative-avatar-evidence)

## Description

`HdToonRenderDelegate::DescribeResidentTargets` returns, in renderer id order,
the Hydra identities behind what the last sync made resident. Each mesh
carries its prim path, bound material path and renderer material id. A mesh
skinned on the GPU also carries its skeleton path, its palette joint tokens
and each entry's skeleton joint index, the inverse of the skeleton's bind
transform per entry in stage units, world-to-skeleton and skeleton-to-mesh.
The palette follows the mesh's `skel:joints` when authored, as usdSkelImaging
remaps it, otherwise the skeleton's order. Subshape slots list the
`skel:blendShapes` name, BlendShape prim and weight (1 for the primary shape,
otherwise the inbetween's) in morph-weight order. Materials are listed by
path and id.

The values come from the same terminal scene index the mesh's sync reads.
The subshape numbering that packs morph targets and normal offsets now
produces these identities too, so the two cannot diverge. Palette identities
are rebuilt only when the binding's skeleton or joints, or the skeleton's
joints or bind transforms, change. A placement change refreshes only
world-to-skeleton and skeleton-to-mesh. A mesh usdSkelImaging's CPU kernel
skins describes no palette or subshapes.

Nothing here interprets a format, expression or humanoid role. The
identities are the same USD paths and tokens the runtime's USD binders use as
opaque target identity, so a host can match them without renderer knowledge.
Matching, inbetween resolution and the `AvatarBindings` it builds remain the
external host's work. `Toon::AvatarState` and its public header are
unchanged; the description is Hydra-adapter API, not a core or backend type.

## Evidence

`skinning.usda`'s linear quad now authors its own `skel:joints` in reverse
skeleton order, with correspondingly reversed joint indices, and binds a
material. `toon-renderer-hydra-skinning` checks every committed mesh is
described with its committed id. The described palette is `base/tip`, `base`
with skeleton indices 1, 0 and the tip's inverse bind first. Subshapes are
`lift` at 0.5 and 1, `tilt` and `plain`, matching all four weight slots. The
material path/id matches the commit, and the dual quaternion quad describes no
palette. Composing the posed joints from the identities as the fast adapter
composes a binding gives usdSkelImaging's palette. A 2-unit skeleton
translation refreshes only world-to-skeleton and skeleton-to-mesh, and the
composition still holds. The existing GPU morph-normal comparisons
(`toon-renderer-hydra-morph-normals`) pass unchanged with the reordered
fixture.

With `--stage`, the same executable checks a real stage at its start time
code. On the local AliciaSolid USDZ, which is not redistributable and is not
committed:

```text
authored rest: 21 meshes, 20 GPU-skinned with 2560 palette entries (0 not the identity) and 158 subshape slots, 20 bound to 12 materials
rotated rest: 21 meshes, 20 GPU-skinned with 2560 palette entries (2560 not the identity) and 158 subshape slots, 20 bound to 12 materials
```

The 21st mesh is the skeleton guide. Every palette entry names a skeleton
joint, every morph weight slot has a described subshape, and composing
`UsdSkelSkeletonQuery`'s skeleton-space joints from the identities matches
the committed palette within a relative 1e-4. The authored rest equals the
bind pose, so its palette is all identity. The second pass rotates every
joint's rest in the session layer by a joint-dependent angle; no entry is then
the identity, so a mismatched joint cannot pass by coincidence.

## Commands and limits

```powershell
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
# With the runtime's bin and lib directories on PATH:
toon-hydra2-skinning-test --stage <AliciaSolid.usdz>
```

All 53 CTests and completion-bound `ost validate` pass; artifact integrity
is an explained skip because no package was created.

This establishes the identities a host binds with, on one avatar without
`vrmImaging` (material identity does not depend on MToon selection). It does
not bind runtime results to them, evaluate inbetweens, render runtime output,
compare Hydra and direct paths, or measure latency; those remain in the
v0.3.0 roadmap.
