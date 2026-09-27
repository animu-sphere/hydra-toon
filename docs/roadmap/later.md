# After v0.3.0

The milestones after the avatar path, in less detail than the next two. Each
gets its own page when it becomes next.

## v0.4.0 — MMD realization

MMD's material and toon look as a first-class capability, drawn from the
canonical USD stage `usd-mmd-plugins` authors. Parsing PMX, PMD, VMD and VPD,
and MMD's semantics, stay there
([integration scope](../design/INTEGRATION_SCOPE_POLICY.md)).

- **Before it starts:** MAT-Q1 agreed with `usd-mmd-plugins`, and MAT-Q2 and
  MAT-Q3 decided
  ([material policy §9](../design/MATERIAL_POLICY.md#9-open-questions)).
- **Materials:** diffuse, ambient, specular, the toon texture, the sphere
  map, the edge, alpha, double-sidedness and MMD's material flags, in
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
([design policy §26](../design/DESIGN_POLICY.md#26-non-goals)); general
rendering is `hydra-merlin`'s.

- Base colour, roughness, metallic, normal, emissive, opacity, their textures
  and basic texture transforms, in `preview_surface`.
- Face-varying `st` and the other mesh data ordinary assets author.

## v0.6.0 — WebGPU

The architecture proven on Vulkan, realized a second time
([design policy §4](../design/DESIGN_POLICY.md#4-backends), §9):

```text
RenderWorld → draw list → Vulkan | WebGPU
```

Device and queue, buffers, textures, bind groups, pipelines, render passes,
MToon, skinning, morphs, outline and presentation, in `backend/webgpu/`
([PROJECT_LAYOUT.md §3](../architecture/PROJECT_LAYOUT.md#3-where-new-code-goes)).

**Done when** Vulkan and WebGPU draw the same basic scene from the same
canonical scene and extraction path.

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
