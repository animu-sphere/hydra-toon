# PreviewSurface reference comparisons

- Date: 2026-10-18
- Machine: Windows x86_64, NVIDIA RTX A5000, driver 597.16
- Build: MSVC 14.51; canonical CY2026 OpenUSD 26.08 `lookdev`, combined
  `viewport-usd`; plain CMake without OpenUSD
- Reference: OpenUSD 26.08 Storm, OpenGL 4.6; hdToon uses Vulkan

## Controlled comparison

`scripts/evaluate_preview_surface.py` runs `testusdview` with Storm at 4x
and hdToon at 1x/4x. Its worker uses the same USD camera and scene, disables
the application's camera and dome lights, and selects sRGB correction.
`HDX_MSAA_SAMPLE_COUNT` states the host sample count; hdToon's per-frame
evidence also asserts its actual count, extent, draw and material selection.

The committed `adapters/hydra2/tests/preview-reference.usda` contains a
planar patch with a mirrored U seam, no authored tangent frame, a perspective
camera and one normalized distant light. Normalization matters: Storm
otherwise multiplies distant intensity by the angle's solid angle, whereas
hdToon's bounded light model reads intensity directly. A zero angle without
normalization would leave Storm dark. No image-derived exposure matching or
per-renderer intensity adjustment is applied.

The driver generates its own 64x64 quadrant colour and normal images. The
normal graph explicitly authors raw encoding and scale/bias `(2,2,2,1)` /
`(-1,-1,-1,0)`. Captures are 256x256. Measurements use 12,288 interior
pixels, excluding the outer edge and an eight-pixel margin at texture/seam
boundaries. Different renderer clears and MSAA edges are outside this
shading comparison. Each image and the fixture have SHA-256 evidence;
commands, renderer identities, OpenUSD/GL versions, logs and a contact sheet
are retained under the output directory.

At both hdToon sample counts, the ten matching cases gave:

| Case | Mean absolute RGB error, /255 | P95 maximum-channel error, /255 |
| --- | ---: | ---: |
| Dielectric, roughness 1 / 0.6 / 0.2 | 0.67 / 0.66 / 0.47 | 2 / 2 / 2 |
| Metal, roughness 0.8 / 0.4 | 1.21 / 1.28 | 3 / 5 |
| Emission | 0.42 | 2 |
| sRGB colour texture / rotated coordinates | 0.86 / 0.88 | 3 / 3 |
| Normal map, mirrored U seam | 0.72 | 4 |
| Rotated normal sampling, baked Storm control | 0.73 | 4 |

The match gate is MAE at most 8 and P95 at most 20 levels, allowing the
different GGX visibility and display-encoding approximations. Fifteen
cross-case checks require a visible change when roughness, metalness, the
normal connection or coordinate rotation changes. Their smallest measured
MAE was 3.84 levels, above the one-level sensitivity threshold. PNGs are
removed before each capture, and host logs are reset; old output cannot
satisfy a missing capture.

## Reference differences

Two additional cases deliberately measure differences and do **not** assert
reference equivalence. `summary.json` records `reference_match: false` on
them and `reference_equivalent: false` on the whole set. Its `passed` field
includes their bounded characterization, not a claim that their images match.

- **Metalness 0.5:** MAE 14.72, P95 35. Storm's
  `previewSurface.glslfx` first forms `C = mix(white, albedo, metallic)` and
  then `F0 = mix(0.04*C, C, metallic)`. The renderer uses the endpoint
  interpolation `mix(0.04, albedo, metallic)` described by the
  [PreviewSurface specification](https://openusd.org/release/spec_usdpreviewsurface.html).
  The characterization bounds are MAE 12–18 and P95 30–40; neither formula
  is silently substituted to make this reference image agree.
- **Normal coordinates rotated 90 degrees:** MAE 35.32, P95 83. Storm's
  normal helper derives its frame from the transformed texture coordinates;
  this renderer retains the mesh tangent frame, as
  [material policy §13](../../design/MATERIAL_POLICY.md#13-tangent-space-normal-mapping)
  requires. Bounds are MAE 30–40 and P95 75–90. A separate Storm control
  bakes the same rotation into the image and removes the coordinate
  rotation. It samples the same vectors with the mesh frame retained and
  matches hdToon at MAE 0.73. This distinguishes sampling and decoding
  fidelity from the intentional frame difference.

## Metallic grazing correction

Inspection against the specification also exposed a renderer error: the
PreviewSurface shader interpolated metal F0 to albedo but retained white
F90. It now interpolates F90 from white to albedo with metalness, preserving
the metal's colour at grazing incidence. This changes no material layout,
pipeline identity, adapter or other shading model.

`toon-preview-surface-gpu` adds a separate analytic case with `H=N` and
`N.V=N.L=0.1`. A pure metal's Fresnel value must equal its albedo at every
angle; the expected GGX/Smith response is computed on the CPU. All three
colour channels are checked at 1x/4x. Compiling the previous white-F90
expression into an isolated shader directory makes the new check fail with
`metal grazing F90 R`; the corrected shader passes. This is an independent
regression, not an assertion that Storm and the renderer use identical
microfacet approximations.

## Verification and reproduction

With the lookdev runtime's `bin` and `lib` on `PATH`, `lib/python` on
`PYTHONPATH` and the current `hdToon` resources registered through
`PXR_PLUGINPATH_NAME`, ran:

```sh
python scripts/evaluate_preview_surface.py --testusdview <lookdev>/bin/testusdview
```

The run produced 36 captures: 20 matching comparisons, four measured
differences and 15 sensitivity checks. Generated assets and captures remain
local under `build/preview-reference/` by default. The fixture and scripts
are project-owned and redistributable; no downloaded asset is required.

Built both CMake trees in the MSVC developer environment, then ran
`ctest --test-dir <tree> --output-on-failure`:

- Combined `viewport-usd`: 61/61 passed, including Hydra, viewport,
  `testusdview`, core, GPU and installed consumers.
- Plain CMake without OpenUSD: 18/18 passed.

The reference driver passed again after the Fresnel correction and final
metadata/stale-output checks. Python compilation, relative documentation
paths and whitespace checks passed.

## Limits

This is a finite planar, direct-light, opaque reference set. It does not
establish curved/grazing external-renderer parity, textured scalar inputs,
Mask/Blend reference equivalence, authored-frame asset parity, glTF tangent
equivalence, deformation under an external renderer, output alpha or
background compositing. Those checks remain in v0.5.0's roadmap. Storm's
two recorded differences remain visible in the summary. No release is
published.
