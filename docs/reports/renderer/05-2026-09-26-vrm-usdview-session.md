# The VRM `usdview` session, composed from packages: `hdToon` selects MToon in `testusdview`, and the Formation that should compose it does not resolve

> Followed by [report 06](06-2026-09-27-vrm-formation.md): the same session composed and run by `ost formation`, with the same counts.

- Date: 2026-09-26
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.10`; runtime `cy2026-windows-x86_64-py313-lookdev`
  (OpenUSD 26.08, oneTBB 2021.12), exported into the local artifact registry
  and re-materialized from it; `usd-vrm-plugins` `vrmSchema` 0.9.0 and
  `vrmImaging` 0.9.0, built and packaged against that runtime
- Occasion: the [roadmap](https://github.com/animu-sphere/hydra-toon/blob/428202238e9d2d2346b21371c5d2f383d1fe1426/docs/roadmap/current.md#before-renderer-phase-1)
  item that [report 03](03-2026-09-26-material-sprim.md) and
  [report 04](04-2026-09-26-material-value-route.md) left open — `vrmSchema`
  and `vrmImaging` in the `usdview` host session, composed as bundles rather
  than by hand

## TL;DR

**Composed from the three packages' own files, a `testusdview` session draws
with `hdToon` and reports its materials per frame: the converted avatar's 12
VRM materials are MToon with `vrmImaging` in the session and PreviewSurface
without it. `ost formation resolve` refuses every package in the session,
so the session is not yet a Formation; what blocks it is `ost`'s, and is
[ost report 04](../ost/04-2026-09-26-v0.23.10-a-renderer-formation.md).**

## 1. What was run

```sh
ost runtime export cy2026 --profile lookdev --slim
ost runtime pull cy2026 --profile lookdev --from-artifact <exported digest> --force
ost build --profile lookdev --jobs auto          # then TOON_ENABLE_HYDRA2=ON in its cache, and again
ost test --profile lookdev
ost package --profile lookdev
ost artifact import dist/toon/0.1.0/cy2026-windows-x86_64-py313-lookdev
# in usd-vrm-plugins, with its strata.lock pinned to lookdev for the run and restored after
ost plugin build plugins/vrmSchema --profile lookdev
ost plugin build plugins/vrmImaging --profile lookdev
ost plugin package plugins/vrmSchema --profile lookdev
ost plugin package plugins/vrmImaging --profile lookdev
ost artifact import <each package's dist directory>
ost formation resolve <formation>.toml
ost artifact extract <digest> <directory>        # toon, vrmImaging
testusdview <stage> --renderer Toon --testScript <script>
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra && ost validate --profile lookdev --intent hydra
ost build --jobs auto && ost test && ost validate
```

The frame evidence `hdToon` appends to `TOON_HYDRA_EVIDENCE` in a host
session gained two fields, `materials_preview` and `materials_mtoon`: how
many material slots in the frame's snapshot selected each model. The
`testusdview` script took one shot and printed the frame's line.

The session was composed without `ost`, from the extracted packages:
`PXR_PLUGINPATH_NAME` named the directories each package's component
contract declares, and `PATH` their `bin` and `lib`, over the runtime's own
environment. Three `PXR_PLUGINPATH_NAME`s were run — the declared one, `hdToon`
alone at its installed resources, and `hdToon` with `vrmImaging`'s two
declared directories (its own, and the `vrmSchema` it carries under
`runtime/bundles/`). The avatar is the converted avatar of
[report 01 §4](01-2026-09-26-phase0-mesh-camera.md#4-a-skinned-avatar); it is
local test data and not in the repository.

## 2. Results

| Build | Result |
| --- | --- |
| `core` | `ost test` 4/4; `ost validate` passed |
| `hydra` intent | `ost test` 9/9; `ost validate` passed |
| `lookdev`, no intent, `TOON_ENABLE_HYDRA2=ON` | `ost test` 9/9; `ost package` validation passed |

| Artifact | Kind | Target | Digest |
| --- | --- | --- | --- |
| `openstrata-cy2026-windows-x86_64-py313-lookdev` 26.08 | runtime | `windows-x86_64-msvc143-py313` | `sha256:4a20422e782cc2777561017a4d1899a13edbeacdc64ecd69a80ccd749aad2020` |
| `toon` 0.1.0 | package, `renderer` component | `cy2026-windows-x86_64-py313-lookdev` | `sha256:c984faef2e8b5bc8cc9c3d8402bf80adad7d2c25e16f442bc1d7578d5303f7e6` |
| `vrmImaging` 0.9.0 | plugin | `cy2026-windows-x86_64-py313-lookdev` | `sha256:ef3469c74720bda57220a25c63e2d77cb20b4ecc7b6fa147a85aee39f4ccec0d` |
| `vrmSchema` 0.9.0 | plugin | `cy2026-windows-x86_64-py313-lookdev` | `sha256:f8fafc8461cfec2a29bcd837443acfb28f082254acb449723d28b5804d5d8b8f` |

These are in this workstation's registry only; none is published.

| Stage | `PXR_PLUGINPATH_NAME` | Renderer | `materials_preview` | `materials_mtoon` |
| --- | --- | --- | --- | --- |
| `material-probe.usda` | as the packages declare | `Toon` is not a choice: `testusdview` offers `Storm`, `GL` | — | — |
| | `hdToon` | `HdToonRendererPlugin` | 2 | 0 |
| | `hdToon` + `vrmImaging` | `HdToonRendererPlugin` | 1 | 1 |
| converted avatar | `hdToon` | `HdToonRendererPlugin` | 13 | 0 |
| | `hdToon` + `vrmImaging` | `HdToonRendererPlugin` | 1 | 12 |

The one PreviewSurface slot left with `vrmImaging` is the fallback material
Sprim, which is created and never synced; the probe stage has one material
and the avatar 12, as reports 03 and 04 counted.

The Formation named the runtime, `vrmImaging` as a `plugin` and `toon` as a
`renderer`, and ran `usdview` on the probe stage. `ost formation resolve`
refused it at its first component:

```text
error[FORMATION_INCOMPATIBLE_COMPONENT]: component 'vrmImaging' is incompatible: target 'cy2026-windows-x86_64-py313-lookdev' does not match runtime platform 'cy2026' and variant 'windows-x86_64-msvc143-py313'
```

A Formation of the runtime and `toon` alone was refused the same way, and so
was one of the canonical `usd` runtime and a `vrmSchema` 0.9.0 package
built against it. Ost report 04 has the detail.

## 3. What it means

- **The session works when its members are found.** Report 03 counted
  MToon in a probe outside any host; the same counts now come from
  `testusdview` drawing with `hdToon`. The silent fall-back is still there:
  13 PreviewSurface slots and no error when `vrmImaging` is missing.
- **The composition is the packages', not ours.** Every directory the
  session named came from a package's declared contract except `hdToon`'s,
  whose contract names `plugin/usd` while the renderer template installs it
  under `lib/usd/hdToon/resources`. With the declared directory `usdview`
  never sees the renderer.
- **Only `vrmImaging` needs naming.** Its package carries the `vrmSchema` it
  was built against and declares both directories, so a session names one
  VRM package.
- **The roadmap item is not done.** It asks for a composed session, not a
  hand-set environment; that waits on `ost`.

## 4. Not checked

A session with `vrmImaging` listed but `vrmSchema` missing from its package.
Listing `vrmSchema` beside `vrmImaging`, which would register it twice.
`ost formation lock`, `env`, `doctor` and `run`, which all need a resolved
Formation. The interactive `usdview`. Linux and macOS. Time-sampled values
in `testusdview` (report 04's route). No cost was measured.
