# MAT-Q1 measured: canonical material values reach a classic delegate only through `vrmImaging`, and a value-only change produces no Sync

- Date: 2026-09-26
- Machine: Windows 11, MSVC 14.51
- Tooling: `ost 0.23.10`; the `hydra` intent's OpenUSD 26.08 runtime;
  `usd-vrm-plugins` `vrmSchema` 0.9 and `vrmImaging` 0.9.0 (Steps I0–I2),
  each registered only through `PXR_PLUGINPATH_NAME`
- Occasion: [MAT-Q1](../../design/MATERIAL_POLICY.md#9-open-questions), the
  last question before Renderer Phase 1's MToon path, and the Hydra path
  `usd-vrm-plugins` left unmeasured ([imaging policy §27](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/VRM_IMAGING_POLICY.md#27-measured-in-step-i0))

## TL;DR

**Neither Hydra's material network nor `HdSceneDelegate::Get` carries a
single `inputs:vrm:*` value to a classic `HdMaterial`. With `vrmSchema` and
`vrmImaging` in the session, the Hydra material prim carries all of them
under `vrm`, and `HdMaterial::Sync` can read them from the render index's
terminal scene index. An authored edit re-syncs the material. A value-only
change — a time sample — dirties `vrm/mtoon/<field>` alone, which emulation
translates to no dirty bit: the material is never synced. The delegate's
`SetTerminalSceneIndex` / `Update` hooks see that locator, and the new value,
before any Sprim sync.**

## 1. What was run

```sh
ost build --profile lookdev --intent hydra --jobs auto
toon-hydra2-material-probe adapters/hydra2/tests/material-probe.usda
```

The probe ran three times, with `PXR_PLUGINPATH_NAME` empty, naming
`vrmSchema`'s resources, and naming `vrmSchema`'s and `vrmImaging`'s. The
`vrmImaging` module was built by `usd-vrm-plugins` against its own `usd`
runtime and loaded into this one unchanged.

`toon-hydra2-material-probe` ([material_probe.cpp](../../../adapters/hydra2/tests/material_probe.cpp))
puts the stage through `UsdImagingCreateSceneIndices` and scene index
emulation — the path `testusdview` takes — into a render delegate whose only
prim is a classic `HdMaterial` that records each `Sync`. The stage
([material-probe.usda](../../../adapters/hydra2/tests/material-probe.usda))
has one Material with `VrmMaterialAPI` and `VrmMToonAPI`, three canonical
inputs no network reads, one of them time-sampled, and a `UsdPreviewSurface`.
It links nothing of a format repository. It then:

1. populates at time 1;
2. authors a new `inputs:vrm:mtoon:shadeColorFactor`;
3. moves the time to 2, across the time-sampled `shadingShiftFactor`.

## 2. Results

| Session | Phase | Locators dirtied | Syncs | Bits | `vrm` values read in `Sync` |
| --- | --- | --- | --- | --- | --- |
| nothing, or `vrmSchema` only | populate | — | 1 | `0x7c` | no `vrm` container |
| | authored edit | `material` | 1 | `0x7c` | no `vrm` container |
| | time move | — | 0 | — | — |
| `vrmSchema` + `vrmImaging` | populate | — | 1 | `0x7c` | `vrm` = `[material, mtoon]`; `shadeColorFactor` (0.5, 0.4, 0.3), `shadingShiftFactor` -0.1 |
| | authored edit | `material`, `vrm/mtoon/shadeColorFactor` | 1 | `0x7c` | `shadeColorFactor` (0.1, 0.2, 0.3) |
| | time move | `vrm/mtoon/shadingShiftFactor` | **0** | — | — |

In every session and phase, the network from `GetMaterialResource` had one
node and no `vrm` parameter, and `Get(id, "inputs:vrm:mtoon:shadeColorFactor")`
was empty. `0x7c` is `HdMaterial::AllDirty`.

The delegate's `Update()`, which `SyncAll` calls before any prim sync, saw
the same locators as the table's "dirtied" column in every phase, and in the
time move read `shadingShiftFactor` 0.2 from the terminal scene index.

## 3. What it means

- **The material network is not a path.** A realization authored by the
  format repository does not connect to the canonical inputs; the
  converted avatar of [report 01 §4](01-2026-09-26-phase0-mesh-camera.md#4-a-skinned-avatar)
  has 47 interface inputs on its body material and no consumer of any.
  What does not connect never enters the network.
- **`vrmImaging` is the path, and it is silent without its schema.** With
  `vrmSchema` alone there is no `vrm` container; with neither, not even
  `GetAppliedSchemas` names `VrmMToonAPI` (the prim's type info still lists
  it). A session that lacks either plugin shows a VRM material as
  PreviewSurface without an error.
- **Emulation drops value-only changes of a material.**
  `HdDirtyBitsTranslator::SprimLocatorSetToDirtyBits` maps only locators under
  `material` for a material Sprim, so `vrm/...` alone is `Clean`. Authored
  edits are not affected only because UsdImaging dirties the whole `material`
  locator on any interface-input edit
  ([imaging policy §28.3](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/VRM_IMAGING_POLICY.md#283-the-whole-material-dirtying-lived-with)).
  Time samples and anything else that reaches Hydra without a USD change
  notice — the path imaging policy §28.3 reserves for high-frequency
  values — never reach `Sync`.
- **The delegate can take them itself.** `SetTerminalSceneIndex` is called
  under emulation too, and an observer registered there sees
  `vrm/<group>/<field>` with the new value readable by `Update()`. That is a
  per-field route, narrower than a re-sync.

## 4. Not checked

`testusdview` with `vrmImaging` in the session; `hdToon` has no material
Sprim yet. `VrmTextureInfoAPI` and the time-sampled case of a value a network
does read. MMD: `mmdImaging` does not exist yet. No cost was measured.
