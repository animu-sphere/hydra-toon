# An expression bake played in `testusdview`: each time move rewrites the MToon slot and uploads nothing

- Date: 2026-09-27
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.13`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`), `vrmImaging` 0.9.0 (`sha256:3043a239…`) and `toon`
  (`sha256:8397030d…`) of [report 10](10-2026-09-27-mtoon-outline.md),
  unchanged
- Occasion: the live-slot half of `usd-vrm-plugins`' imaging Step I4, which
  that repository's v0.10.0 waits on — a canonical colour an expression bake
  drives reaches a live `hdToon` material slot without rebuilding geometry or
  pipelines. [Report 04](04-2026-09-26-material-value-route.md) proved the
  value route on a probe; this plays a bake in the host session.

## TL;DR

**`motion_retarget`'s bake of an MToon expression, played in `testusdview`
with `vrmImaging`, rewrites the material's parameter slot once per time move
and uploads no points, topology, skin or texture and creates no pipeline;
the skeleton's move writes the joint buffer, as report 09's fast path does.
At time 15 the half-red emission the bake wrote is on screen: red over green
goes from 0.99 to 1.62. Without `vrmImaging` the material is PreviewSurface,
no time move writes it, and the ratio stays at 0.99.**

## 1. The stage

The bake is `usd-vrm-plugins`' `expressions_mtoon.vrm` fixture, baked with
`expressive_clip.usda` by `motion_retarget` and flattened with
`usdVrmFileFormat` in the session, since the Formation composes no `.vrm`
reader. The fixture is one triangle skinned to `hips` and `spine`, bound to
`Face_Mat`: an MToon material with a dark grey lit colour (0.2) and shade
colour (0.1), double-sided, whose `happy` expression binds `emissionColor`
to red and binds no morph target. The bake writes that bind as time samples
on the Material's canonical `inputs:vrm:material:emissiveFactor` — black at
0, (0.5, 0, 0) at 15, red at 30 — and turns the hips about Y by 0°, 45° and
90° at the same times.

The existing I4 fixture, `expressions.vrm`, binds the same emission on a
glTF material. `hdToon` draws that as PreviewSurface with the fallback's
values until it reads a surface network (Renderer Phase 5), so its bake
moves nothing here; `usd-vrm-plugins` added the MToon twin for this run
([usd-vrm-plugins#260](https://github.com/animu-sphere/usd-vrm-plugins/pull/260)), and
reads its bake through the stage scene index in its own suite
(`workspace_expression_bake_mtoon_imaging`).

## 2. What was run

```sh
# In usd-vrm-plugins, with usdVrmFileFormat and vrmSchema registered.
motion_retarget --avatar plugins/usdVrmFileFormat/tests/fixtures/expressions_mtoon.vrm \
    --animation tools/motionRetarget/tests/fixtures/expressive_clip.usda \
    --output expression_bake_mtoon.usda
# Flattened by Usd.Stage.Flatten(), then, here, two Formations of report
# 10's digests: runtime + vrmImaging + toon, and runtime + toon.
ost formation lock formation.toml
ost formation run formation.toml    # testusdview expression_bake_mtoon.usda
                                    #   --renderer Toon
                                    #   --testScript vrm_expression_check.py
```

`vrm_expression_check.py` takes a shot at 0, 15 and 30, moving only the
time (`appController.setFrame`), and reads the last `TOON_HYDRA_EVIDENCE`
line after each. It measures the shot as the mean red over the mean green of
the pixels that differ from the background. It asserts, between consecutive
shots: `pose_writes` advanced; `point_uploads`, `topology_uploads`,
`skin_uploads`, `texture_uploads`, `pipelines`, `draws` and the material
counts did not; `material_writes` advanced by exactly one with `vrmImaging`
and not at all without it. And at 15: the ratio rose by more than 0.2 with
`vrmImaging` and moved by less than 0.1 without it.

## 3. Results

Both runs passed. The evidence at each shot:

| Formation | Time | `material_writes` | `pose_writes` | point / topology / skin uploads | `texture_uploads` | `pipelines` | red / green | pixels |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| runtime + `vrmImaging` + `toon` | 0 | 2 | 1 | 1 / 1 / 1 | 0 | 3 | 0.99 | 9,667 |
| | 15 | 3 | 2 | 1 / 1 / 1 | 0 | 3 | **1.62** | 9,824 |
| | 30 | 4 | 3 | 1 / 1 / 1 | 0 | 3 | — | 419 |
| runtime + `toon` | 0 | 2 | 1 | 1 / 1 / 1 | 0 | 3 | 0.99 | 9,667 |
| | 15 | 2 | 2 | 1 / 1 / 1 | 0 | 3 | 0.99 | 9,824 |
| | 30 | 2 | 3 | 1 / 1 / 1 | 0 | 3 | — | 419 |

With `vrmImaging`, `materials_mtoon` is 1 and `materials_preview` 1, the
fallback material; without it they are 0 and 2. `material_writes` starts at
2 in both: `Face_Mat` and the fallback, each written once. There is one draw,
skinned, and no outline, since the fixture asks for none.

## 4. Observations

- **The bake reaches the slot as a value.** Each time move is one
  `material_writes`: the delegate's terminal-scene-index observer sees the
  `vrm/material/emissiveFactor` locator dirtied without `material` and
  re-reads the material in `Update()` (report 04), and the render world
  advances its parameters revision alone. Nothing structural changed, so no
  pipeline, texture or geometry work followed. That is Step I4's done-when,
  measured in the host session.
- **The skeleton moves on its own path.** The same time move writes the
  mesh's joint buffer (`pose_writes`) and nothing else of the mesh, which is
  report 09's fast path; the fixture binds no morph target, so the CPU blend
  shape stand-in uploads no points.
- **At 30 the triangle is edge-on.** The hips have turned 90° about Y, so the
  419 pixels that differ from the background are the viewport's axis lines,
  the same in both runs. The shot still shows the slot written and nothing
  uploaded; only the pixel measure is left out.
- **The first fixture colour hid the emission.** With the base colour
  `expressions.vrm` uses, (0.8, 0.6, 0.5), the lit side already clips red in
  the RGBA8 target under the stand-in lights (report 07), so half-red
  emission changed the ratio by 0.003 while the slot was written all the
  same. The fixture's colours are dark and grey for that reason: red over
  green is 1 however the triangle is lit, until the emission moves it.

## 5. Not checked

The value in the slot itself: the evidence counts writes, and the pixels show
the emission, but no probe reads the parameter buffer at each time. Linux.
An avatar: no avatar this workstation has binds a material colour in an
expression (AliciaSolid, Seed-san and the constraint sample bind morph
targets only). Scrubbing or playback at frame rate; each shot is a settled
frame. No timing was taken.
