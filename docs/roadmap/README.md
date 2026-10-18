# Roadmap

What this repository builds next. Each milestone is a `v0.x.0` release
([design policy §25](../design/DESIGN_POLICY.md#25-implementation-phases));
a milestone is released when its work is done, not on a date.

The roadmap holds only work that is not done. It carries no status marks:
what is implemented is the
[capability matrix](../reference/CAPABILITY_MATRIX.md), and what a release
established is its [record](../releases/). A finished item is deleted here in
the change that finishes it, and a milestone's page is deleted when that
version is released. Work no milestone carries yet waits under
[not yet in a milestone](#not-yet-in-a-milestone) until one takes it. Sibling
repositories' work is planned in their own roadmaps.

## Sequence

| Milestone | Theme | Page |
| --- | --- | --- |
| v0.5.0 | USD interoperability and render output: PreviewSurface, tangents, reusable RGBA and compositing | [v0.5.0.md](v0.5.0.md) |
| v0.6.0 | Shared shadow and lighting: one light rig and shadow resources across MToon, MMD and PreviewSurface | [later.md](later.md#v060--shared-shadow-and-lighting) |
| v0.7.0 | WebGPU, the second backend | [later.md](later.md#v070--webgpu) |
| v0.8.0-scale | Host / compositor integration and GPU texture handoff; version assignment remains provisional | [later.md](later.md#host-and-compositor-integration-v080-scale) |
| later | Platform and GPU coverage: Linux, and AMD and Intel Vulkan | [later.md](later.md#platform-and-gpu-coverage) |
| later | Environment lighting, render layers, shadow catchers, GPU-driven rendering and other candidates, as evidence asks | [later.md](later.md#after-v070) |
| later | glTF PBR fidelity through MaterialX, including mixed MToon/PBR avatars | [later.md](later.md#gltf-pbr-through-materialx) |

## Priority

Work is taken in this order, across milestones:

1. USD interoperability, tangent fidelity and reusable RGBA output
2. Shared shadow and lighting across material families, on Vulkan
3. WebGPU parity with the Vulkan semantics
4. Host / compositor integration and zero-copy consumption
5. Platform expansion; GPU-driven work when measurements require it

v0.5.0 is finished within its scope, not widened. Shared scene services —
lights, shadow resources, output and visibility — are settled on Vulkan
before a second backend reproduces them, so WebGPU implements them once
rather than following a contract still in motion
([design policy §35](../design/DESIGN_POLICY.md#35-shared-scene-services)).
The current milestone is finished before a new feature family is added, and
platform coverage is not widened before the renderer core, the viewport and
the fast path are settled
([design policy §25](../design/DESIGN_POLICY.md#25-implementation-phases)).

## Project infrastructure

Not tied to a milestone:

- **CI on pull requests.** A generated OpenStrata CI lane: the `core` build
  with its GPU checks as capability-gated `SKIP`s on hosted runners, and the
  `hydra` intent against a digest-pinned runtime.
- **Documentation check.** A `scripts/check_docs.py` that resolves relative
  links and checks category indexes, as the sibling repositories have.
- **Core boundary check by glob.** The check lists its headers by name
  ([PROJECT_LAYOUT.md §4](../architecture/PROJECT_LAYOUT.md#4-dependency-directions));
  it should find every public core header itself.

## Not yet in a milestone

Planned, but not yet given to a milestone. When one takes an item, it moves
to that milestone's page.

- **Actual display latency evidence.** Extend explicitly clock-mapped producer
  timestamps beyond present API return with display instrumentation, and trace
  which source-produced sample reached the display. Scanout/photon timing is
  distinct from submit and API return
  ([design policy §23](../design/DESIGN_POLICY.md#23-performance-kpis)).
  v0.3.0 closed at submit/present API return; actual motion-to-photon evidence
  remains a separate measurement task.
- **Parity with the runtime's Hydra publication.** Compare the fast path
  with `usd-avatar-runtime`'s own Hydra publication, once its Scene Index
  adapter exists
  ([runtime roadmap](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/roadmap/current.md),
  Runtime Phase C), instead of the session-layer oracle v0.3.0 closed on.
- **Material binds on a real avatar.** Exercise runtime material outputs on
  an avatar whose expressions bind material colours or texture transforms.
  No local avatar binds colours, and `vrmSchema` keeps texture-transform
  binds in its raw block rather than the typed expression contract
  ([schema contract](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/plugins/vrmSchema/docs/SCHEMA_CONTRACT.md)),
  so the runtime publishes none. v0.3.0 established them on a committed
  fixture.
- **MMD's self-shadow distance and mode 2** as a host setting. v0.4.0's
  comparison and the map are fitted to mode 1 at MMD's default distance,
  and MMD's shadow moves with the distance
  ([renderer report 66](../reports/renderer/66-2026-10-17-mat-q5-shadow-depth.md));
  the stage carries neither.
- **PMX 2.1's material flags**, `vertexColor`, `drawPoints` and
  `drawLines`. v0.4.0 reads them into the MMD block and draws nothing from
  them ([material policy §7](../design/MATERIAL_POLICY.md#7-pipelines-and-parameter-buffers)):
  every local model is PMX 2.0, MikuMikuDance v9.32 reads no PMX 2.1, and
  points and lines change a draw's primitives. Drawing them needs a PMX 2.1
  model and a renderer that draws 2.1 to compare with.
- **Dual quaternion skinning on the GPU.** A dual quaternion variant of the
  skinning vertex stage, replacing usdSkelImaging's CPU kernel and its point
  uploads, when a target asset's profile shows the CPU fallback as its
  bottleneck. CPU fallback remains acceptable; no milestone through v0.7.0
  gates on GPU dual quaternion skinning.
- **More viewport debug views.** Outline debug modes, and wireframe and
  normal display
  ([design policy §31](../design/DESIGN_POLICY.md#31-evaluation-hosts)).
- **Picking.** Fill the `primId`, `instanceId` and `elementId` AOVs, when a
  lookdev viewport needs selection and inspection.
- **Render tags, instancers and the framing data window**, driven by a
  concrete generic USD or accessory fixture.
- **The VRM host session on macOS arm64: the runtime and the VRM package
  only.** `hdToon` has no Metal backend
  ([design policy §4](../design/DESIGN_POLICY.md#4-backends)), so no renderer
  package is planned for macOS. The planned commands, on macOS 15 with
  Xcode's 15.5 SDK, not run yet:

  ```sh
  # A lookdev runtime with usdview, at CY2026's SDK and deployment target;
  # there is no canonical macOS lookdev leaf. The build interpreter needs
  # Jinja2, PySide6 and PyOpenGL.
  ost runtime pull cy2026 --profile lookdev --build <OpenUSD v26.08 checkout>       --openusd-variant metal --sdk 15.5 --deployment-target 13.0 --jobs <n>
  ost runtime validate cy2026 --profile lookdev
  ost runtime export cy2026 --profile lookdev --slim

  # In usd-vrm-plugins; it publishes no macOS lookdev package.
  ost plugin build plugins/vrmSchema --profile lookdev
  ost plugin build plugins/vrmImaging --profile lookdev
  ost plugin package plugins/vrmImaging --profile lookdev
  ost artifact import plugins/vrmImaging/dist/plugins/vrmImaging/0.10.0/cy2026-macos-arm64-py313-lookdev
  ```

  With those, `usdview` can show whether `vrmImaging` contributes its
  container on macOS, but with Storm; that is `usd-vrm-plugins`' evidence,
  not this renderer's.
