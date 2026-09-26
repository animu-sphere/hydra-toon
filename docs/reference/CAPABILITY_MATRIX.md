# Capability matrix

What is implemented now. This page is the only document that says so; the
[design policy](../design/DESIGN_POLICY.md) describes intent and the
[roadmap](../roadmap/README.md) describes what is left.

Legend: ✅ implemented and tested · 🧪 scaffold only (works, but is the
generated bootstrap, not the design) · ⬜ not implemented

Configurations each row was measured on are
[SUPPORTED_CONFIGURATIONS.md](SUPPORTED_CONFIGURATIONS.md).

## Renderer

| Capability | Status | Evidence / notes |
| --- | --- | --- |
| Host-neutral core with an enforced header boundary | ✅ | `renderer.core.boundary`; CTest `toon-renderer-core-boundary` |
| Vulkan device bring-up with validation layers, messages treated as errors | ✅ | `renderer.backend.capability`, `renderer.validation.messages` |
| Offscreen colour (RGBA8) and depth (D32) render products, read back | 🧪 | a fixed 64×64 bootstrap triangle; `renderer.render_product.color`, `.depth` |
| Repeated frames on persistent resources | 🧪 | 1,000 deterministic frames; `renderer.gpu.frame`, `renderer.frame.persistence` |
| Swapchain presentation, one frame in flight | 🧪 | `toon-viewport`; the bootstrap triangle |
| Slang shaders compiled to SPIR-V | 🧪 | `backend/vulkan/shaders/triangle.slang` only |
| Mesh rendering from scene data | ⬜ | Renderer Phase 0 |
| Camera | ⬜ | the Hydra camera is accepted but not used for projection |
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
| First frame and a stable update in `testusdview` | 🧪 | `renderer.host.first_frame`, `.host.stable_update`; the bootstrap triangle, upscaled from 64×64 |
| Supported prim types | 🧪 | `mesh` (points and topology are read only to decide whether a visible mesh with a face exists; none of it is drawn), `camera`, `renderBuffer` |
| Materials, skeletons, lights | ⬜ | |

## Hosts

| Host | Status | How |
| --- | --- | --- |
| Headless runner (`toon-headless`) | ✅ | runs during `ost build`; writes `renderer-report.json` |
| Standalone viewport (`toon-viewport`) | 🧪 | `ost renderer viewport` |
| `usdview` | 🧪 | `ost renderer view`; `testusdview` in CTest |
