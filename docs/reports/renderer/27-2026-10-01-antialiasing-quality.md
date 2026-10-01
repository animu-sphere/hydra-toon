# Viewport anti-aliasing quality: 4x baseline, thin features and remaining shading error

- Date: 2026-10-01
- Environment: Windows, NVIDIA RTX A5000; OpenStrata 0.23.14 and the
  canonical CY2026 OpenUSD 26.08 `lookdev` runtime
- VRM imaging: published `vrmImaging` 0.10.0 at
  `sha256:894fd616f1414d5b393ff0d50abbf7ef18cb603562133c72e642493b48f71667`,
  with the existing local file-format and package-resolver bundles
- Occasion: v0.2.0's anti-aliasing baseline evaluation, following reports
  [16](16-2026-09-28-msaa.md),
  [21](21-2026-09-30-viewport-msaa-switch.md) and
  [24](24-2026-09-30-outline-stability-cost.md)

## Decision

Keep **4x MSAA as the default**, with 8x available for finer geometric
coverage. At 4x, the controlled geometric-line error falls by 59%, the
Mask-line error by 44%, and the hull error by 73% relative to 1x. The
avatar's full-body and close-up captures show smoother hair silhouettes,
eyelash mesh edges and outline boundaries. More samples do not resolve
texture or shading aliasing inside a triangle.

This settles the v0.2.0 baseline choice, not temporal filtering or universal
avatar quality. FXAA/SMAA and TAA are considered below; this evaluation does
not justify adding either to the release baseline. Outline motion and
visibility work remains in the roadmap independently.

## Reproducible evaluation

[`scripts/evaluate_antialiasing.py`](../../../scripts/evaluate_antialiasing.py)
uses the actual Hydra-fed viewport and its swapchain captures. It generates
an original USD fixture and two PNG textures under its output directory:

1. Opaque diagonal geometry, with horizontal projected widths of roughly
   0.5, 0.75, 1 and 2 pixels at 256x256.
2. The same family of texture lines using Mask and alpha to coverage.
3. Blend alpha lines, whose shading and alpha are evaluated per pixel.
4. RGB emissive line art, without an alpha cut.
5. A closed, flattened octahedron with a nominal 0.75-pixel screen outline.

A fixed black backdrop holds the camera bounds. The five feature rows move
one output pixel over nine phases, independently of frame rate. Their
materials use emission or an unlit outline to isolate sampling from lights.
The fixture draws six MToon meshes, one transparent surface and one hull;
it uploads six point/topology buffers, seven material slots and two textures.
Every count is checked through the completed viewport run. No model or
capture is committed.

For each phase, a 1024x1024 capture at 4x MSAA is box-averaged to 256x256
**in linear light**, after decoding the captured sRGB. This is a finite
supersampled comparison, not analytical ground truth or a reference MToon
renderer. Its finer shading rate and different texture mip selection also
affect the texture rows. Whole-row mean absolute linear red-channel error
is averaged over the nine phases; black gaps separate the rows. This metric
includes empty pixels, so compare sample counts within a row, rather than
the absolute errors of different rows.

The viewport adds `--camera-pan X Y` and `--camera-dolly N` to reproduce a
close-up. They apply the existing drag and wheel operations after startup
framing, pan first, then dolly. `F`, `R` and a successful file open still
return to ordinary framing. A live sample-count change preserves the view;
its capture matches a fresh start at the final sample count byte for byte.

## Controlled results

Mean absolute linear error against the supersampled comparison:

| Feature | 1x | 2x | 4x | 8x |
| --- | ---: | ---: | ---: | ---: |
| Geometric strands | 0.008067 | 0.005690 | 0.003309 | 0.001729 |
| Mask texture lines | 0.010656 | 0.007772 | 0.005986 | 0.005172 |
| Blend texture lines | 0.006466 | 0.005978 | 0.005978 | 0.005978 |
| RGB texture lines | 0.006396 | 0.006204 | 0.006256 | 0.006229 |
| Inverted hull | 0.021644 | 0.010603 | 0.005942 | 0.003988 |

The geometric row's thinnest strands break into isolated pixels at 1x;
4x and 8x retain fractional coverage along their edges. Mask improves with
alpha to coverage, but a heavily minified line can still fall below the
cutoff. More raster samples cannot restore alpha lost during texture
filtering. Blend and RGB lines already use filtered textures, and their
interiors gain essentially nothing from 4x to 8x. Samples can still change
the quad edges or pixels shared by triangles; that is not additional
shading sampling.

The range of integrated linear red-channel signal across the nine phases,
in equivalent full-intensity pixels:

| Feature | 1x | 2x | 4x | 8x |
| --- | ---: | ---: | ---: | ---: |
| Geometric strands | 2.000 | 0.514 | 1.514 | 1.017 |
| Mask texture lines | 6.000 | 6.503 | 4.506 | 1.745 |
| Blend texture lines | 0.412 | 0.441 | 0.441 | 0.441 |
| RGB texture lines | 0.281 | 0.281 | 0.294 | 0.288 |
| Inverted hull | 2.000 | 3.017 | 6.014 | 3.884 |

