# The VRM Formations pin the published `vrmImaging` 0.10.0, and every run gives its report's numbers

- Date: 2026-09-28
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.13`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`), unchanged; `vrmImaging` 0.10.0, carrying `vrmSchema`,
  pulled from `ghcr.io/animu-sphere/usd-vrm-plugins`; each Formation's `toon`
  package unchanged from the report that made it
- Occasion: `usd-vrm-plugins` v0.10.0 published a `lookdev` package of
  `vrmImaging` and left re-pinning this repository's VRM Formation to its
  digests to this repository
  ([its roadmap](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/roadmap/current.md#shipped-v0100--canonical-materials-reach-hydra-)).
  Until now every Formation with `vrmImaging` pinned a 0.9.0 package built
  and packaged on this workstation ([report 06](06-2026-09-27-vrm-formation.md))

## TL;DR

**The six VRM Formations of reports 06–11 now pin the published `vrmImaging`
0.10.0 instead of the workstation's 0.9.0. Each locks, passes `doctor`, and
its `testusdview` check passes with the same evidence its report recorded:
12 MToon materials on the avatar, 20 of 20 draws MToon, 15 outlined hulls
falling to 13, the same skinning and upload counts, and the expression
bake's red over green of 0.99 and 1.62. The `toon` packages are still this
workstation's, so no Formation is in the repository yet.**

## 1. The package

The pin is the Windows row of the release's `lookdev-package-pins.json`:

| | Digest |
| --- | --- |
| Archive (`artifact` in a Formation) | `sha256:894fd616f1414d5b393ff0d50abbf7ef18cb603562133c72e642493b48f71667` |
| Pulled from | `oci://ghcr.io/animu-sphere/usd-vrm-plugins@sha256:84dbb7e550c55d249798f7288066de3589c383a75a35a6268328a497f147dda8` |

Its manifest names `usd-vrm-plugins`' `release.yml` at `refs/tags/v0.10.0`
(`e6bce65`) as the builder, and its environment puts both `vrmImaging` and
the `vrmSchema` bundle it carries on `PXR_PLUGINPATH_NAME`. The lock resolves
it as `vrmImaging` 0.10.0, target `cy2026-windows-x86_64-py313-lookdev`, on
runtime digest `sha256:330d3e83…`, the one the runtime above resolves to.

## 2. What was run

```sh
ost artifact pull oci://ghcr.io/animu-sphere/usd-vrm-plugins@sha256:84dbb7e5… \
    --expect-artifact sha256:894fd616…
# In each Formation: vrmImaging's artifact changed, nothing else.
ost formation lock formation.toml
ost formation doctor formation.toml
ost formation run formation.toml    # vrm_expression_check.py, the declared command
ost formation run formation.toml -- testusdview <avatar.usdz> \
    --renderer Toon --testScript <check>.py
```

The pull passed every check it ran, the SBOM and provenance among them.
Every `lock` and `doctor` passed. The avatar is AliciaSolid, with
`TOON_EXPECT_MTOON=12`.

## 3. Results

Every run exited 0, and each gave its report's numbers:

| Formation's report | `toon` | Stage | Check | Evidence, as in that report |
| --- | --- | --- | --- | --- |
| [06](06-2026-09-27-vrm-formation.md) | `sha256:e91ac742…` | avatar | `vrm_material_check.py` | 12 MToon, 1 PreviewSurface (the fallback) |
| [07](07-2026-09-27-mtoon-opaque.md) | `sha256:5b723bac…` | avatar | `vrm_material_check.py` | 20 of 20 draws MToon; 13 material writes; 2 pipelines |
| [08](08-2026-09-27-basic-textures.md) | `sha256:91bc5c8b…` | avatar | `vrm_material_check.py` | as 07, and 6 textures uploaded once each |
| [09](09-2026-09-27-gpu-skinning.md) | `sha256:7a679c13…` | avatar | `vrm_skinning_check.py` | 20 of 20 draws skinned; pose writes 20, 40, 60; point and topology uploads 20, 40, 40 |
| [10](10-2026-09-27-mtoon-outline.md) | `sha256:8397030d…` | avatar | `vrm_outline_check.py` | 15 outlined hulls, 15 after the width edit, 13 after `none`; material writes 13, 14, 15 |
| [11](11-2026-09-27-expression-bake.md) | `sha256:8397030d…` | `expression_bake_mtoon.usda` | `vrm_expression_check.py` | material writes 2, 3, 4; red over green 0.99, then 1.62 |

## 4. Observations

- **The swap is invisible to `hdToon`.** Every count, and the pixel ratio of
  the one check that measures pixels, is its report's, on the same `toon`
  package. The workstation's "0.9.0" was built from `usd-vrm-plugins`' `main`
  during the v0.10.0 cycle, before the version bump, with texture roles
  already in `vrmImaging`; this compares that build with the release, not
  v0.9.0 with v0.10.0.
- **The published pin is one digest pair.** A Formation names the archive
  digest; the OCI digest is only where to pull it from. The release writes
  both in `lookdev-package-pins.json`, so a Formation can be re-pinned from
  the release alone.

## 5. Not checked

The Formations without `vrmImaging`, which this pin does not touch. Linux,
where `usd-vrm-plugins` published the other `lookdev` package. Whether the
pull works without credentials: `usd-vrm-plugins` checked that for both
packages; this run used whatever `ost` found on this workstation. A
Formation in the repository: it pins published digests only, and the `toon`
packages are not published.
