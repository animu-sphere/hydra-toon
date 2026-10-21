# Shared shadows, stage 3: cross-family acceptance, parity, looks and bounded cost

- Date: 2026-10-21
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Tooling: `ost 0.23.14`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`; plain CMake
  without OpenUSD; `usd-mmd-plugins` v0.2.0's `mmdImaging` and
  `usd-vrm-plugins`' `vrmImaging` at the `lookdev` profile; the
  `usd-avatar-runtime` providers of reports
  [47](47-2026-10-11-avatar-real-providers.md) and
  [50](50-2026-10-11-avatar-motion-provider.md)
- Occasion: stage 3 of the [shadow policy](../../design/SHADOW_POLICY.md#9-stages),
  the remaining work and done-when condition of the
  [v0.6.0 roadmap](../../releases/v0.6.0.md)

## TL;DR

**The v0.6.0 done-when condition is met on this machine. A new GPU check
draws every receiving family (MToon, MMD, PreviewSurface, unbound) under
every casting one, static, skinned and morphed, with shared shadows and
MMD's self-shadow each on and off: 192 cases, all as policy §3 and §4 say,
and each family's mesh shadowing itself, with no geometry uploaded after
the first frame. Through the runtime's providers the Hydra and fast paths
draw the same shadow views with the same casters and identical images,
pose and weight updates uploading nothing static. MMD's eight frames of
布伦妮 are byte-identical to `main` before shared shadows; MToon's
shadows-off frames are byte-identical to v0.5.0's, and its shadows-on
frames are recorded as the new reference. Two committed, original fixtures
record the cost: four 2,048-texel views of sixteen moving casters take
0.138 / 0.139 / 0.141 ms of GPU (mean / p95 / p99), 0.023 / 0.024 / 0.028 ms
of CPU fit and 64 MiB.** No renderer or shader source changed in this
stage.

## 1. Cross-family cases

`renderer.shadow.cross_family` (`adapters/headless/main.cpp`) takes
`renderer.shadow.receivers`' wall and the square raised 0.4 before it under a
light along (1, 0, −1), and reads the shadow material debug view, which is
each receiver's visibility: 0 at the centre, in the square's shadow, and 255
beside it. The roadmap's pairs are cells of one matrix:

- **Receivers**: MToon (white lit, 0.2 shade), MMD (report 60's wall,
  casting and receiving its self-shadow), PreviewSurface (rough), and an
  unbound white display colour.
- **Casters**: the same four families.
- **Deformation**: a static square; a skinned square whose rest points lie
  1.5 above and whose pose moves it into place; a morphed square whose
  weight does.
- **Settings**: shared shadows and MMD's self-shadow each on and off.

The expected visibility is the policy's: MMD receives only while its
self-shadow is on, from MMD casters always and from the others while shared
shadows are on; every other family receives from every caster, MMD's
included, while shared shadows are on. All 192 cases read 0 or 255 within
two levels, as expected. For every receiver and caster pair the shaded
frame also darkens in the shadow by at least 40 levels against the frame
with both settings off: MToon 255 to 51, MMD 255 to 156, PreviewSurface and
the unbound wall lose the light's direct response. A mesh of each family
that is a wall with its own raised flap shadows itself.

Squares out of a case are moved aside by their transform rather than
hidden: a first version hid them and counted a geometry upload each time
one reappeared, which is the visibility path releasing a hidden mesh's
buffers, not the shadow path. After a first frame that draws every mesh,
the whole matrix writes poses, morph weights and material bindings and
uploads no points, topology, skins or morph targets.

## 2. Hydra and fast paths

`toon-renderer-hydra-avatar-providers-gpu` and `-motion-gpu` draw each
result of the runtime's providers twice: through the fast adapter over the
bound Hydra scene, and authored for UsdImaging and usdSkelImaging to
compose. Their avatar has no light, so the camera key casts. Each frame now
also requires both renderers to draw the same number of shadow views, more
than none, with the same caster draws:

| Case | Results | Shadow views (both) | Caster draws | Colour / depth | Static uploads after the first |
| --- | ---: | ---: | ---: | --- | --- |
| `gpu` (gaze) | 18 | 36 | 2 each | identical, 0 flipped pixels | none; 3 joint, 16 weight writes |
| `motion-gpu` (clip, then gaze) | 18 | 36 | 2 each | identical, 0 flipped pixels | none; 34 joint, 16 weight writes |

The bounded fixtures (§5) hold the Hydra viewport path to the same: across
240 frames of pose and weight updates the uploads stay at one per mesh.

## 3. MMD's looks

This branch's `hydra` package (`sha256:b204b346…`, built twice to the same
digest) in a copy of report 86's MMD Formation, drawing 布伦妮 through report
63's reference-shot script at its face, turned face, bust and full body,
1,920 × 1,080, 4x, with `toon:shadows` on and off: 8 of 8 PNGs
byte-identical to stage 2's and to `main` before shared shadows. The
evidence line reads `draws_mmd_shadow=19 shadow_views=1 draws_shadow=19`.
The fixture's 16 MMD self-shadow files were unchanged in stage 2 and no
renderer source changed since. Reports
[60](60-2026-10-12-mmd-self-shadow.md),
[63](63-2026-10-17-self-shadow-quality.md) and
[66](66-2026-10-17-mat-q5-shadow-depth.md) remain MMD's baseline.

## 4. MToon's looks

AliciaSolid's `alicia_VRMA_01.usda` in the `viewport-usd` viewport with
`vrmImaging`, 800 × 900, 4x, under the camera key, all 20 draws MToon.

**Shadows off must not change.** Report 83's representative smoke again (120
frames, time step 0.25, checkerboard, `--shadows off`): its foreground PNG
is byte-identical to v0.5.0's. The still frames at report 85's three
framings with shadows off are byte-identical to stage 1's.

**Shadows on is the new reference.** The still frames at time 0 and the
smoke's last frame, with the defaults of report 86 (2,048 texels, 1.5 / 1.0 /
1.5 texels):

| Frame | Covered pixels | Changed from shadows off by more than 8 levels | Mean change | PNG SHA-256 |
| --- | ---: | ---: | ---: | --- |
| Full body | 57,300 | 6,912 | 4.70 | `9071c6ae5f5103d2…` |
| Legs (`--camera-dolly 9 --camera-pan 0 -120`) | 215,385 | 54,895 | 10.96 | `6b5f96c02338d992…` |
| Face (`--camera-dolly 12 --camera-pan 0 170`) | 319,672 | 19,130 | 1.47 | `6c67ba92a246b29b…` |
| Smoke, frame 120 | 64,377 | 10,278 (from v0.5.0's) | 4.97 | `5d76e75c4ecd661a…` |

Alpha is unchanged in every pair. Against stage 1's shadows-on frames, the
normal bias of report 86 changes 32, 103 and 38 pixels by more than 8
levels (mean 0.07 or less), along shadow edges. Inspected: the skirt's
shadow on the thighs and stockings, the bangs' on the forehead and the
hair's on itself, with no speckle, banding or detached shadow. These frames,
by their digests, are MToon's reference with shadows on; captures stay in
the ignored build directory, as the avatar is not redistributable.

## 5. Bounded cost

Two original fixtures are committed beside the viewport's tests:
`shadow-cost.usda`, a 4 m PreviewSurface backdrop facing the camera under
one distant light and a 0.25 dome, before it a 4 × 4 grid of sixteen tall
octahedra, each rigidly skinned to its own joint, half PreviewSurface and
half unbound display colour, turning and moving toward and away from the
backdrop over 61 time codes, one also stretched by an animated blend
shape; and `shadow-cost-4.usda`, the same under three more distant lights.
Every update after the first is a pose or a weight.

`toon-viewport-shadow-cost-1` and `-4` (`check_shadow_cost.cmake`) draw each
64 frames at 256 × 256, vsync off, at the default samples and resolution,
fail if a pose or weight uploads a static resource or the views drawn,
resolution or memory are wrong, and write a record of the scene, device,
samples, views, resolution, layers, memory and the mean, p95 and p99 of the
GPU shadow pass, GPU work, CPU fit and CPU submit. In `ost test`: 1 view
0.037 / 0.039 / 0.048 ms GPU shadow pass, 16 MiB; 4 views 0.138 / 0.141 /
0.145 ms, 64 MiB.

At report 86's size (800 × 900, 4x, vsync off, `--time-step 0.25`, 240
frames, 239 measured), milliseconds as mean / p95 / p99:

| Scene | Views, texels | GPU shadow pass | GPU work | CPU fit | CPU submit | Image |
| --- | --- | --- | --- | --- | --- | ---: |
| `shadow-cost`, shadows off | 0 | 0 | 0.060 / 0.062 / 0.064 | 0.001 / 0.001 / 0.001 | 0.565 / 0.698 / 0.963 | 16 MiB |
| `shadow-cost` | 1, 2,048 | 0.037 / 0.038 / 0.039 | 0.134 / 0.136 / 0.137 | 0.020 / 0.021 / 0.026 | 0.777 / 0.881 / 1.040 | 16 MiB |
| `shadow-cost-4`, shadows off | 0 | 0 | 0.077 / 0.079 / 0.080 | 0.001 / 0.001 / 0.002 | 0.572 / 0.692 / 0.783 | 16 MiB |
| `shadow-cost-4` | 4, 1,024 | 0.046 / 0.048 / 0.049 | 0.255 / 0.257 / 0.259 | 0.024 / 0.025 / 0.035 | 1.207 / 1.329 / 1.539 | 16 MiB |
| `shadow-cost-4` | 4, 2,048 | 0.138 / 0.139 / 0.141 | 0.353 / 0.355 / 0.356 | 0.023 / 0.024 / 0.028 | 1.252 / 1.498 / 1.654 | 64 MiB |
| `shadow-cost-4` | 4, 4,096 | 0.492 / 0.507 / 1.026 | 0.723 / 0.746 / 1.256 | 0.024 / 0.028 / 0.037 | 1.697 / 1.937 / 2.293 | 256 MiB |
| AliciaSolid, shadows off | 0 | 0 | 0.363 / 0.368 / 0.506 | 0.007 / 0.002 / 0.003 | 1.112 / 1.294 / 1.564 | 16 MiB |
| AliciaSolid, camera key | 1, 2,048 | 0.248 / 0.249 / 0.355 | 0.626 / 0.847 / 1.035 | 0.152 / 0.150 / 0.228 | 1.617 / 1.978 / 2.265 | 16 MiB |

Every run uploaded each mesh once (17 for the fixtures, 20 for the avatar)
and then only joint buffers and weights. A shadows-off session keeps the
16 MiB single-layer image it starts with. The views' cost follows their
texels and their casters: a 2,048-texel view of the fixture's 130
triangles costs 0.04 ms where AliciaSolid's 61,896 skinned triangles cost
0.25 ms, and four views of the fixture cost less than one of the avatar.
The CPU fit follows the receivers' joint boxes: 0.02 ms for sixteen
one-joint figures, 0.15 ms for the avatar. Against design policy
§23's initial targets, the CPU submit stays under 2 ms at p95 in every run
and the GPU work well under the avatar's 2 to 6 ms. AliciaSolid's numbers agree with
reports 85 and 86 within 0.01 ms of mean.

## 6. Tests

| Check | What it holds |
| --- | --- |
| `renderer.shadow.cross_family` (new) | §1: 192 receiver, caster, deformation and setting cases through the shadow debug view, the shaded wall darkening by 40 levels for every pair, four self-shadowing meshes, no geometry uploaded after the first frame |
| `toon-renderer-hydra-avatar-providers-gpu`, `-motion-gpu` | add: both paths draw the same shadow views, more than none, with the same caster draws (§2) |
| `toon-viewport-shadow-cost-1`, `-4` (new) | §5: the bounded fixtures' record, and no static upload under pose and weight |

`ost build`, `ost test` and `ost validate` pass for `core` (23 of 23),
`lookdev` `hydra` (37 of 37) and `viewport-usd` (73 of 73); plain CMake 23
of 23; the provider host 5 of 5; `ost package --profile lookdev --intent
hydra` twice gives `sha256:b204b346…` both times;
`renderer.shadow.cross_family` is in `openstrata.renderer.yaml`; Vulkan
validation stays clean.

## 7. Limits

- One machine and GPU. The fixtures name their device in their record;
  other GPUs and Linux follow
  [platform and GPU coverage](../../roadmap/later.md#platform-and-gpu-coverage).
- MToon's reference is AliciaSolid's three still framings and one motion
  frame, by digest; the avatar and captures are not redistributable.
- Point-light, cascaded, coloured and falloff shadows and shadow linking
  remain outside the policy (§10); the coarse-mesh shadow terminator of
  report 86 remains.