These ranges are **not monotonic with sample count**. A fixed raster sample
pattern reduces spatial error without guaranteeing constant integrated
coverage under translation. The hull result in particular prevents a claim
that 4x or 8x eliminates flicker. Integrated signal is only one statistic;
it does not measure pixelwise temporal error or feature visibility. Report
24's moving diamond and this shallow octahedron also have different slopes
and depths, so their ranges are not interchangeable.

## Representative avatar

The local AliciaSolid retargeted VRMA 01 stage is drawn at 1280x720, with
vsync and overlay off. The motion run presents 1,200 frames, time codes
0 through 299.75 in steps of 0.25. Each count draws 20 MToon meshes with
authored normals and GPU skinning, five transparent surfaces and 15 hulls.
Upload totals stay at 20 point/topology/skin buffers, 13 material slots,
seven textures and 24,000 joint-buffer writes. The ordinary motion frames
read nothing back; a captured run reads its final frame once.

The final 2x, 4x and 8x images differ from 1x at 7,751, 10,610 and 11,476
pixels respectively. These are image-difference counts, not error against
ground truth. The 4x improvement is visible on the hair tips, ribbons,
limbs and clothes, with a smaller further geometric improvement at 8x.

Close-ups use `--camera-pan 0 155 --camera-dolly 12` at time codes 0,
29.75 and 299.75, at the same output size. They retain the whole scene and
its draw counts. They show the bangs and braid, eyelash boundaries, eyebrow
and mouth lines, with differing head tilt in the selected poses. The
outline and mesh edges become smoother; the texture line art retains
per-pixel shading limits, and thin lines can still be faint or discontinuous.
These are selected static poses in addition to a motion run, not a measured
real-avatar temporal-error study. Lighting reproduction is outside this
comparison; all counts use the same camera key and materials.

Three further motion runs per count change the order and take no captures.
Their telemetry retains the final 1,024 frames, excluding initial upload
and warm-up. No other GPU test runs concurrently with these benchmarks.

| Samples | GPU work means, three runs (ms) | CPU frame-interval means, three runs (ms) |
| --- | --- | --- |
| 1x | 0.108 / 0.110 / 0.108 | 1.235 / 1.247 / 1.269 |
| 2x | 0.125 / 0.125 / 0.124 | 1.262 / 1.286 / 1.264 |
| 4x | 0.130 / 0.132 / 0.130 | 1.293 / 1.305 / 1.264 |
| 8x | 0.137 / 0.137 / 0.137 | 1.266 / 1.271 / 1.272 |

4x adds 22 microseconds of mean GPU work over 1x in each repetition; 8x
adds another 5–7 microseconds over 4x. CPU interval noise exceeds that GPU
difference, so it cannot establish a frame-rate advantage. These are this
avatar and device's results, not a general budget. `work` excludes the
swapchain wait, and per-pass timings are not independently additive.

## Additional methods and limits

8x is a useful selectable refinement for geometry and Mask. Keeping 4x
balances that improvement with sample-target cost and the existing default;
this run does not establish a need to raise the default to 8x.

FXAA/SMAA could smooth remaining resolved-image steps, but the Blend/RGB
rows already contain filtered line art and show little gain from more
coverage samples. A post-filter cannot reconstruct a missing strand or
alpha that fell below its cutoff, and could soften this art. This is a
reason to defer it, not a measured comparison with a working FXAA/SMAA
implementation. TAA could address temporal shading error, but this renderer
has no motion-vector/history path; its latency, ghosting and motion
contracts need their own evaluation. Neither method is added here.

Outstanding quality work includes broad real-avatar temporal measurements,
rotating/subpixel silhouettes, Mask mip coverage preservation, shading and
UV-seam aliasing, and additional avatars/materials. The synthetic Mask row
supplies coverage evidence; this avatar's eyelashes use Blend, so it does
not establish real-avatar Mask fidelity. This report does not replace a
comparison with UniVRM or three-vrm.

## Commands and validation

With a matching runtime and VRM plugin paths/DLL directories registered:

```sh
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
python scripts/evaluate_antialiasing.py \
    --viewport <viewport-usd build>/adapters/viewport/toon-viewport \
    --avatar <local Alicia motion stage> --output build/aa-quality
ost renderer viewport -- --hidden --frames 8 --vsync off \
    --camera-pan 13 -7 --camera-dolly 2
```

The Hydra viewport build passes 30/30 CTests, including the three camera /
sample-switch comparison tests, and `ost validate` passes. The standalone
viewport without Hydra also builds and presents the documented eight
frames. Captures and motion runs complete without Vulkan validation errors.
The evaluation refuses a silently downgraded sample count or missing MToon
imaging, and writes commands, image comparisons, timing and upload evidence
to `build/aa-quality/summary.json` and the per-run logs.
