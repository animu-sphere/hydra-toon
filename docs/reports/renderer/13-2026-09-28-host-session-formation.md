# The VRM host session is a Formation in the repository, and the published `toon` 0.1.0 draws the avatar as the workstation's packages did

- Date: 2026-09-28
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.13`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`); `vrmImaging` 0.10.0, carrying `vrmSchema`, as in
  [report 12](12-2026-09-28-published-vrmimaging.md); `toon` 0.1.0, pulled
  from `ghcr.io/animu-sphere/hydra-toon`
- Occasion: [v0.1.0](../../releases/v0.1.0.md) published the `hydra`
  intent's package. Every member of the VRM host session is now published,
  so its Formation can pin published digests only and be committed — the last
  item [before Renderer Phase 1](../../roadmap/current.md#before-renderer-phase-1)'s
  MToon path. The release lane's runner has no Vulkan device, so this is also
  the first time the published package draws on a GPU

## TL;DR

**[`formations/vrm-host-session/`](../../../formations/vrm-host-session/)
pins the runtime, `vrmImaging` 0.10.0 and the published `toon` 0.1.0, and
nothing else. Its lock carries no workstation path. Its declared command
needs no environment and no file outside the repository: on the committed
`material-probe.usda` it selects MToon for the one VRM material and exits 0,
and without `vrmImaging` the same check fails. On AliciaSolid, the published
package gives report 12's numbers: 12 MToon materials, 20 of 20 draws MToon
and skinned, 15 outlined.**

## 1. The package

The pin is the one row of the release's `toon-package-pins.json`:

| | Digest |
| --- | --- |
| Archive (`artifact` in a Formation) | `sha256:265328f06d0fcbdc718b459011f321c769dcf423881f8b67a12321254d728c29` |
| Pulled from | `oci://ghcr.io/animu-sphere/hydra-toon@sha256:a14c583fcd7a6b29c07b955808cbfc631820786589567618a3156182dbc91a93` |

It is the package the release workflow built and pushed; nothing was built
on this workstation for this report.

## 2. The Formation

`formation.toml` names each member by its archive digest and declares the
check as its command:

```toml
[command]
program = "testusdview"
args = ["../../adapters/hydra2/tests/material-probe.usda", "--renderer", "Toon", "--testScript", "vrm_material_check.py"]
```

The arguments are relative to the working directory
([ost report 06](../ost/06-2026-09-27-v0.23.13-report-05-reverified.md#1-what-was-done)),
so the Formation is run from its own directory. The stage is the MAT-Q1 probe
([report 02](02-2026-09-26-mat-q1-material-inputs.md)): one Material with
`VrmMaterialAPI` and `VrmMToonAPI`, no geometry.

`vrm_material_check.py` is the check reports 06–12 ran, with one change: when
`TOON_HYDRA_EVIDENCE` or `TOON_HYDRA_IMAGE` is unset, it sets it to a
temporary file inside `usdview`'s process before the shot. `hdToon` reads
`TOON_HYDRA_EVIDENCE` on every frame, so the frames of the shot are recorded.
A Formation's command declares no environment, so this is what lets the
declared command run with nothing set.

`formation.lock` records only digests and component-relative paths, so it is
committed. `ost formation run` leaves its run records in `.strata/` beside
the manifest, which Git ignores.

## 3. What was run

From `formations/vrm-host-session/`, with no `TOON_*` variable set:

```sh
ost artifact pull oci://ghcr.io/animu-sphere/hydra-toon@sha256:a14c583f… \
    --expect-artifact sha256:265328f0…
ost formation lock formation.toml
ost formation doctor formation.toml
ost formation run formation.toml
TOON_EXPECT_MTOON=12 ost formation run formation.toml -- testusdview <AliciaSolid.usdz> \
    --renderer Toon --testScript vrm_material_check.py
```

The pull passed every check it ran, the SBOM and provenance among them.
`doctor` passed all four of its checks. As a control, the same check ran in a
throwaway Formation of the runtime and `toon` alone, from a sibling
directory.

## 4. Results

| Formation | Stage | `TOON_EXPECT_MTOON` | Exit | `materials_preview` | `materials_mtoon` | `draws` / MToon / skinned / outline |
| --- | --- | --- | --- | --- | --- | --- |
| runtime + `vrmImaging` + `toon` | `material-probe.usda` | unset (1) | 0 | 1 | 1 | 0 |
| runtime + `toon` | `material-probe.usda` | unset (1) | 101, the assertion | 2 | 0 | 0 |
| runtime + `toon` | `material-probe.usda` | 0 | 0 | 2 | 0 | 0 |
| runtime + `vrmImaging` + `toon` | AliciaSolid | 12 | 0 | 1 | 12 | 20 / 20 / 20 / 15 |

Every run drew with `HdToonRendererPlugin`. On the avatar the published
package uploaded each mesh's topology and points once (20 each), 6 textures
once each, 20 skins, and wrote 13 material slots and 20 poses: the counts
of reports 08–10 and 12, which pinned workstation packages.

## 5. Observations

- **The declared command is the check.** It asserts the VRM material selected
  MToon, and it fails when `vrmImaging` is missing, which is the failure it
  exists for: without the plugin a VRM material silently draws as
  PreviewSurface.
- **Setting the evidence path from the script works.** `hdToon`'s
  `std::getenv` sees what Python's `os.environ` set in the same process.
- **A failed run is noisy on exit.** After the assertion, `usdview`'s
  teardown printed `HgiInteropOpenGL` and `HgiGLBuffer` GL errors; the
  passing runs printed none. They come after the check has failed and did not
  change the exit code.

## 6. Not checked

Linux, where no `toon` package is published. Whether the pull works without
credentials. The avatar is not redistributable, so only the probe run is
the Formation's own; the avatar run needs a local copy.
