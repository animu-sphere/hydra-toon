# Evaluated MMD material morphs draw as MMD draws them

- Date: 2026-10-17
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Tooling: `ost 0.23.14`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`;
  `usd-mmd-plugins` v0.2.0's `mmdImaging` at the `lookdev` profile
  (`sha256:39f3e24d…`, [report 53](53-2026-10-11-mmd-material-read.md));
  this branch's `toon` `hydra` package, built here
  (`sha256:7b7ee0d2400f6a0808a451d59cc2a7450aea24aef69bf40b8bf9a6628dbeee46`),
  and `main`'s (`sha256:e64b8e01…`, [report 63](63-2026-10-17-self-shadow-quality.md))
  for the frames before the change; MikuMikuDance v9.32 x64 for the
  captures; Python 3 with NumPy and Pillow for the comparisons
- Occasion: the evidence v0.4.0 owes that an MMD model's evaluated
  material morphs draw as MMD draws them
  ([roadmap](../../roadmap/v0.4.0.md),
  [report 62](62-2026-10-17-mat-q6-morph-tints.md) §5)

## TL;DR

**Evaluated as a runtime evaluates them and written as the canonical
inputs' opinions, `全灰モード`, `全黒モード` and `全白モード` change
hdToon's frame of YYB Hatsune Miku as they change MMD's, at weights 1 and
0.5: on the pixels each morph changes in MMD's frame, hdToon's frame of the
morph is 4.0 to 21 levels from MMD's on average, where its frame without
the morph is 30 to 115, and the morphs move the frame's total brightness by
0.83 to 1.08 times what they move MMD's. The comparison also found two
faults that every MMD model shows, morph or not, which this branch fixes.
A material drawn on an earlier one's vertices, as YYB's blush and its
hair, skirt and clothes' ambient-occlusion overlays are, lost the depth
test, which was strict; MMD draws with Direct3D 9's LESSEQUAL, so the MMD
pipelines now pass a tie. And a sphere mode with no sphere texture added
white, which those overlays, once drawn, showed. Without a morph, hdToon's
frame of YYB is now 6.37 from MMD's where it was 6.91, and 布伦妮's
self-shadowed full body 5.17 where it was 5.54.**

## 1. The morphs and how they were evaluated

The user's MMD v9.32 captures of [report 62](62-2026-10-17-mat-q6-morph-tints.md)
include YYB Hatsune Miku (default, 1.0; 52 materials) with no morph and
with three material morphs, each at weight 1 and 0.5: 3,840 × 2,160 RGBA,
centre (0, 10, 0), distance 45, a 30° field of view, MMD's default light,
self-shadow, the ground shadow and physics off. Every one of the three
targets every material:

| Morph | Operation | Values a runtime applies (no tints) |
| --- | --- | --- |
| `全灰モード` | multiply | edge size × 0 |
| `全黒モード` | multiply | diffuse, specular, ambient × 0, edge size × 0 |
| `全白モード` | add | diffuse + (1, 1, 1, 1), specular + 1, ambient + 1, edge colour + 1 |

The renderer weighs no morph (material policy §11); a runtime evaluates
them and overrides the material's canonical inputs. The local session's
reference-shot script did that here: for each material it read the
stage's `inputs:mmd:material:*`, applied each listed morph prim's
`mmd:morph:*` arrays — every multiply morph's `lerp(1, value, weight)`,
then every add morph's `value × weight` — and wrote the result as the
session layer's opinion on `diffuseColor`, `specularColor`,
`specularPower`, `ambientColor`, `edgeColor` and `edgeSize`. The tints
have no canonical input and were left out, as MMD draws none (MAT-Q6).
hdToon then drew from MMD's camera through `mmdImaging`'s view, as
[report 60](60-2026-10-12-mmd-self-shadow.md) describes, its value-only
path rewriting the 52 slots.

## 2. What the first frames showed

With `main`'s build the values arrived — `全黒モード` turned the model
black, `全灰モード` took the edges away — but `全白モード`'s frame lacked
MMD's blush. YYB's blush is `face00` (material 11): `face2.png`, cheeks on
a transparent texture, at diffuse alpha 0.01, drawn after the face
(materials 9 and 10) on the face's own vertices. `全白モード` adds 1 to
that alpha, and MMD draws the cheeks; hdToon drew nothing, because its
depth test was LESS and a fragment at the very depth the face wrote lost
the tie.

The same holds without a morph. YYB draws `hair01+`, `hair02+`,
`hair03+`, `skirt01+` to `skirt03+` and `clothes01+` after their bases on
the bases' vertices, each an ambient-occlusion texture on a transparent
background, and MMD's frame of the hair shows their darker strands.
Passing ties drew them, and also showed a second fault: those materials
are in sphere mode `add` with no sphere texture, and the missing texture,
sampled from the white placeholder, added white. In MMD a sphere mode
without a texture changes nothing.

## 3. The change

- **Depth ties pass on MMD's surfaces.** `mmd_opaque` and
  `mmd_transparent` compare with `LESS_OR_EQUAL`
  (`PipelineDescription::depth_ties_pass`), as MMD's LESSEQUAL does, so a
  later material on an earlier one's vertices draws over it. MToon's
  pipelines, the edge hulls, which a depth bias already keeps behind, and
  the shadow map keep LESS.
- **A sphere mode needs its texture.** `SphereMode` reports no sphere when
  the material names no sphere texture, so the slot neither adds nor
  multiplies one.

Nothing else changed: no slot layout, no parameter and no pipeline count.

## 4. Against MMD's frames

Each hdToon frame against MMD's frame of the same morph and weight, over
the pixels MMD's frame covers opaquely; "on MMD's changes" is the mean
difference over the pixels the morph changes in MMD's frame by more than
4 levels, for hdToon's frame of the morph and, for scale, its frame
without it.

| Frame | Mean difference, before → after | Within 4 levels, before → after | Pixels MMD's morph changes | On those: hdToon with the morph | without |
| --- | --- | --- | ---: | ---: | ---: |
| no morph | 6.91 → 6.37 | 62.0% → 65.7% | — | — | — |
| `全灰モード` 1 | 5.20 → 4.63 | 63.9% → 68.3% | 27,754 | 10.98 | 48.90 |
| `全灰モード` 0.5 | 6.05 → 5.50 | 62.6% → 66.5% | 22,604 | 21.22 | 35.88 |
| `全黒モード` 1 | 4.55 → 4.03 | 55.6% → 56.9% | 709,648 | 4.03 | 115.17 |
| `全黒モード` 0.5 | 5.13 → 4.60 | 63.2% → 66.4% | 717,061 | 4.55 | 52.05 |
| `全白モード` 1 | 7.89 → 7.73 | 60.9% → 61.6% | 88,869 | 15.67 | 49.34 |
| `全白モード` 0.5 | 5.89 → 5.49 | 61.5% → 64.6% | 84,690 | 12.00 | 29.61 |

How much each morph moves the frame, as the sum of its change in
luminance over the pixels both of a model's frames cover, hdToon's against
MMD's:

| Morph | 1 | 0.5 |
| --- | ---: | ---: |
| `全灰モード` | 0.90 | 0.98 |
| `全黒モード` | 1.00 | 1.00 |
| `全白モード` | 1.08 (1.19 before) | 0.83 |

- **`全黒モード`** matches across the model: diffuse, specular and ambient
  at 0 leave the added spheres, as in MMD, and what differs is as near as
  the frame without a morph.
- **`全灰モード`** changes only the edge lines, so its pixels are a few
  thin lines, where the two renderers' coverage of a line a pixel wide
  differs most. At 0.5 its change in brightness is 0.98 of MMD's, at 1
  0.90.
- **`全白モード`** now draws the blush. What remains lies along the edges,
  which it turns white: a white line beside a dark contour differs by a
  fraction of a pixel more than a black one.

## 5. Other models

The fixes change every MMD model that layers on shared vertices or names
a sphere mode without a texture. Against MMD's frames, with `main`'s
build and this branch's, from the same cameras:

| Model, camera | Mean difference, before → after | Within 4 levels, before → after |
| --- | --- | --- |
| Sour式初音ミク `Black`, full body, no self-shadow | 3.35 → 3.35 | 87.3% → 87.3% |
| 布伦妮, full body, self-shadow ([report 63](63-2026-10-17-self-shadow-quality.md)) | 5.54 → 5.17 | 70.4% → 72.0% |
| 布伦妮, face, self-shadow | 2.44 → 2.26 | 87.7% → 87.7% |

## 6. Tests

`renderer.material.mmd` (headless, GPU) gained two checks. An add sphere
with no texture over an ambient of 0.5 reads 55, the ambient alone, where
the white placeholder made it 255. And a mesh whose two parts share
vertices, red then green in MMD's order, reads green when both are opaque
and half of each, (128, 128, 0), when the green is at alpha 0.5. Without
this branch's backend each check fails with its own message.

The OpenStrata gates passed, each with `ost build`, `ost test` and
`ost validate`: `core` 15/15, `hydra` 24/24 and `viewport-usd` 55/55;
`ost package` packaged the `hydra` build twice, to the same digest.

## 7. What it does not do yet

- **Vertex morphs.** No MMD frame of a vertex morph has been compared;
  that is the morph evidence v0.4.0 still owes.
- **A morph that takes alpha to or from 1** moves a material between the
  opaque and transparent draws, which is structural (§6); here every
  layer that changed alpha was already transparent through its texture.
- **MAT-Q5** waits for captures of a contact or grazing self-shadow.
