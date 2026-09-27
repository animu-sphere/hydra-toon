# Capability matrix

What is implemented now. This page is the only document that says so; the
[design policy](../design/DESIGN_POLICY.md) describes intent and the
[roadmap](../roadmap/README.md) describes what is left.

Legend: ✅ implemented and tested · ⚠️ implemented as an accepted Renderer
Phase 0 stand-in for the design · 🧪 scaffold only (works, but is the generated
bootstrap, not the design) · ⬜ not implemented

Configurations each row was measured on are
[SUPPORTED_CONFIGURATIONS.md](SUPPORTED_CONFIGURATIONS.md).

## Renderer

| Capability | Status | Evidence / notes |
| --- | --- | --- |
| Host-neutral core with an enforced header boundary | ✅ | `renderer.core.boundary`; CTest `toon-renderer-core-boundary` |
| Vulkan device bring-up with validation layers, messages treated as errors | ✅ | `renderer.backend.capability`, `renderer.validation.messages` |
| Offscreen colour (RGBA8) and depth (D32) render products at any extent, read back | ✅ | `Toon::OffscreenRenderer`; `renderer.render_product.color`, `.depth` at 64×64, the Hydra AOV's extent in `testusdview` |
| Persistent device, pipelines and render targets | ✅ | the two scene pipelines created once for the renderer's life, targets reallocated only on a resize; `renderer.frame.persistence` requires 1,000 frames on those pipelines, one target allocation and one mesh upload |
| One frame in flight on a timeline semaphore, Synchronization2 barriers, dynamic rendering | ✅ | offscreen and swapchain paths; `vkDeviceWaitIdle` only at swapchain recreation and teardown |
| Swapchain presentation, one frame in flight | ✅ | `toon-viewport`, drawing the bootstrap triangle scene through the mesh pipeline |
| Slang shaders compiled to SPIR-V | ✅ | `backend/vulkan/shaders/mesh.slang`: unlit, one colour per draw; `mtoon.slang`: `mtoon_opaque` |
| Mesh rendering from scene data | ✅ | `RenderWorld` meshes (triangulated indices, points, transform, colour, visibility, material) → `DrawList` → indexed draws; CTest `toon-render-world` |
| Smooth vertex normals | ✅ | derived from points and topology at commit, only when either changes, as Storm derives them for a skinned mesh; authored normals are not read. CTest `toon-render-world` |
| Dirty routing by kind | ✅ | points, topology, transform, colour, visibility and camera each advance only their own revision; the GPU re-uploads only what changed ([design policy §14](../design/DESIGN_POLICY.md#14-dirty-propagation)) |
| Geometry upload | ⚠️ | written into host-visible buffers on the render thread, not staged off-thread ([design policy §20](../design/DESIGN_POLICY.md#20-asset-upload)) |
| Camera | ✅ | a `ToonView` in the OpenGL clip convention; the Vulkan backend flips y and maps z to [0, 1] |
| Lighting | ⚠️ | a stand-in in `mtoon.slang`: one white key light fixed to the camera and a uniform ambient; scene lights are not read. Meshes without an MToon material draw flat, unlit |
| `UsdSkel` skinning | ⬜ | Renderer Phase 1 |
| `ToonMaterial` | ✅ | material policy §4's common part and MToon block, without textures; a value edit advances a material's parameters revision, a model, alpha-mode or double-sidedness edit its structure revision; CTest `toon-render-world` |
| Material parameter slots | ✅ | one 64-byte slot per material in a storage buffer, written only when its parameters revision changes ([material policy §7](../design/MATERIAL_POLICY.md#7-pipelines-and-parameter-buffers)); `renderer.material.mtoon_opaque` requires a value-only edit to rewrite one slot and nothing else |
| MToon opaque (`mtoon_opaque`) | ✅ | lit / shade colours with shading shift and toony, GI equalization, emission, cull mode from double-sidedness; `renderer.material.mtoon_opaque`; [renderer report 07](../reports/renderer/07-2026-09-27-mtoon-opaque.md). No textures; Mask tests the constant alpha |
| MToon transparent, rim, MatCap, UV animation | ⬜ | Renderer Phase 2; Blend materials draw through `mtoon_opaque` until then ⚠️ |
| Inverted-hull outline | ⬜ | Renderer Phase 1 |
| Morphs, expressions, look-at | ⬜ | Renderer Phase 3 |
| Late motion latching | ⬜ | Renderer Phase 3 |
| MMD materials | ⬜ | Renderer Phase 4 |
| `UsdPreviewSurface` | ⬜ | Renderer Phase 5 |
| WebGPU backend | ⬜ | Renderer Phase 6 |
| Renderer telemetry | ⬜ | [design policy §24](../design/DESIGN_POLICY.md#24-profiling) |

## Hydra adapter (`hdToon`)

| Capability | Status | Evidence / notes |
| --- | --- | --- |
| Plugin discovery through `plugInfo.json` | ✅ | `renderer.plugin.discovery` |
| Render delegate creation | ✅ | `renderer.delegate.creation`; measured against OpenUSD 26.08 (`HD_API_VERSION` 98); the pre-98 branch compiles only in principle |
| CPU colour / depth / primId `HdRenderBuffer`s | ✅ | `renderer.render_buffer.cpu` |
| First frame and a stable update in `testusdview` | ✅ | `renderer.host.first_frame`, `.host.stable_update`: the smoke scene's mesh through its camera at the AOV's resolution; a points edit re-uploads points only, asserted from the frame evidence |
| Per-frame host evidence (`TOON_HYDRA_EVIDENCE`) | ✅ | one line per frame: completion, revision, extent, buffers written, pipelines, target allocations, uploads, material slot writes, how many materials selected PreviewSurface and MToon, and how many draws went through `mtoon_opaque`; renderer reports [05](../reports/renderer/05-2026-09-26-vrm-usdview-session.md), [07](../reports/renderer/07-2026-09-27-mtoon-opaque.md) |
| `mesh` | ✅ | points, topology (triangulated by `HdMeshUtil`, left-handed winding reversed), transform, visibility, constant `displayColor` and material binding, each synced only when its dirty bit is set |
| `camera` | ✅ | view and projection read through `HdRenderPassState` |
| Skinned points (`UsdSkel` ext computations) | ⚠️ | the computations' CPU kernels run during mesh sync, as HdEmbree does; GPU skinning is Renderer Phase 1 |
| Colour and depth AOVs | ✅ | read back from the GPU and written bottom row first, as Hydra buffers are laid out |
| `primId`, `instanceId`, `elementId` AOVs | 🧪 | filled with -1; no picking |
| `material` | ✅ | selected by [material policy §3](../design/MATERIAL_POLICY.md#3-selection-a-realization-is-chosen-not-merged): `vrm/mtoon` on the prim, read from the terminal scene index in `Sync`, is MToon, and its `vrm/material` and `vrm/mtoon` values are normalized into `ToonMaterial`; anything else is PreviewSurface with the fallback's values. CTest `toon-renderer-hydra-material`; [renderer report 03](../reports/renderer/03-2026-09-26-material-sprim.md) |
| MMD selection, PreviewSurface network, material textures | ⬜ | Renderer Phase 4, 5 and 1 |
| Value-only material changes (time samples) | ✅ | a `vrm` locator dirtied without `material` never reaches `Sync`; the delegate observes the terminal scene index and re-reads that material in `Update()` into the same slot. CTest `toon-renderer-hydra-material`; [renderer report 04](../reports/renderer/04-2026-09-26-material-value-route.md) |
| Material binding on meshes | ✅ | by path, resolved again when a material prim under it is created or destroyed; a mesh whose material selected MToon draws through `mtoon_opaque`, any other its display colour. On the avatar with `vrmImaging`, 20 of 20 draws are MToon ([renderer report 07](../reports/renderer/07-2026-09-27-mtoon-opaque.md)) |
| Render tags, instancers, framing data window | ⬜ | every visible mesh is drawn over the whole AOV |
| Lights | ⬜ | |

## Hosts

| Host | Status | How |
| --- | --- | --- |
| Headless runner (`toon-headless`) | ✅ | runs during `ost build`; writes `renderer-report.json` |
| Standalone viewport (`toon-viewport`) | 🧪 | `ost renderer viewport`; shows the bootstrap triangle scene only |
| `usdview` | ✅ | `testusdview` in CTest; in an `ost formation run` session with `vrmImaging`, VRM materials select MToon, with the host's Python supplied to the run ([renderer report 06](../reports/renderer/06-2026-09-27-vrm-formation.md)); `ost renderer view` has not been run |
