# MMD's self-shadow: a light's depth map, and receivers shaded as `full.fx`'s self-shadow pass

- Date: 2026-10-12
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Tooling: `ost 0.23.14`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`); `usd-mmd-plugins` v0.2.0's `mmdImaging` at the
  `lookdev` profile (`sha256:39f3e24d…`, [report 53](53-2026-10-11-mmd-material-read.md));
  this branch's `toon` `hydra` package, built here
  (`sha256:7a5abacc4b3487242d29e5bee101031ccd3452f303b59b4e8b1ff31fec432994`); Python 3 with Pillow and NumPy for the comparisons
- Occasion: v0.4.0's MMD materials: the self-shadow that `castSelfShadow`
  and `receiveSelfShadow` ask for, which [report 59](59-2026-10-12-mmd-sub-texture.md)
  left as the largest difference from MMD's frames
  ([roadmap](../../roadmap/v0.4.0.md)).

## TL;DR

**MMD's self-shadow now draws. Each frame, before the scene, `mmd_shadow`,
the eighth scene pipeline, draws a depth map of every MMD draw whose
material casts the self-shadow, seen from the first direct light when it is
directional and fitted along it to the MMD draws that cast or receive. A
material that receives it is shaded as MikuMikuEffect's `full.fx` shades its
self-shadow pass: not toned by its ramp, and falling to its colour times the
ramp's far side where the map, or the surface turning from the light, cuts
the light. On a model's two MMD captures, hdToon's frame with self-shadow is
within 4 levels of MMD's with it on 73% of the pixels, where it was 38%, a
mean difference of 5.62 where it was 11.21; without, it is as near MMD's
without as before. How deep behind the map a point must lie to be shadowed
hardly changes that, so the comparison's constants stay provisional, as
MAT-Q5.**

## 1. What MMD does

PMX's material flags `0x04` and `0x08` ask a material to draw into MMD's
self-shadow map and to be shaded by it
([`usd-mmd-plugins` PMX contract](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/PMX_CONTRACT.md));
the stage authors them as `castSelfShadow` and `receiveSelfShadow`, which
the hydra adapter already read. MMD itself has a switch for the
self-shadow, on in the captures below, and a self-shadow distance, which a
stage does not carry.

`full.fx`, which reproduces MMD's own shading and which `mmd_opaque`
follows ([report 56](56-2026-10-11-mmd-surface.md)), draws a receiving
material in its `object_ss` pass:

- its colour starts as the non-self-shadow pass's does, MMD's lighting term
  with the base texture and sphere applied, and the same colour is the
  shadow's colour;
- the specular is added to the lit colour only;
- the ramp does not tone the lit colour; the shadow's colour is multiplied
  by the ramp's colour (`TOONCOLOR`), read here as its far-side texel;
- the share of the lit colour is the smaller of `saturate(3 N·L)` and the
  map's `1 − saturate((depth − map depth) × 1500 − 0.3)`, where the depths
  are in the map's own clip space; a point outside the map is lit.

The map is the depth of every casting material, drawn from the light
without regard to its texels' alpha.

## 2. The change

- **Core.** `DrawList::mmd_self_shadow`, on by default, is MMD's switch, a
  per-frame value like `outlines`. `OutlineBounds::SceneBounds` bounds a
  mesh's posed points in the scene's space from the envelopes it already
  keeps for hull omission, and, unlike `OutsideView`, bounds a morphed
  mesh, without its offsets.
- **Backend.** A `ShadowMap` of 2,048 × 2,048 D32 texels. Each frame, when
  MMD's switch is on, the first direct light is directional and some MMD
  draw receives, it fits an orthographic projection along the light to the
  box around every MMD draw that casts or receives, padded by 2%, and
  `mmd_shadow` draws every casting draw's skinned points into it, both
  faces, single-sampled, pushed back by the slope; a frame with no map
  draws nothing into it. The map and its parameters, its clip space from
  view space and MMD units per unit of its depth, are bindings 5 and 6 of
  the material descriptor set; the receive flag is the slot's `modes.z`.
  `OffscreenStatistics::shadow_draws` counts the draws into it.
- **Shading.** `ShadeMmd` takes `full.fx`'s self-shadow path for a
  receiving material while the map is drawn, reading the map texel by
  texel. The comparison is on a depth in MMD units: shadowed from 0.02 MMD
  units behind what the map saw first and wholly by 0.087, `full.fx`'s form
  with 15 per MMD unit for its 1,500 per unit of a depth whose range MMD
  does not state.
- **Hosts.** The render setting `toon:mmdSelfShadow`, true by default;
  `draws_mmd_shadow` in `TOON_HYDRA_EVIDENCE`; the viewport's
  `--mmd-self-shadow on|off` and an overlay checkbox.

## 3. Tests

- `renderer.material.mmd_self_shadow` (headless, GPU): a white MMD wall
  with `toon01`'s shared ramp and a square in front of it, under a light
  travelling along (1, 0, −1). The wall is 255 where the light reaches it
  and 156, `toon01`'s far side decoded, in the square's shadow, and both
  draw into the map without the wall shadowing itself. MMD's switch off, a
  wall that does not receive, a square that does not cast, or a point
  light as the first direct light each leave the centre lit.
- `renderer.frame.persistence` and the `usdview` smoke test count eight
  pipelines.
- `toon-outline-bounds`: `SceneBounds` follows the mesh's transform and
  refuses a non-finite one.

The OpenStrata gates passed, each with `ost build`, `ost test` and
`ost validate`: `core` 15/15, `hydra` 24/24 and `viewport-usd` 55/55; `ost package` packaged the `hydra` build twice, to
the same digest.

## 4. Against MMD's captures

The user's two captures of a local model, unmodified, in MMD v9.32 at
3,840 × 2,160 from centre (0, 10, 0), distance 45 and a 30° field of view,
one with MMD's self-shadow and one without (report 59 §5). hdToon drew the
model from the same camera under MMD's default light through the local MMD
Formation, once with `toon:mmdSelfShadow` and once without. Of the 562,270
pixels both captures cover opaquely, MMD's self-shadow changes 277,083 by
more than 4 levels.

| hdToon frame | MMD capture | Mean difference | Within 4 levels | hdToon over by 20 or more | MMD over by 20 or more |
| --- | --- | --- | --- | --- | --- |
| self-shadow off | without self-shadow | 5.34 | 76.3% | 23,218 | 17,571 |
| self-shadow off | with self-shadow | 11.21 | 41.0% | 100,037 | 13,160 |
| self-shadow on | with self-shadow | 5.62 | 72.7% | 21,607 | 20,552 |
| self-shadow on | without self-shadow | 11.35 | 38.9% | 14,486 | 95,638 |

On the 277,083 pixels the self-shadow changes, hdToon's frame with it is
5.52 from MMD's with it on average, where its frame without was 17.11. The
hair's shadow on the forehead, the shadowed thigh and the darker hair, cape
lining and ornaments that report 59 found missing now draw; what differs
lies along contours, a fraction of a pixel apart, and at the skirt, which
MMD's physics moved. 19 of the model's 22 draws cast.

The comparison's sharpness, the depth behind the map at which a point
falls into shadow, barely matters on this frame:

| Sharpness, per MMD unit | Shadowed from, MMD units | Wholly by | Mean difference | Within 4 levels |
| --- | --- | --- | --- | --- |
| 5 | 0.06 | 0.26 | 5.53 | 73.2% |
| 15 | 0.02 | 0.087 | 5.62 | 72.7% |
| 50 | 0.006 | 0.026 | 5.64 | 72.5% |

The renderer keeps 15, which shadows a contact within about 7 mm, since
these captures do not tell the three apart. MAT-Q5 records that, and what
the map covers, as provisional.

## 5. What it does not do yet

- **MMD's own map.** MMD's map follows its camera and its self-shadow
  distance, and compares in a depth whose range it does not state; this map
  is fitted to the model along the light, so it does not move with the
  camera, and how sharp its edges are differs. MAT-Q5 is settled against
  captures of a contact or grazing shadow.
- **A map for a large scene.** One map covers every MMD draw that casts or
  receives, so a stage with large MMD props spreads its texels thinner.
- **`groundShadow`**, MMD's shadow on its floor, is not drawn; a stage has
  no floor for it.
- **Alpha in the map.** A caster's transparent texels cast, as `full.fx`'s
  map draws them.
- **Its cost.** The pass's GPU time is not yet measured in the viewport,
  and the viewport's telemetry has no part for it.
