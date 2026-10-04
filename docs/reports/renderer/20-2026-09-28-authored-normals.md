# Authored normals: the avatar's meshes draw the normals they author, and the line across the bangs where two meshes meet is gone

- Date: 2026-09-28
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.13`; the canonical CY2026 `lookdev` runtime (OpenUSD
  26.08) for the build; the committed `formations/vrm-host-session/`
  Formation of [report 13](13-2026-09-28-host-session-formation.md), with
  the local viewport as its command, as in
  [report 19](19-2026-09-28-hydra-fed-viewport.md)
- Occasion: a faint horizontal line across the avatar's bangs, seen in
  `usdview` and not in a three-vrm rendering of the same model; the normal
  item of [v0.2.0](../../releases/v0.2.0.md)'s "The rest
  of MToon" asks for authored normals

## TL;DR

**The bangs are two meshes: the opaque roots, `Alicia_hair`, and the Blend
tips, `Alicia_hair_trans_zwrite`. They share 78 points, with identical
positions and identical authored normals. `hdToon` read no normals and
derived smooth ones per mesh, from each mesh's own triangles, so at those
points the two meshes' normals differed by 39° on average, and MToon's toon
step shaded the two sides of the seam apart. The line was that step, not
the transparency: the published v0.1.0, which draws Blend as opaque, shows
it too. A mesh now draws its authored normals when it has one per point.
usdSkelImaging hands a skinned mesh's normals to Hydra only under
`USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS`, which `toon-viewport` now sets;
with it, 20 of the avatar's 20 draws use authored normals, and the line is
gone on screen.**

## 1. The cause

Measured on the stage with OpenUSD's Python, the roots' 3,904 points and
the tips' 264 share 78 positions exactly. At those positions:

| Normals | Roots against tips |
| --- | --- |
| authored (`primvars:normals`, vertex) | identical |
| texture coordinates | identical, but for points that two strands share |
| smooth, derived per mesh as `hdToon` did | 39.2° apart on average, 174.6° at most |

Across the roots, the derived normals are 11.4° from the authored ones on
average. The two materials differ only in alpha mode, depth writes and
queue offset; their textures, colours and shading values are the same. In
a `usdview` capture of the published v0.1.0, the blue channel steps by 4
levels across the seam, along one line through every strand.

## 2. What changed

- **Core.** `RenderWorld::SetMeshNormals` takes authored normals, one per
  point. At commit a mesh draws them when there are at least as many as its
  topology reaches, and otherwise derives smooth normals as before.
  Authored normals do not follow a points edit, so a blend shape weight or
  any other points change re-sends none; setting the normals a mesh already
  has changes nothing. `MeshSnapshot::authored_normals` says which it drew.
- **`hdToon`.** A mesh syncs its normals when they are dirty: a vertex or
  varying `normals` primvar, or, for a mesh UsdSkel skins, the rest normals
  that its normals computation's aggregator carries, re-read only when that
  aggregator's inputs change, as the rest points are. The GPU skins them
  with the points, as it skinned the derived ones. Face-varying normals are
  not read, as face-varying `st` is not, and neither are blend shapes'
  normal offsets. The frame evidence adds `draws_authored_normals`.
- **The viewport.** usdSkelImaging blocks a skinned mesh's `normals`
  primvar, and makes a normals computation only when
  `USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS` is on and the mesh's
  subdivision scheme is none (`dataSourceResolvedPointsBasedPrim.cpp`,
  `HasNormalsExtComputations`); the setting is off by default and read
  once. `toon-viewport --usd` sets it to 1 before creating any scene index,
  unless the environment already sets it. A `usdview` session sets it in
  its environment; [BUILDING.md](../../guides/BUILDING.md#the-vrm-host-session)
  says so, and the Formation is unchanged.
- **Tests.** CTest `toon-render-world` checks that authored normals replace
  derived ones, that a points edit or the same normals re-send nothing, and
  that too few or none derive them. `skinning.usda`'s linear quad authors
  normals that are not its derived +z, and a subdivision scheme of none;
  CTest `toon-renderer-hydra-skinning` sets the setting and checks that the
  quad draws them, that a pose change and a blend shape weight change leave
  them alone, and that the dual quaternion quad, which authors none, derives
  them.

## 3. What was run

```sh
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
ost build --jobs auto && ost test && ost validate
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra && ost validate --profile lookdev --intent hydra
cd formations/vrm-host-session
ost formation run formation.toml -- <build>/adapters/viewport/toon-viewport.exe \
    --usd <avatar.usdz> --hidden --frames 8 --vsync off --expect-draws 20
```

The avatar is `AliciaSolid.usdz`, the one reports 06–19 use; it is not
redistributable and not in the repository.

## 4. Results

| Build or run | Result |
| --- | --- |
| `viewport-usd` intent, canonical `lookdev` | `ost test` 13/13; `ost validate` passed |
| `core` | `ost test` 4/4; `ost validate` passed |
| `hydra` intent | `ost test` 10/10; `ost validate` passed |

The avatar in the viewport, from the last frame's summary:

| Run | Draws | MToon | Transparent | Outlined | Skinned | Authored normals |
| --- | --- | --- | --- | --- | --- | --- |
| before the viewport set the setting, and without it in the environment | 20 | 20 | 5 | 15 | 20 | 0 |
| with it, in the environment or set by the viewport | 20 | 20 | 5 | 15 | 20 | 20 |

Between the two, at 3000 × 2141 with 4 samples, 2,052 pixels change by more
than 24 levels. On screen, orbiting and dollying in on the face, the line
across the bangs is gone.

## 5. Observations

- **The line was the normals, not the blending.** It showed in the
  published v0.1.0, which has no transparent pass, and it stayed with
  report 15's pass once the tips blended.
- **The hull moves with the normals.** `mtoon_outline` pushes each point
  along its normal, so the hull now follows the authored normals too;
  report 10 noted that derived normals keep a hard edge an asset authors
  from splitting the hull. Where an asset splits its normals, the hull now
  splits there, as UniVRM's and three-vrm's do.
- **usdSkelImaging decides what reaches a delegate.** Without the setting,
  a skinned mesh's normals are blocked for every delegate, and Storm
  derives its own from the skinned points. A mesh with a subdivision scheme
  other than none gets no normals computation even with it, so a USD mesh
  that leaves the scheme at its default, `catmullClark`, derives; a VRM
  importer's meshes state none.

## 6. Not checked

A `usdview` session with a `toon` package built from this change: the
viewport alone was run. Timing, and the cost of the setting in Storm. A
mesh with face-varying normals, which derives. Blend shapes' normal
offsets. Linux.
