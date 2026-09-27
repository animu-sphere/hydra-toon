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
> keeps so either can be cited by number. Where this repository departs from
> that policy, §30 records it.
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
input to display, and the stability of frame time**.

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
USD / Hydra ─────────────────→ Scene / Geometry / Material

                 fast path
MotionPose ──────────────────→ Skeleton / Morph
                                    │
                                    │ late latch
                                    ▼
                                GPU submit
```

## 3. Relationship with hydra-merlin

`hydra-toon` and [`hydra-merlin`](https://github.com/animu-sphere/hydra-merlin)
are not forced into a shared core.

| | `hydra-merlin` | `hydra-toon` |
| --- | --- | --- |
| Purpose | general USD rasterization; MaterialX / PreviewSurface / OpenPBR; ordinary USD scenes | avatar-first; MToon / MMD look; high-frequency animation; low motion-to-photon latency; VTuber and real-time characters |

Sharing, if any, stays at the level of: CMake utilities, OpenUSD discovery,
Slang build utilities, small common helpers, CI and packaging infrastructure.
The renderer core and the GPU execution architecture are independent.
`hydra-merlin` is a reference for implementation technique, not a dependency.

## 4. Backends

Initial scope is exactly two backends:

1. **Vulkan** — the reference backend. It is finished first and is the
   baseline for the low-latency design: explicit synchronization, timeline
   semaphores and Synchronization2 are assumed, and native Vulkan performance
   comes first.
2. **WebGPU** — not treated as a full abstraction of Vulkan. The renderer
   architecture and shader logic are shared; each backend has its own
   implementation. The Vulkan backend is never limited to what WebGPU can do.

WebGPU is not a copy of the Vulkan implementation but another realization of
the same renderer model, and backend-specific needs never flow back into the
render world or the Hydra adapter. Metal is out of scope until a need for it is
clear, and is re-evaluated then. OpenGL is not pursued as a new backend.

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

## 7. Renderer core

Between Hydra classes and GPU-API classes the core keeps a small, explicit
internal data model. Candidates:

```cpp
ToonRenderer  ToonScene  ToonView  ToonMesh  ToonMaterial
ToonSkeleton  ToonTexture  DrawPacket  RenderGraph
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
predictable GPU cost. Later the mode may become selectable
(`None | InvertedHull | ScreenSpace`), but only after the inverted hull's width
stability, aliasing and cost are finished; a second method does not come first.
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

The latest `MotionPose` is written to GPU resources **after** the ordinary
Hydra scene sync, immediately before submit:

```text
Hydra scene sync (geometry, material, camera)
        │
latest MotionPose → late motion latch → joint / morph buffers → GPU submit
```

This shortens `tracking → motion processing → pose → GPU → display`, which
matters most for MediaPipe, mocap, controller and XR input.

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

Texture and geometry upload are off the render thread:

```text
I/O thread → decode / transcode → staging → transfer queue → device-local resource
```

The graphics queue stays on rendering. An asset that has not loaded draws with
a placeholder, and the reference switches when it arrives.

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

## 23. Performance KPIs

Maximum FPS is not the measure. In order of priority:

1. motion-to-photon latency;
2. stable frame time;
3. per-frame CPU cost;
4. no unnecessary upload;
5. GPU frame cost;
6. peak throughput.

- **Renderer latency:** pose → GPU submit, CPU render-thread time, GPU frame
  time, present latency.
- **Frame consistency:** p50 / p95 / p99 frame time, hitch count, shader
  compilation stalls, upload stalls.

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
follow.

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

Until v0.1.0 the sequence was **Renderer Phase 0–7**. It is retired; reports
and the v0.1.0 record that name a phase map onto milestones as follows:

| Renderer Phase | Milestone |
| --- | --- |
| 0 Skeleton, 1 Avatar MVP | v0.1.0 |
| 2 MToon completion | v0.2.0, MToon quality |
| 3 Animation fast path | v0.3.0, avatar animation fast path |
| 4 MMD | v0.4.0, MMD realization |
| 5 UsdPreviewSurface | v0.5.0, generic USD fallback |
| 6 WebGPU | v0.6.0, WebGPU |
| 7 Optimization | after v0.6.0, as measurement asks |

## 26. Non-goals

Initially not goals: a general-purpose AAA renderer; a full deferred renderer;
ray tracing; path tracing; large worlds; a geometry-shader architecture; full
MaterialX, production PBR or arbitrary material graphs (`UsdPreviewSurface` is
a fallback, not a lookdev path); a Metal backend; a large render-graph framework; every Hydra
feature. The value is **responsiveness for avatar rendering**, not feature
count.

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
      → MotionPose ── late latch ──┤
                             ▼
                    GPU resource update
                     ┌───────┴───────┐
                   Vulkan          WebGPU
                     └───────┬───────┘
                          Display
```

## 29. Conclusion

`hydra-toon` does not stop at "a Hydra renderer that can draw MToon and MMD".
It is **a low-latency, highly responsive Hydra renderer for real-time digital
characters**, built on: Vulkan-first, avatar-first, animation-first, late
motion latching, persistent draw packets, strict dirty propagation, minimal
synchronization, and separation of USD structural state from real-time motion
state. That gives it a role distinct from a general renderer as the drawing
foundation for `usd-vrm-plugins`, `usd-mmd-plugins`, `usd-motion-plugins`,
`motion-connectors`, `usd-avatar-runtime` and Mimikuri.

---

## 30. Where this repository departs from the implementation policy

Both departures are binding.

| Implementation policy | Here | Why |
| --- | --- | --- |
| §4, §7 — `src/{hd,renderer,material,backend,shaders}` and `renderer/` + `backend/` | The OpenStrata renderer layout: `core/`, `backend/`, `adapters/`, `include/toon/`, `validation/` ([PROJECT_LAYOUT.md](../architecture/PROJECT_LAYOUT.md) §2–3) | The scaffold's layout is what `ost build`, `ost validate` and the renderer evidence contract are wired to, and its core-boundary check enforces §7 mechanically. The policy's directories were an example; its separation (core / backend / Hydra adapter / material / shaders) is kept one-to-one. |
| §5 — "the `HdRenderDelegate` adapter" | The adapter lives at `adapters/hydra2/` and is named `hydra2` in `openstrata.renderer.yaml` | That is OpenStrata's name for the Hydra scene-input slot. The code is a classic `HdRenderDelegate` + `HdRendererPlugin`, which is what §5 asks for; the directory name claims nothing about the Hydra 2.0 renderer interface. |
