# After v0.6.0

Unfinished work after shared shadows and lighting. Each milestone gets its
own page when it becomes next; [the index](README.md) owns the sequence. All work below
is owned by `hydra-toon`; upstream prerequisites are linked to their owners.

## Host and compositor integration (v0.8.0-scale)

Start after WebGPU parity; the version assignment remains provisional.

- Deliver the RGBA product to an intended host without per-frame CPU
  `HdRenderBuffer` readback, using the
  [GPU handoff contract](../design/RENDER_OUTPUT_POLICY.md#7-gpu-texture-handoff).
- Select an integration from actual consumer needs: Vulkan image handoff,
  Hgi interop, shared textures, external memory/semaphores or compositor APIs.
  Mimikuri and Tessera are prospective consumers.
- Validate resize, device loss, multiple consumers and capture, retaining a
  measurable readback fallback. Keep orchestration and avatar evaluation at
  their owning boundaries.

**Done when** one foreground product reaches an intended host without CPU
readback and its viewport/compositor results are checked. Actual display
latency is the separate measurement task [below](#not-yet-in-a-milestone).

## Platform and GPU coverage

Take up Linux x86_64 and NVIDIA/AMD/Intel Vulkan after WebGPU and host
integration, once the core, viewport and fast path are settled, and a concrete
consumer or reproducible portability requirement asks for them.

- Build/test/validate `hydra` against the Linux canonical runtime.
- Publish the Linux package beside Windows and exercise the VRM host Formation
  with owner-published runtime and VRM packages.
- Record vendor/compiler/validation differences in a report; add actually
  measured environments to [supported configurations](../reference/SUPPORTED_CONFIGURATIONS.md).

## glTF PBR through MaterialX

Unassigned; take up when a mixed MToon/PBR avatar has a reproducible material
requirement beyond [PreviewSurface's scope](../design/MATERIAL_POLICY.md#12-previewsurface-and-generic-usd-fallback).
Consume the format owner's imported MaterialX realization, retaining toon
selection priority. Bound its vocabulary to base colour/textures, metallic,
roughness, normals, emission/strength and Opaque/Mask/Blend for those assets.

Establish redistributable fixtures and one local avatar/reference comparison;
keep restricted models and captures out of the repository. General material
graph compilation and production PBR remain outside the renderer's goals.

## After v0.7.0

Unassigned candidates, owned here and accepted for consideration only when
their trigger is met. Detail belongs to the linked design policies.

| Candidate | Start when |
| --- | --- |
| Environment lighting and IBL | a reproducible mixed PBR/toon scene requirement exceeds uniform ambient |
| Exposure, tone mapping and HDR | an intended host has reproducible display differences or an HDR requirement |
| Render layers and visibility masks | a host needs separate character, background and shadow products |
| Shadow catchers and contact shadows | a video/live-action/3DGS compositor needs them, after host integration, reusing shared shadows |
| Lightweight pass graph | documented pass/resource dependencies make manual synchronization hard to follow |
| Bindless or a larger texture table | an actual asset exceeds capacity or descriptor cost is a measured bottleneck |
| Off-thread staging and asynchronous transfer | measured first-load/streaming p95/p99 shows upload hitches; [upload policy](../design/DESIGN_POLICY.md#20-asset-upload) |
| GPU-driven rendering, meshlets, culling and indirect draw | draw/instance count is a measured bottleneck; [GPU-driven policy](../design/DESIGN_POLICY.md#22-gpu-driven-rendering) |

Do not add speculative feature lists without a trigger and an owning
repository. Propose a separate scope decision when a new requirement appears.

## Not yet in a milestone

Unassigned renderer work. Move a row to the milestone page when selected;
completed work leaves this list in the same change.

Project infrastructure, independent of renderer milestones:

- Add a generated OpenStrata pull-request lane for implementation changes:
  `core` with capability-gated GPU skips and `hydra` against a digest-pinned
  runtime. Documentation-only changes use the lightweight docs lane.
- Make the core boundary check discover all public core headers instead of
  enumerating them, so newly added headers cannot evade the
  [dependency check](../architecture/PROJECT_LAYOUT.md#4-dependency-directions).

| Work | Start when / owner prerequisite |
| --- | --- |
| External authored/deformed tangent and glTF-engine comparisons | a visible normal/seam defect, tangent semantic change or reproducible recurrence needs an external oracle; owned here and unassigned under the [material stop rule](../design/MATERIAL_POLICY.md#121-reference-comparison-and-stop-rule); [bounded disposition](../reports/renderer/83-2026-10-19-v050-acceptance.md#bounded-tangent-comparison-and-stop) |
| Native usdview transparent sRGB consumer repair | a native-host consumer needs transparent display/export and can fix/validate the host boundary; host-owned conversion, with renderer-side input unchanged; [measured limitation](../reports/renderer/82-2026-10-19-usdview-rgba-consumer.md) |
| Actual display latency evidence | a host can instrument scanout/photon timing and map source clocks; trace the displayed sample under [performance policy §23](../design/DESIGN_POLICY.md#23-performance-kpis) |
| Runtime Hydra-publication parity | the [runtime's output path](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/architecture/OUTPUT_PATHS.md) is available for comparing with the fast adapter |
| Material binds on a real avatar | a redistributable or local avatar exercises colour/texture-transform binds exposed by the [format owner's expression contract](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) and upstream runtime |
| Point-light cube shadows, cascades, contact and soft shadows, coloured or translucent shadows, UsdLux shadow colour/distance/falloff and shadow linking | an avatar or scene shows a reproducible need the four directional/spot views cannot meet; [shadow policy §10](../design/SHADOW_POLICY.md#10-outside-this-policy); shadow catchers are the candidate [above](#after-v070) |
| MMD self-shadow distance and mode 2 host settings | a host needs non-default settings and provides reference captures; [existing evidence](../reports/renderer/66-2026-10-17-mat-q5-shadow-depth.md) |
| PMX 2.1 vertex colours, points and lines | a target PMX 2.1 asset and a renderer that draws it provide an independent oracle; [material policy §7](../design/MATERIAL_POLICY.md#7-pipelines-and-parameter-buffers) |
| GPU dual quaternion skinning | a target profile identifies CPU fallback/point upload as the bottleneck; CPU fallback is acceptable otherwise |
| Outline, wireframe and normal debug views | an avatar/viewport defect needs a view beyond [existing diagnostics](../reference/CAPABILITY_MATRIX.md#hosts) |
| Picking via primId, instanceId and elementId AOVs | a lookdev viewport needs selection/inspection |
| Render tags, instancers and framing data window | a concrete generic USD/accessory fixture requires them |

Runtime/VRM-only macOS session work belongs to the upstream runtime and format
owners; it is not a renderer task. See [integration scope](../design/INTEGRATION_SCOPE_POLICY.md).
