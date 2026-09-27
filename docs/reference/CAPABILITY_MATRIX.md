# Capability matrix

What is implemented now. This page is the only document that says so: the
[design policy](../design/DESIGN_POLICY.md) describes intent, the
[roadmap](../roadmap/README.md) what is built next, and a
[release record](../releases/) what a version established. A change that
implements something updates its row here.

Legend: ✅ implemented and tested · ⚠️ implemented as a stand-in that departs
from the design · 🧪 scaffold only (works, but is the generated bootstrap, not
the design) · ⬜ not implemented

Configurations each row was measured on are
[SUPPORTED_CONFIGURATIONS.md](SUPPORTED_CONFIGURATIONS.md).

## Renderer

| Capability | Status | Evidence / notes |
| --- | --- | --- |
| Host-neutral core with an enforced header boundary | ✅ | `renderer.core.boundary`; CTest `toon-renderer-core-boundary` |
| Vulkan device bring-up with validation layers, messages treated as errors | ✅ | `renderer.backend.capability`, `renderer.validation.messages` |
| Offscreen colour (RGBA8) and depth (D32) render products at any extent, read back | ✅ | `Toon::OffscreenRenderer`; `renderer.render_product.color`, `.depth` at 64×64, the Hydra AOV's extent in `testusdview` |
| Persistent device, pipelines and render targets | ✅ | the four scene pipelines created once for the renderer's life, targets reallocated only on a resize; `renderer.frame.persistence` requires 1,000 frames on those pipelines, one target allocation and one mesh upload |
| One frame in flight on a timeline semaphore, Synchronization2 barriers, dynamic rendering | ✅ | offscreen and swapchain paths; `vkDeviceWaitIdle` only at swapchain recreation and teardown |
| Swapchain presentation, one frame in flight | ✅ | `toon-viewport`, drawing the bootstrap triangle scene through the mesh pipeline |
| Slang shaders compiled to SPIR-V | ✅ | `backend/vulkan/shaders/mesh.slang`: unlit, one colour per draw; `mtoon.slang`: `mtoon_opaque`; `mtoon_transparent.slang`: `mtoon_transparent`; `mtoon_outline.slang`: `mtoon_outline`; the MToon three share `mtoon_common.slang`, and all four include `skinning.slang` |
| Mesh rendering from scene data | ✅ | `RenderWorld` meshes (triangulated indices, points, UVs, transform, colour, visibility, material, skin and pose) → `DrawList` → indexed draws; CTest `toon-render-world` |
| Smooth vertex normals | ✅ | derived from points and topology at commit, only when either changes, as Storm derives them for a mesh that authors none; authored normals are not read. A skinned mesh's are its rest pose's, skinned with its points. CTest `toon-render-world` |
| Dirty routing by kind | ✅ | points, UVs, topology, transform, colour, visibility, camera, skin, pose and texture pixels each advance only their own revision; the GPU re-uploads only what changed ([design policy §14](../design/DESIGN_POLICY.md#14-dirty-propagation)) |
| Geometry upload | ⚠️ | written into host-visible buffers on the render thread, not staged off-thread ([design policy §20](../design/DESIGN_POLICY.md#20-asset-upload)) |
| Texture upload | ⚠️ | RGBA8, sRGB or linear by role, staged and copied with blitted mipmaps at the start of the frame that first samples it, on the graphics queue, not off-thread ([design policy §20](../design/DESIGN_POLICY.md#20-asset-upload)); only when a texture's revision changes. `renderer.material.mtoon_textured` |
| Texture table | ⚠️ | 128 sampled-image entries and 9 wrap samplers in the material set, entry 0 a white placeholder; a fixed array, not bindless. Sampling is trilinear |
| Camera | ✅ | a `ToonView` in the OpenGL clip convention; the Vulkan backend flips y and maps z to [0, 1] |
| Lighting | ⚠️ | a stand-in in `mtoon_common.slang`: one white key light fixed to the camera and a uniform ambient; scene lights are not read. The rim's lighting mix reads the same light. Meshes without an MToon material draw flat, unlit |
| GPU skinning | ✅ | linear blend skinning in the vertex stage of every scene pipeline: a mesh's influences, uploaded when its skin changes, and its joint buffer, written when its pose changes and nothing else does ([design policy §11](../design/DESIGN_POLICY.md#11-animation-fast-path)); one skin descriptor set per skinned mesh. `renderer.skinning.gpu`; [renderer report 09](../reports/renderer/09-2026-09-27-gpu-skinning.md) |
| `ToonMaterial` | ✅ | material policy §4's common part and MToon block, with the base texture, the shade texture, the outline width texture, the MatCap texture and the rim multiply texture, each a texture id with glTF's wrap and texture transform; a value edit advances a material's parameters revision, a model, alpha-mode, double-sidedness or texture-identity edit its structure revision; CTest `toon-render-world` |
| Material parameter slots | ✅ | one 352-byte slot per material in a storage buffer, read by the vertex and fragment stages, with its texture table entries, samplers and texture transforms, written only when its parameters revision or a texture's entry changes ([material policy §7](../design/MATERIAL_POLICY.md#7-pipelines-and-parameter-buffers)); `renderer.material.mtoon_opaque`, `.mtoon_textured`, `.mtoon_outline` and `.mtoon_rim` require a value-only edit to rewrite one slot and nothing else |
| MToon opaque (`mtoon_opaque`) | ✅ | lit / shade colours with shading shift and toony, GI equalization, emission, cull mode from double-sidedness; the base colour texture and the shade multiply texture through the mesh's UVs; Opaque and Mask draw here, alpha 1; Mask cuts away a fragment whose base alpha × texture alpha is below the cutoff. `renderer.material.mtoon_opaque`, `.mtoon_textured`, `.mtoon_transparent`; renderer reports [07](../reports/renderer/07-2026-09-27-mtoon-opaque.md), [08](../reports/renderer/08-2026-09-27-basic-textures.md) |
| MToon rim | ✅ | in both MToon pipelines, added after emission: `matcapFactor` × the MatCap texture, sampled where the view-space normal points and nothing without the texture, plus the parametric rim `pow(saturate(1 − N·V + lift), fresnelPower)` × its colour, with V toward the eye from each fragment; both × the rim multiply texture through the mesh's UVs, then × lerp(1, key light + GI, `rimLightingMixFactor`). The MatCap frame takes the camera's up, as three-vrm does ⚠️. Every factor is a value in the slot. `renderer.material.mtoon_rim`; [renderer report 14](../reports/renderer/14-2026-09-28-mtoon-rim.md) |
| MToon Blend (`mtoon_transparent`) | ✅ | base alpha × texture alpha blended source-over, after every opaque and Mask draw, ordered by MToon's render queue (`transparentWithZWrite` 2501 + `renderQueueOffsetNumber` in [0, 9], otherwise 3000 + it in [−9, 0]) and within one queue as the draws are listed, not by distance; depth written only with `transparentWithZWrite`, as dynamic state. A double-sided surface draws its back faces, then its front faces; its hull draws after it, with its alpha. Queue and depth writes are values, so an edit rewrites one slot. `renderer.material.mtoon_transparent`; [renderer report 15](../reports/renderer/15-2026-09-28-mtoon-transparent.md) |
| MToon UV animation, emissive and shading shift textures, normal map | ⬜ | |
| Inverted-hull outline (`mtoon_outline`) | ✅ | drawn for every MToon draw whose material asks for an outline of some width, before an opaque surface and after a transparent one, whose alpha it blends by: the skinned vertex moves out along its view-space normal by `outlineWidthFactor` × the width texture's G, sampled at mip 0 in the vertex stage, as a world length or as a ratio of the screen height, a world length in stage units whatever the stage's `metersPerUnit` ⚠️; front faces culled whatever the double-sidedness; `outlineColorFactor` × the surface's shading by `outlineLightingMixFactor`. Width, mode, colour and mix are values in the slot, so an edit, or turning the outline off, rewrites one slot. `renderer.material.mtoon_outline`; [renderer report 10](../reports/renderer/10-2026-09-27-mtoon-outline.md) |
| Blend shapes | ⚠️ | a skinned mesh's `UsdSkel` blend shapes are applied to its rest points on the CPU, so a weight change uploads the points |
| GPU morphs, look-at | ⬜ | |
| Late motion latching | ⬜ | |
| MMD materials | ⬜ | |
| `UsdPreviewSurface` | ⬜ | |
| WebGPU backend | ⬜ | |
| Anti-aliasing | ⬜ | |
| Renderer telemetry | ⬜ | [design policy §24](../design/DESIGN_POLICY.md#24-profiling) |

## Hydra adapter (`hdToon`)

| Capability | Status | Evidence / notes |
| --- | --- | --- |
| Plugin discovery through `plugInfo.json` | ✅ | `renderer.plugin.discovery` |
| Render delegate creation | ✅ | `renderer.delegate.creation`; measured against OpenUSD 26.08 (`HD_API_VERSION` 98); the pre-98 branch compiles only in principle |
| CPU colour / depth / primId `HdRenderBuffer`s | ✅ | `renderer.render_buffer.cpu` |
| First frame and a stable update in `testusdview` | ✅ | `renderer.host.first_frame`, `.host.stable_update`: the smoke scene's mesh through its camera at the AOV's resolution; a points edit re-uploads points only, asserted from the frame evidence |
| Per-frame host evidence (`TOON_HYDRA_EVIDENCE`) | ✅ | one line per frame: completion, revision, extent, buffers written, pipelines, target allocations, uploads, material slot writes, texture uploads and textures, skin uploads and joint buffer writes, how many materials selected PreviewSurface and MToon, and how many draws selected MToon, went through `mtoon_transparent`, added `mtoon_outline`'s hull and were skinned; renderer reports [05](../reports/renderer/05-2026-09-26-vrm-usdview-session.md), [07](../reports/renderer/07-2026-09-27-mtoon-opaque.md), [08](../reports/renderer/08-2026-09-27-basic-textures.md), [09](../reports/renderer/09-2026-09-27-gpu-skinning.md), [10](../reports/renderer/10-2026-09-27-mtoon-outline.md), [15](../reports/renderer/15-2026-09-28-mtoon-transparent.md) |
| `mesh` | ✅ | points, topology (triangulated by `HdMeshUtil`, left-handed winding reversed), transform, visibility, constant `displayColor`, a vertex or varying `st` and material binding, each synced only when its dirty bit is set; a face-varying `st` is not read |
| `camera` | ✅ | view and projection read through `HdRenderPassState` |
| Skinned meshes (`UsdSkel` ext computations) | ✅ | a `classicLinear` mesh's aggregator computation becomes its skin, re-read only when the aggregator's inputs change, and its skinning computation's scene inputs its pose; on the avatar, 20 of 20 draws are skinned on the GPU. CTest `toon-renderer-hydra-skinning`; [renderer report 09](../reports/renderer/09-2026-09-27-gpu-skinning.md) |
| Dual quaternion skinning | ⚠️ | runs usdSkelImaging's CPU kernel during mesh sync, as HdEmbree does, and uploads the points |
| Colour and depth AOVs | ⚠️ | read back from the GPU every frame, waited for, and written bottom row first, as Hydra buffers are laid out; no Hgi interop |
| `primId`, `instanceId`, `elementId` AOVs | 🧪 | filled with -1; no picking |
| `material` | ✅ | selected by [material policy §3](../design/MATERIAL_POLICY.md#3-selection-a-realization-is-chosen-not-merged): `vrm/mtoon` on the prim, read from the terminal scene index in `Sync`, is MToon, and its `vrm/material` and `vrm/mtoon` values are normalized into `ToonMaterial`; anything else is PreviewSurface with the fallback's values. CTest `toon-renderer-hydra-material`; [renderer report 03](../reports/renderer/03-2026-09-26-material-sprim.md) |
| MToon textures | ✅ | `vrm/textureInfo/baseColor`, `shadeMultiply`, `matcap` and `rimMultiply`, as colour, and `outlineWidthMultiply`, as data: `file`'s resolved path decoded by `HioImage` (so a path inside a `.usdz` reads), `wrapS`, `wrapT` and `transform`; one texture per image and encoding, shared by every material that names it and removed with the last. A path that does not resolve samples nothing, and so does a `texCoord` other than 0 in a role sampled at the mesh's UVs; MatCap's is not read. The other six roles are not read. CTest `toon-renderer-hydra-material`; renderer reports [08](../reports/renderer/08-2026-09-27-basic-textures.md), [14](../reports/renderer/14-2026-09-28-mtoon-rim.md) |
| MMD selection, PreviewSurface network | ⬜ | a PreviewSurface material has the fallback's values |
| Value-only material changes (time samples) | ✅ | a `vrm` locator dirtied without `material` never reaches `Sync`; the delegate observes the terminal scene index and re-reads that material in `Update()` into the same slot. CTest `toon-renderer-hydra-material`; [renderer report 04](../reports/renderer/04-2026-09-26-material-value-route.md). An expression bake played in `testusdview` rewrites the slot once per time move and uploads nothing ([renderer report 11](../reports/renderer/11-2026-09-27-expression-bake.md)) |
| Material binding on meshes | ✅ | by path, resolved again when a material prim under it is created or destroyed; a mesh whose material selected MToon draws through `mtoon_opaque`, any other its display colour. On the avatar with `vrmImaging`, 20 of 20 draws are MToon ([renderer report 07](../reports/renderer/07-2026-09-27-mtoon-opaque.md)) |
| Render tags, instancers, framing data window | ⬜ | every visible mesh is drawn over the whole AOV |
| Lights | ⬜ | |

## Hosts

| Host | Status | How |
| --- | --- | --- |
| Headless runner (`toon-headless`) | ✅ | runs during `ost build`; writes `renderer-report.json` |
| Standalone viewport (`toon-viewport`) | 🧪 | `ost renderer viewport`; shows the bootstrap triangle scene only |
| `usdview` | ✅ | `testusdview` in CTest; in an `ost formation run` session with `vrmImaging`, VRM materials select MToon, with the host's Python supplied to the run ([renderer report 06](../reports/renderer/06-2026-09-27-vrm-formation.md)); `formations/vrm-host-session/` composes the published packages and its command checks it ([renderer report 13](../reports/renderer/13-2026-09-28-host-session-formation.md)); `ost renderer view` has not been run |
