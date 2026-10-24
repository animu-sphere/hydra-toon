---
status: proposed
owner: hydra-toon
---

# WebGPU backend policy

> The v0.7.0 WebGPU RFC, proposed on 2026-10-24. It expands
> [DESIGN_POLICY.md](DESIGN_POLICY.md) §4, §9 and §17–§19 for the second
> backend and wins over it on WebGPU. What the backend reproduces stays with
> the owners of those contracts: materials with
> [MATERIAL_POLICY.md](MATERIAL_POLICY.md), output with
> [RENDER_OUTPUT_POLICY.md](RENDER_OUTPUT_POLICY.md), shadows with
> [SHADOW_POLICY.md](SHADOW_POLICY.md). Placement follows
> [PROJECT_LAYOUT.md](../architecture/PROJECT_LAYOUT.md). Implementation
> status belongs to the [capability matrix](../reference/CAPABILITY_MATRIX.md);
> the milestone's remaining work is the [v0.7.0 roadmap](../roadmap/v0.7.0.md).
> The toolchain measurements behind this page are
> [report 90](../reports/renderer/90-2026-10-24-webgpu-toolchain-probe.md).

## 1. Purpose and decisions

The WebGPU backend realizes the renderer model that Vulkan settled through
v0.6.0 — materials, deformation, outline, shared lighting and shadows, and
reusable RGBA — from the same core extraction, on a second GPU API. It is not
a translation layer over Vulkan, and nothing it needs flows back into the
render world, extraction or the adapters (§3). Four decisions were taken by
the project on 2026-10-24 and shape the rest:

- **Dawn.** The backend is written against the standard `webgpu.h` C API as
  Dawn implements it. Dawn is Chrome's implementation and its Tint compiler
  validates WGSL strictly; on Windows it reaches the GPU through Direct3D 12,
  so parity is measured against a driver path independent of the Vulkan
  backend's (§2).
- **Native only.** v0.7.0 targets native Windows. A browser or WASM build is
  a separate validation target with its own host, not implied by this
  backend (§12).
- **Headless and the viewport.** The offscreen renderer and the standalone
  viewport gain a WebGPU choice. `hdToon` stays on Vulkan; Hydra reaches
  WebGPU parity through the same extracted scenes, not a second delegate
  backend (§8).
- **One bind group per material.** WebGPU's core has no arrays of texture
  bindings, so each material binds its own textures and samplers in fixed
  role slots instead of indexing the Vulkan backend's 128-entry table. No
  atlas and no bindless extension (§4).

## 2. Implementation and acquisition

