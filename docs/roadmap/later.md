# After v0.5.0

The milestones after USD interoperability and RGBA output, in less detail
than the next one. Each gets its own page when it becomes next.

## v0.6.0 — WebGPU

The architecture proven on Vulkan, realized a second time
([design policy §4](../design/DESIGN_POLICY.md#4-backends), §9):

```text
RenderWorld → draw list → Vulkan | WebGPU → same logical RGBA product
```

Shared with Vulkan: the render world, render extraction, the material model,
shader semantics, MToon / MMD / PreviewSurface realization, the animation
fast path, skinning, morphs, outline, background mode and alpha semantics.
Backend-specific: device and queue, resource allocation, synchronization,
presentation, descriptors / bind groups, pipelines and render-pass encoding.
Implement backend resources, material passes, deformation, outline and
presentation in `backend/webgpu/`
([PROJECT_LAYOUT.md §3](../architecture/PROJECT_LAYOUT.md#3-where-new-code-goes)).

This milestone reproduces the semantics established through v0.5.0; it does
not redesign renderer-core semantics while implementing WebGPU.

**Done when** the same canonical scenes and extraction path produce visually
equivalent Vulkan and WebGPU results, with transparent background, outline,
Blend, normal map and GPU skinning/morph parity, and practical performance
evidence. The shared output contract is
[RENDER_OUTPUT_POLICY.md](../design/RENDER_OUTPUT_POLICY.md).

## Host and compositor integration (v0.7.0-scale)

After WebGPU parity, make the GPU RGBA product consumable without per-frame
CPU `HdRenderBuffer` readback. The version is provisional; the ordering is
intentional. Candidates include Vulkan image handoff, Hgi interop, shared GPU
textures, external memory/semaphores and compositor APIs, with Mimikuri and
Tessera as prospective host consumers.

Specify and validate the
[GPU handoff contract](../design/RENDER_OUTPUT_POLICY.md#7-gpu-texture-handoff):
extent, format, colour space, alpha convention, ownership, lifetime,
synchronization, frame identity, producer timestamp and resource state. A
native handle alone does not establish a usable integration. Keep host
orchestration and avatar evaluation outside the renderer.

Actual motion-to-photon instrumentation remains a separate host measurement
task in the [roadmap](README.md#not-yet-in-a-milestone), distinct from the
renderer core's completion and from submit/present API return.

## Platform and GPU coverage

Vulkan portability beyond Windows and the main development GPU, as its own
piece of work, taken up after WebGPU and host / compositor integration, once
the renderer core, the viewport and the fast path are settled
([design policy §25](../design/DESIGN_POLICY.md#25-implementation-phases));
until then it is no release gate. Linux x86_64, with NVIDIA, AMD and Intel
Vulkan, and what differs between them: vendor quirks, validation layer
differences and shader compiler differences. On Linux x86_64:

- the `hydra` intent built, tested and validated against the Linux leaf of
  the canonical `lookdev` runtime;
- the release workflow building and publishing the Linux package beside the
  Windows one;
- the VRM host session run as a Formation of the Linux runtime, `vrmImaging`'s
  Linux package from `usd-vrm-plugins`' release pins, and the published
  Linux `toon` package.

Each measured configuration is a [report](../reports/) and a row of
[supported configurations](../reference/SUPPORTED_CONFIGURATIONS.md).

## glTF PBR through MaterialX

No milestone is assigned yet. Support glTF metallic-roughness PBR materials
through their imported MaterialX realization, including PBR materials mixed
with MToon in a VRM avatar. PreviewSurface remains the ordinary USD fallback;
glTF PBR fidelity is evaluated on the MaterialX path. MToon continues to read
its canonical material inputs with selection priority, preserving its look.
The format owner authors the realization; this repository consumes it rather
than parsing glTF or VRM.

Preserve base colour and textures, metallic and roughness, normals, emission
including strength, and Opaque, Mask and Blend behaviour. Bound the supported
MaterialX vocabulary around those avatar materials; this does not introduce
general material-graph compilation or production PBR as a renderer goal.

Use Seed-san's `backpack_metal`, `backpack_nm`, `backpack_plastic`,
`anim_logo`, `wear_metal`, `green_emit` and `glass` as local reproduction
cases: retain the backpack's texture, the logo's colour and green emission,
and let glass reveal the surface behind it. Establish redistributable
controlled fixtures and a local avatar comparison against a reference
renderer, keeping restricted assets and captures out of the repository.
These are outside v0.5.0's release condition.

## After v0.6.0

Candidates, taken up as features and measurements require them; performance
work is not introduced before v0.6.0 as a rule, and GPU-driven work waits for
evidence
([design policy §22](../design/DESIGN_POLICY.md#22-gpu-driven-rendering)):
general shadow mapping (MMD's self-shadow, which MMD materials ask for, is
v0.4.0's), multiple lights, environment lighting, HDR, bloom, colour
grading, advanced temporal reconstruction, GPU-driven rendering, indirect
draw, bindless textures, meshlets, a visibility buffer, GPU culling, compute
skinning and morphs, async compute, pipeline caching and shader pre-warming,
stereo rendering, OpenXR, WebXR, foveated rendering, and a WASM / browser
runtime.

Off-thread asset decode/staging and asynchronous upload on a transfer queue
follow the same measurement-driven priority, with placeholders so loading
does not stall playback ([design policy §20](../design/DESIGN_POLICY.md#20-asset-upload)).
The fixed 128-entry texture table is retained until an actual avatar or scene
demonstrates its limit; bindless conversion is not an early release gate.
