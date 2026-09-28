# The scene's unit: a world-coordinates outline is metres in a stage of any `metersPerUnit`

> Followed by [report 18](18-2026-09-28-outline-depth-bias.md): a depth bias on the hull settles §4's fight between a hull as thin as nothing and its surface.

- Date: 2026-09-28
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.13`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`) and the published `vrmImaging` 0.10.0
  (`sha256:894fd616…`) of [report 12](12-2026-09-28-published-vrmimaging.md),
  unchanged
- Occasion: the first part of [v0.2.0](../../roadmap/v0.2.0.md)'s outline
  stability item, after [report 16](16-2026-09-28-msaa.md); report 10 left a
  stage not in metres unchecked

## TL;DR

**MToon's world-coordinates outline width is metres, and the renderer now
divides it by the scene's `metersPerUnit`. Hydra does not carry a stage's
metadata, so `hdToon` takes the unit as the render setting
`toon:metersPerUnit`, 1 by default, which the host sets. The avatar
referenced into a stage of centimetres, scaled by 100, draws as it does in
its own stage of metres once the setting is 0.01: 7 pixels differ, by at
most 20 levels; left at 1, its outlines vanish, its all but flat hulls
fight the skirt for depth, and 3,661 pixels differ. A
new unit is a new scene revision and nothing else: no slot, upload,
pipeline or target.**

## 1. What changed

- **Core.** `RenderWorld::SetMetersPerUnit` sets the scene's linear unit in
  metres, 1 until set; a value that is not positive and finite changes
  nothing. `FrameSnapshot` and `DrawList` carry it as `meters_per_unit`. A
  new unit advances the scene revision and no view, mesh or material
  revision.
- **Backend.** The MToon draw constants' last word, which was padding,
  carries the scene's units per metre, so the constants stay 128 bytes and
  a new unit rewrites no parameter slot.
- **Shader.** `mtoon_outline`'s vertex stage multiplies a
  worldCoordinates width by those units per metre; a screenCoordinates
  width is a ratio of the screen height, which no unit changes.
- **`hdToon`.** No scene index in OpenUSD 26.08 exposes `metersPerUnit`:
  `HdSceneGlobalsSchema` carries the primary camera, the active render pass
  and settings, the time codes, the current frame and the scene state id,
  and no imaging header names the unit. The delegate therefore offers the
  render setting `toon:metersPerUnit` and reads it each frame into the
  world. Its default is a float: usdview's settings list takes a
  `double` setting out with the warning *"doesn't have a UI
  implementation"*, and a float it shows.

## 2. What was run

```sh
ost build --jobs auto && ost test && ost validate
ost renderer viewport -- --frames 8 --hidden
ost validate --intent renderer-viewport
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra && ost validate --profile lookdev --intent hydra
ost package --profile lookdev --intent hydra
ost artifact import dist/toon/0.1.0/cy2026-windows-x86_64-py313-lookdev--hydra
# report 16's Formation, with toon at the new digest
ost formation lock formation.toml
TOON_UNITS_SHOTS=-m=- ost formation run formation.toml -- \
    testusdview <avatar.usdz> --renderer Toon --testScript vrm_units_check.py
TOON_UNITS_SHOTS=-cm-unset=-,-cm=0.01 ost formation run formation.toml -- \
    testusdview alicia-cm.usda --renderer Toon --testScript vrm_units_check.py
```

The new package is `sha256:57e37ee284438bbb0c224953b5bd5f00f7cd3af885580bd66e89e47a087c329b`,
in this workstation's registry only. `alicia-cm.usda` is a stage with
`metersPerUnit = 0.01` whose `/World/Avatar` references the avatar and
scales it by 100, since composition does not rescale a reference. The prim
is untyped, so the reference's `SkelRoot` survives; declared an `Xform`, it
took the avatar's skinning away, and every draw drew its rest points.
`vrm_units_check.py`, with `TOON_EXPECT_MTOON=12`, asserts that the
delegate offers `toon:metersPerUnit` at 1, takes the shots it is given,
setting the setting first where the shot names a value, and asserts that
each shot after the first advanced the scene revision and changed no
counter: pipelines, targets, uploads, slot writes, joint buffer writes and
draws.

## 3. Results

| Build | Result |
| --- | --- |
| `core` | `ost test` 4/4; `ost validate` passed |
| `hydra` intent, canonical `lookdev` | `ost test` 10/10; `ost validate` passed |
| `renderer-viewport` | 8 frames presented, no swapchain recreation; `ost validate --intent renderer-viewport` passed |

