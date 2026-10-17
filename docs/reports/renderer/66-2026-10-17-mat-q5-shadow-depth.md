# MAT-Q5: how far behind MMD's map a point falls into its self-shadow

- Date: 2026-10-17
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Tooling: `ost 0.23.14`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`;
  `usd-mmd-plugins` v0.2.0's `mmdImaging` at the `lookdev` profile
  (`sha256:39f3e24d…`, [report 53](53-2026-10-11-mmd-material-read.md));
  `toon` `hydra` packages built here, one for each sharpness compared, and
  `main`'s (`sha256:7b7ee0d2…`,
  [report 64](64-2026-10-17-mmd-material-morph-evidence.md)) for 15;
  MikuMikuDance v9.32 x64 for the captures; Python 3 with NumPy and Pillow
  for the comparisons
- Occasion: MAT-Q5, provisional since
  [report 60](60-2026-10-12-mmd-self-shadow.md), to be settled against
  captures of a contact or grazing shadow
  ([roadmap](../../roadmap/v0.4.0.md),
  [material policy §9](../../design/MATERIAL_POLICY.md#9-open-questions))

## TL;DR

**MAT-Q5 is resolved. A point now falls into MMD's self-shadow from 0.12
MMD units behind what the map saw first and wholly by 0.52, a sharpness
of 2.5 per MMD unit in `full.fx`'s form, where it was 15. On MMD's frames
of a chin and neck close up, under a light grazing the face and the neck,
and on report 63's face, turned face, bust and full body, 2.5 is nearest
MMD's on five of the seven frames and within 0.04 of the nearest on the
other two; 15 was furthest but for 50 and 1. The grazing face comes from
2.70 to 2.11 levels from MMD's, the full body from 5.17 to 4.97, as near
as hdToon's frame without self-shadow is to MMD's without. MMD's own
answer also depends on its self-shadow distance, which the stage does not
carry; 2.5 is MMD's at its default. What the map covers stays as
[report 63](63-2026-10-17-self-shadow-quality.md) made it.**

## 1. The question

`full.fx` shades a receiver by `1 − saturate(depth behind × 1500 − 0.3)`,
in the depth of MMD's own map, whose range MMD does not state. hdToon
compares in MMD units instead, so the sharpness is an MMD-units constant:
a point is lit until it is 0.3 / sharpness behind what the map saw first,
and wholly shadowed 1 / sharpness later. Report 60 chose 15 per unit, a
contact shadowed within 7 mm, because its full-body frame barely told 5,
15 and 50 apart. A contact, where a caster meets its receiver, and a
grazing light, which stretches the distance over which a shadow deepens,
were the frames that would.

## 2. The captures

The user's MMD v9.32 frames of 布伦妮, unmodified, at 3,840 × 2,160 RGBA,
a 30° field of view, MMD's default light colour, physics and the ground
shadow off, each with self-shadow at mode 1 and its default distance and
with self-shadow off, beside report 63's four:

| Frame | Centre (MMD units) | Distance | Light, MMD's direction |
| --- | --- | ---: | --- |
| Neck | (0, 13, 0) | 4 | (−0.5, −1, 0.5), the default |
| Grazing face | (0, 14.3, 0) | 9 | (−1, −0.25, 0.25) |
| Grazing neck | (0, 13, 0) | 4 | (−1, −0.25, 0.25) |
| Face, turned face, bust, full body | report 63 §2 | | the default |

and the face at self-shadow distances 7,500 and 9,500, where MMD's default
is 8,875 (§4).

## 3. The sharpness

Each sharpness built into its own package and drawn from the local MMD
Formation through the reference-shot script, every frame against MMD's
frame with self-shadow, over the pixels MMD's frame covers opaquely. Mean
difference in levels:

| Sharpness, per MMD unit | 1 | 2 | 2.5 | 3 | 5 | 15 | 50 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Shadowed from, MMD units | 0.30 | 0.15 | 0.12 | 0.10 | 0.06 | 0.02 | 0.006 |
| Wholly by | 1.30 | 0.65 | 0.52 | 0.43 | 0.26 | 0.087 | 0.026 |
| Neck | 1.76 | 1.62 | 1.61 | **1.60** | 1.63 | 1.70 | 1.70 |
| Grazing face | 2.56 | 2.12 | **2.11** | 2.17 | 2.41 | 2.70 | 2.84 |
| Grazing neck | 2.58 | 1.64 | **1.61** | 1.67 | 1.77 | 1.82 | 1.82 |
| Face | 2.42 | 2.15 | **2.09** | **2.09** | 2.14 | 2.26 | 2.32 |
| Face, turned | 2.24 | 1.99 | **1.95** | 1.97 | 2.05 | 2.17 | 2.22 |
| Bust | 3.63 | 3.10 | **3.05** | 3.11 | 3.27 | 3.39 | 3.42 |
| Full body | 5.19 | **4.93** | 4.97 | 5.01 | 5.10 | 5.17 | 5.18 |
| Sum | 20.38 | 17.55 | **17.39** | 17.62 | 18.37 | 19.71 | 20.00 |

hdToon's frames without self-shadow are 1.29, 1.92, 1.29, 1.86, 1.81,
2.87 and 4.93 from MMD's without, in the same order: at 2.5 the full body
with self-shadow is within 0.04 of that, and the others within 0.14 to
0.32.

At 2.5, the share of pixels within 4 levels of MMD's, against 15:

| Frame | 15 | 2.5 |
| --- | ---: | ---: |
| Neck | 92.4% | 94.9% |
| Grazing face | 86.5% | 91.4% |
| Grazing neck | 92.5% | 94.4% |
| Face | 87.7% | 90.2% |
| Face, turned | 88.5% | 91.2% |
| Bust | 80.9% | 84.1% |
| Full body | 72.0% | 73.5% |

Where 15 differed, it shadowed what MMD leaves lit: layers lying close over
others, hair strands on hair, the straps of the cape over the cape, and
under the grazing light the hair's near side. MMD lets a caster lie about
a tenth of an MMD unit over its receiver before it shadows it. Above 3 the
grazing frames and the bust grow worse together; below 2 every frame
does, the grazing neck most.

## 4. MMD's self-shadow distance

MMD's frames of the face at distances 7,500 and 9,500 are 0.87 and 0.96
levels from its frame at the default. Both change the same places, where
the hair lies over the face and over itself: at 7,500 more of the hair's
strands shadow one another, and at 9,500 the bangs' shadow on the right
cheek goes. So MMD's answer to MAT-Q5 moves with its self-shadow distance,
as a depth range set by that distance would make it. The stage does not
carry the distance, as it carries no camera of MMD's, so the renderer
takes MMD's default; a host that wants another would need a setting, which
v0.4.0 does not add ([later](../../roadmap/later.md)).

## 5. The change

`kShadowSharpness` in `mmd_common.slang` is 2.5 where it was 15. Nothing
else changed: no slot, no parameter, no pipeline, and the edge's softness,
normal offset and slope allowance of report 63 are as they were.

## 6. Tests

`renderer.material.mmd_self_shadow` still passes: its square lies 0.4
units, five MMD units, in front of the wall, far past 0.52.

The OpenStrata gates passed, each with `ost build`, `ost test` and
`ost validate`: `core` 15/15, `hydra` 24/24 and `viewport-usd` 55/55;
`ost package` packaged the `hydra` build to
`sha256:0fb82ba352c29ae2b61906dc3f1bd927f2c62dea932d900e7f3235e2daf45f81`,
the digest the sweep's 2.5 build had.

## 7. What it does not do yet

- **MMD's self-shadow distance and mode 2.** Neither is read; the
  comparison and the map are fitted to mode 1 at the default distance.
- **A caster nearer than 0.12 MMD units** to its receiver, such as a thin
  layer over a surface, does not shadow it, as in MMD at its default.
