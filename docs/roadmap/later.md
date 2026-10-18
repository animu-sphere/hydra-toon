# After v0.5.0

The milestones after USD interoperability and RGBA output, in less detail
than the next one. Each gets its own page when it becomes next.

## v0.6.0 — Shared shadow and lighting

Meshes of different material families occlude, light and shadow each other
from one scene geometry and one light rig: a PreviewSurface object casts on
an MToon avatar, MToon on MMD, and each still shades as its own model. What
is shared is scene input and render resources, not the shading algorithm
([design policy §35](../design/DESIGN_POLICY.md#35-shared-scene-services),
[material policy §14](../design/MATERIAL_POLICY.md#14-shadows-across-material-families)):

```text
Hydra / fast evaluated scene
          │
      RenderWorld
          ├── scene lights + shadow settings
          ├── shadow caster geometry / visibility
          └── material-specific shading parameters
                         │
               shared shadow passes
                         │
                 shadow resources
                    ├── PreviewSurface receiver (PBR response)
                    ├── MToon receiver (toon response)
                    └── MMD receiver (MMD-compatible response)
```

Work proceeds as a shadow contract first, then the shared pass, then the
cross-material cases, then diagnostics and the release gate:

- **The shadow contract.** A short RFC before code: the boundary between the
  render world, light data, casters, receivers and shadow resources, and how
  MMD's self-shadow map relates to the shared one
  ([MAT-Q7](../design/MATERIAL_POLICY.md#9-open-questions)).
- **Light data.** Directional and spot lights first: light identity,
  visibility, light-space transform, shadow map reference, bias and
  filtering, defined independently of material, compatible with the
  existing light buffer.
- **The shared caster pass.** Opaque and Mask geometry into a common shadow
  depth: static and dynamic meshes, skinned and morphed casters correct.
- **Material-specific receivers.** PreviewSurface attenuates its PBR
  response; MToon keeps its shade and ramp semantics; MMD keeps its
  self-shadow semantics. MMD's existing self-shadow checks stay green.
- **Cross-material cases.** PreviewSurface → MToon, MToon → PreviewSurface,
  MMD → MToon, MToon → MMD and self-shadow, with skinned and morphed
  casters.
- **Quality controls and diagnostics.** Resolution, depth bias, normal bias,
  basic PCF, shadow on/off and a debug view, so look and cost can be traced.
- **Fast-path parity.** A caster moved by motion shadows the same through
  Hydra and the fast path, and a pose-only update reuploads no geometry.

Staged: one directional shadow, then multiple directional and spot
shadows, then quality and atlas work. Point-light cube shadows, cascades,
advanced soft shadows and ray tracing are outside the first release gate,
and the shadow atlas need not start as a general large-scale allocator.
Environment lighting is not implemented here; only the interface leaves
room for it.

**Done when** meshes of different material families cast on and receive
from each other, MToon's and MMD's representative looks show no regression,
and each fixture records the shadowed frame's CPU and GPU timing, p95/p99
included, and its memory use.

## v0.7.0 — WebGPU

The architecture proven on Vulkan, realized a second time
([design policy §4](../design/DESIGN_POLICY.md#4-backends), §9):

```text
RenderWorld → draw list → Vulkan | WebGPU → same logical RGBA product
```

Shared with Vulkan: the render world, render extraction, the material model,
shader semantics, MToon / MMD / PreviewSurface realization, the animation
fast path, skinning, morphs, outline, the shared lights and shadows, background
mode and alpha semantics.
Backend-specific: device and queue, resource allocation, synchronization,
presentation, descriptors / bind groups, pipelines and render-pass encoding.
Implement backend resources, material passes, deformation, outline and
presentation in `backend/webgpu/`
([PROJECT_LAYOUT.md §3](../architecture/PROJECT_LAYOUT.md#3-where-new-code-goes)).

This milestone reproduces the semantics established through v0.6.0; it does
not redesign renderer-core semantics while implementing WebGPU, and a WebGPU
restriction is no reason to loosen the core's texture or resource contract.
Optimization starts from measured p95/p99 on the avatar fixtures.

**Done when** the same canonical scenes and extraction path produce visually
equivalent Vulkan and WebGPU results, with transparent background, outline,
Blend, normal map, GPU skinning/morph and cross-material shadow parity, and
practical performance evidence. Semantic parity is the condition; pixel
identity is not. The shared output contract is
[RENDER_OUTPUT_POLICY.md](../design/RENDER_OUTPUT_POLICY.md).

## Host and compositor integration (v0.8.0-scale)

After WebGPU parity, make the GPU RGBA product consumable without per-frame
CPU `HdRenderBuffer` readback. The version is provisional; the ordering is
intentional. Candidates include Vulkan image handoff, Hgi interop, shared GPU
textures, external memory/semaphores and compositor APIs, with Mimikuri and
Tessera as prospective host consumers.

Specify and validate the
[GPU handoff contract](../design/RENDER_OUTPUT_POLICY.md#7-gpu-texture-handoff):
extent, format, colour space, alpha convention, ownership, lifetime,
synchronization, frame identity, producer timestamp and resource state. A
native handle alone does not establish a usable integration. Handle resize,
device loss, several consumers and capture, and keep a measurable readback
fallback beside readback-less presentation. Keep host orchestration and
avatar evaluation outside the renderer.

**Done when** one foreground render product reaches an intended host without
CPU readback, and the viewport's and the compositor's results are checked.

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

## After v0.7.0

Candidates, taken up as features and measurements require them; performance
work is not introduced before WebGPU parity as a rule, and GPU-driven work
waits for evidence
([design policy §22](../design/DESIGN_POLICY.md#22-gpu-driven-rendering)).
Each waits for its trigger:

| Candidate | Taken up when |
| --- | --- |
| Environment lighting and IBL | ambient alone falls short for mixed PBR and toon scenes; v0.6.0 leaves only the interface |
| Exposure, tone mapping and HDR output | display differences with OBS, UI or an editor, or an HDR requirement; v0.5.0 states only the RGBA and transfer contract |
| Render layers and visibility masks | a host needs character, background and shadow as separate products; a minimal design from that requirement, with no new schema |
| Shadow catchers and contact shadows | compositing over video, live action or 3DGS; after host / compositor integration, reusing the shared shadow pass |
| A lightweight render-pass graph | added passes make manual synchronization hard to follow; pass resources and dependencies are documented first |
| Bindless or a larger texture table | an actual avatar or scene exceeds the fixed 128 entries, or descriptors measure as the bottleneck; designed from an interface that allows for both backends |
| Off-thread staging and asynchronous transfer | first load or texture streaming hitches, shown by p95/p99 ([design policy §20](../design/DESIGN_POLICY.md#20-asset-upload)) |
| GPU-driven rendering, meshlets, GPU culling, indirect draw | many draws or instances are measured as the bottleneck; a candidate to the last |

Further candidates on the same terms: point-light cube shadows, cascades and
advanced soft shadows beyond v0.6.0's, bloom, colour grading, advanced
temporal reconstruction, a visibility buffer, compute skinning and morphs,
async compute, pipeline caching and shader pre-warming, stereo rendering,
OpenXR, WebXR, foveated rendering, and a WASM / browser runtime. Picking,
instancers, GPU dual quaternion skinning and display timing are planned in
the [roadmap](README.md#not-yet-in-a-milestone).
