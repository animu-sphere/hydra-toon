# MMD's material morphs: the texture, sphere and toon tints draw

- Date: 2026-10-12
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Tooling: `ost 0.23.14`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`); this branch's `toon` `hydra` package, built here
  (`sha256:1f882f0c56676e973cfd6dd2a9290ea493a74fa093cefa7e765ded63d628e049`); Python 3 for the PMX survey
- Occasion: v0.4.0's MMD morphs
  ([roadmap](../../roadmap/v0.4.0.md)), after MMD's shadow flags
  ([report 60](60-2026-10-12-mmd-self-shadow.md)), in the order
  `usd-mmd-plugins` brings up the `hydra-toon` MMD path: material morphs
  next, then vertex colour.

## TL;DR

**An MMD material morph's texture, sphere and toon tints now draw. The
stage gives them no canonical input, so the MMD block of `ToonMaterial`
carries each as the evaluated multiply and add, an evaluated override sets
them, and `mmd_opaque` and `mmd_transparent` tint the base, sphere and toon
texels as MikuMikuEffect's `full.fx` does: the colour moves from what the
texture adds when it contributes nothing toward texel × multiply + add, by
the sum of the two alphas. That form reads the tinted morphs of the local
models as their names say, where a plain multiply would not, but it is not
yet compared with MMD's frames, so it is provisional, as MAT-Q6. The
renderer still evaluates no morph; a runtime does.**

## 1. What MMD does

A PMX material morph names a material, or every material, an operation,
multiply or add, and a value for each of diffuse, specular, specular power,
ambient, edge colour, edge size, and the texture, sphere and toon tints,
each tint an RGBA
([`usd-mmd-plugins` PMX contract](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/PMX_CONTRACT.md)).
MMD weighs each morph: a multiply's factor is `lerp(1, value, weight)` and
an add's `value × weight`, the multiplies' factors multiplied together and
the adds' summed. The stage keeps the morph declarative on its prim, and a
runtime evaluates it; the values with a canonical input reach the
material's inputs, and the tints, which have none, are for an MMD-aware
renderer
([`usd-mmd-plugins` material policy §11](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/MATERIAL_POLICY.md#11-material-morphs)).

PMX does not say how a tint's multiply and add apply to a texel, nor does
MMD. `full.fx`, which `mmd_opaque` follows
([report 56](56-2026-10-11-mmd-surface.md)), takes MikuMikuEffect's
evaluated values (`MULTIPLYINGTEXTURE`, `ADDINGTEXTURE` and their sphere
counterparts) and applies them to the texel's colour as
`lerp(1, texel × multiply + add, multiply.a + add.a)`, from 0 rather than 1
for a sphere that adds, keeping the texel's alpha. No copy of `full.fx` was
at hand to check against; that form is taken as `full.fx` is known to
write it.

### The local models' morphs

A survey of the 41 local PMX models, all PMX 2.0: 15 have material morphs,
57 in all, and three, two variants of one model and another, have tints
that change anything, 11 offsets. Under the
form above they read as their authors named them; under a plain
texel × multiply + add, most would not:

| Morph | Tints | `full.fx`'s form | A plain multiply and add |
| --- | --- | --- | --- |
| `spa消`, `spa消える` (sphere vanishes) | sphere × 0, alpha 0 | the sphere contributes nothing, whether it multiplies or adds | a multiply sphere turns the material black |
| `全灰モード` (all grey) | texture, sphere and toon × 0, alpha 0 | each material its own colour, untextured and unshaded | every textured material black |
| `全黒モード` (all black) | texture × 0, sphere × (0, 0, 0, 1), toon × 0; diffuse and ambient × 0 | black | black |
| `全白モード` (all white) | texture and sphere + 1, toon + 10; diffuse and ambient + 1 | white | white |
| `toon白くなる` (toon turns white) | toon × 3 | the ramp brightens toward white | the ramp brightens toward white |
| `toon深まる` (toon deepens) | toon × 0, alpha 0 | the ramp contributes nothing, so the shading flattens | the material turns black |

`toon深まる` reads against its name either way, so it is one of the
captures MAT-Q6 asks for.

None of the 41 sets PMX 2.1's vertex colour, point or line flags.

## 2. The change

- **Core.** `ToonMaterial::MMD` gains `texture_tint`, `sphere_tint` and
  `toon_tint`, each a `TextureTint` of a multiply (1, 1, 1, 1 by default)
  and an add (0 by default), with `Toon::Float4`. They are values, not
  structure: `IsStructuralChange` ignores them, so
  `RenderWorld::SetMaterialParametersOverride` and its delegate and
  viewport forwards accept them.
- **Backend.** `MmdParameters` grows from 144 to 240 bytes with the three
  tints, a multiply row and an add row each.
- **Shading.** `ShadeMmd` tints the base texel when the material has a base
  texture, the sphere texel when it has a sphere, from black for a sphere
  that adds, and the toon ramp's texel and its far-side texel, which the
  self-shadow pass reads, when it has a ramp. A texture's alpha is kept, so
  a tint moves no material between the opaque and transparent draws.
- **Viewport.** The Materials panel shows an MMD material's specular
  colour and power, ambient, edge alpha and its three tints, and edits
  them through the override route
  ([report 41](41-2026-10-05-viewport-material-debug.md)).
- **Not here.** The hydra adapter reads nothing new: a tint has no
  canonical input, and evaluating the morph prims is a runtime's
  ([integration scope](../../design/INTEGRATION_SCOPE_POLICY.md)).
  `Toon::AvatarState`'s material fields are not extended; a runtime that
  evaluates MMD's morphs has yet to name them.

## 3. Tests

- `renderer.material.mmd_morph` (headless, GPU): on a white MMD material, a
  base texel of 128, which reads 55, multiplied by half reads 13, with 0.25
  added 134, and with the multiply's alpha at half too 89, halfway from
  white; with every multiply 0 the pixel is white, and a material without
  a base texture stays white under any tint. A sphere tint of 0 takes an
  add sphere's 64 and a multiply sphere's alike away, to 55. Lit from
  behind, `toon01`'s far side, 156, multiplied by half reads 34 and by 0 is
  white. A tint edit rewrites one slot and nothing else.
- `toon-render-world-test`: tints are a value-only change.

The OpenStrata gates passed, each with `ost build`, `ost test` and
`ost validate`: `core` 15/15, `hydra` 24/24 and `viewport-usd` 55/55; `ost package` packaged the `hydra` build twice, to
the same digest.

## 4. What it does not do yet

- **MAT-Q6.** The tint's form is `full.fx`'s, not yet compared with MMD's
  frames. It is settled with captures of a local model in MMD v9.32, its
  self-shadow off, from report 60's camera (centre (0, 10, 0), distance
  45, 30° field of view, 3,840 × 2,160, MMD's default light), with no morph
  and with each of `spa消える`, `全灰モード`, `toon白くなる` and
  `toon深まる` at weights 1 and 0.5, and of a second model's `spa消`, whose
  only sphere adds.
- **Evaluation.** The renderer weighs no morph and reads no morph prim. In
  `usdview` a material morph shows only where a runtime has written its
  canonical values, and its tints not at all; a host that evaluates the
  morphs sets them through the override.
- **Vertex morphs on an MMD stage.** They are `UsdSkelBlendShape`s, which
  the morph path v0.3.0 established already draws; that an MMD model's
  draw as MMD's do is not yet shown.
- **Vertex colour, points and lines.** PMX 2.1's flags are read but not
  drawn; no local model sets them.
