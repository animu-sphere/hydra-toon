# After v0.5.0

The milestones after the avatar path, and platform coverage, in less detail
than the next one. Each gets its own page when it becomes next.

## v0.6.0 — WebGPU

The architecture proven on Vulkan, realized a second time
([design policy §4](../design/DESIGN_POLICY.md#4-backends), §9):

```text
RenderWorld → draw list → Vulkan | WebGPU
```

Shared with Vulkan: the render world, render extraction, the material model,
shader semantics and the animation fast path. Not shared: resource
implementation, synchronization, presentation and backend-specific bindings.
Device and queue, buffers, textures, bind groups, pipelines, render passes,
MToon, skinning, morphs, outline and presentation, in `backend/webgpu/`
([PROJECT_LAYOUT.md §3](../architecture/PROJECT_LAYOUT.md#3-where-new-code-goes)).

**Done when** Vulkan and WebGPU draw the same basic scene from the same
canonical scene and extraction path.

## Platform and GPU coverage

Vulkan portability beyond Windows and the main development GPU, as its own
piece of work, taken up once the renderer core, the viewport and the fast
path are settled
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

## After v0.6.0

Candidates, none required before the milestones above are settled, and the
GPU-driven ones only after a measurement asks for them
([design policy §22](../design/DESIGN_POLICY.md#22-gpu-driven-rendering)):
general shadow mapping (MMD's self-shadow, which MMD materials ask for, is
v0.4.0's), multiple lights, environment lighting, HDR, bloom, colour
grading, advanced temporal reconstruction, GPU-driven rendering, indirect
draw, bindless textures, meshlets, a visibility buffer, GPU culling, compute
skinning and morphs, async compute, pipeline caching and shader pre-warming,
stereo rendering, OpenXR, WebXR, foveated rendering, and a WASM / browser
runtime.
