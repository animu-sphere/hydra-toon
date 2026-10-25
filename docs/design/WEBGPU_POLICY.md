---
status: accepted
owner: hydra-toon
---

# WebGPU backend policy

> The v0.7.0 WebGPU RFC, accepted on 2026-10-24. It expands
> [DESIGN_POLICY.md](DESIGN_POLICY.md) §4, §9 and §17–§19 for the second
> backend and wins over it on WebGPU. What the backend reproduces stays with
> the owners of those contracts: materials with
> [MATERIAL_POLICY.md](MATERIAL_POLICY.md), output with
> [RENDER_OUTPUT_POLICY.md](RENDER_OUTPUT_POLICY.md), shadows with
> [SHADOW_POLICY.md](SHADOW_POLICY.md). Placement follows
> [PROJECT_LAYOUT.md](../architecture/PROJECT_LAYOUT.md). Implementation
> status belongs to the [capability matrix](../reference/CAPABILITY_MATRIX.md);
> the milestone's acceptance is recorded in the [v0.7.0 release record](../releases/v0.7.0.md).
> The toolchain measurements behind this page are
> [report 90](../reports/renderer/90-2026-10-24-webgpu-toolchain-probe.md);
> stage 1 and the settled WGPU-Q1 and WGPU-Q2 are
> [report 91](../reports/renderer/91-2026-10-24-webgpu-stage-1.md),
> stage 2 and WGPU-Q1's multisampled classes
> [report 92](../reports/renderer/92-2026-10-25-webgpu-stage-2.md), and
> stage 3, shadows and presentation,
> [report 93](../reports/renderer/93-2026-10-25-webgpu-stage-3.md), and
> stage 4, acceptance,
> [report 94](../reports/renderer/94-2026-10-25-webgpu-acceptance.md).

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
  added when the backend first builds (§10), and a `webgpu-usd` intent adds
  the Hydra adapter for USD stages in the WebGPU viewport (stage 4).
- **Dawn's adapter.** The default request is a high-performance Direct3D 12
  adapter. Dawn's Vulkan adapter may be selected for diagnosis; parity
  evidence names which one ran. The Direct3D 11 and null adapters are not
  used. Dawn's toggles stay at their defaults except where a report names a
  measured reason: a device that times passes disables
  `timestamp_quantization`, which rounds timestamps to 2^16 ns
  ([report 93](../reports/renderer/93-2026-10-25-webgpu-stage-3.md)).
- **Licences.** Dawn is BSD-3-Clause; its licence and third-party notices
  install with any executable that links it, as `third_party` code already
  does. `third_party/dawn/README.md` lists what the static library carries
  (Abseil, SPIRV-Tools and -Headers, Vulkan-Headers and -Utility-Libraries,
  PartitionAlloc), each licence copied at the revision Dawn's `DEPS` pins;
  a pin change rechecks the list.
