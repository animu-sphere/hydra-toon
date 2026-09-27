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
| v0.2.0 | MToon quality: the rest of MToon, outline stability, scene lights, Linux and more GPU vendors | [v0.2.0.md](v0.2.0.md) |
| v0.3.0 | Avatar animation fast path: GPU morphs, expressions, late motion latching, latency telemetry | [v0.3.0.md](v0.3.0.md) |
| v0.4.0 | MMD realization | [later.md](later.md#v040--mmd-realization) |
| v0.5.0 | `UsdPreviewSurface` and generic USD fallback | [later.md](later.md#v050--usdpreviewsurface-and-generic-usd-fallback) |
| v0.6.0 | WebGPU, the second backend | [later.md](later.md#v060--webgpu) |
| after v0.6.0 | Candidates, taken up when the above is settled | [later.md](later.md#after-v060) |

## Priority

Work is taken in this order, across milestones:

1. Outline stability and cost
2. The rest of MToon
3. Linux and multi-vendor Vulkan
4. Scene lights
5. GPU morphs
6. The expression fast path
7. Late motion latching
8. MMD
9. `UsdPreviewSurface`
10. WebGPU

The avatar path is finished before a new feature family is added.

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

- **Off-thread asset upload.** Geometry and textures staged and copied off
  the render thread, on a transfer queue, with a placeholder until they
  arrive ([design policy §20](../design/DESIGN_POLICY.md#20-asset-upload)).
- **Dual quaternion skinning on the GPU.** A dual quaternion variant of the
  skinning vertex stage, replacing usdSkelImaging's CPU kernel and its point
  uploads, when an asset needs it.
- **Hgi interop.** Hand the Hydra host the rendered image without reading
  every frame back into CPU `HdRenderBuffer`s and waiting for it.
- **Picking.** Fill the `primId`, `instanceId` and `elementId` AOVs.
- **Render tags, instancers and the framing data window.**
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
