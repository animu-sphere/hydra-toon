# After v0.3.0

The milestones after the avatar path, and platform coverage, in less detail
than the next two. Each gets its own page when it becomes next.

## v0.4.0 — MMD realization

MMD's material and toon look as a first-class capability, drawn from the
canonical USD stage `usd-mmd-plugins` authors. Parsing PMX, PMD, VMD and VPD,
and MMD's semantics, stay there
([integration scope](../design/INTEGRATION_SCOPE_POLICY.md)). It is built on
the material, morph and animation infrastructure v0.2.0 and v0.3.0 establish
for MToon, not on an MMD-specific architecture.

- **Before it starts:** MAT-Q1 agreed with `usd-mmd-plugins`, and MAT-Q2 and
  MAT-Q3 decided
  ([material policy §9](../design/MATERIAL_POLICY.md#9-open-questions)).
- **Materials:** MMD material normalization; diffuse, ambient, specular, the
  toon texture, the sphere texture, the edge and its outline semantics,
  alpha and MMD transparency, double-sidedness and MMD's material flags, in
  `mmd_opaque`, `mmd_transparent` and `mmd_outline`, sharing only what
  [material policy §7](../design/MATERIAL_POLICY.md#7-pipelines-and-parameter-buffers)
  says is common.
- **Morphs:** MMD's vertex and material morphs through v0.3.0's morph and
  parameter paths.

**Done when** a representative MMD model's look is reproduced at practical
quality from the canonical USD stage.

## v0.5.0 — UsdPreviewSurface and generic USD fallback

Enough interoperability that an ordinary USD asset does not draw broken just
because it is not a toon material, while the renderer stays a toon renderer
and generic coverage never outranks it
([design policy §26](../design/DESIGN_POLICY.md#26-non-goals)); general
rendering is `hydra-merlin`'s.

- Base colour, roughness, metallic, normal, emissive, opacity, their textures
  and basic texture transforms, in `preview_surface`: a basic PBR fallback.
- glTF metallic-roughness PBR materials mixed with MToon in a VRM avatar,
  consumed through the imported `UsdPreviewSurface` network. Preserve base
  colour and textures, metallic and roughness, normals, emission including
  its strength, and Opaque, Mask and Blend behaviour through that network.
- Seed-san reproduction checks for `backpack_metal`, `backpack_nm`,
  `backpack_plastic` and `anim_logo`, plus `wear_metal`, `green_emit` and
  `glass`: retain the backpack's texture, the logo's colour and the green
  emission, and let the glass reveal the surface behind it. Cover these
  behaviours with redistributable controlled fixtures and a local avatar
  comparison against a reference renderer; keep MToon materials unchanged.
- Generic textured meshes: face-varying `st` and the other mesh data
  ordinary assets author.
- A fallback for materials the renderer does not support.

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
shadow mapping, multiple lights, environment lighting, HDR, bloom, colour
grading, advanced temporal reconstruction, GPU-driven rendering, indirect
draw, bindless textures, meshlets, a visibility buffer, GPU culling, compute
skinning and morphs, async compute, pipeline caching and shader pre-warming,
stereo rendering, OpenXR, WebXR, foveated rendering, and a WASM / browser
runtime.
