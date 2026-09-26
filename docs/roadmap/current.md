# Current

The work around Renderer Phase 0 and before Renderer Phase 1. Which release
carries a phase is the [status table](README.md#status-at-a-glance).

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
  Replace before avatar-sized uploads are measured, no later than Renderer
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
  with the host's Python supplied to the run
  ([ost report 05](../reports/ost/05-2026-09-27-v0.23.11-report-04-reverified.md));
  the packages are not published yet. Per platform:
  [vrm-host-session.md](vrm-host-session.md).

Before Renderer Phase 4:

- ⬜ **MAT-Q1 for MMD.** Propose to `usd-mmd-plugins` that `mmdImaging`
  expose `MmdMaterialAPI` in the shape `vrmImaging` froze, so one read path
  serves both models.

## Project infrastructure

- ⬜ **CI.** A generated OpenStrata CI lane: the `core` build with its GPU
  checks as capability-gated `SKIP`s on hosted runners, and the `hydra` intent
  against a digest-pinned runtime.
- ⬜ **Documentation check.** A `scripts/check_docs.py` that resolves relative
  links and checks category indexes, as the sibling repositories have.
- ⬜ **Core boundary check by glob.** The check lists its headers by name
  ([PROJECT_LAYOUT.md §4](../architecture/PROJECT_LAYOUT.md#4-dependency-directions));
  it should find every public core header itself.
