# The VRM `usdview` session as a Formation: `ost formation run` draws with `hdToon`, and every VRM material is MToon with `vrmImaging` and PreviewSurface without it

- Date: 2026-09-27
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.11`; runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64` (OpenUSD 26.08,
  oneTBB 2022.1.0), pulled from GHCR by digest; `usd-vrm-plugins`
  `vrmImaging` 0.9.0, carrying `vrmSchema` 0.9.0, built and packaged against
  it
- Occasion: [report 05](05-2026-09-26-vrm-usdview-session.md) composed the
  session by hand because `ost formation resolve` refused it;
  [ost report 05](../ost/05-2026-09-27-v0.23.11-report-04-reverified.md)
  re-verifies that against `ost` 0.23.11

## TL;DR

**The session report 05 set by hand is now a Formation: the canonical
`lookdev` runtime, `vrmImaging` and `toon`, digest-pinned, resolved, locked
and run by `ost formation run`. `testusdview` draws with `hdToon`, and the
material counts are report 05's, exactly: 12 MToon on the avatar with
`vrmImaging`, none without. The Formation's own `usdview` command does not
start yet; the run supplies the host's Python.**

## 1. What was run

```sh
ost runtime pull cy2026 --profile lookdev --from-artifact sha256:b982656c… --force
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra && ost validate --profile lookdev --intent hydra
ost package --profile lookdev --intent hydra
ost artifact import dist/toon/0.1.0/cy2026-windows-x86_64-py313-lookdev--hydra
# in usd-vrm-plugins
ost plugin build plugins/vrmSchema --profile lookdev
ost plugin build plugins/vrmImaging --profile lookdev
ost plugin package plugins/vrmImaging --profile lookdev
ost artifact import plugins/vrmImaging/dist/plugins/vrmImaging/0.9.0/cy2026-windows-x86_64-py313-lookdev
# the Formation below, and one without the vrmImaging component
ost formation resolve formation.toml && ost formation lock formation.toml
ost formation doctor formation.toml
ost formation run formation.toml -- <host python.exe> run_testusdview.py <stage> \
    --renderer Toon --testScript vrm_material_check.py
ost build --jobs auto && ost test && ost validate
```

The Formation:

```toml
schema = "openstrata.formation/v1alpha1"

[formation]
name = "hydra-toon-vrm-usdview"

[runtime]
artifact = "sha256:b982656c07dd9147973e3c7197d9b050ee48dd1ac291988f7e6025c76c4a785b"

[[components]]
id = "vrmImaging"
kind = "plugin"
artifact = "sha256:3043a23979bf4d884807878247e221ef7d565548c547fef1ca15ce56117f6447"

[[components]]
id = "toon"
kind = "renderer"
artifact = "sha256:e91ac742b2b8e8b4db2144440d996adff6476fc6190626a543e5dfff7e77b1a7"

[command]
program = "testusdview"
args = ["material-probe.usda", "--renderer", "Toon", "--testScript", "vrm_material_check.py"]
```

`[command]` is what the Formation should run; it cannot yet, because `run`
gives it no Python (ost report 05 Q1). Each run overrode it after `--` with
the host's `python.exe` and a launcher that finds `testusdview` on the
Formation's `PATH` and runs it with `runpy`. The environment — the runtime,
the plugin directories, `PATH` — is the Formation's.

The `testusdview` script takes one shot and asserts that the renderer is
`HdToonRendererPlugin` and that the last `TOON_HYDRA_EVIDENCE` line has the
expected `materials_mtoon`. The stages are
`adapters/hydra2/tests/material-probe.usda` and the converted avatar of
[report 01 §4](01-2026-09-26-phase0-mesh-camera.md#4-a-skinned-avatar),
which is local test data and not in the repository.

## 2. Results

| Build | Result |
| --- | --- |
| `core` | `ost test` 4/4; `ost validate` passed |
| `hydra` intent, canonical `lookdev` | `ost test` 9/9; `ost validate` passed with 13 of 13 renderer assertions |

| Artifact | Kind | Target | Digest |
| --- | --- | --- | --- |
| `openstrata-cy2026-windows-x86_64-py313-lookdev` 26.08 | runtime, published | `windows-x86_64-msvc143-py313` | `sha256:b982656c07dd9147973e3c7197d9b050ee48dd1ac291988f7e6025c76c4a785b` |
| `toon` 0.1.0, `hydra` intent | package, `renderer` component | `cy2026-windows-x86_64-py313-lookdev` | `sha256:e91ac742b2b8e8b4db2144440d996adff6476fc6190626a543e5dfff7e77b1a7` |
| `vrmImaging` 0.9.0 | plugin | `cy2026-windows-x86_64-py313-lookdev` | `sha256:3043a23979bf4d884807878247e221ef7d565548c547fef1ca15ce56117f6447` |

`toon` and `vrmImaging` are in this workstation's registry only.

`toon`'s package now comes from the `hydra` intent's own build tree and
declares `lib/usd/hdToon/resources` for `PXR_PLUGINPATH_NAME`, so report 05's
hand-corrected plugin directory is no longer needed: the Formation's
environment is the packages' declared one, unchanged.

| Stage | Formation | Renderer | `materials_preview` | `materials_mtoon` |
| --- | --- | --- | --- | --- |
| `material-probe.usda` | runtime + `toon` | `HdToonRendererPlugin` | 2 | 0 |
| | runtime + `vrmImaging` + `toon` | `HdToonRendererPlugin` | 1 | 1 |
| converted avatar | runtime + `toon` | `HdToonRendererPlugin` | 13 | 0 |
| | runtime + `vrmImaging` + `toon` | `HdToonRendererPlugin` | 1 | 12 |

Every run exited 0. As in report 05, the one PreviewSurface slot left with
`vrmImaging` is the fallback material Sprim. The avatar's 20 meshes drew in
the rest pose, flat grey, uploading topology and points once each.

`formation lock` wrote a lock with no machine paths, and `doctor` passed all
four checks. The first runs, from a Formation under this session's scratch
directory, did not start `usdview`: Qt could not load its platform plugin
from a 254-character path (ost report 05 Q2). The runs above are from
`C:\dev\hydra-toon\formations\`.

## 3. What it means

- **The roadmap's session is composed, not hand-set.** Every directory on
  `PXR_PLUGINPATH_NAME` and `PATH` came from a digest-pinned package's
  declared contract through `ost formation`, and the counts match report 05's
  hand-set ones. `vrmImaging` alone brings `vrmSchema`.
- **The silent fall-back is still there.** Without `vrmImaging`, the avatar
  draws with 13 PreviewSurface slots and no error; a Formation is what keeps
  the plugin in the session.
- **Two things remain before the session is a checked-in, repeatable run.**
  The Formation's own command must start (ost report 05 Q1), and it must pin
  published digests for `toon` and `vrmImaging`, not this workstation's.

## 4. Not checked

The Formation's `[command]` as written. The interactive `usdview`. A
Formation from published component digests, and so any machine but this one.
Linux and macOS. No cost was measured.
