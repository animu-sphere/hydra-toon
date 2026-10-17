# MAT-Q6: MMD draws no material morph tint, so hdToon draws none by default

> Followed by [report 64](64-2026-10-17-mmd-material-morph-evidence.md): hdToon's frames of `全灰モード`, `全黒モード` and `全白モード`, evaluated and set as the canonical inputs, compared with these captures (§5).

- Date: 2026-10-17
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Tooling: `ost 0.23.14`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`; this
  branch's `toon` `hydra` package, built here
  (`sha256:b2ad1c14c1afa9c4fa35666edc0abbe283e0fe12452c54144ec7a3e92d3ccf3e`); MikuMikuDance v9.32 x64 for
  the captures; Python 3 with NumPy and Pillow for the comparison
- Occasion: MAT-Q6, which
  [report 61](61-2026-10-12-mmd-material-morph.md) left provisional until
  MMD's frames of the tinted morphs were compared
  ([roadmap](../../roadmap/v0.4.0.md))

## TL;DR

**MikuMikuDance v9.32 does not draw a material morph's texture, sphere or
toon tint. On the user's captures of two local models, every morph that
only tints leaves MMD's frame as it was, at full and half weight, and the
morphs that also change a material's colours or edge change only those.
So MAT-Q6 is resolved: as MMD draws, `mmd_opaque` and `mmd_transparent`
leave the tints out, and a host that wants MikuMikuEffect's `full.fx`
reading asks for it with `DrawList::mmd_morph_tints`, the render setting
`toon:mmdMorphTints` or the viewport's `--mmd-morph-tints`. The tints stay
in the material's slot either way, and the switch rewrites no slot.**

## 1. The captures

Report 61 asked for MMD's frames of the local models' tinted morphs. The
user took them in MMD v9.32: 3,840 × 2,160 PNG with a transparent
background, from report 60's camera (centre (0, 10, 0), distance 45, a 30°
field of view), under MMD's default light, with self-shadow, the ground
shadow and physics off, so every frame shows the bind pose. One morph at a
time was set in the face panel's "other" group, each at weight 1 and 0.5,
after a frame with no morph:

- YYB Hatsune Miku (default, 1.0), 52 materials: `spa消える`,
  `全灰モード`, `toon白くなる`, `toon深まる`, and also `全黒モード` and
  `全白モード`. Its spheres multiply on the body and glasses and add on the
  hair, clothes and boots.
- Sour式初音ミク 1.02 `Black.pmx`, 20 materials: `spa消`, whose only sphere,
  on the hair, adds.

Every one of these morphs targets every material. What each sets, read from
the PMX:

| Morph | Operation | Texture | Sphere | Toon | Other values |
| --- | --- | --- | --- | --- | --- |
| `spa消える`, `spa消` | multiply | 1 | 0, alpha 0 | 1 | 1 |
| `toon白くなる` | multiply | 1 | 1 | 3 | 1 |
| `toon深まる` | multiply | 1 | 1 | 0, alpha 0 | 1 |
| `全灰モード` | multiply | 0, alpha 0 | 0, alpha 0 | 0, alpha 0 | edge size 0 |
| `全黒モード` | multiply | 0, alpha 0 | (0, 0, 0, 1) | 0, alpha 0 | diffuse, specular and ambient 0, edge size 0 |
| `全白モード` | add | 1 | 1 | 10 | diffuse, specular, ambient and edge colour 1 |

## 2. What MMD draws

Each frame against the same model's frame with no morph, over the pixels
both cover opaquely (725,895 for YYB, 657,921 for Sour):

| Frame | Pixels changed by more than 20 | Mean difference |
| --- | ---: | ---: |
| YYB `spa消える` 1 / 0.5 | 49 / 117 | 0.01 / 0.02 |
| YYB `toon白くなる` 1 / 0.5 | 89 / 125 | 0.02 / 0.02 |
| YYB `toon深まる` 1 / 0.5 | 107 / 98 | 0.02 / 0.02 |
| Sour `spa消` 1 / 0.5 | 45 / 51 | 0.01 / 0.01 |
| YYB `全灰モード` 1 / 0.5 | 20,560 / 15,548 | 2.64 / 1.65 |
| YYB `全白モード` 1 / 0.5 | 57,084 / 44,698 | 7.13 / 4.10 |
| YYB `全黒モード` 1 / 0.5 | 693,228 / 567,644 | 139.54 / 62.05 |

- **Tints alone change nothing.** The four morphs that only tint leave at
  most 125 pixels in a few specks on the hair and face, at either weight.
  Under `full.fx`'s form
  `spa消える` would take every sphere away, and under any form the hair's
  added sphere, on a few hundred thousand pixels, would go.
- **`全灰モード` removes the edge and keeps the textures.** Its changed
  pixels lie on the edge lines, which its edge size × 0 removes, and the
  background's alpha changes there; the textured materials are as they
  were, where its texture tint × 0 would have changed nearly every pixel.
- **`全黒モード` keeps the spheres that add.** Diffuse, specular and ambient
  at 0 leave MMD's colour black before a texture applies, yet 138,112
  pixels stay brighter than 40, where the hair, clothes and boots add their
  spheres. A sphere tint of (0, 0, 0, 1) would take those away under any
  form.
- **`全白モード` brightens by its values, not its tints.** Diffuse and
  ambient + 1 saturate MMD's lighting term, and edge colour + 1 turns the
  edges white; the textures are not lifted toward white by the texture's
  + 1 or the ramp's + 10.

So MMD reads a material morph's diffuse, specular, ambient, edge colour and
edge size, and ignores its texture, sphere and toon tints. What `full.fx`
does with `MULTIPLYINGTEXTURE` and its counterparts is MikuMikuEffect's
own reading, not MMD's.

## 3. The change

- **Default.** `DrawList::mmd_morph_tints`, false by default, says whether
  MMD materials' tints draw. Off, `ShadeMmd` leaves the base, sphere and
  toon texels untinted, as MMD draws them. On, it tints them in
  `full.fx`'s form, as report 61 implemented. The tints stay in
  `ToonMaterial::MMD` and the material's slot whichever way the switch is,
  so an evaluated override still sets them, and a host can turn them on
  without rewriting a slot.
- **Backend.** The switch is a frame value: the MMD frame parameters'
  `w`, next to the self-shadow map's values, written every frame.
  `MmdParameters` is unchanged.
- **Hosts.** The render setting `toon:mmdMorphTints` (false by default,
  listed as "MMD morph tints"), the viewport's `--mmd-morph-tints on|off`
  and an "MMD morph tints" checkbox next to "MMD self-shadow", each as
  MMD's self-shadow switch is carried
  ([report 60](60-2026-10-12-mmd-self-shadow.md)).

## 4. Tests

- `renderer.material.mmd_morph` (headless, GPU) runs report 61's tint
  checks with the switch on, and with it off: the base texel multiplied by
  half still reads 55, an add sphere tinted by 0 still adds its 64, and
  toon01's far side multiplied by half still reads 156. Turning the switch
  off and on again rewrites no material slot.

The OpenStrata gates passed, each with `ost build`, `ost test` and
`ost validate`: `core` 15/15, `hydra` 24/24 and `viewport-usd` 55/55;
`ost package` packaged the `hydra` build twice, to the same digest.

## 5. What it does not do yet

- **`full.fx`'s form is unverified.** With the switch on, the tints draw
  as `full.fx` is known to apply them; no MikuMikuEffect frame has been
  compared. That is an optional reading, not MMD's look, so v0.4.0 does
  not wait for it.
- **The values MMD does read.** These captures also show what MMD makes of
  a morph's diffuse, specular, ambient, edge colour and edge size
  (`全灰モード`, `全黒モード`, `全白モード`). Comparing hdToon's frames of
  those morphs, their evaluated values set through the override, with MMD's
  is the evidence v0.4.0 still owes that evaluated material morphs draw as
  MMD draws them.