- **Pinned release.** Dawn is consumed from a
  [published Dawn release](https://github.com/google/dawn/releases) of
  prebuilt Windows binaries, pinned by tag and archive SHA-256, through a
  pinned download like the viewport's GLFW and Dear ImGui. Its CMake package
  provides `dawn::webgpu_dawn`, a static library. Building Dawn from source is
  allowed for a local investigation, never required. A pin moves only with a
  report that reruns the parity fixtures (§9).
- **Opt-in build.** `TOON_ENABLE_WEBGPU` (default `OFF`) builds the backend;
  it requires the viewport or headless runner that consumes it. The `core`,
  `hydra` and `viewport-usd` intents and the published `toon` package do not
  change, and no Dawn binary enters the Formation. A `webgpu` build intent is
  added when the backend first builds (§10).
- **Dawn's adapter.** The default request is a high-performance Direct3D 12
  adapter. Dawn's Vulkan adapter may be selected for diagnosis; parity
  evidence names which one ran. The Direct3D 11 and null adapters are not
  used. Dawn's toggles stay at their defaults except where a report names a
  measured reason.
- **Licences.** Dawn is BSD-3-Clause; its licence and third-party notices
  install with any executable that links it, as `third_party` code already
  does.

## 3. Boundary and targets

- `backend/webgpu/` builds `toon-render-webgpu` (`Toon::WebGPU`). It links
  `Toon::RenderExtraction` and consumes the same `DrawList`, `FrameSnapshot`
  revisions, `ShadowFit` views and late-frame source as the Vulkan backend.
- The backend-neutral public types now declared in `vulkan_backend.hpp` and
  `vulkan_present.hpp` — `OffscreenRenderer`, `PresentSession`,
  `LateFrameSource`, `FrameLatency`, `RenderOptions`, `BackgroundSettings`,
  `ColorProduct`, `DepthProduct` and the statistics — move to a shared
  `include/toon/backend.hpp`, with the Vulkan headers including it so
  existing consumers keep compiling. Each backend keeps its own factory
  header (`webgpu_backend.hpp`). That move is a structural change and lands
  in PROJECT_LAYOUT first, in its own pull request.
- No `webgpu.h` or Dawn type crosses a public header, as no Vulkan type does;
  the core boundary check gains WebGPU and Dawn includes. Surfaces cross as
  an opaque native-window provider, as `PresentSurfaceProvider` does for
  `VkSurfaceKHR`.
- Shader sources move from `backend/vulkan/shaders/` to a root `shaders/`
  when the second backend first compiles them, as the layout already says.

## 4. Binding model

WebGPU has four bind groups by default, no push constants, and limits of
16 sampled textures, 16 samplers and 8 storage buffers per stage. The
backend's layout is grouped by update frequency:

| Group | Contents | Changes |
| --- | --- | --- |
| 0, frame | the frame buffer (lights, camera), the shadow array, shadow views, the shadow comparison/gather sampler | once per frame |
| 1, material | the model's parameter record, its role textures and their samplers | per material; rebuilt only on a texture or sampler edit |
| 2, deformation | influences, joints, morph offsets, ranges and weights | per resident mesh; rebound only when buffers are reallocated |
| 3, draw | `DrawConstants` at a dynamic offset in one per-frame uniform ring | per draw, by offset only |

- **Draw constants.** The Vulkan push-constant block becomes a dynamic-offset
  uniform binding with 256-byte alignment. Its fields and meanings do not
  change; the ring is written once per frame with the fast values (§6).
- **Role slots.** Each material family declares its texture roles once —
  MToon's base, shade, normal, emissive, rim multiply, MatCap, outline width,
  UV animation mask and shading shift; MMD's base, sphere and toon;
  PreviewSurface's channels — each with its own sampler binding. A role
  without a texture binds a shared 1 × 1 fallback, so the existing "entry 0
  means absent" flag remains the shader's test. The model's parameter
  records keep their layouts; only their texture entries stop being table
  indices on this backend.
- **Samplers.** A sampler is bound per role, never selected by a dynamic
  index, which WGSL forbids. The renderer's nine sampler states are created
  once and shared across bind groups.
- **Limits.** The backend requests the WebGPU defaults and fails creation
  with an explained `Skip` if an adapter offers less, as the Vulkan backend
  does for its device limits. Raising a limit is a measured decision.

The Vulkan backend's descriptor layout does not change for WebGPU's sake
([design policy §4](DESIGN_POLICY.md#4-backends)).

## 5. Shaders

- **One source, two bindings.** The Slang modules stay the shading source of
  truth. Resource declarations — the texture table and push constants on
  Vulkan, role bindings and the draw uniform on WebGPU — move into one small
  bindings module per backend, behind the existing `SampleTexture` and
  `DrawConstants` accessors. Shading functions, parameter records and
  material semantics stay shared.
- **WGSL generation.** `slangc -target wgsl`, the same `slangc` as SPIR-V,
  generates each entry point at build time. The build validates every
  generated module with the pinned Dawn release's `tint` before it is
  installed, so a WGSL error is a build failure, not a runtime one.
- **Measured gaps.** Generating today's shaders unchanged
  ([report 90](../reports/renderer/90-2026-10-24-webgpu-toolchain-probe.md))
  shows what the bindings module and explicit declarations must cover: the
  texture table, push constants, the composite pass's multisampled load, and
  MMD's vertex interfaces, which need explicit locations. Every other module
  validates once its draw constants are bound.
- **Uniformity.** WGSL requires implicit-derivative sampling in uniform
  control flow. A texture sample behind a material-dependent branch is
  restructured to sample unconditionally or with explicit gradients; the
  `derivative_uniformity` diagnostic is relaxed only for a sample whose
  derivatives a fixture shows to be correct, and a report names it.
- **Backend-specific shaders** are allowed where WGSL or the binding model
  would bend shared code, as design policy §9 already permits; each is listed
  in the capability matrix.

## 6. Frames, synchronization and upload

- **One frame in flight** by default, as on Vulkan
  ([§18](DESIGN_POLICY.md#18-frames-in-flight)). The renderer waits on the
  previous submission's completion future before reusing its per-frame
  buffers; it never blocks on the device otherwise.
- **Late latch.** The late-frame source is read after that wait and before
  `wgpuQueueWriteBuffer` writes the joint, morph-weight, material and draw
  values, then the command buffer is submitted. The `FrameLatency` fields
  keep their meanings; `present_returned` is the return of the surface
  present call.
- **Upload.** Structural uploads (geometry, textures, deformation buffers)
  use queue writes on revision changes only, as Vulkan's staging does; the
  existing upload counters report the same zero for a pose-only frame.
  Mipmaps are generated by a render or compute pass on this backend.
- **Readback.** Offscreen colour and depth are copied to mappable buffers and
  read after their map completes, giving the same `ColorProduct` and
  `DepthProduct`, origin top left, rows repacked to the products' pitch.
- **Timing.** GPU pass times use the `timestamp-query` feature where the
  adapter offers it; without it the timings report as unavailable, never as
  zero.
- **Device loss** fails the frame with Dawn's reason; recreating the device
  is host-integration work ([later](../roadmap/later.md#host-and-compositor-integration-v080-scale)).

## 7. Output semantics

The products keep [render output policy](RENDER_OUTPUT_POLICY.md) §3–§4
exactly: linear premultiplied RGBA in `rgba8unorm`, `depth32float`, the
background modes, and resolve before readback or presentation.

- **Samples.** Core WebGPU renders 1 or 4 samples. A request is clamped to
  the highest of those at or below it, as the Vulkan backend clamps to its
  device; 2x and 8x are Vulkan-only.
- **Mask coverage without alphaToOne.** WebGPU offers alpha-to-coverage but
  not alpha-to-one, which the Vulkan backend uses so a Mask sample's alpha is
  not counted twice. The WebGPU pipelines for multisampled Mask write the
  coverage themselves through `@builtin(sample_mask)` from the same cutoff
  ramp and output alpha 1, so one opaque sample in 4x over a transparent
  clear still resolves to alpha 0.25. Opaque and 1x output need neither.
- **Blend and outline** keep their draw order, blend factors and depth rules
  from [material policy](MATERIAL_POLICY.md) §5–§7; the outline's depth bias
  is a pipeline state on both backends.

## 8. Presentation and hosts

- **Viewport.** `toon-viewport --backend webgpu` creates a surface from the
  GLFW window's `HWND` and presents through the same composite and display
  transfer as Vulkan
  ([render output policy §5](RENDER_OUTPUT_POLICY.md#5-hydra-and-viewport-boundaries)).
  Present modes map as FIFO for vsync and mailbox or immediate otherwise,
  where the surface offers them; the overlay uses Dear ImGui's WebGPU
  backend at the same pinned version. Captures and foreground PNG export go
  through the same `capture.*` path.
- **Headless.** `toon-headless --backend webgpu` renders the same evidence
  run through `CreateWebGpuOffscreenRenderer`.
- **Choosing.** The Vulkan backend stays the default everywhere. The
  viewport's Hydra host keeps using Vulkan through `hdToon`; WebGPU's USD
  coverage comes from extracted scenes the headless runner and viewport
  replay (§9).
- **Not in v0.7.0.** A Hydra render setting choosing WebGPU, Hgi or GPU
  texture handoff to a host
  ([render output policy §7](RENDER_OUTPUT_POLICY.md#7-gpu-texture-handoff)).

## 9. Parity and evidence

Parity is semantic and visual, not pixel identity. Each canonical scene
renders on both backends, same machine, same extraction, at 1x and 4x:

- transparent and opaque output over each background mode, including Mask
  coverage and Blend silhouettes;
- MToon, MMD and PreviewSurface shading, outline hulls and their
  occlusion/frustum omission;
- authored, generated and deformed normals and tangents; skinning, morphs
  and evaluated late values;
- shared shadows across families with one to four views.

The comparison records, per scene and channel, the maximum and mean
absolute difference and the fraction of pixels above a threshold, plus
exact checks where a quantity is defined exactly: alpha of a known coverage,
an unchanged image across a pose-only frame, upload counters, draw and hull
counts. Thresholds come from the first measured scenes (WGPU-Q1); a
difference above one is explained or fixed, not absorbed. Performance
evidence is CPU frame time and GPU pass time with p95 and p99 on the
bounded avatar and `shadow-cost` fixtures
([design policy §23](DESIGN_POLICY.md#23-performance-kpis)), beside the
Vulkan numbers from the same run. WebGPU need not match Vulkan's speed; a
large gap is reported with its pass.

## 10. Stages

1. **Foundation.** Shared backend header, `Toon::WebGPU`, the Dawn pin and
   `webgpu` intent; device, queue and offscreen targets; shaders at the root
   with generated, Tint-validated WGSL; the unlit mesh pipeline and
   PreviewSurface through `toon-headless --backend webgpu`; the first parity
   comparisons and the WGPU-Q1 thresholds.
2. **Materials and deformation.** Per-material bind groups; MToon and MMD
   opaque, transparent and outline pipelines; skinning and morphs; late
   latch; 4x with sample-mask Mask coverage; background modes.
3. **Shadows and presentation.** The shared caster pass and receivers; the
   viewport's surface, composite, overlay, capture and timing.
4. **Acceptance.** The §9 scenes, cross-family shadows, avatar and
   `shadow-cost` p95/p99, and the done-when condition of the
   [v0.7.0 milestone](../roadmap/v0.7.0.md).

## 11. Open questions

| ID | Question | Proposal |
| --- | --- | --- |
| WGPU-Q1 | Visual parity thresholds per scene class | Measure in stage 1 on the unlit and PreviewSurface fixtures, then fix per-class limits on max and fraction-above values before stage 2's material scenes |
| WGPU-Q2 | Whether the bindings module is selected by a preprocessor define or by Slang link-time modules | The simpler of the two that keeps `slangc` one invocation per entry point; decided with the first moved shader |
| WGPU-Q3 | How often the Dawn pin moves | Only for a fix or feature the backend needs, each with a parity rerun report; no tracking of Dawn's daily releases |

## 12. Outside this policy

Browser, WASM and Emscripten builds; WebGPU compatibility mode; a Hydra or
Hgi WebGPU path; GPU texture handoff; Metal; bindless or binding-array
extensions; Linux and other vendors' adapters, which follow
[platform coverage](../roadmap/later.md#platform-and-gpu-coverage). Each is
taken up by its own trigger in the [later roadmap](../roadmap/later.md).
