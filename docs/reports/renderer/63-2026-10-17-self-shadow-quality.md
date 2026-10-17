# MMD's self-shadow follows the camera and softens its edge as MMD's does

- Date: 2026-10-17
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Tooling: `ost 0.23.14`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`;
  `usd-mmd-plugins` v0.2.0's `mmdImaging` at the `lookdev` profile
  (`sha256:39f3e24d…`, [report 53](53-2026-10-11-mmd-material-read.md));
  this branch's `toon` `hydra` package, built here
  (`sha256:e64b8e01921527a5e6c500b88e893af7837e3ab7a0aad485b474c51a2286ac08`);
  MikuMikuDance v9.32 x64 for the captures; Python 3 with NumPy and Pillow
  for the comparisons
- Occasion: v0.4.0's MMD self-shadow, whose edges step close up
  ([roadmap](../../roadmap/v0.4.0.md),
  [report 60](60-2026-10-12-mmd-self-shadow.md))

## TL;DR

**MMD's self-shadow no longer steps or streaks close up, and its edge is
about as soft as MMD's. Across the light, its map now spans what the
camera sees of the receivers, as MMD's own map follows its camera, so a
face close-up spends about 1.9 and 2.7 times as many texels across the
light's two axes as before. A receiver reads a disc of the map six texels
in radius, 32 bilinear taps, compared on the point's own plane, after
moving along its normal and allowing for its slope. Six texels came from
MMD's frames: MMD's edge widens as its camera draws back, as an edge a
fixed number of its camera-following map's texels wide would, and six of
this map's texels matched it on a face, a bust and a full body better than
a sharper edge, a wider one or a coarser map. Against MMD's frames from
four cameras, hdToon's frames are nearer than before, the full body's mean
difference 5.54 where report 60's was 5.62 and the face's 2.44 where it
was 2.59. The map's pass costs about 0.23 ms of GPU time a frame for one
model, which the viewport's telemetry now shows as its own part.**

## 1. What stepped

[Report 60](60-2026-10-12-mmd-self-shadow.md)'s map is 2,048 texels
fitted to the box around every MMD draw that casts or receives, whatever
the camera sees, and a receiver reads it texel by texel, as `full.fx`
does. On the reference model, 布伦妮, that is about 0.0098 MMD units a
texel across the light. Seen from distance 9 at 3,840 × 2,160 and a 30°
field of view, a pixel is about 0.0022 MMD units, so a texel spans four
to five pixels, and every shadow edge on the face is a staircase. Where a
surface turns from the light, the facets of its triangles, which the map
saw at slightly different depths, shadowed one another in streaks beside
the mouth and on the chest.

MMD's own frames from the same cameras have smooth, soft edges (§3); its
map follows its camera.

## 2. The change

- **Core.** `OutlineBounds::ScenePieces` gives a mesh's parts in the
  scene's space: each joint's box around the points it moves, posed as
  `SceneBounds` poses them, or the mesh's one box when it is not skinned.
  A skinned point lies among its joints' boxes, not always within one, so
  the pieces place the mesh's parts rather than bound it.
- **The map's span.** `MeshCache::SelfShadowBounds` returns a
  `SelfShadowExtent`: along the light, the box around every MMD draw that
  casts or receives, as before; across it, the receivers' pieces that the
  camera's frustum reaches, each cut to the frustum's cross-section at its
  far side, in the light's frame. `ShadowMap::Update` spans that, no more
  than the whole box, padded by 2% and 16 texels for the disc and the
  normal offset. So that a moving camera does not make the edges crawl,
  each axis's span changes in steps of an eighth of an octave and its
  centre in whole texels; the axes are fitted apart, so a texel need not
  be square. A camera that sees no receiver spans the whole box. Along the
  light nothing changes, so the depth comparison in MMD units is as it
  was.
- **Reading it.** `SelfShadowLight` reads 32 taps spread evenly over a
  disc six texels in radius (Vogel's spiral), each the bilinear blend of
  its four texels' comparisons, from one `Gather`, each comparison in
  `full.fx`'s form on a depth in MMD units. A tap compares the depth the
  point's own plane has there, which the point's texel and depth changing
  from pixel to pixel give, limited to a slope of four. Before reading,
  the point moves along its normal by up to three texels, by the sine of
  its angle from the light, and must lie further behind the map by two
  texels' width for each unit of its slope, up to four.
  `MmdShadowParameters` carries the texel's size, in view-space units and
  MMD units, and is 96 bytes.
- **Its cost.** `PresentGpuTimes::shadow` times the map's pass, which
  `uploads` included before; the viewport's overlay, telemetry and
  `--telemetry-output` show it as `shadow`.

The roadmap's earlier plan kept anything MMD does not do, such as
filtering, behind a host's switch or left report 60's full-body frame as
it was. The project chose instead that the shadow's quality may exceed
MMD's, and then that its edge should be as soft as MMD's: what the
renderer reproduces is where MMD's shadow falls, how dark it is and how
soft its edge is, not its map's steps
([material policy §10](../../design/MATERIAL_POLICY.md#10-mmds-self-shadow)).

## 3. Against MMD's captures

The user took eight frames of the same model, unmodified, in MMD v9.32:
3,840 × 2,160 PNG with a transparent background, MMD's default light, a
30° field of view, physics and the ground shadow off, self-shadow at its
default and off. hdToon drew each from the same camera through the local
MMD Formation. `TOON_MMD_ANGLE` in the local reference-shot script turns
its camera as MMD's angle does; a 30° turn about the vertical lines up
with MMD's.

| Frame | Centre (MMD units) | Angle | Distance |
| --- | --- | --- | --- |
| Full body | (0, 10, 0) | 0 | 45 |
| Bust | (0, 12.5, 0) | 0 | 18 |
| Face | (0, 14.3, 0) | 0 | 9 |
| Face, turned | (0, 14.3, 0) | 30° about the vertical | 9 |

### 3.1 How soft the edge is

MMD's edge is soft but keeps the shadow's shape: a face close-up shows the
hair's and nose's shadows in detail, with edges several pixels wide. Its
width was matched by drawing the same frames with a width set from the
environment for the comparison and measuring each against MMD's, over the
pixels MMD's self-shadow changes by more than 4 levels and over a band 12
pixels either side of the edges of those pixels. Mean differences:

| Edge | Face, changed | Face, band | Bust, changed | Bust, band | Full body, changed | Full body, band |
| --- | --- | --- | --- | --- | --- | --- |
| 4 × 4 texels, about 3 wide | 2.76 | 4.14 | 3.95 | 5.36 | 5.52 | 6.75 |
| Disc of 4 texels | 2.68 | 4.11 | 3.89 | 5.30 | 5.49 | 6.71 |
| Disc of 6 texels | 2.65 | 4.10 | 3.87 | 5.29 | 5.48 | 6.70 |
| Disc of 8 texels | 2.63 | 4.11 | 3.86 | 5.30 | 5.48 | 6.69 |

The differences are small, since most of what differs lies in where the
shadows fall, not how their edges blend; seen side by side, MMD's face
edges lie between four and six texels' discs, and eight blur the shadow's
shape. A disc fixed in MMD units instead suits one framing only: 0.02 MMD
units suits the face and is too narrow for the full body, where 0.04 to
0.08 did best, which is what an edge a fixed number of a camera-following map's
texels wide does. Six texels is about 8 to 10 pixels on the face and 5 on
the full body at 3,840 × 2,160.

A coarser map softens the edge too, but loses the shape MMD keeps: drawn
into 1,024, 512 or 256 of its texels, read 4 × 4, the full body's
changed pixels were 5.58, 5.70 and 6.09 from MMD's where 2,048 gave 5.52,
and the bust's 3.99, 4.15 and 4.55 where 2,048 gave 3.95.

### 3.2 The frames

Mean difference and share of pixels within 4 levels, over the pixels both
frames cover opaquely, and over those MMD's self-shadow changes, before
this change (report 60's package) and after it:

| Frame | Before | After | Before, where the self-shadow changes MMD | After, there |
| --- | --- | --- | --- | --- |
| Full body | 5.62, 72.7% | 5.54, 72.9% | 5.52, 72.8% | 5.48, 72.6% |
| Bust | 3.57, 82.3% | 3.44, 82.9% | 4.02, 81.6% | 3.87, 82.2% |
| Face | 2.59, 88.7% | 2.44, 89.4% | 2.92, 87.0% | 2.65, 88.4% |
| Face, turned | 2.40, 89.3% | 2.29, 89.8% | 3.07, 86.1% | 2.82, 87.2% |

The full-body frame is report 60's: its new capture without physics
differs from report 60's by under 0.01 on average, and its numbers
reproduce report 60's. The averages move little, since what changed lies
along the shadow's edges, a small share of any frame. Seen close up, the
face's staircase edges, the streaks beside the mouth and on the chest,
and the hair's stepped shadow on the turned face's ribbon are gone, and
the edges blend over about as many pixels as MMD's. The face's map spans
0.50 × 0.59 m across the light where the whole box is 0.95 × 1.58 m, so a
texel is about 1.4 to 1.7 pixels.

On the way, the streaks beside the mouth outlasted a filter with a
two-texel normal offset and a three-texel one; allowing for the slope
removed them, and comparing on the point's own plane keeps the wide disc
from bringing them back.

## 4. Its cost

The viewport drew the model through Hydra 600 frames at 1,920 × 1,080, 4
samples per pixel, vsync off, self-shadow on and off, at its own framing
and closer (`--camera-dolly 3`):

| Framing | Self-shadow | `shadow`, mean (p95) | GPU work, mean (p95) |
| --- | --- | --- | --- |
| Whole model | on | 0.228 ms (0.237) | 0.811 ms (1.142) |
| Whole model | off | 0 | 0.517 ms (0.848) |
| Closer | on | 0.228 ms (0.239) | 0.887 ms (1.502) |
| Closer | off | 0 | 0.521 ms (0.801) |

The map's pass is about 0.23 ms whatever the camera sees. The self-shadow
adds 0.29 to 0.37 ms to the frame's GPU work, the rest the receivers' 32
taps, more as the model covers more of the frame.

## 5. Tests

- `toon-outline-bounds`: an unskinned mesh's one piece is its scene
  bounds, and a skinned mesh's two pieces are its joints' posed boxes.
- `renderer.material.mmd_self_shadow` passes unchanged: the wall's lit and
  shadowed centre, and the switch, receive and cast cases, are as report
  60 states.

The OpenStrata gates passed, each with `ost build`, `ost test` and
`ost validate`: `core` 15/15, `hydra` 24/24 and `viewport-usd` 55/55;
`ost package` packaged the `hydra` build twice, to the same digest.

## 6. What it does not do yet

- **MAT-Q5.** How far behind the map a point falls into shadow is still
  report 60's provisional 15 per MMD unit, and MMD's self-shadow distance
  and its modes are not read. The face frames are close enough to tell a
  contact shadow apart, but this change did not compare the sharpness on
  them.
- **MMD's edge by its own map.** The edge's width is six of this map's
  texels, which track MMD's on these four framings; MMD's self-shadow
  distance, which changes its map's texels, would change its edge, and
  that is not followed.
- **More than one map.** One map still covers every receiver the camera
  sees: a view that sees a large MMD prop and a model far apart spreads
  its texels over both, and widens the edge with them.
- **Temporal evidence.** The span's steps keep the edges still between
  steps; a moving camera's frames were not captured.
