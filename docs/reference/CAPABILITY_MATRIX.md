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
| Persistent device, pipeline and render targets | ✅ | one pipeline for the renderer's life, targets reallocated only on a resize; `renderer.frame.persistence` requires 1,000 frames on one pipeline, one target allocation and one mesh upload |
| One frame in flight on a timeline semaphore, Synchronization2 barriers, dynamic rendering | ✅ | offscreen and swapchain paths; `vkDeviceWaitIdle` only at swapchain recreation and teardown |
| Swapchain presentation, one frame in flight | ✅ | `toon-viewport`, drawing the bootstrap triangle scene through the mesh pipeline |
| Slang shaders compiled to SPIR-V | ✅ | `backend/vulkan/shaders/mesh.slang`: unlit, one colour per draw |
| Mesh rendering from scene data | ✅ | `RenderWorld` meshes (triangulated indices, points, transform, colour, visibility) → `DrawList` → indexed draws; CTest `toon-render-world` |
| Dirty routing by kind | ✅ | points, topology, transform, colour, visibility and camera each advance only their own revision; the GPU re-uploads only what changed ([design policy §14](../design/DESIGN_POLICY.md#14-dirty-propagation)) |
| Geometry upload | ⚠️ | written into host-visible buffers on the render thread, not staged off-thread ([design policy §20](../design/DESIGN_POLICY.md#20-asset-upload)) |
| Camera | ✅ | a `ToonView` in the OpenGL clip convention; the Vulkan backend flips y and maps z to [0, 1] |
| Lighting and shading | ⬜ | flat colour only |
| `UsdSkel` skinning | ⬜ | Renderer Phase 1 |
| MToon | ⬜ | Renderer Phase 1–2 |
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
| `mesh` | ✅ | points, topology (triangulated by `HdMeshUtil`), transform, visibility and constant `displayColor`, each synced only when its dirty bit is set |
| `camera` | ✅ | view and projection read through `HdRenderPassState` |
| Skinned points (`UsdSkel` ext computations) | ⚠️ | the computations' CPU kernels run during mesh sync, as HdEmbree does; GPU skinning is Renderer Phase 1 |
| Colour and depth AOVs | ✅ | read back from the GPU and written bottom row first, as Hydra buffers are laid out |
| `primId`, `instanceId`, `elementId` AOVs | 🧪 | filled with -1; no picking |
| Render tags, instancers, framing data window | ⬜ | every visible mesh is drawn over the whole AOV |
| Materials, lights | ⬜ | |

## Hosts

| Host | Status | How |
| --- | --- | --- |
| Headless runner (`toon-headless`) | ✅ | runs during `ost build`; writes `renderer-report.json` |
| Standalone viewport (`toon-viewport`) | 🧪 | `ost renderer viewport`; shows the bootstrap triangle scene only |
| `usdview` | ✅ | `testusdview` in CTest; `ost renderer view` has not been run |
