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

- ⬜ **MAT-Q1: how Material interface inputs reach the delegate.** Measure on
  OpenUSD 26.08 and agree the adapter's home with `usd-vrm-plugins` and
  `usd-mmd-plugins` ([material policy §9](../design/MATERIAL_POLICY.md#9-open-questions)).
- ⬜ **Raise the cross-repository observations** in
  [integration scope §6](../design/INTEGRATION_SCOPE_POLICY.md#6-cross-repository-observations)
  with their owners.

## Project infrastructure

- ⬜ **CI.** A generated OpenStrata CI lane: the `core` build with its GPU
  checks as capability-gated `SKIP`s on hosted runners, and the `hydra` intent
  against a digest-pinned runtime.
- ⬜ **Documentation check.** A `scripts/check_docs.py` that resolves relative
  links and checks category indexes, as the sibling repositories have.
- ⬜ **Core boundary check by glob.** The check lists its headers by name
  ([PROJECT_LAYOUT.md §4](../architecture/PROJECT_LAYOUT.md#4-dependency-directions));
  it should find every public core header itself.
