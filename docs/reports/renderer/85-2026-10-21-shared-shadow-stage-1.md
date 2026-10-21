# Shared shadows, stage 1: one directional view across material families

- Date: 2026-10-21
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Tooling: `ost 0.23.14`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`; plain CMake
  without OpenUSD; `usd-mmd-plugins` v0.2.0's `mmdImaging` and
  `usd-vrm-plugins`' `vrmImaging` at the `lookdev` profile; `main`'s `toon`
  `hydra` package (`sha256:d247221c…`) as the baseline
- Occasion: stage 1 of the [shadow policy](../../design/SHADOW_POLICY.md#9-stages),
  the first work item of the [v0.6.0 roadmap](../../releases/v0.6.0.md)

## TL;DR

**Stage 1 of the shadow policy is in. The core's `ShadowFit` fits one
directional view, MMD's former map fit generalised to every family, and
one `shadow` caster pass draws MToon, PreviewSurface, unbound, unlit and
MMD casters, Mask cut, at their skinned or morphed positions. MMD reads
that view as its self-shadow map; MToon scales a light's ramped shading
term by its visibility, leaving the rim unshadowed as three-vrm does;
PreviewSurface scales its direct response. MMD is unchanged byte for
byte: the RGBA fixture's 16 MMD self-shadow files and eight 1,920 × 1,080
frames of 布伦妮 (face, turned face, bust, full body, shared shadows on and
off) are identical to `main`'s. On AliciaSolid the skirt shadows the
thighs and the hair the face and its own strands; the pass costs about
0.25 ms of GPU time at 2,048 texels and the fit 0.15 ms of CPU.**

## 1. What changed

**The fit moved into the core.** `ShadowFit` (`core/render-extraction/shadow_fit.cpp`)
decides each frame which lights are given a view and fits each view, from
the draw list and posed bounds it keeps per mesh (`OutlineBounds`), as the
backend fitted MMD's map before: along the light the box around every draw
that casts into the view or receives it; across it what the camera sees of
the receivers, in eighth-octave steps and whole texels. Stage 1 draws one
view: the first visible directional light whose `shadow:enable` is on, or
the camera fallback key. Spot lights and lights past the capacity are
counted, not drawn. `ShadowRoleOf` states policy §4's table: MMD by its
flags, MToon and PreviewSurface Opaque and Mask (cut), faces as their
surface draws them, the unbound display colour both faces, unlit
diagnostics casting and not receiving, Blend not casting. The view's
arithmetic is the backend's former code moved unchanged, so its floats are
the same.

**One caster pass.** `mmd_shadow` became `shadow`: positions and UVs, the
skinning and morph module every pipeline uses, dynamic cull mode and front
face, and a fragment stage that cuts an MToon or PreviewSurface Mask caster
by its slot's alpha; an MMD caster is drawn as before, both faces, uncut.
The view's clip space keeps y up, so a front face is clockwise in its
image, counter-clockwise under a mirroring transform.

**MMD reads the shared view.** MMD's parameters (binding 6) are derived
from the view of the first direct light, when MMD's self-shadow is on;
its comparison and filter are untouched.

**MToon and PreviewSurface receive.** Each light entry in the frame buffer
names its view, while `toon:shadows` is on; a view parameter buffer
(binding 8) carries the view's clip-from-view transform, size, depth range
and texel. A receiver moves along its normal by up to 1.5 texels as it
turns from the light, must lie 1.5 texels behind the view (more with
slope, to 4), and reads 16 bilinear taps over a 1.5-texel disc, each
compared with the depth its own plane has there; its screen derivatives
are taken before any divergence. These constants are provisional until
stage 2 measures them. MToon multiplies a light's ramped shading term by
the visibility before the shade/lit mix; PreviewSurface multiplies the
light's direct response.

**Settings and evidence.** `ToonLight::shadow` (`UsdLux` `shadow:enable`,
true when unauthored, read through `HdLightTokens->shadowEnable`),
`DrawList::shadows` (`toon:shadows`, `--shadows`, a viewport checkbox),
`OffscreenStatistics::shadow_views`, `shadow_caster_draws`,
`shadow_unviewed`, `shadow_views` and `draws_shadow` in the Hydra evidence
line, `PresentDrawCounts::shadow`, and `FrameLatency::shadow_fit_ms` with
the viewport's `shadow_fit` series.

## 2. The rim

Policy §7 left open whether MToon's rim sums shadowed light. three-vrm's
MToon fragment loop passes each light's shadow to `RE_Direct_MToon` beside
the light, not multiplied into it: `getShading` ends `shading *= shadow`,
and the rim's `directSpecular` adds the light's unshadowed irradiance
before the shading is computed. The renderer now does the same: the rim's
incident sum stays unshadowed. The policy records this.

## 3. MMD unchanged

The migration landed first (commit `275a1f6`) and was checked before any
receiver existed:

- **Fixture.** `toon-rgba-fixtures-gpu`'s outputs, captured from `main`
  before the change, against the migrated build: all 34 files identical,
  among them the MMD self-shadow fixture at 1x and 4x over its four
  backgrounds. After the receivers (§1), the 16 MMD files are still
  identical; the 16 avatar files and the summary change, as MToon now
  receives.
- **Avatar.** `main`'s `hydra` package (`sha256:d247221c…`) and this
  branch's (`sha256:3b60ed14…`, built twice to the same digest), each in a
  copy of the local MMD Formation with `mmdImaging` 0.2.0, drawing 布伦妮
  (unmodified) through report 63's reference-shot script at report 63's
  face, turned face, bust and full body, 1,920 × 1,080, 4x, MMD's default
  light, with `toon:shadows` on and off: 8 of 8 PNGs byte-identical. The
  Hydra evidence line reads `draws_mmd_shadow=19 shadow_views=1
  draws_shadow=19` for the new package, `draws_mmd_shadow=19` for `main`.

Reports 60, 63 and 66 therefore remain MMD's regression baseline.

## 4. Tests

| Check | What it holds |
| --- | --- |
| `toon-shadow-fit` (new, no GPU) | `ShadowRoleOf` per family; one view of the first shadowing directional light, MMD's only when that light is the first direct one; a spot light first leaves MMD unshadowed and is counted; `shadow:enable` off and point lights draw no view; lights past the capacity are counted; the key with no rig or scene lights off; no view without a receiver (MMD's self-shadow off in an MMD scene, shared shadows off for MToon, an unlit scene); the whole-box fallback and a close-up's eighth-octave span and whole-texel centre; a pose moves the view without changing rest points |
| `renderer.material.mmd_self_shadow` | unchanged, passes: lit 255, shadowed 156 |
| `renderer.shadow.shared_casters` (new) | MMD receiver: an opaque MToon square shadows it with shared shadows on and not off (only MMD's casters draw then); MToon and PreviewSurface Mask squares cut below their cutoff and cast above; Blend casts nothing; a single-sided square turned from the light casts nothing, double-sided or mirrored-and-facing it casts, mirrored-and-turned it does not; an unbound square casts; a skinned square casts where its pose puts it, the pose uploading no geometry |
| `renderer.shadow.receivers` (new) | MToon wall, white lit, 0.2 shade: 255 beside the shadow, 51 in it, under MToon, MMD (with MMD's self-shadow off too) and PreviewSurface casters; lit with shared shadows off or the light's shadow off (no view drawn); a square morphed into the light's path casts, the weight uploading no points; a PreviewSurface wall falls from 56 to 0 |
| `toon-renderer-hydra-lights` | `shadow:enable` authored false on the spot reads false; unauthored reads true |

Plain CMake 23 of 23; `ost build`, `ost test` and `ost validate` pass for
`core` (23 of 23), `lookdev` `hydra` (37 of 37) and `viewport-usd` (71 of
71); `ost package --profile lookdev --intent hydra` twice gives
`sha256:3b60ed14…` both times; the new assertions are in
`openstrata.renderer.yaml`; Vulkan validation stays clean.

Two problems found on the way, both fixed before the numbers above: the
caster pipeline first declared a normal stream its vertex stage did not
read, which validation reported; and a receiver compared every tap with
its own depth, which darkened the fixture's octahedral head along its
ridge by up to 18 levels, the far face's texels lying nearer the light.
Comparing each tap with the depth the point's plane has there, as MMD's
receiver already does, reduced the ridge to 2 levels; what remains of the
fixture's difference is the terminators, where N·L crosses 0, and the
shadow one of the raised squares now casts on the dress.

## 5. Avatars

AliciaSolid's `alicia_VRMA_01.usda` in the `viewport-usd` viewport with
`vrmImaging`, 800 × 900, 4x, the camera key (the stage has no lights),
`--time 0`, still foregrounds with `--shadows on` and `off`, all 20 draws
MToon:

| Framing | Covered pixels | Changed by more than 8 levels | Mean change |
| --- | ---: | ---: | ---: |
| Full body | 57,300 | 6,900 | 4.69 |
| Legs (`--camera-dolly 9 --camera-pan 0 -120`) | 215,385 | 54,877 | 10.95 |
| Face (`--camera-dolly 12 --camera-pan 0 170`) | 319,672 | 19,007 | 1.45 |

Alpha is identical in every pair. The skirt casts a soft-edged shadow on
the thighs and stockings; on the face the bangs shadow the forehead's top,
the head shadows the far side's long hair, and strands shadow each other
along their edges. The difference images show no speckle or banding on
lit surfaces. These are MToon's looks with shadows on; their recording as
the new reference is stage 3's (§7). Captures stay in the ignored build
directory: the avatar is not redistributable.

## 6. Cost

The same stage in motion (`--time-step 0.25`, 240 frames, vsync off,
`--telemetry-output`), milliseconds as mean / p95 / p99 over 239 frames:

| Framing | Shadows | GPU shadow pass | GPU work | CPU fit (`shadow_fit`) | CPU submit |
| --- | --- | --- | --- | --- | --- |
| Full body | on | 0.248 / 0.248 / 0.349 | 0.621 / 0.838 / 0.919 | 0.154 / 0.154 / 0.166 | 1.606 / 1.887 / 2.008 |
| Full body | off | 0 | 0.359 / 0.362 / 0.367 | 0.007 / 0.002 / 0.004 | 1.167 / 1.279 / 1.549 |
| Legs | on | 0.245 / 0.252 / 0.261 | | 0.153 / 0.156 / 0.209 | |
| Face | on | 0.252 / 0.253 / 0.358 | 0.710 / 0.837 / 1.097 | 0.153 / 0.210 / 0.223 | 1.766 / 2.096 / 2.330 |
| Face | off | 0 | 0.431 / 0.440 / 0.729 | 0.007 / 0.001 / 0.002 | 1.166 / 1.418 / 1.541 |

The pass, one 2,048-texel D32 view, costs what MMD's map cost in report 63
(0.23 ms); the fit, which walks every receiving mesh's joint boxes, about
0.15 ms; the CPU submit grows by about 0.44 ms, which this run does not
divide between recording and waiting on the GPU. The view's memory is
16 MiB, as MMD's map was. These are one machine's numbers, not the
bounded fixtures with p95/p99 of the roadmap's done-when condition.

## 7. Limits and next

- One view: spot views, more directional views and the array image are
  stage 2, with resolution, bias and filter settings and the debug views.
- The receiver constants are provisional; the stage 2 measurement replaces
  them.
- The cross-family cases, MToon's re-recorded baseline frames with shadows
  on, and bounded cost fixtures with p95/p99 are stage 3's acceptance.
