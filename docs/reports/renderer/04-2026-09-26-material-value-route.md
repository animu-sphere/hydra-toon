# `hdToon` takes value-only material changes from the terminal scene index: a time move across a canonical value reaches `ToonMaterial` without a Sync

> Followed by [report 05](05-2026-09-26-vrm-usdview-session.md): `testusdview` run with the VRM plugins in the session, composed from packages.

- Date: 2026-09-26
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.10`; runtimes `cy2026-windows-x86_64-py313-core` and
  `…-lookdev` (OpenUSD 26.08); `usd-vrm-plugins` `vrmSchema` 0.9 and
  `vrmImaging` 0.9.0, each registered only through `PXR_PLUGINPATH_NAME`
- Occasion: the item [report 03](03-2026-09-26-material-sprim.md) left
  next on the [roadmap](https://github.com/animu-sphere/hydra-toon/blob/428202238e9d2d2346b21371c5d2f383d1fe1426/docs/roadmap/current.md#before-renderer-phase-1) —
  the delegate-side route for a `vrm/<group>/<field>` change that scene index
  emulation turns into no dirty bit
  ([report 02](02-2026-09-26-mat-q1-material-inputs.md);
  [material policy §8](../../design/MATERIAL_POLICY.md#8-values-that-change-at-run-time))

## TL;DR

**`HdToonRenderDelegate` now observes the terminal scene index. A material
prim whose `vrm` locators are dirtied without `material` is re-read in
`Update()`, before any Sprim sync, into the same `ToonMaterial` slot. On the
probe stage, a time move from 1 to 2 carries `shadingShiftFactor` from -0.1
to 0.2; with the route disabled it stays at -0.1.**

## 1. What was run

```sh
ost build --jobs auto && ost test && ost validate
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra
ost validate --profile lookdev --intent hydra
toon-hydra2-material-test --stage adapters/hydra2/tests/material-probe.usda
toon-hydra2-material-test --stage <converted avatar>.usdz
```

The CTest `toon-renderer-hydra-material` ([material_test.cpp](../../../adapters/hydra2/tests/material_test.cpp))
gained a value-only case: the retained scene index's `shadingShiftFactor` leaf
returns a value the test changes, and the test dirties only
`vrm/mtoon/shadingShiftFactor`. It checks that the change tracker leaves the
material `Clean` — emulation drops the change, as report 02 found — and that
after `SyncAll` the material holds the new value.

`--stage` now syncs at the stage's start time code, moves to its end time
code and syncs again, printing each material both times. The stage runs were
repeated with `PXR_PLUGINPATH_NAME` empty, naming `vrmSchema`'s resources,
and naming both, as in reports 02 and 03. The avatar is the converted avatar
of [report 01 §4](01-2026-09-26-phase0-mesh-camera.md#4-a-skinned-avatar);
it is local test data and not in the repository.

Both the CTest and the stage run were repeated with the call in `Update()`
commented out.

## 2. Results

| Build | Result |
| --- | --- |
| `core` | `ost test` 4/4; `ost validate` passed |
| `hydra` intent | `ost test` 9/9; `ost validate` passed |

| Stage | Session | Time 1 | Time 2 |
| --- | --- | --- | --- |
| `material-probe.usda` | nothing, or `vrmSchema` only | PreviewSurface | PreviewSurface |
| | `vrmSchema` + `vrmImaging` | MToon, shading shift -0.1 | MToon, shading shift **0.2** |
| | both, route disabled | MToon, shading shift -0.1 | MToon, shading shift -0.1 |
| converted avatar | `vrmSchema` + `vrmImaging` | 12 materials, 12 MToon | — (no time range) |

With the route disabled, the CTest failed with "a value-only change must
reach the material's values", after its check that the material was left
`Clean` had passed. It checks what it claims.

## 3. What it means

- **Value-only changes now reach the material, by the route report 02
  proposed.** The observer keeps the prim paths; `Update()` re-reads each one
  that is a live material Sprim. A dirtied `material` locator is left to
  `Sync`, which emulation still schedules and which reads everything.
- **The value lands as a value.** The route reads through the same
  normalization as `Sync` and hands the result to `RenderWorld`, which
  advances only the parameters revision unless the model, alpha mode or
  double-sidedness changed (CTest `toon-render-world`, report 03).
- **It re-reads the whole material, not the one field.** A material has a
  few dozen leaves; that is narrower than a re-sync, which would also re-read
  the network, but wider than the per-field route report 02 described. No
  cost was measured.

## 4. Not checked

`testusdview` playing a time-sampled VRM material; the host smoke test ran
without the VRM plugins. Material binding and drawing — Renderer Phase 1.
Textures. `usd-vrm-plugins`' Step I4 values (Renderer Phase 3). No cost was
measured.
