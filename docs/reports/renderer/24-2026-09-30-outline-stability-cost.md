# Outline evaluation under motion: width, texture sampling, depth ties and hull cost

- Date: 2026-09-30
- Environment: Windows 11, MSVC 14.51, NVIDIA RTX A5000, Vulkan 1.4.321
- Tooling: `ost` 0.23.14; the canonical CY2026 OpenUSD 26.08 `lookdev`
  runtime; published `vrmImaging` 0.10.0 at
  `sha256:894fd616f1414d5b393ff0d50abbf7ef18cb603562133c72e642493b48f71667`;
  the existing local `usdVrmFileFormat` and `usdVrmPackageResolver` bundles
- Occasion: [v0.2.0](../../releases/v0.2.0.md)'s outline stability and cost
  work, following the unit and depth fixes in reports
  [17](17-2026-09-28-outline-meters-per-unit.md) and
  [18](18-2026-09-28-outline-depth-bias.md)

## What changed

The viewport accepts `--time` and `--time-step` in USD time codes. Each
presented frame evaluates a fixed pose, independently of rendering speed.
There is no wall-clock playback or looping. Opening another scene restarts
the sequence. `--outlines on|off` and the overlay's Outlines checkbox change
`DrawList::outlines`, which suppresses command recording without editing
materials or uploading geometry. The checkbox clears the timing history.

Actual hull counts are exposed in `PresentDrawCounts::hulls` and
`OffscreenStatistics::outline_draws`. They include transparent hulls, whose
GPU time remains part of the transparent pass. The viewport prints
`Hull draws:` and checks `--expect-hulls`; its earlier scene summary still
counts materials requesting an outline, while the new count reports
commands actually recorded.

The texture cache records whether a resident image's entire G channel is
zero, once per pixel revision. Such an image suppresses hulls only on meshes
with UVs: a mesh without UVs samples white in the shader and must retain its
hull. Missing images and exhausted texture-table entries also keep the
white fallback. A pixel edit refreshes the omission decision even when the
material and its descriptor entry do not change; it rewrites no material
slot. Zero, negative and non-finite width factors emit no hull.

Texture upload barriers now expose transfer writes to both the vertex and
fragment stages. Previously they named only the fragment stage, although
the outline vertex shader samples the width image. The transition of the
earlier mip levels also names their transfer writes. Width sampling itself
remains linear G at explicit mip 0 through the role's UV transform: a
vertex stage has no fragment derivatives, and changing the level with the
camera would change the hull geometry.

## GPU regression evidence

The headless runner adds three checks, and extends the existing outline
depth check:

| Check | Result on this device |
| --- | --- |
| `renderer.outline.width` | At 128x128, camera distance 2 to 4: a screen width of 0.05 stays 6/6 px, a world width of 0.1 becomes 6/3 px. Orthographic screen width is 6/6 px under compensated zoom and a mirrored, nonuniform object scale |
| `renderer.outline.sampling` | Opaque and Blend: G independently of R/B, UV offsets, linear half-width interpolation, all-zero omission, nonzero pixel restoration without a material write, no-UV and missing-image fallbacks, and the hull comparison switch |
| `renderer.material.mtoon_outline` | None and an all-zero width image record no hull. A 1 pm back-face hull tilted through 12 GPU-skinned poses draws exactly the surface-only image at every pose |
| `renderer.outline.motion` | A 0.75 px screen hull moves one pixel in 65 GPU-skinned poses. Repeating each pose is pixel-identical; each sample-count run writes 65 poses and uploads the geometry, skin and material once |

The motion check subtracts the surface-only image's G from the outlined
image's G and sums over the target, divided by 255. This measures integrated
hull coverage in equivalent green pixels, not an individual pixel's motion
error. The diamond is unchanged except for a translation, so its continuous
coverage should not change.

| Samples | Minimum coverage | Maximum coverage | Range |
| --- | ---: | ---: | ---: |
| 1x | 28.862745 | 59.529412 | 30.666667 |
| 4x | 36.815686 | 51.796078 | 14.980392 |
| 8x | 44.239216 | 44.419608 | 0.180392 |

4x reduces this fluctuation by 51.2%; 8x by 99.4%, compared with 1x. The
4x result still varies substantially. Repeated-pose determinism rules out
unstable renderer state in this fixture; it does not eliminate spatial
sampling changes between moving poses. This is evidence to continue the
temporal-quality item, not to declare it finished.

The committed viewport fixture `tests/outline-motion.usda` moves one
skinned mesh and returns to its original pose. Nine presented frames write
nine joint buffers, with one point, topology, skin and material upload.
Its final capture equals the static reference byte for byte. This fixture
tests the host's time route with an unlit material; the headless checks
above and the VRM run below exercise MToon.

## Animated VRM in the viewport

