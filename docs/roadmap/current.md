# Current

Renderer Phase 0 and the work around it. Which release carries the phase is
the [status table](README.md#status-at-a-glance).

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked · ⚠️ accepted workaround

## Renderer Phase 0 — Skeleton

The OpenStrata renderer scaffold was generated on 2026-09-26 and passes its
own contract on Windows
([report](../reports/ost/01-2026-09-26-v0.23.6-renderer-template-bootstrap.md)).
What is left of [design policy §25](../design/DESIGN_POLICY.md#25-implementation-phases)
Phase 0:

- ✅ **Plugin registration and `HdRenderDelegate`.** `hdToon` is discovered,
  creates its delegate, and draws a first frame in `testusdview`.
- 🚧 **Vulkan instance, device and swapchain.** Offscreen rendering and
  swapchain presentation exist in the backend. The Hydra path renders a fixed
  64×64 offscreen image and copies it into CPU `HdRenderBuffer`s, which
  usdview upscales.
  - ⬜ Render at the AOV's resolution.
- ⬜ **Camera.** Use the Hydra camera's view and projection; the
  `camera` Sprim is accepted and ignored today.
- 🚧 **Triangle and mesh rendering.** The triangle is hard-coded.
  - ⬜ Upload a mesh's points and triangulated topology into `ToonMesh`
    through the core, and draw it.
  - ⬜ Route Hydra dirty bits by kind before Renderer Phase 1 builds on them.
    `HdToonMesh::Sync` re-reads points and topology on every sync and clears
    every bit, which is the pattern
    [§14](../design/DESIGN_POLICY.md#14-dirty-propagation) forbids.
- ⬜ **Basic synchronization.** Replace the scaffold's binary fences with a
  timeline semaphore and Synchronization2
  ([§19](../design/DESIGN_POLICY.md#19-cpu--gpu-synchronization)). The
  swapchain path already runs one frame in flight; `vkDeviceWaitIdle` remains
  only at swapchain recreation and teardown.
- 🚧 **Basic shader system.** One Slang file compiled to SPIR-V at build time.
  - ⬜ Shader modules and pipelines created once and cached, with no pipeline
    compile after the first frame
    ([§23](../design/DESIGN_POLICY.md#23-performance-kpis)).

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
