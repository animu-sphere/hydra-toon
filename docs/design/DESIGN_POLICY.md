---
status: accepted
owner: hydra-toon
---

# hydra-toon — design policy

> This is the canonical, long-form policy: what the renderer is for, how it is
> shaped, and the order it is built in. It says what should be, never what is:
> what is implemented is
> [reference/CAPABILITY_MATRIX.md](../reference/CAPABILITY_MATRIX.md). It is
> distilled from the 2026-09-26 implementation policy, whose section numbers it
> keeps so either can be cited by number, and revised by the 2026-09-28
> direction, which brought the dedicated viewport forward and put platform
> coverage after the renderer. Where this repository departs from the
> implementation policy, §30 records it; §31 onward are this repository's own.
> Two focused documents own the detail of one area each, and **on its own area
> the focused document wins**:
>
> | Area | Owning document |
> | --- | --- |
> | What this repository owns, and what it consumes from its siblings | [INTEGRATION_SCOPE_POLICY.md](INTEGRATION_SCOPE_POLICY.md) |
> | Consuming MToon, MMD and PreviewSurface materials; `ToonMaterial`; pipelines | [MATERIAL_POLICY.md](MATERIAL_POLICY.md) |
> | Targets, directories and dependency directions | [architecture/PROJECT_LAYOUT.md](../architecture/PROJECT_LAYOUT.md) |

---

## 1. Purpose

`hydra-toon` is a **low-latency, responsiveness-first Hydra raster renderer**
for VRM / MToon and MMD looks. It is not merely "a renderer that supports a
toon shader". Its primary uses are:

- VRM and MMD avatar display;
- VTuber and streaming applications;
- real-time motion from MediaPipe, mocap, OpenXR, WebXR and similar sources;
- integration with `usd-motion-plugins`, `motion-connectors` and
  `usd-avatar-runtime`;
- digital characters whose pose, expression and look-at change at high
  frequency;
- vendor-neutral real-time rendering on Vulkan and WebGPU.

The most important measure is not maximum FPS. It is **end-to-end latency from
input to display, and the stability of frame time**. What the renderer weighs,
in order:

1. the VRM and MMD looks, reproduced faithfully and stably;
2. low latency from a pose, expression, look-at or camera change to the
   screen;
3. stable frame time and presentation;
4. a renderer core that does not lean on Hydra.

## 2. Core concept

> **hydra-toon is a low-latency, avatar-first Hydra raster renderer optimized
> for continuously changing animation rather than continuously changing
> scenes.**

In an ordinary scene, geometry, materials and topology change rarely. For an
avatar, these change every frame:

- skeleton pose;
- morph targets / blend shapes;
- facial expression;
- look-at;
- camera;
- some material parameters.

So the founding rule is: **separate static scene state from high-frequency
motion state.**

```text
                 slow path
USD / Hydra ─────────────────→ Scene / Geometry / Material / Texture /
                               Topology / Skeleton definition

                 fast path
MotionPose, expression, look-at,
camera, small material values ─→ Skeleton / Morph / Parameters
                                    │
                                    │ late latch
                                    ▼
                                GPU submit
```

This separation is never given up for a feature.

## 3. Relationship with hydra-merlin

