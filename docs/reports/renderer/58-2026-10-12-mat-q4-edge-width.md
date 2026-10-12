# MAT-Q4 settled: MMD's edge is a share of the frame's height that falls with depth, fitted on MMD's frames from five cameras

- Date: 2026-10-12
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Tooling: `ost 0.23.14`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`); `usd-mmd-plugins` v0.2.0's `mmdImaging` at the
  `lookdev` profile (`sha256:39f3e24d…`, [report 53](53-2026-10-11-mmd-material-read.md));
  this branch's `toon` `hydra` package, built here
  (`sha256:330e4e1a0fd13c084b76a18e7f13d270a60b34693749baa15b1ee3cede4c51aa`),
  and [report 57](57-2026-10-11-mmd-outline.md)'s (`sha256:a127eda7…`) for
  its width; MikuMikuDance v9.32 x64 for five more reference captures;
  Python 3 with Pillow and NumPy for the measurements
- Occasion: [MAT-Q4](../../design/MATERIAL_POLICY.md#9-open-questions), the
  MMD edge's width, which report 57 fitted to two distances and left to be
  settled against captures at more distances, another field of view and
  another output size ([roadmap](../../roadmap/v0.4.0.md)).

## TL;DR

**On MMD's own frames an edge is a share of the frame's height. At 18 MMD
units from the camera, a 15° field of view makes the face twice as large as
30° does and leaves its edge as many pixels wide, 0.98 against 0.91; a
2,160-pixel frame makes it 1.80 pixels against 0.91. With the distance, the
share falls from 1.61 pixels at 9 units to 0.91 at 18 and 0.73 at 30: a
constant share plus one over the depth. Report 57's width, a length plus a
number of pixels, gave 1.75 pixels at 15° and 1.53 at 2,160. `mmd_outline`
now draws an edge of `edgeSize` × `edgeScale` as that size times (0.00108 +
0.0203 / the vertex's view depth in MMD units) of the target's height,
constants at which hdToon's edge is MMD's width at 9, 18 and 30 units; it
is 0.88 pixels at 15° and 1.75–1.96 at 2,160. MAT-Q4 is resolved.**

## 1. The captures

The reference model and its terms are [report 55](55-2026-10-11-mat-q3-mmd-colour.md)'s:
the Sour-style Hatsune Miku Ver.1.02, `Black.pmx`, of which neither the
model, its textures nor any render is committed. The face (`face`) and the
body (`base`) draw an edge of size 0.4 in (147, 79, 141); every vertex's
edge scale is 1.

Report 57 had capture C (centre (0.80, 10, 0), distance 45) and a close-up
at centre (0, 18, 0), distance 9, both at a 30° field of view and 1920 ×
1080. The user took five more in MMD, as PNG with coverage in alpha, all at
centre (0, 18, 0):

| Capture | Distance | Field of view | Size |
| --- | --- | --- | --- |
| 1 | 18 | 30° | 1920 × 1080 |
| 2 | 30 | 30° | 1920 × 1080 |
| 3 | 70 | 30° | 1920 × 1080 |
| 4 | 18 | 15° | 1920 × 1080 |
| 5 | 18 | 30° | 3840 × 2160 |

The chin, where the edge is measured, lies 0.96 MMD units in front of the
centre's plane, at a height of 17.0: the chin's place in hdToon's frames at
9, 18 and 30 units gives both. Its depth from the camera is the distance
less 0.96.

## 2. Measuring an edge thinner than a pixel

Report 57 unmixed each pixel along a contour into the edge colour and the
colours a few pixels either side, at the pixel nearest the edge colour. Two
things were wrong with it for the far and thin edges this report needs.

- **The line.** The pixel nearest the edge colour is not always on the
  line: at 30 units the collar's dark blue is nearer the edge's purple than
  a faint line over skin is, and the measure found the collar. The line is
  now where the unmixed share of the edge peaks; a mixture of the two
  colours either side, as the collar's top is, unmixes to no edge.
- **hdToon's frames.** hdToon resolves its samples in linear values, MMD in
  stored ones. Unmixed as stored, a line that covers part of a pixel in
  hdToon's frame reads thinner than it is. hdToon's frames are now decoded
  to linear before they are unmixed; MMD's are unmixed as stored. Report 57
  measured both as stored: its hdToon widths at distance 45 read low.

For hdToon the width can also be had exactly: the same camera with every
edge scale 0 draws the frame without edges, and each pixel's share of the
edge is its move from that frame towards the edge colour. Against those
exact widths, at report 57's width times 0.25 to 1.5, the contour measure
reads within 0.04 pixels at 9 units (0.48, 0.91, 1.33, 1.76, 2.64 against
0.44, 0.87, 1.31, 1.75, 2.63), and at 18 and 30 units within 0.05 on 8 of
10 readings each, the other two 0.14–0.23 over. MMD's frames are measured with it.

On the chin over the neck, left and right of its point, MMD's edge is:

| Capture | Depth of the chin | Field of view | Height | Width (pixels) |
| --- | --- | --- | --- | --- |
| report 57's close-up | 8.04 | 30° | 1080 | 1.61, 1.62 |
| 1 | 17.04 | 30° | 1080 | 0.93, 0.90 |
| 4 | 17.04 | 15° | 1080 | 0.98, 0.99 |
| 5 | 17.04 | 30° | 2160 | 1.91, 1.70 |
| 2 | 29.04 | 30° | 1080 | 0.70, 0.76 |
| C | 44.04 | 30° | 1080 | 0.66, 0.66 |
| 3 | 69.04 | 30° | 1080 | 0.52–0.84 over four short contours |

At 45 and 70 units the chin's contours are a few pixels long and meet the
neck's own edges, and the widths are worth less than the others.

## 3. What MMD's widths say

- **Not a length.** At 15° the face is 2.03 times as large in the frame as
  at 30° from the same distance. A length would draw twice as many pixels;
  MMD's edge draws 0.98 against 0.91.
- **Not a number of pixels.** In a frame twice as tall, MMD's edge is 1.80
  pixels against 0.91: the same share of the frame's height.
- **Falling with depth, but not as one over it.** From 8.04 to 17.04 units
  the share falls by 1.77, from 17.04 to 29.04 by 1.25. One over the depth
  would give 2.12 and 1.70. A constant share plus one over the depth,
  0.358 + 10.03 / depth pixels for an edge of 0.4 in 1,080, fits the
  twelve near contours within 0.1 pixels and gives 0.59 at 44 units and 0.50 at
  69, where MMD's frames show 0.66 and 0.52–0.84.

So MMD's edge is `edgeSize` × `edgeScale` × (a + b / depth) of the frame's
height. Whether MMD's depth is each vertex's or the camera's distance from
its centre cannot be told from one model at one centre; the renderer takes
each vertex's.

## 4. The width the renderer draws

hdToon's hull moves each vertex along its normal, and on the chin the line
it shows is not exactly the move: at 4 samples a line under a pixel wide
shows a little less than the move, and at 9 units a little more. The
constants are fitted to what hdToon shows rather than to the move. At each
capture, hdToon drew report 57's width times 0.25, 0.5, 0.75, 1 and 1.5;
the exact widths of §2 were interpolated to MMD's, and that multiple of
report 57's move at the chin is the move hdToon needs there. At 45 and 70
units hdToon's own width does not rise with the multiple on these short
contours, so those two captures are left out of the fit. Fitted to the
other ten contours:

```text
edgeSize × edgeScale × (0.00108 + 0.0203 / depth in MMD units) of the target's height
```

which is 1.56, 0.98 and 0.77 pixels of 1,080 for an edge of 0.4 at 8.04,
17.04 and 29.04 units, against moves of 1.57, 0.89–1.06 and 0.78–0.82 the
interpolation asked for, and 0.67 at 44.04 and 0.60 at 69.04.

`mmd_outline` turns that share into a length at the vertex's clip w as a
screen-coordinates MToon outline is, so a field of view or a target's size
leaves the share alone. MMD units are 0.08 m on the stage
([`usd-mmd-plugins` stage contract §6](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/STAGE_CONTRACT.md)),
and the view depth is divided into them by the scene's `metersPerUnit`. The
depth is taken as at least one MMD unit, which bounds the width at the
camera and gives a vertex at an orthographic camera's depth one.

## 5. The change

- **Shader.** `mmd_outline.slang`'s `EdgeWidth` takes the vertex's view
  depth and returns the share above as a view-space length; the length in
  MMD units and the pixels of report 57 are gone.
- **Draw constants.** `outline_scale` is the scene's units per metre for
  every draw again; `mmd_outline` no longer reads the target's height.

The depth bias `mmd_outline` shares with `mtoon_outline` was suspected of
hiding the thin edge where the chin lies just in front of the neck; a
build without it drew the same widths at 18 units, so it stays.

## 6. Tests

- `renderer.material.mmd_outline` (headless, GPU): under the orthographic
  camera at the origin the edge of size 4 reaches the same pixel on a
  64 × 64 and a 128 × 128 target, where report 57's pixels did not; under a
  perspective camera an edge of size 40 is 11 pixels wide at 1.5 units, 11
  at twice the zoom, and 7 from 6 units zoomed to keep the diamond's size.
  Its other steps are report 57's.

The OpenStrata gates passed, each with `ost build`, `ost test` and
`ost validate`: `core` 15/15, `hydra` 24/24 and `viewport-usd` 55/55;
`ost package` packaged the `hydra` build twice, to the same digest.

## 7. Against MMD's captures

The local MMD Formation, repinned to this branch's package, drew the model
in `testusdview` from each capture's camera, as
[report 57 §5](57-2026-10-11-mmd-outline.md#5-against-mmds-captures) did,
with 20 MMD draws and 9 edge hulls. hdToon's width, exact against its
edge-free frame, on the same contours:

| Capture | MMD | report 57's width | this width |
| --- | --- | --- | --- |
| report 57's close-up, 9 units | 1.61, 1.62 | 1.75, 1.76 | 1.54, 1.54 |
| 1, 18 units | 0.93, 0.90 | 0.87, 0.88 | 0.87, 0.88 |
| 4, 18 units at 15° | 0.98, 0.99 | 1.74, 1.75 | 0.88, 0.87 |
| 5, 18 units at 2,160 | 1.91, 1.70 | 1.53, 1.53 | 1.75, 1.96 |
| 2, 30 units | 0.70, 0.76 | 0.64, 0.66 | 0.75, 0.86 |
| C, 45 units | 0.66, 0.66 | 0.45, 0.64 | 0.59, 0.42 |
| 3, 70 units | 0.52, 0.66, 0.84, 0.59 | 0.39, 0.43, 0.43, 0.20 | 0.50, 0.44, 0.40, 0.22 |

The two widths are alike at 18 units and 30°. At 15° report 57's edge is
nearly twice MMD's and this one 0.1 pixels under it; at 2,160 pixels this
one is within MMD's two contours and report 57's under both. At 9 units it
is 0.07 pixels under MMD where report 57's was 0.14 over it. At 30 units and
beyond it is nearer MMD's on most contours, within what the measure
resolves there.

Mean absolute difference from MMD's capture in 8-bit levels, on the pixels
either width changes from the edge-free frame, where MMD's frame is opaque:

| Capture | Pixels | No edge | report 57's width | this width |
| --- | --- | --- | --- | --- |
| report 57's close-up | 27,775 | 35.58 | 22.05 | 22.76 |
| 1 | 14,298 | 32.16 | 23.23 | 23.28 |
| 4, 15° | 27,745 | 28.30 | 22.34 | 21.58 |
| 5, 2,160 | 25,369 | 52.50 | 54.14 | 54.96 |
| 2 | 11,711 | 29.47 | 22.58 | 22.51 |
| C | 5,395 | 32.94 | 28.42 | 28.72 |
| 3 | 4,939 | 28.93 | 22.29 | 21.98 |

These pixels follow where an edge lands as much as how wide it is: a pose
that moved, as the hair did under MMD's physics, or a contour a fraction of
a pixel away decides most of the difference. Only at 15°, where the widths
part most, does the width decide it. At 2,160 pixels the edges add to the
difference with either width; on the 1,080-pixel frames they take it from
29–36 levels to 22–29.

## 8. What it does not do yet

- **Depth per vertex or per camera.** The fit cannot tell whether MMD's
  depth is the vertex's or the camera's distance from its centre (§3).
- **An orthographic camera** is not measured; the renderer draws there the
  share at the vertex's view depth, at least one MMD unit.
- **Hull omission** for MMD edges, **edge and surface order across meshes
  and models**, and the rest of
  [report 57 §6](57-2026-10-11-mmd-outline.md#6-what-it-does-not-do-yet).