The existing AliciaSolid retargeted VRMA 01 stage references the raw VRM.
Each run is 1,200 frames at 1280x720, from time code 0 with a step of 0.25,
with vsync and the overlay off. It samples 0 through 299.75, within the
stage's authored range. Telemetry retains the last 1,024 frames, leaving
out the initial uploads and warm-up. Captured runs read back one final
frame; ordinary frames read back none. The model, motion and captures are
local evidence and are not committed.

Every run has 20 MToon meshes, all GPU-skinned with authored normals,
12 MToon materials, one fallback material and seven textures. Upload totals
are 20 point, topology and skin uploads, 13 material writes, seven texture
uploads and 24,000 joint-buffer writes. Changing time uploads no rest
geometry, topology, texture or material. Validation reports no messages.

| Outlines | Opaque hull calls | Opaque surface calls | Transparent calls, including hulls | All hulls | Triangles | Pipeline binds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| On | 13 | 15 | 7 | 15 | 61,896 | 7 |
| Off | 0 | 15 | 5 | 0 | 31,798 | 3 |

The initial sample-count comparison, mean milliseconds:

| Run | Frame interval | Hydra sync | GPU outline part | GPU opaque part | GPU work |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1x, on | 1.285 | 0.451 | 0.094 | 0.003 | 0.102 |
| 4x, on | 1.300 | 0.437 | 0.093 | 0.006 | 0.121 |
| 4x, off | 1.162 | 0.431 | 0.000 | 0.083 | 0.110 |
| 8x, on | 1.299 | 0.433 | 0.047 | 0.013 | 0.134 |

At 4x, omitting hulls removes 15 calls and 30,098 triangles. The initial
whole-frame GPU difference is 0.011 ms, rather than the outline part's
0.093 ms: with hulls omitted, the opaque part becomes the first to fetch
geometry and takes 0.083 ms. This confirms the attribution caution in
[report 22](22-2026-09-30-viewport-telemetry.md). The GPU overlaps the
parts, so their times must not be treated as independently additive
pipeline costs. `work` excludes the presentation image wait.

Three additional 4x pairs alternate the on/off order and omit captures:

| Pair | Interval on/off (ms) | GPU work on/off (ms) | GPU difference (ms) |
| --- | ---: | ---: | ---: |
| 1, on first | 1.302 / 1.167 | 0.122 / 0.111 | 0.011 |
| 2, off first | 1.277 / 1.167 | 0.122 / 0.110 | 0.012 |
| 3, on first | 1.273 / 1.167 | 0.121 / 0.110 | 0.011 |

The observed incremental GPU work is therefore 11–12 microseconds in these
pairs. All use the same sequence and retain the same uploads and draw
counts; none reads back a frame. CPU interval differences are 0.106–0.135
ms, which include command recording and presentation, not only hull work.

The final on/off captures differ in 2,089 pixels. They show the same pose;
turning hulls off preserves the material and mesh uploads. 8x GPU work is
0.013 ms above 4x in this comparison. These figures characterize this
device and this avatar, not a general performance budget.

## Commands and checks

```sh
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
ost build --jobs auto
ost test
ost validate
# Repeat with (samples, outlines, hulls) = (1,on,15), (4,on,15),
# (4,off,0), (8,on,15); local bundle paths supplied at launch:
ost renderer viewport --intent viewport-usd --profile lookdev \
    --with <usdVrmFileFormat bundle> --with <usdVrmPackageResolver bundle> \
    --with sha256:894fd616f1414d5b393ff0d50abbf7ef18cb603562133c72e642493b48f71667 -- \
    --usd <Alicia motion stage> --time 0 --time-step 0.25 \
    --hidden --frames 1200 --vsync off --overlay off \
    --samples <samples> --outlines <outlines> --expect-draws 20 \
    --expect-hulls <hulls> --screenshot <capture.ppm>
```

The `viewport-usd` CTests pass 26/26, including three new motion tests;
the `core` CTests pass 4/4. Both `ost validate` runs pass. The GPU checks
are additional evidence entries under the existing headless CTest.
The standalone viewport also presents eight hidden frames with outlines
off, and the documented motion fixture presents nine through the managed
Hydra viewport command. Both workflow-specific validation runs pass.

## Remaining scope

No frustum or occlusion culling is added: hulls outside the camera or
behind other surfaces can still be submitted. Partially zero width maps
are conservatively retained; the omission proves an entire resident image
zero, not the samples of a particular animated mesh. The main tested thin
silhouette is a translating diamond; rotating silhouettes, varying camera
distances, arbitrary edges and real-avatar temporal error still need
quality evaluation. The unit and depth contracts are covered by controlled
regressions, not an exhaustive close-up study of every avatar. The outline
item stays in the roadmap for those remaining quality and visibility gaps.