`hydra-toon` and [`hydra-merlin`](https://github.com/animu-sphere/hydra-merlin)
are not forced into a shared core.

| | `hydra-merlin` | `hydra-toon` |
| --- | --- | --- |
| Purpose | general USD rasterization; MaterialX / PreviewSurface / OpenPBR; ordinary USD scenes | avatar-first; MToon / MMD look; high-frequency animation; low motion-to-photon latency; VTuber and real-time characters |

`hydra-merlin` is a reference for implementation technique, not a
dependency: Vulkan device and resource management, the Slang build, CMake
utilities, CI and packaging, Hydra integration across OpenUSD versions, GPU
profiling, presentation, and Hgi / Vulkan interop. Designed separately here:
the renderer core, the material system, the animation fast path, draw
scheduling, the latency architecture, the toon outline, and everything
avatar-specific. What is shared is a small utility, and only once both need
it; the renderer core and the GPU execution architecture are never shared.

## 4. Backends

Initial scope is exactly two backends:

1. **Vulkan** — the reference backend. It is finished first and is the
   baseline for the low-latency design: Vulkan 1.3 with dynamic rendering,
   Synchronization2 and timeline semaphores, explicit resource lifetime,
   explicit staging and upload, and explicit control of presentation. Native
   Vulkan performance comes first.
2. **WebGPU** — not treated as a full abstraction of Vulkan. The renderer
   architecture and shader logic are shared; each backend has its own
   implementation. The Vulkan backend is never limited to what WebGPU can do.

WebGPU is not a copy of the Vulkan implementation but another realization of
the same renderer model, and backend-specific needs never flow back into the
render world or the Hydra adapter. Metal is out of scope until a need for it is
clear, and is re-evaluated then. OpenGL is not pursued as a new backend.

Linux, and Vulkan validation across vendors (AMD and Intel as well as
NVIDIA), matter, but they come after the renderer: its architecture and
quality are settled on Windows and the main development GPU first, and
platform coverage is then taken up as one piece of work (§25).

Where the backends live in this repository is
[PROJECT_LAYOUT.md](../architecture/PROJECT_LAYOUT.md); the implementation
policy's sketch (`renderer/`, `backend/`) is mapped onto the OpenStrata
renderer layout there (§30).

## 5. Hydra integration

Hydra is the **scene integration layer**. The renderer architecture as a whole
does not depend on Hydra or Hgi.

```text
USD Stage → Hydra / Scene Index → hydra-toon adapter → hydra-toon renderer core → Vulkan | WebGPU
```

- **`HdRenderDelegate` first.** The initial adapter is an `HdRenderDelegate`.
  The renderer core stays separable from Hydra API so that a later move to the
  Hydra 2.0 renderer interface replaces the adapter, not the core.

  ```text
  HdRenderDelegate adapter ──→ ToonScene ←── future renderer adapter
  ```

- **Scene indices are used deliberately** to normalize input just before the
  renderer — MToon API schema, MMD material and `UsdPreviewSurface` each
  become a `ToonMaterial` ([MATERIAL_POLICY.md](MATERIAL_POLICY.md)). The
  renderer core does not handle USD-, VRM- or MMD-specific structure directly.
- **The adapter stays thin.** It turns Hydra's scene into the core's scene
  data and nothing more: no Vulkan resources or their lifetime, no shaders or
  pipelines, no backend scheduling. A Hydra API change is absorbed in the
  adapter.

## 6. Relationship with Hgi

Hgi is not the centre of the renderer. HgiVulkan is a reference for
implementation and design, but a stack of

```text
Hydra → Hgi → hydra-toon abstraction → Vulkan
```

is avoided. The shape is `Hydra → hydra-toon renderer core → native Vulkan |
WebGPU`, and backend-specific code is allowed wherever explicit low-level GPU
control is needed.

Reading the Hydra AOVs back into CPU `HdRenderBuffer`s is acceptable for
integration correctness, but it is not a premise of the architecture. The
candidates to replace it are Hgi interop, a Vulkan image handoff, a shared
GPU texture, and the renderer-native presentation path. Latency is judged on
the renderer-native path, in the dedicated viewport (§31), not through a
Hydra host.

## 7. Renderer core

Between Hydra classes and GPU-API classes the core keeps a small, explicit
internal data model. Candidates:

```cpp
ToonRenderer  ToonScene  ToonView  ToonMesh  ToonMaterial
ToonSkeleton  ToonTexture  ToonLight  DrawPacket  RenderGraph
```

The core's public headers carry no OpenUSD, Hydra, Vulkan or WebGPU type; the
scaffold enforces that for OpenUSD, Hydra and Vulkan with a CTest boundary
check. Where each candidate lives is
[PROJECT_LAYOUT.md](../architecture/PROJECT_LAYOUT.md) §3.

## 8. Material architecture

Initial materials: **MToon**, **MMD-style**, **UsdPreviewSurface**. Inside the
renderer they normalize to one representation, selected by a shading model:

```cpp
enum class ToonShadingModel { PreviewSurface, MToon, MMD };
```

Per-material shader compilation is avoided. The unit is
`shader / pipeline + material parameter buffer + texture indices`, with a small
fixed pipeline set:

```text
mtoon_opaque  mtoon_transparent  mtoon_outline
mmd_opaque    mmd_transparent    mmd_outline
preview_surface
```

No permutation explosion. The detail — what is read from which schema, and
what stays model-specific — is [MATERIAL_POLICY.md](MATERIAL_POLICY.md).

## 9. Slang

Slang is the shader source language, compiled to SPIR-V for Vulkan and WGSL
for WebGPU. The shared subset is intentionally small: vertex, fragment and
compute shaders only. Geometry shaders, tessellation and mesh shaders are not
used, so both backends stay stable. One source for both backends is a means,
not a goal: where WGSL generation, WebGPU's restrictions or its binding model
would bend the architecture, a backend-specific shader is allowed.

## 10. Outline

The initial outline is **inverted hull**:

```text
Pass 1: Outline       vertex += normal * outlineWidth; front-face culling
Pass 2: Toon surface
```

It is easy on both backends, needs no geometry shader, suits MToon, and has a
predictable GPU cost. A transparent material's hull is the exception to the
order: it draws after its own surface
([MATERIAL_POLICY.md](MATERIAL_POLICY.md) §6). Later the mode may become selectable
(`None | InvertedHull | ScreenSpace`), but only after the inverted hull's width
stability, aliasing and cost are finished; a second method does not come first.
For v0.2.0, finishing it means: a stable width in world and screen units;
thin-outline sampling and motion measured against finite supersampling,
with 4x MSAA accepted as the practical baseline and its remaining subpixel
variation recorded as a known limitation; no depth conflict with the
surface it outlines; the width texture sampled correctly; no hull drawn that
cannot be seen; and its draw and GPU cost measured. Anti-aliasing is part of
outline quality (§16), and both are judged in the dedicated viewport (§31).
Eliminating residual aliasing and flicker requires further anti-aliasing
work; v0.2.0 does not promise temporal filtering or an alias-free silhouette.
The MToon outline *semantics* (`outlineWidthMode` and friends) are
the avatar's request, not a rendering instruction
([MATERIAL_POLICY.md](MATERIAL_POLICY.md) §5).

## 11. Animation fast path

Skeleton and morph updates are **the renderer's most important hot path**, not
a special feature. Joint matrices, morph weights, expressions and look-at
change every frame; updating them never rebuilds topology or GPU vertex
resources.

```text
MotionPose → joint matrices → GPU buffer update → skinning
```

Skinning starts in a vertex or compute shader.

| Forbidden | Target |
| --- | --- |
| pose changed → mesh dirty → vertex buffer rebuild → draw packet rebuild | pose changed → skeleton buffer update, and nothing else |

## 12. Late motion latching

The latest `MotionPose`, expression, look-at and camera are written to GPU
resources **after** the ordinary Hydra scene sync, immediately before submit:

```text
Hydra scene sync → render world snapshot → draw list extraction
        │
latest MotionPose, expression, look-at, camera
        → late motion latch → joint / morph / camera buffers → GPU submit
```

This shortens `tracking → motion processing → pose → GPU → display`, which
matters most for MediaPipe, mocap, controller and XR input. The boundary the
latest pose crosses is shaped so `usd-motion-plugins`' `MotionPose` can feed
it directly (§13). The end goal is to trace one motion sample from when it
was produced to when it reached the display (§24).

## 13. USD state and real-time state are separate

Turning real-time input into many `UsdAttribute::Set()` calls per frame is not
the primary path.

```text
USD stage      persistent / structural state
MotionStream   transient real-time state
```

```text
MediaPipe → motion-connectors → MotionStream / MotionPose → hydra-toon real-time override
```

Recording that stream as USD animation is a separate recording layer's job.
`MotionPose` and `MotionStream` are defined by `usd-motion-plugins`, not here
([INTEGRATION_SCOPE_POLICY.md](INTEGRATION_SCOPE_POLICY.md) §3).

## 14. Dirty propagation

Dirty propagation is designed exactly, because it decides performance:

| Change | Work |
| --- | --- |
| camera | camera buffer only |
| pose | skeleton buffer only |
| expression | morph buffer only |
| material parameter | material buffer only |
| texture | texture / descriptor only |
| topology | mesh rebuild |

High frequency: pose, morph, expression, look-at, camera. Low frequency: mesh
topology, material graph structure, texture topology, skeleton topology.

## 15. Persistent draw packets

Draw command representation is not rebuilt every frame. `Mesh → DrawPacket`
happens at load and is cached; a normal frame is `pose update → skeleton
buffer update → existing DrawPacket`. A packet is rebuilt only when its
material, geometry or pipeline changes.

## 16. Render pipeline

The initial render graph is small:

```text
optional shadow → opaque toon → outline → transparent → composite / post → present
```

It exists for resource lifetime, dependencies, barriers, transient resources
and pass ordering — not as a game-engine graph compiler.

**Anti-aliasing is MSAA**, 4 samples per pixel by default, resolved as the
scene pass ends: colour by averaging, depth by taking sample 0. The samples
are transient and never leave the pass. A Mask material turns its alpha
into coverage (alpha to coverage), its cut spread over the pixel the alpha
takes to cross the cutoff, so a cutout's edge is anti-aliased like a
silhouette; a hull cuts a Mask fragment away whole, since it blends by the
same alpha. The count is fixed for a renderer's life: another count is
another renderer, which uploads the scene again, so it is a setting, not a
per-frame choice. One sample turns anti-aliasing off.

What aliases most in a toon renderer is geometric — silhouettes, the
inverted hull's edge, hair strands and eyelashes, facial features — and
MSAA resolves exactly that, with no history. MSAA is therefore the baseline:
it is evaluated first, in the dedicated viewport (§31), and another method
is considered only after that evaluation shows aliasing MSAA leaves, and
only as an addition to it, knowing what it costs:

- **TAA** resolves shading as well, but ghosts under exactly what an avatar
  does: fast motion, pose changes, hair and facial motion, and an outline
  that moves with them. It needs motion vectors for skinned and morphed
  geometry, so the previous frame's joints and weights, and its jitter and
  history are at odds with late motion latching (§12), which wants what is
  on screen to be the latest sample, not a blend with the last frames.
- **FXAA or SMAA** is cheap, but a post-process filter sees only the
  resolved image: it cannot recover a hull or a strand thinner than a pixel,
  which MSAA covers by its samples, and it softens the texture line art a
  toon material is drawn with.

## 17. Vulkan frame architecture

```text
CPU                                   GPU
 ├─ consume Hydra dirty state          ├─ optional compute skinning
 ├─ update persistent scene data       ├─ shadow
 ├─ update material / camera buffers   ├─ opaque toon
 ├─ late-latch the latest pose         ├─ outline
 ├─ build or reuse draw packets        ├─ transparent
 └─ submit                             ├─ composite
                                       └─ present
```

## 18. Frames in flight

Low-latency mode uses **one** frame in flight. Two are allowed only when
throughput is preferred. Three or more are not used: for avatars they add
input latency. Modes may later be named `LowLatency`, `Balanced`,
`Throughput`.

## 19. CPU / GPU synchronization

`vkDeviceWaitIdle` and `vkQueueWaitIdle` are not used in an ordinary frame.
Synchronization is timeline semaphores, Synchronization2, per-frame fences and
explicit resource ownership and barriers. The CPU does not wait for the GPU,
and the GPU does not wait for the CPU, without need.

## 20. Asset upload

Geometry, texture and morph-target upload are off the render thread:

```text
I/O thread → decode / transcode → staging → transfer queue → device-local resource
```

The graphics queue stays on rendering. An asset that has not loaded draws with
a placeholder, and the reference switches when it arrives. The asset loading
path and the playback fast path are kept apart, so loading an asset never
stalls an avatar that is playing.

## 21. Memory allocation

Steady-state rendering allocates as close to nothing as possible, CPU or GPU:
persistent buffers, ring buffers, frame arenas, descriptor reuse, object pools,
cached pipelines, cached draw packets. No per-frame `new` / `delete` and no
large container rebuilds.

## 22. GPU-driven rendering

Not required initially: meshlets, visibility buffer, GPU culling, full
indirect rendering, bindless everything. The main case is a few characters,
some hundreds of thousands of polygons and tens to hundreds of materials, for
which CPU-side persistent draw packets should be enough. GPU-driven techniques
come after a measurement shows the need.

Textures are bound through a fixed texture table, and it is not made bindless
early: while MToon and VRM fit in it, it stays. Descriptor indexing, a
bindless texture table and how the backends abstract it are re-evaluated when
MMD, PreviewSurface or a larger scene runs into the table's limits.

## 23. Performance KPIs

Maximum FPS is not the measure. In order of priority:

1. motion-to-photon latency;
2. stable frame time;
3. per-frame CPU cost;
4. no unnecessary upload;
5. GPU frame cost;
6. peak throughput.

- **Renderer latency:** pose update → GPU buffer write → submit → present;
  expression update → submit; camera update → present; CPU render-thread
  time, GPU frame time.
- **Frame consistency:** p50 / p95 / p99 frame time and its variance, hitch
  count, shader compilation stalls, upload stalls.
- **Update cost:** joint-buffer writes, morph updates, material parameter
  updates.

Initial targets, revised through benchmarks rather than fixed:

| Metric | Target |
| --- | ---: |
| CPU render thread | < 2 ms |
| avatar GPU rendering | 2–6 ms |
| pose → submit | < 4 ms |
| frames in flight | 1 |
| steady-state allocation | ≈ 0 |
| unexpected pipeline compile | 0 |
| full-device stall | 0 |

## 24. Profiling

The renderer carries its own telemetry so regressions are caught early. At
least: Hydra sync, scene update, pose update, draw preparation, CPU submit,
GPU pass time, present interval. GPU markers: skinning, shadow, opaque,
outline, transparent, composite. JSON, Chrome Trace or Tracy export may
follow. The dedicated viewport (§31) is where telemetry is shown and checked
first.

## 25. Implementation phases

The renderer is built in **milestones, and each milestone is a `v0.x.0`
release**: a version number says how far the renderer has come, not how much
changed. What each milestone contains, and the order, is the
[roadmap](../roadmap/README.md). The order follows three rules:

- **Depth before breadth.** The avatar path — MToon quality, the animation
  fast path, latency, frame stability, outline quality, expressions — is
  finished before a new feature family (MMD, `UsdPreviewSurface`, WebGPU) is
  added. A new family never leaves the avatar path half done.
- **Vulkan first.** A capability is completed on Vulkan — scene model,
  material model, resource lifetime, animation fast path, latency model,
  diagnostics — before it is carried to WebGPU (§4).
- **Measure before complexity.** GPU-driven and other advanced techniques
  wait for a benchmark that asks for them (§22).
- **See it before tuning it.** The dedicated viewport (§31) comes before the
  work it is used to judge: what cannot be seen, measured and compared is not
  tuned. It is built early, and grows with each milestone.
- **Platforms after the renderer.** Architecture and quality are settled on
  Windows and the main development GPU. Linux and multi-vendor Vulkan
  validation come after the renderer core, the viewport and the fast path
  are settled, as their own piece of work, and are no release gate before
  then (§4).

In short: v0.2.0 finishes how an avatar looks, and v0.3.0 how it moves.

Until v0.1.0 the sequence was **Renderer Phase 0–7**. It is retired; reports
and the v0.1.0 record that name a phase map onto milestones as follows:

| Renderer Phase | Milestone |
| --- | --- |
| 0 Skeleton, 1 Avatar MVP | v0.1.0 |
| 2 MToon completion | v0.2.0, MToon quality and the viewport foundation |
| 3 Animation fast path | v0.3.0, avatar animation fast path |
| 4 MMD | v0.4.0, MMD realization |
| 5 UsdPreviewSurface | v0.5.0, generic USD fallback |
| 6 WebGPU | v0.6.0, WebGPU |
| 7 Optimization | after v0.6.0, as measurement asks |

## 26. Non-goals

Initially not goals: a general-purpose AAA renderer; a full deferred renderer;
ray tracing; path tracing; large worlds; a geometry-shader or mesh-shader
dependency; full MaterialX, production PBR or arbitrary material graphs
(`UsdPreviewSurface` is a fallback, not a lookdev path); a Metal or OpenGL
backend; clustered lighting or a production shadow system (§32); a
screen-space outline before the inverted hull is finished (§10); tuning for
large generic USD scenes; a renderer core shared with `hydra-merlin` (§3); a
bindless redesign before the fixed texture table runs out (§22); a large
render-graph framework; every Hydra feature. The value is **responsiveness
for avatar rendering**, not feature count.

## 27. Design principles

When a decision is unclear, prefer in this order:

1. **Latency over throughput.** Input-to-photon latency beats average FPS.
2. **Animation is the hot path.** Pose, morph and expression take the shortest
   path.
3. **Static state stays static.** Topology, material structure and pipelines
   are not touched every frame.
4. **Persistent resources.** Draw packets, pipelines, buffers and descriptors
   are reused.
5. **No unnecessary stalls.** CPU / GPU synchronization is minimal.
6. **Vulkan-first.** The best implementation is built on Vulkan first.
7. **WebGPU-friendly, not WebGPU-limited.**
8. **Hydra at the boundary.** Hydra is the integration layer; the core does
   not lean on Hydra internals.
9. **Existing USD schemas first.** No new schema without need; use existing
   USD and API schemas and normalize materials in the renderer.
10. **Measure before complexity.** Advanced GPU-driven techniques follow a
    benchmark that shows the need.

## 28. Target architecture

```text
                         USD Stage
                             │
                    Hydra / Scene Index
                ┌────────────┴────────────┐
          structural state         toon normalization
                └────────────┬────────────┘
                         ToonScene
                    persistent resources
                         DrawPackets
Motion connector             │
      → MotionStream         │
      → MotionPose           │
  expression, look-at,       │
  camera ───── late latch ───┤
                             ▼
                    GPU resource update
                     ┌───────┴───────┐
                   Vulkan          WebGPU
                     └───────┬───────┘
                 low-latency presentation
```

## 29. Conclusion

`hydra-toon` does not stop at "a Hydra renderer that can draw MToon and MMD".
It is **a low-latency, highly responsive Hydra renderer for real-time digital
characters**, built on: Vulkan-first, avatar-first, animation-first, late
motion latching, persistent draw packets, strict dirty propagation, minimal
synchronization, and separation of USD structural state from real-time motion
state. It draws MToon faithfully and MMD's look naturally, skins and morphs on
the GPU as a matter of course, keeps expression, look-at and motion updates
light, measures its own input-to-display latency, and is judged on its own
native path in a dedicated viewport, with Hydra confined to scene integration.
That gives it a role distinct from a general renderer as the drawing
foundation for `usd-vrm-plugins`, `usd-mmd-plugins`, `usd-motion-plugins`,
`motion-connectors`, `usd-avatar-runtime` and Mimikuri.

---

## 30. Where this repository departs from the implementation policy

Both departures are binding.

| Implementation policy | Here | Why |
| --- | --- | --- |
| §4, §7 — `src/{hd,renderer,material,backend,shaders}` and `renderer/` + `backend/` | The OpenStrata renderer layout: `core/`, `backend/`, `adapters/`, `include/toon/`, `validation/` ([PROJECT_LAYOUT.md](../architecture/PROJECT_LAYOUT.md) §2–3) | The scaffold's layout is what `ost build`, `ost validate` and the renderer evidence contract are wired to, and its core-boundary check enforces §7 mechanically. The policy's directories were an example; its separation (core / backend / Hydra adapter / material / shaders) is kept one-to-one. |
| §5 — "the `HdRenderDelegate` adapter" | The adapter lives at `adapters/hydra2/` and is named `hydra2` in `openstrata.renderer.yaml` | That is OpenStrata's name for the Hydra scene-input slot. The code is a classic `HdRenderDelegate` + `HdRendererPlugin`, which is what §5 asks for; the directory name claims nothing about the Hydra 2.0 renderer interface. |

## 31. Evaluation hosts

The dedicated viewport, `toon-viewport`, is not an auxiliary tool: it is the
main place the renderer is judged. It owns the frame loop and presents
natively: no Hydra host's readback or scheduling sits between the renderer
and the display.

```text
bootstrap scene (core build) ──────────────┐
USD stage → Hydra, in process (--usd) ─────┴→ RenderWorld → RenderExtraction
                                                  → Vulkan → GLFW / swapchain
```

The default build stays OpenUSD-free and draws a built-in scene. A USD
scene — a VRM avatar among them — reaches the viewport as `hydra-merlin`'s
viewport receives one: a build intent that enables both the Hydra adapter
and the viewport, launched by `ost renderer viewport --intent <intent>
--profile <runtime> -- --usd <stage>`. In that build the viewport links the
adapter's runtime library (`toon-hydra2-runtime`), creates the render index
with the render delegate itself, populated through UsdImaging's scene index
chain as `usdview`'s engine populates one, and syncs it in its own frame
loop, so the scene arrives through the same adapter `usdview` exercises.
No Hydra render pass runs: the viewport draws the delegate's committed
render world straight into the swapchain, so there is neither an offscreen
target nor a copy. A normal frame reads nothing back to the CPU, and only a
screenshot, a reference check or picking asks for a readback.
[PROJECT_LAYOUT.md](../architecture/PROJECT_LAYOUT.md) §4 lets OpenUSD into
the viewport's Hydra source, in that build only.

The two hosts divide the work:

| Host | Judges |
| --- | --- |
| `usdview` | Hydra and USD integration correctness |
| `toon-viewport` | image quality, anti-aliasing, outline, motion, frame pacing, presentation and latency |

MToon fidelity, the outline, anti-aliasing, frame pacing and animation
latency are all easier to judge without a Hydra host's readback and
scheduling in the way, so the viewport is built early and grows with the
renderer. First: presentation control (resize, swapchain recreation, vsync),
an orbit / pan / zoom camera with framing, the MSAA sample count, frame time,
CPU and GPU timing and renderer statistics, material and light debug
controls, image capture, and an automated presentation test. Then: animation
playback, expression and morph debug, skeleton display, outline debug modes,
wireframe and normal display, and latency telemetry. Which milestone takes
what is the [roadmap](../roadmap/README.md).

### 31.1 Open questions

None open. DP-Q1 — how the Hydra-fed viewport is populated so that a VRM
avatar selects MToon, since `hdToon` reads the `vrm` container that
`vrmImaging` contributes to the stage scene index
([MATERIAL_POLICY.md](MATERIAL_POLICY.md) §2) — is answered as proposed:
through UsdImaging's scene index chain, with `vrmImaging` supplied to the
run as a Formation supplies it. The avatar's 20 draws are 20 MToon
([renderer report 19](../reports/renderer/19-2026-09-28-hydra-fed-viewport.md)).

## 32. Scene lighting

A light fixed to the camera is a stand-in, not the design. Scene lights come
from Hydra:

```text
HdLight / scene index → ToonLight → light buffer → MToon
```

First come directional lights, basic point and spot lights, the ambient
contribution, and MToon's lighting semantics over them. Clustered lighting
and a production shadow system are not the goal (§26). The viewport carries
basic light controls, so a lighting change can be compared quickly.

## 33. Checks for a new feature

Before a feature is added, it is checked against these questions:

1. Does it improve how an avatar looks? Then it is high priority.
2. Does it shorten motion-to-photon latency? Then it is high priority.
3. Does it steady frame time? Then it is high priority.
4. Does it make something easier to measure or compare in the viewport
   (§31)? Then, for now, it is high priority.
5. Does it break the separation of the slow and fast paths (§2)? Then it is
   redesigned.
6. Does it bring a Hydra or Vulkan type into the core (§7)? Then that part
   goes back to the adapter or the backend.
7. Does it force code sharing with `hydra-merlin` (§3)? The renderer core is
   not shared.
8. Does it add breadth beyond the current milestone? Then, as a rule, it
   waits.
9. Does it widen platform coverage early? Until the architecture and quality
   are settled on Windows, Linux and multi-vendor coverage are no release
   gate (§25).
