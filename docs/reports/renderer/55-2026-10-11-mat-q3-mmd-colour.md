# MAT-Q3 measured: MMD shades its colours as stored, with no decoding or encoding

> Followed by [report 56](56-2026-10-11-mmd-surface.md): `mmd_opaque` and `mmd_transparent` draw on stored values, and the reader samples MMD's textures undecoded.

- Date: 2026-10-11
- Machine: Windows 11 x86_64
- Tooling: MikuMikuDance v9.32 x64 for the reference captures; Python 3
  with Pillow and NumPy for the measurement; `usdcat` from the OpenUSD 26.08
  runtime for the stage's values
- Occasion: [MAT-Q3](../../design/MATERIAL_POLICY.md#9-open-questions), one
  of the two questions v0.4.0 settles before anything draws through an MMD
  pipeline ([roadmap](../../releases/v0.4.0.md)), to be decided against
  reference renders taken in MMD itself. Report 53 read MMD's colours as
  authored and left their interpretation to this question
  ([report 53 §5](53-2026-10-11-mmd-material-read.md#5-what-it-does-not-do-yet)).

## TL;DR

**Lowering MMD's light colour from 255 to 77 scales every pixel of the model
by 0.842 (R 0.8421, G 0.8416, B 0.8413), in every brightness band from 40 to
245. MMD's lighting term computed on the stored values,
`saturate(diffuse × light + ambient)` = `0.8 × 77/255 + 0.6`, predicts 0.8416.
A linear workflow predicts 0.914 to 0.926 if only the shaded result is
decoded and encoded, and 0.80 to 0.83, rising with brightness, if the light
colour is decoded too. At the default light, 154, the term saturates, and
the image equals the one at 255. MMD multiplies stored values and writes
them as they are. MAT-Q3 is resolved: the MMD path computes MMD's terms on
the stored values and converts the result to linear once, at the end.**

## 1. The model

One PMX model, Sour-style Hatsune Miku Ver.1.02 (`Black.pmx`), whose terms
do not allow it, its textures or renders of it to be committed. Its stage,
exported to USDZ by `usd-mmd-plugins`' `mmd_export` at stage-contract
version 2, read with `usdcat --flatten`:

| Value | All 20 materials |
| --- | --- |
| `diffuseColor` | (0.8, 0.8, 0.8, 1) |
| `ambientColor` | (0.6, 0.6, 0.6) |
| `specularColor` | (0, 0, 0) |
| `toonSource` | 8 `individual`, 12 `none` |
| `sphereMode` | 1 `add`, 19 `disabled` |

Every material has a base texture, declared `srgb_rec709_scene`. The
factors are uniform, so a change of light colour scales every material by
one factor, and the textures and toon ramps, identical between captures,
divide out of a ratio of two captures. No material uses a shared toon
index, so this model says nothing about MAT-Q2.

## 2. The captures

Taken in MMD with File → Output image, 1920 × 1080, the grid, axes, ground
shadow and self-shadow off, the background white:

| Capture | Light colour | Camera centre | Format |
| --- | --- | --- | --- |
| A | 154 (default) | (0, 10, 0) | JPEG |
| B | 154 | (0, 18.29, −0.06), face | JPEG |
| C | 255 | (0.80, 10, 0) | PNG, RGBA |
| D | 77 | (0.80, 10, 0) | PNG, RGBA; also BMP |

The camera's angle is 0 and its distance 45 in all four; only the light
colour changes between C and D, not its direction. The 0.8-unit shift of
centre between A and C moves the model 36 px, as a 30° field of view, MMD's
default, predicts at that distance. The PNGs carry coverage in alpha, in
steps of 32, over a transparent background. The BMP and PNG of D agree to a
mean of 0.1 per channel. None of the captures is committed.

## 3. The measurement

Pixels inside the model in both C and D, at least three pixels from its
silhouette, with every channel of C between 40 and 245: 113,246 pixels.
The ratio D / C:

| C's brightness | Pixels | R | G | B |
| --- | --- | --- | --- | --- |
| 40–80 | 43,975 | 0.837 | 0.841 | 0.836 |
| 80–120 | 4,374 | 0.859 | 0.849 | 0.852 |
| 120–160 | 13,001 | 0.853 | 0.846 | 0.848 |
| 160–200 | 32,841 | 0.845 | 0.842 | 0.843 |
| 200–245 | 19,055 | 0.843 | 0.842 | 0.843 |
| all | 113,246 | **0.8421** | **0.8416** | **0.8413** |

What each reading of the colours predicts for that ratio, at a stored value
`c`:

| Reading | Lighting term at 77 | Ratio at `c` = 60 / 140 / 220 |
| --- | --- | --- |
| stored values, as MMD stores them | 0.8 × 0.302 + 0.6 = 0.8416 | 0.842 / 0.842 / 0.842 |
| linear shading, light as stored | 0.8416 | 0.914 / 0.924 / 0.926 |
| linear shading, light decoded | 0.8 × 0.074 + 0.6 = 0.659 | 0.803 / 0.825 / 0.831 |

Only the first is flat and at 0.842. Against 0.8416, D is within one code
value of C × 0.8416 on 84 % of the pixels and within two on 91 %, the rest
at the toon ramps' and texture filtering's edges.

A against C, after shifting A 36 px: the median ratio is 1.00 in every
channel, with a mean absolute difference of 5.5 code values from JPEG
compression and sub-pixel misalignment. At 154, `0.8 × 0.604 + 0.6` = 1.08
saturates to 1, as it does at 255.

## 4. The decision

MMD's colours carry no colour space, and its textures declare sRGB, but MMD
neither decodes them nor encodes its result: it computes its terms on the
values as stored and writes them to the image. `hdToon`'s pipelines work in
linear colour, and the host encodes the AOV to sRGB
([capability matrix](../../reference/CAPABILITY_MATRIX.md)). To write MMD's
values, the MMD path:

- samples its base, sphere and toon textures without sRGB decoding;
- computes MMD's terms on those values and the material's factors as
  authored;
- converts the result from sRGB to linear once, so the host's encoding gives
  back the value MMD writes.

Recorded in [material policy §9](../../design/MATERIAL_POLICY.md#9-open-questions).

## 5. What it does not settle

- **Transparency.** MMD blends in stored values; `hdToon` blends in its
  linear target, so a partly transparent material over another differs from
  MMD's. That is a known approximation, not part of this decision.
- **Each term's form.** This measurement confirms the lighting term and
  that MMD's product is not decoded or encoded. The sphere, specular and
  toon terms are checked when `mmd_opaque` draws, against these captures.
- **The reader.** `HdToonReadMaterial` still marks MMD's textures as sRGB
  colour, as report 53 left it. Nothing samples them yet; the encoding
  follows this decision when `mmd_opaque` does.
- **MAT-Q2.** The model has no shared toon index.