`renderer.material.mtoon_outline` now runs in a scene of centimetres. Its
outline is 2 mm, which is 0.2 units, the width [report 10](10-2026-09-27-mtoon-outline.md)'s
check drew:

| Frame | Unit | Outline | Rim pixel | `material_writes` |
| --- | --- | --- | --- | --- |
| 1 | centimetre | worldCoordinates, 0.002 | green | 1 |
| 2 | metre | as 1 | background: 2 mm is 0.002 units | 1 |
| 3 | centimetre | none | background | 2 |
| 4 | centimetre | screenCoordinates, 0.1 of the height | green | 3 |
| 5 | centimetre | as 4, with a width texture whose G is 0 | background | 3 |

Without the conversion, frame 1 draws a hull of 0.002 units and fails. The
CTest `toon-render-world` asserts that a unit edit commits, reaches the
draw list and touches neither the camera nor geometry, and that 0 and −1
change nothing. The `usdview` smoke test asserts that the setting is
offered at 1, and that setting it to 0.01 advanced the scene revision and
changed no counter.

The avatar, the evidence at each shot:

| Stage | `toon:metersPerUnit` | `scene_revision` | `pipelines` | point / topology / skin uploads | `material_writes` | draws: MToon / transparent / outlined / skinned |
| --- | --- | --- | --- | --- | --- | --- |
| the avatar, metres | 1 | 2 | 4 | 20 / 20 / 20 | 13 | 20 / 5 / 15 / 20 |
| `alicia-cm.usda` | 1 | 2 | 4 | 20 / 20 / 20 | 13 | 20 / 5 / 15 / 20 |
| `alicia-cm.usda` | 0.01 | 3 | 4 | 20 / 20 / 20 | 13 | 20 / 5 / 15 / 20 |

The images, leaving out usdview's axes:

| Images | Pixels that differ | by more than 24 levels | largest difference |
| --- | --- | --- | --- |
| report 16's 4x shot → metres | 0 | 0 | 0 |
| metres → centimetres, setting 1 | 3,661 | 2,399 | 194 |
| metres → centimetres, setting 0.01 | 7 | 0 | 20 |

Every difference lies within the avatar's box, 152–444 by 66–467 on the
597×540 AOV.

## 4. Observations

- **The unit is the host's to state.** A stage's `metersPerUnit` is layer
  metadata that UsdImaging does not put into the scene index, so a Hydra
  renderer cannot find it; any host that opens a stage of another unit
  must set `toon:metersPerUnit`. The default, 1, is VRM's unit and the
  unit of every stage `usd-vrm-plugins` writes, so the avatar in its own
  stage needs nothing.
- **Too thin, not too thick.** Left at 1 in a stage of centimetres, the
  avatar's 0.5 mm outlines become 0.0005 cm. Mapped, the pixels that change
  by more than 24 levels are the outlines' silhouettes, which vanish, and
  blotches across the skirt's frills, where the outline colour now covers
  the surface. In a stage of a larger unit, the failure would be the
  opposite.
- **A hull as thin as nothing fights its surface.** The skirt's blotches
  are the hull, moved out by next to nothing, winning the depth test
  against the skirt's inner faces, which it now all but coincides with.
  With the unit right, the hull lies 0.5 mm behind them and loses. Nothing
  here depends on the unit: an outline thinner than the depth buffer
  resolves at the camera's distance does the same in any stage, which is
  [v0.2.0](../../roadmap/v0.2.0.md)'s outline stability to settle.
- **The residue is precision.** The 7 pixels that still differ lie on
  edges, by 1 to 20 levels: the scaled stage reaches the same image through
  another transform and camera distance.
- **usdview's axes are not the renderer's.** usdview draws its axes over
  the renderer's image in every stage, at a length it derives from the
  camera distance, and they are not a setting a script can turn off; they
  showed in the stage of metres and not in the stage of centimetres, and
  were left out of the comparison: 2,276 pixels, pure red, green or blue
  and their neighbours.

## 5. Not checked

A stage whose `metersPerUnit` a host sets on its own: no host here reads
the stage's unit and passes it on, and `ost renderer view` has not been
run. A screenCoordinates outline on a real asset. The rest of the outline
stability item: the width under animation, thin-outline aliasing and
flicker, how the width texture is sampled, and the hull's cost.
