# Current

Renderer Phase 0's stand-ins, the work that precedes Renderer Phase 1's
MToon path, and Renderer Phase 1 itself. Which release carries a phase is the
[status table](README.md#status-at-a-glance).

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked · ⚠️ accepted workaround

## Renderer Phase 0 — Skeleton

Every item of [design policy §25](../design/DESIGN_POLICY.md#25-implementation-phases)
Phase 0 is implemented; what now exists is the
[capability matrix](../reference/CAPABILITY_MATRIX.md), and the run that
measured it is [renderer report 01](../reports/renderer/01-2026-09-26-phase0-mesh-camera.md).
It ships when v0.1.0 is released.

Stand-ins Phase 0 accepted, each replaced by the phase named:

- ⚠️ **Geometry is written on the render thread** into host-visible buffers,
  not staged off-thread ([§20](../design/DESIGN_POLICY.md#20-asset-upload)).
  Textures, since Renderer Phase 1, are staged, but decoded during material
  sync and copied on the graphics queue at the start of a frame. Replace
  both before avatar-sized uploads are measured, no later than Renderer
  Phase 1.
- ⚠️ **Skinned points come from Hydra's CPU ext computations.** Renderer
  Phase 1's GPU skinning replaces them.
- ⚠️ **The Hydra path reads every frame back** into CPU `HdRenderBuffer`s and
  waits for it. Hgi/Vulkan interop is not planned yet.

Left out of Phase 0 and not yet placed in a phase:

- ⬜ **Picking.** The id AOVs are filled with -1.
- ⬜ **Render tags, instancers and the framing data window.** Every visible
  mesh is drawn over the whole AOV.

## Before Renderer Phase 1

MAT-Q1 is answered for MToon
([material policy §2, §9](../design/MATERIAL_POLICY.md#9-open-questions);
[renderer report 02](../reports/renderer/02-2026-09-26-mat-q1-material-inputs.md)),
`hdToon`'s material Sprim reads and selects MToon by it, and value-only
changes reach it from the terminal scene index
([capability matrix](../reference/CAPABILITY_MATRIX.md#hydra-adapter-hdtoon);
renderer reports [03](../reports/renderer/03-2026-09-26-material-sprim.md),
[04](../reports/renderer/04-2026-09-26-material-value-route.md)).
What is left for Renderer Phase 1's MToon path:

- 🚧 **`vrmSchema` and `vrmImaging` in the `usdview` host session.** Without
  either, a VRM material silently draws as PreviewSurface. The session
  composes them as bundles; nothing links them. On Windows it runs as a
  Formation of the canonical runtime and the two packages, and selects MToon
  in `testusdview`
  ([renderer report 06](../reports/renderer/06-2026-09-27-vrm-formation.md)),
  from the Formation's own command
  ([ost report 06](../reports/ost/06-2026-09-27-v0.23.13-report-05-reverified.md));
  the packages are not published yet. Per platform:
  [vrm-host-session.md](vrm-host-session.md).

Before Renderer Phase 4:

- ⬜ **MAT-Q1 for MMD.** Propose to `usd-mmd-plugins` that `mmdImaging`
  expose `MmdMaterialAPI` in the shape `vrmImaging` froze, so one read path
  serves both models.

## Renderer Phase 1 — Avatar MVP

[Design policy §25](../design/DESIGN_POLICY.md#25-implementation-phases):
a VRM character displays in real time.

- ✅ **Material binding and `mtoon_opaque`, untextured.** Meshes bind their
  material; MToon's lit / shade split, shading shift and toony, GI
  equalization and emission from a parameter slot
  ([renderer report 07](../reports/renderer/07-2026-09-27-mtoon-opaque.md)).
- ✅ **Basic textures.** The base and shade colour textures, with UVs from
  the mesh's `st`, each with its wrap and texture transform; a texture's
  identity is structural
  ([renderer report 08](../reports/renderer/08-2026-09-27-basic-textures.md)).
- ⬜ **GPU skinning**, replacing the CPU ext computations. Next.
- ⬜ **Inverted-hull outline** (`mtoon_outline`,
  [material policy §5](../design/MATERIAL_POLICY.md#5-outline)).

Stand-ins this phase accepts so far:

- ⚠️ **Lighting is fixed in the shader**: one white key light attached to
  the camera and a uniform ambient. Replace when `hdToon` reads the scene's
  lights.
- ⚠️ **Blend materials draw opaque** through `mtoon_opaque`, until
  `mtoon_transparent` (Renderer Phase 2's alpha mode).
- ⚠️ **Normals are derived, never read**: smooth normals from the points,
  so an authored hard edge is lost.
- ⚠️ **Only a vertex or varying `st` is read.** The VRM importer writes
  TEXCOORD_0 there and no other set; a face-varying `st` needs split
  vertices, which the core does not make.
- ⚠️ **The texture table has 128 entries**, a fixed array indexed per draw,
  not bindless ([design policy §22](../design/DESIGN_POLICY.md#22-gpu-driven-rendering)).
  Past 128 images, a texture samples white until an entry frees.
- ⚠️ **Every texture is sampled trilinearly**, because glTF's sampler filters
  are not on the stage.

## Project infrastructure

- ⬜ **CI.** A generated OpenStrata CI lane: the `core` build with its GPU
  checks as capability-gated `SKIP`s on hosted runners, and the `hydra` intent
  against a digest-pinned runtime.
- ⬜ **Documentation check.** A `scripts/check_docs.py` that resolves relative
  links and checks category indexes, as the sibling repositories have.
- ⬜ **Core boundary check by glob.** The check lists its headers by name
  ([PROJECT_LAYOUT.md §4](../architecture/PROJECT_LAYOUT.md#4-dependency-directions));
  it should find every public core header itself.