- **DXC.** Dawn's Direct3D 12 device compiles its shaders with DXC, which
  it loads at run time as `dxcompiler.dll` from the executable's directory
  and which the Dawn release does not carry. A published
  [DXC release](https://github.com/microsoft/DirectXShaderCompiler/releases)
  is pinned the same way, by tag and archive SHA-256, and only its
  `dxcompiler.dll` is copied beside the hosts and installed, with its LLVM
  and MIT licences; Microsoft's `dxil.dll` is neither needed nor
  redistributed. Its pin moves with a parity rerun, as Dawn's does.

## 3. Boundary and targets

- `backend/webgpu/` builds `toon-render-webgpu` (`Toon::WebGPU`). It links
  `Toon::RenderExtraction` and consumes the same `DrawList`, `FrameSnapshot`
  revisions, `ShadowFit` views and late-frame source as the Vulkan backend.
- The backend-neutral public types — `OffscreenRenderer`, `PresentSession`,
  `LateFrameSource`, `FrameLatency`, `RenderOptions`, `BackgroundSettings`,
  `ColorProduct`, `DepthProduct` and the statistics — are declared in the
  shared `include/toon/backend.hpp`, which the Vulkan headers include, as
  [PROJECT_LAYOUT §4](../architecture/PROJECT_LAYOUT.md#4-dependency-directions)
  places them. Each backend keeps its own factory header
  (`webgpu_backend.hpp`).
- No `webgpu.h` or Dawn type crosses a public header, as no Vulkan type does;
  the core boundary check gains WebGPU and Dawn includes. Surfaces cross as
  an opaque native-window provider, as `PresentSurfaceProvider` does for
  `VkSurfaceKHR`.
- Shader sources live in the root `shaders/`, by role: `bindings/`,
  `common/`, `scene/`, `preview/`, `mtoon/`, `mmd/`, `present/`, and
  `webgpu/` for this backend's own modules. A module keeps its basename,
  which names its compiled artifacts on either backend.

## 4. Binding model

WebGPU has four bind groups by default, no push constants, and limits of
16 sampled textures, 16 samplers and 8 storage buffers per stage. The
backend's layout is grouped by update frequency:

| Group | Contents | Changes |
| --- | --- | --- |
| 0, frame | the frame buffer (lights, camera), the shadow array, shadow views, the shadow comparison/gather sampler | once per frame; rebound when the shadow array is rebuilt |
| 1, material | the model's parameter record, its role textures and their samplers | per material; rebuilt only on a texture or sampler edit |
| 2, deformation | influences, joints, morph offsets, ranges and weights | per resident mesh; rebound only when buffers are reallocated |
| 3, draw | `DrawConstants` at a dynamic offset in one per-frame uniform ring | per draw, by offset only |

- **Caster pass.** A pass drawing into a layer of the shadow array cannot
  bind that array, so the caster pipeline's group 0 holds the frame buffer
  alone. Casters draw with the view's y flipped, so each layer holds the
  rows the Vulkan backend's does and the receivers read both alike
  ([report 93](../reports/renderer/93-2026-10-25-webgpu-stage-3.md)).
- **Draw constants.** The Vulkan push-constant block becomes a dynamic-offset
  uniform binding with 256-byte alignment. Its fields and meanings do not
  change; the ring is written once per frame with the fast values (§6).
- **Role slots.** Each material family declares its texture roles once, as
  constants of its common module —
  MToon's base, shade, normal, emissive, rim multiply, MatCap, outline width,
  UV animation mask and shading shift; MMD's base, sphere and toon;
  PreviewSurface's channels — each with its own sampler binding. A role
  without a texture binds a shared 1 × 1 fallback, so the existing "entry 0
  means absent" flag remains the shader's test. The model's parameter
  records keep their layouts; only their texture entries stop being table
  indices on this backend, where a present texture's entry is 1. Group 1
  has nine role slots, the most a model declares (MToon's), role r's
  texture at binding 3 + r and its sampler at 12 + r, and each model's
  buffer at its own binding (0 MToon, 1 MMD, 2 PreviewSurface), so the
  caster pass can read two models' slots.
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
  Vulkan, role bindings and the draw uniform on WebGPU — live in one small
  bindings module per backend, `shaders/bindings/bindings_vulkan.slang` and
  `bindings_webgpu.slang`, which `bindings.slang` selects by the
  `TOON_WEBGPU` define (WGPU-Q2). Each states where every buffer binds, the
  draw constants' binding, and the accessors the shared modules sample
  through: `SampleRole` and `SampleRoleLevel` (a role, its entry and the
  Vulkan sampler index) and `GatherShadow`. Shading functions, parameter
  records and material semantics stay shared.
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
- **Interfaces.** WGSL generation takes a semantic's nonzero index as its
  location and fills the rest in declaration order, which SPIR-V generation
  uses throughout, so a shared stage interface carries each place after the
  first in its semantic (`NORMAL1`, `TEXCOORD2`), and a struct with
  semantics is not an ordinary function's parameter or result
  ([report 92](../reports/renderer/92-2026-10-25-webgpu-stage-2.md)).
- **Derivatives.** Explicit derivatives go through the bindings'
  `DerivativeX`, `DerivativeY` and `DerivativeWidth`: Vulkan's plain ones,
  as its driver takes them, and WebGPU's fine ones, since Dawn's Direct3D 12
  device takes WGSL's plain ones coarse where the Vulkan reference driver
  takes them fine.
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
  Mipmaps are generated on this backend by a render pass per level
  (`shaders/webgpu/mipmap.slang`), each drawn from the level above, an sRGB
  texture's through sRGB views so the filter averages linear values, as a
  blit between sRGB images does.
- **Readback.** Offscreen colour and depth are copied to mappable buffers and
  read after their map completes, giving the same `ColorProduct` and
  `DepthProduct`, origin top left, rows repacked to the products' pitch.
- **Timing.** GPU pass times use the `timestamp-query` feature where the
  adapter offers it; without it the timings report as unavailable, never as
  zero. Core WebGPU writes timestamps only as a pass begins and ends, and
  neither of Dawn's adapters offers its experimental timestamps inside
  passes with the default toggles, so the present session times uploads, shadow views, the
  scene pass whole, composite, capture and overlay; the scene's parts and
  the image wait report as unmeasured (`PresentStatistics::gpu_parts_timed`)
  ([report 93](../reports/renderer/93-2026-10-25-webgpu-stage-3.md)).
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
  How many samples a share keeps, and which, is the implementation's under
  alpha to coverage; `webgpu/mtoon_coverage.slang` keeps those the Vulkan
  backend's reference driver keeps, its count, 2 x 2 dither and sample
  order as measured in
  [report 92](../reports/renderer/92-2026-10-25-webgpu-stage-2.md), and the
  parity run's coverage probe measures them again on another driver.
- **Depth resolve.** WebGPU resolves colour as a pass ends but not depth;
  a multisampled frame's depth product is each pixel's sample 0, written by
  a resolve pass (`webgpu/depth_resolve.slang`), as the Vulkan backend's
  sample-zero resolve gives it.
- **Blend and outline** keep their draw order, blend factors and depth rules
  from [material policy](MATERIAL_POLICY.md) §5–§7; the outline's depth bias
  is a pipeline state on both backends.

## 8. Presentation and hosts

- **Viewport.** `toon-viewport --backend webgpu` creates a surface from the
  GLFW window's `HWND` and presents through the same composite and display
  transfer as Vulkan
  ([render output policy §5](RENDER_OUTPUT_POLICY.md#5-hydra-and-viewport-boundaries)).
  Present modes map as FIFO for vsync and immediate or mailbox otherwise,
  where the surface offers them. The surface takes its sRGB form as a view
  format where it offers only the UNORM one. The overlay is the host's
  `OverlayDrawList`, shadow-view previews included, which the backend draws
  with its own pass as the Vulkan backend does, so no Dear ImGui renderer
  backend is compiled (corrected in
  [report 93](../reports/renderer/93-2026-10-25-webgpu-stage-3.md)).
  Captures and foreground PNG export go through the same `capture.*` path.
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
   [v0.7.0 milestone](../releases/v0.7.0.md).

## 11. Open questions

| ID | Question | Proposal |
| --- | --- | --- |
| WGPU-Q1 | Visual parity thresholds per scene class | Settled 2026-10-24 ([report 91](../reports/renderer/91-2026-10-24-webgpu-stage-1.md)): unlit output exact; lit and textured output at most one 8-bit step apart, with no pixel more than one step apart; coverage identical and depth within 1e-6. Measured exact on every stage 1 scene but one textured pixel at one step, through Dawn's Direct3D 12 and Vulkan adapters. A later class is measured against these and gets a limit of its own only with a report that explains the difference. MToon and MMD hold them at 1x and 4x ([report 92](../reports/renderer/92-2026-10-25-webgpu-stage-2.md)) but for two multisampled classes and the hulls' depth: at 4x a scene with outline hulls at most two steps on at most 2e-4 of its pixels, a sample at a thin hull's edge falling on its other side; MToon Mask at most one sample (64 steps) on at most 2e-4 of its pixels, a share on a coverage threshold, coverage identical; a hull's depth within 5e-6, its bias's slope term being the driver's. Shadows measured in stage 3 ([report 93](../reports/renderer/93-2026-10-25-webgpu-stage-3.md)): six scenes of one to four views across every family, a Mask caster and a skinned one, identical at 1x through either adapter and at 4x within one step, depth within 1.8e-7, but for the Mask class, whose share on a threshold can keep sample 0, the second kept, so that class lets sample 0's coverage differ on at most 2e-4 of its pixels. USD stages in the viewport measured in stage 4 ([report 94](../reports/renderer/94-2026-10-25-webgpu-acceptance.md)): a presented or exported image is compared in the linear steps of the scene target it was encoded from; twenty committed stages hold the lit and textured limits at 1x and 4x. Dense avatars through Dawn's Direct3D 12 adapter differ on mesh and hull edges, a pixel centre or sample on an edge falling on the other side (at most 4e-4 of their pixels at 4x, a few pixels at 1x), and are byte-identical through its Vulkan adapter; recorded, not limited, as they cannot be committed |
| WGPU-Q2 | Whether the bindings module is selected by a preprocessor define or by Slang link-time modules | Settled 2026-10-24: the `TOON_WEBGPU` preprocessor define selects `bindings_webgpu.slang`; `slangc` stays one invocation per entry point, and nothing needed link-time modules |
| WGPU-Q3 | How often the Dawn pin moves | Only for a fix or feature the backend needs, each with a parity rerun report; no tracking of Dawn's daily releases |

## 12. Outside this policy

Browser, WASM and Emscripten builds; WebGPU compatibility mode; a Hydra or
Hgi WebGPU path; GPU texture handoff; Metal; bindless or binding-array
extensions; Linux and other vendors' adapters, which follow
[platform coverage](../roadmap/later.md#platform-and-gpu-coverage). Each is
taken up by its own trigger in the [later roadmap](../roadmap/later.md).
