# `hdToon` gains a material Sprim: MToon is selected from `vrmImaging`'s container and normalized into `ToonMaterial`

> Followed by [report 04](04-2026-09-26-material-value-route.md): value-only changes now reach the material through the delegate's terminal-scene-index observer.

- Date: 2026-09-26
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.10`; runtimes `cy2026-windows-x86_64-py313-core` and
  `…-lookdev` (OpenUSD 26.08); `usd-vrm-plugins` `vrmSchema` 0.9 and
  `vrmImaging` 0.9.0, each registered only through `PXR_PLUGINPATH_NAME`
- Occasion: the first item [renderer report 02](02-2026-09-26-mat-q1-material-inputs.md)
  left for Renderer Phase 1's MToon path — a material Sprim that reads the
  Hydra material prim's `vrm` container from the terminal scene index in
  `Sync` and selects the model by
  [material policy §3](../../design/MATERIAL_POLICY.md#3-selection-a-realization-is-chosen-not-merged)

## TL;DR

**`hdToon` now creates a material Sprim for every Hydra material. With
`vrmSchema` and `vrmImaging` in the session, the probe stage's material and
all 12 of the converted avatar's select MToon and carry their canonical
values; without them, every one is PreviewSurface. The values are read and
held, not drawn: meshes do not bind materials yet.**

## 1. What was run

```sh
ost build --jobs auto && ost test && ost validate
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra
ost validate --profile lookdev --intent hydra
toon-hydra2-material-test --stage adapters/hydra2/tests/material-probe.usda
toon-hydra2-material-test --stage <converted avatar>.usdz
```

`toon-hydra2-material-test` ([material_test.cpp](../../../adapters/hydra2/tests/material_test.cpp))
without arguments is the CTest `toon-renderer-hydra-material`: a retained
scene index spells `vrmImaging`'s locators itself
([its imaging policy §28.1](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/VRM_IMAGING_POLICY.md#281-the-vrm-locator-hierarchy)),
so it needs no format plugin and no GPU. With `--stage` it puts a stage
through `UsdImagingCreateSceneIndices` and scene index emulation into the real
`HdToonRenderDelegate` and prints what each material Sprim read. The stage
runs were repeated with `PXR_PLUGINPATH_NAME` empty, naming `vrmSchema`'s
resources, and naming both, as in report 02. The avatar is the converted
avatar of [report 01 §4](01-2026-09-26-phase0-mesh-camera.md#4-a-skinned-avatar);
it is local test data and not in the repository.

## 2. Results

| Build | Result |
| --- | --- |
| `core` | `ost test` 4/4; `toon-render-world` now also checks that a value edit advances only a material's parameters revision and a model or alpha-mode edit its structure revision; `ost validate` passed |
| `hydra` intent | `ost test` 9/9, including the new `toon-renderer-hydra-material`; `ost validate` passed with 13 of 13 renderer assertions |

| Stage | Session | Materials | MToon |
| --- | --- | --- | --- |
| `material-probe.usda` | nothing, or `vrmSchema` only | 1 | 0 |
| | `vrmSchema` + `vrmImaging` | 1 | 1: base colour (0.8, 0.7, 0.6), shading shift -0.1 at time 1, toony 0.9 |
| converted avatar | nothing | 12 | 0 |
| | `vrmSchema` + `vrmImaging` | 12 | 12: 4 with alpha mode `BLEND`, 8 `OPAQUE`; 8 with an outline |

The toony 0.9 of the probe stage is not authored: `vrmImaging` supplies the
schema's fallback, so the Sprim never had to interpret an absence. A
mutation of the retained test's expectation made it fail, so it checks what
it claims.

`testusdview` drew the avatar with both plugins in the session as a flat
silhouette in the meshes' display colour: the new Sprims were created and
synced without disturbing the mesh path.

## 3. What it means

- **The read path report 02 proposed works in a delegate.** The Sprim reads
  its own prim from `GetRenderIndex().GetTerminalSceneIndex()` in `Sync`; no
  network or `Get` is involved.
- **The silent fall-back is real, and it is the session's.** The same
  avatar is 12 PreviewSurface materials or 12 MToon ones depending only on
  `PXR_PLUGINPATH_NAME`, with no error either way. Composing the `usdview`
  host session with both plugins is still on the
  [roadmap](https://github.com/animu-sphere/hydra-toon/blob/428202238e9d2d2346b21371c5d2f383d1fe1426/docs/roadmap/current.md#before-renderer-phase-1).
- **Value-only changes still stop short.** A time move across
  `shadingShiftFactor` does not reach this Sprim, as report 02 predicted; the
  delegate-side route is the next roadmap item.

## 4. Observed on the way

An incremental `hydra` build segfaulted in `toon-headless`, the post-build
evidence run. Ninja had recorded no dependencies for
`adapters/headless/…/main.cpp.obj` (`ninja -t deps` shows `#deps 0`), so the
object was not recompiled after `FrameSnapshot` gained a member and linked
against the new core with the old layout. Deleting the object fixed the
build. The `core` tree records none for it either, even right after a fresh
compile, while every other object's dependencies are recorded; the cause was
not found.

## 5. Not checked

Material binding and drawing — Renderer Phase 1. Textures
(`VrmTextureInfoAPI`). `ost renderer view`. No cost was measured.
