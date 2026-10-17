# MMD vertex morphs draw as MMD draws them

- Date: 2026-10-17
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Tooling: `ost 0.23.14`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`;
  `usd-mmd-plugins` v0.2.0's `mmdImaging` at the `lookdev` profile
  (`sha256:39f3e24d…`, [report 53](53-2026-10-11-mmd-material-read.md));
  `main`'s `toon` `hydra` package
  (`sha256:7b7ee0d2…`, [report 64](64-2026-10-17-mmd-material-morph-evidence.md));
  MikuMikuDance v9.32 x64 for the captures; Python 3 with NumPy and Pillow
  for the comparisons
- Occasion: the evidence v0.4.0 still owes that an MMD model's vertex
  morphs draw as MMD draws them
  ([roadmap](../../roadmap/v0.4.0.md),
  [report 64](64-2026-10-17-mmd-material-morph-evidence.md) §7)

## TL;DR

**Weighed through the stage's own blend shapes, eight vertex morphs on two
models — eyes, mouth, brows, a blush and glasses, alone and two on the same
vertices — change hdToon's face close-up as they change MMD v9.32's. On
the pixels a morph changes in MMD's frame, hdToon's frame of the morph is
1.3 to 12 levels from MMD's on average, where its frame without the morph
is 7 to 155, and every morph moves the frame's brightness by 0.92 to 1.04
times what it moves MMD's but the blush, at 0.75. The blush is a
translucent layer, and the difference is the one
[report 56](56-2026-10-11-mmd-surface.md) records: hdToon blends in its
linear target, MMD in stored values. Two morphs that change nothing at
weight 0.5 in MMD, Sour's `照れ` and `メガネ`, whose geometry is still
inside the head there, change nothing in hdToon either. No renderer change
was needed.**

## 1. The captures

The user's MMD v9.32 captures, 3,840 × 2,160 RGBA, of each model alone at
frame 0 with no motion: centre (0, 18, 0), distance 9, angle 0, a 30° field
of view, MMD's default light (154, from (−0.5, −1, 0.5)), self-shadow, the
ground shadow and physics off. Each morph was set in MMD's facial panel and
registered, the others at 0.

| Model | Morph (panel) | Source index | Points it moves | Weights |
| --- | --- | ---: | ---: | --- |
| YYB Hatsune Miku (default, 1.0) | `まばたき` (eye) | 13 | 4,678 | 0.5, 1 |
| | `□` (mouth) | 45 | 1,880 | 0.5, 1 |
| | `怒り` (eyebrow) | 4 | 684 | 0.5, 1 |
| | `まばたき` + `笑い` (eye) | 13, 14 | 4,678 each | 1 + 1 |
| Sour式初音ミク `Black` | `まばたき` (eye) | 0 | 3,207 | 0.5, 1 |
| | `照れ` (other) | 83 | 98 | 0.5, 1 |
| | `メガネ` (other) | 94 | 976 | 0.5, 1 |
| | `まばたき` + `笑い` (eye) | 0, 1 | 3,207 and 3,147 | 1 + 1 |

Each is a vertex morph on the stage, a `UsdSkelBlendShape` the mesh names
([`usd-mmd-plugins` stage contract §11.1](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/STAGE_CONTRACT.md)).
Group morphs, such as either model's `あ`, were left out: they also move
bones or UVs, which is not this evidence. `照れ` is a blush, faces drawn
inside the head until the morph moves them out, on a translucent texture;
`メガネ` draws glasses the same way.

## 2. How hdToon drew them

The local session's reference-shot script weighed the morphs as an
animation does: a session `SkelAnimation` with no joints, under the
model's `SkelRoot`, listing each morph's blend shape and weight, made the
skeleton's animation source. The skeleton keeps its rest pose, MMD's pose
without motion, and usdSkelImaging hands hdToon the blend shapes, which it
evaluates on the GPU before skinning
([report 34](34-2026-10-04-gpu-morphs.md)). The evidence line shows one
morph upload and a weight write for each frame, and every draw skinned
with its authored normals. The stage carries no normal offsets and MMD
moves no normal for a vertex morph, so neither does hdToon. The camera and
light are MMD's, as [report 60](60-2026-10-12-mmd-self-shadow.md)
describes.

## 3. Against MMD's frames

Each hdToon frame against MMD's frame of the same morph and weight, over
the pixels MMD's frame covers opaquely, measured as in
[report 64](64-2026-10-17-mmd-material-morph-evidence.md) §4; "on MMD's
changes" is over the pixels the morph changes in MMD's frame by more than
4 levels, for hdToon's frame of the morph and, for scale, its frame
without it. The brightness ratio is the morph's total change in luminance
in hdToon's frames over its change in MMD's, on the pixels both of a
model's frames cover.

| Frame | Mean difference | Within 4 levels | Pixels MMD's morph changes | On those: with the morph | without | Brightness ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| YYB, no morph | 3.49 | 82.1% | — | — | — | — |
| `まばたき` 0.5 | 3.44 | 82.4% | 76,800 | 5.74 | 118.31 | 1.00 |
| `まばたき` 1 | 3.37 | 82.8% | 93,472 | 3.37 | 135.81 | 0.99 |
| `□` 0.5 | 3.51 | 82.0% | 17,703 | 11.43 | 67.51 | 0.92 |
| `□` 1 | 3.51 | 82.0% | 32,286 | 7.07 | 65.80 | 0.94 |
| `怒り` 0.5 | 3.50 | 82.1% | 8,339 | 10.97 | 154.54 | 0.98 |
| `怒り` 1 | 3.50 | 82.0% | 9,503 | 11.76 | 149.96 | 0.99 |
| `まばたき` + `笑い` | 3.43 | 82.8% | 96,034 | 4.24 | 115.63 | 0.98 |
| Sour, no morph | 1.89 | 92.1% | — | — | — | — |
| `まばたき` 0.5 | 1.88 | 92.2% | 92,180 | 3.18 | 66.77 | 1.00 |
| `まばたき` 1 | 1.85 | 92.2% | 121,545 | 1.93 | 64.28 | 1.00 |
| `照れ` 0.5 | 1.89 | 92.1% | 0 | — | — | — |
| `照れ` 1 | 1.92 | 92.1% | 49,771 | 1.65 | 7.29 | 0.75 |
| `メガネ` 0.5 | 1.89 | 92.1% | 0 | — | — | — |
| `メガネ` 1 | 1.99 | 91.6% | 101,396 | 4.49 | 20.96 | 1.04 |
| `まばたき` + `笑い` | 1.83 | 92.3% | 123,743 | 1.32 | 52.76 | 0.99 |

- **Eyes.** Half and fully closed, alone and with `笑い` on the same
  points, the eyes match: the two morphs' offsets add, as in MMD, and the
  frame is as near MMD's as it is without a morph.
- **Mouth and brows.** `□` opens YYB's mouth onto its inside, and `怒り`
  moves brows a few pixels wide; what differs on them lies along their
  contours and edges, where the two renderers' coverage of a thin line
  differs most, as with `全灰モード` in report 64.
- **Half weights that change nothing.** MMD's frames of Sour's `照れ` and
  `メガネ` at 0.5 are identical to its frame without them, as the user
  saw while setting `メガネ`, and so are hdToon's: at half the offset
  their faces are still inside the head.
- **`メガネ` at 1** draws the glasses where MMD does; its brightness
  ratio is 1.04.
- **`照れ` at 1** draws the blush where MMD does, but paler: on its
  cheeks MMD's frame falls from (238.4, 207.3, 208.7) to (238.9, 203.4,
  205.6) and hdToon's to (239.0, 204.5, 206.2), 0.75 of MMD's change in
  brightness. The blush texture is translucent, and hdToon blends it in
  its linear target where MMD blends stored values; a darker layer
  blended in linear is encoded lighter. That is the open difference
  [report 56](56-2026-10-11-mmd-surface.md) §5 and MAT-Q3 already record,
  not a morph's.

## 4. Tests

No code changed, so no gate was rerun; the package is `main`'s, whose
gates [report 64](64-2026-10-17-mmd-material-morph-evidence.md) records.
The morph path itself is covered by `toon-morph-state` and
`toon-morph-gpu` ([report 34](34-2026-10-04-gpu-morphs.md)).

## 5. What it does not do yet

- **Blending in stored values.** A translucent layer, such as a blush,
  blends in the linear target, so it is paler than MMD's when it is
  darker than what lies under it.
- **Group, bone and UV morphs.** They are MMD's semantics and the stage
  keeps them declarative; a runtime evaluates them, and none was compared
  here.
- **A morph in motion.** Every frame here is still; a VMD's morph curves
  played through the avatar runtime were not compared with MMD's frames.
