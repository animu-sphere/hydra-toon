# Shared shadows, stage 2: more views, spot views, settings and debug views

- Date: 2026-10-21
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Tooling: `ost 0.23.14`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`; plain CMake
  without OpenUSD; `usd-mmd-plugins` v0.2.0's `mmdImaging` and
  `usd-vrm-plugins`' `vrmImaging` at the `lookdev` profile; stage 1's `toon`
  `hydra` package (`sha256:3b60ed14…`, [report 85](85-2026-10-21-shared-shadow-stage-1.md))
  as the baseline
- Occasion: stage 2 of the [shadow policy](../../design/SHADOW_POLICY.md#9-stages),
  the first work item of the [v0.6.0 roadmap](../../roadmap/v0.6.0.md)

## TL;DR

**Stage 2 of the shadow policy is in. Up to four shadowing lights get a
view each, directional lights an orthographic one and spot lights up to an
80-degree cone a perspective one, drawn into the layers of one D32 array
image; the fifth light, and a wider spot, are counted. The resolution, the
receivers' depth and normal bias and their filter radius are render
settings (`toon:shadowResolution`, `toon:shadowDepthBias`,
`toon:shadowNormalBias`, `toon:shadowFilterRadius`, viewport flags and
controls), measured here to 2,048 texels, 1.5, 1.0 and 1.5 texels. A
material debug view shows each receiver's visibility and the viewport
previews each view. MMD is unchanged byte for byte: the fixture's 16 MMD
files and the eight frames of 布伦妮 match stage 1's and `main`'s. On
AliciaSolid in motion, four views cost 0.40 ms of GPU time against one
view's 0.25 ms, 64 MiB against 16 MiB, and 0.18 ms of CPU fit against
0.16 ms.**

## 1. What changed

**Views.** `ShadowFit` (`core/render-extraction/shadow_fit.cpp`) takes the
shadowing lights in the light list's order up to `kShadowViewCapacity`,
four. A directional light is fitted as in stage 1. A spot light is seen
from its position down its axis through a perspective projection over its
cone, padded by 16 texels as a directional view is; its near and far planes
span the box around every draw that casts into it or receives it, the near
plane no closer than a thousandth of the far one. A spot cone wider than
`kShadowSpotLimit`, 80 degrees from the axis, is counted with the lights
past the capacity (`shadow_unviewed`): a perspective view cannot hold a
hemisphere, which is the cube maps' work (policy §10). MMD still reads only
the first direct light's view when that light is directional.

Each mesh's box, its roles and the corners of what the camera sees of it
are gathered once a frame and every view reads them; fitting each view
from the meshes again cost 0.16 ms of CPU per view on AliciaSolid (0.66 ms
at four views), now 0.18 ms for four. The fit's numbers are unchanged: the
fixture outputs were identical before and after.

**Resources.** The views draw into one D32 two-dimensional array image, a
layer each, each pass into its layer's own image view. The image grows when
a frame draws more views than it has layers and is rebuilt alone, every
other resource kept, when the resolution changes (`shadow_rebuilds`).
`OffscreenStatistics` and `PresentStatistics` report the views drawn, the
lights without one, the resolution, layers, memory and builds; the viewport
prints them on exit.

**Receivers.** MToon and PreviewSurface receivers read the view parameters
at binding 8 (now 112 bytes: transform, size, layer, kind, depth range,
near plane, texel, and the three settings) and compare in texels of the
view where the point is, a spot view's texel growing with the distance. A
perspective view's depth is turned back into the distance it was drawn at
before the comparison, and the receiver's plane across the view follows
from the point's screen derivatives through the perspective divide. MMD's
receiver reads its view's layer; its comparison, filter and constants are
untouched, and it ignores the bias and filter settings.

**Settings.** `ShadowSettings` in the draw list: `resolution` (a power of
two from 512 to 4,096, `ShadowResolution` rounding down), `depth_bias`,
`normal_bias` and `filter_radius` in texels. The Hydra delegate reads them
from `toon:shadowResolution`, `toon:shadowDepthBias`,
`toon:shadowNormalBias` and `toon:shadowFilterRadius`, warning on an
unsupported value; the viewport from `--shadow-resolution`,
`--shadow-depth-bias`, `--shadow-normal-bias` and
`--shadow-filter-radius`, and from its "Shadow views" panel.

**Debug.** `MaterialDebug::Shadow` (`--material-view shadow`, "Shadow" in
the viewport's material views) shows, linear, the mean visibility over the
lights that have a view for MToon and PreviewSurface, and the self-shadow's
visibility for MMD; 1 elsewhere. The viewport's "Shadow views" panel shows
each view's depth through the overlay (`kOverlayShadowView`), nearer the
light darker; a capture leaves it out, as it leaves out the whole overlay.

## 2. Bias and filter

Stage 1's receiver constants were provisional: 1.5 texels of depth bias,
normal bias and filter radius. They were measured on a scene written for
it and on the avatar.

**The scene.** A 6 m plane, a smooth UV sphere (64 × 32), a coarse
icosphere (80 faces, smooth normals), a 0.6 m box resting on the plane, and
four 0.3 m plates 0.5, 1, 2 and 4 cm above it, all unbound display colours
(PreviewSurface receivers, both faces cast), under one distant light from
the front right at 35 and at 12 degrees of elevation; the viewport at
800 × 800, one sample, through the shadow debug view. An ambient-only
frame gives each object's mask and a shadows-off frame each pixel's N·L.
The plane's points come from the camera (`--camera-output`) and are
classed as shadowed or lit by an analytic ray test against the spheres,
the box and the plates, a margin of the filter radius plus 1.5 texels from
every boundary left out. Measured: **acne**, the share of a surface's
pixels with N·L above 0.2 whose visibility reads below 0.9 (the convex
spheres and the box cannot shadow such points; on the plane, below 0.95
where it should be lit); **lost shadow**, the share of the plane's
analytically shadowed pixels that read lit; and, in close-ups of the
plates, the mean visibility where each plate's shadow must fall
(peter-panning: 0 kept, 1 lost). Resolutions 1,024, 2,048 and 4,096; depth
bias 0 to 3; normal bias 0 to 3; 582 frames with the filter's.

At 2,048 texels (the view's texel is about 3 mm here):

| Depth / normal bias | Acne, coarse 35° | Acne, plane 35° | Acne, coarse 12° | Lost, plane 12° | Plate 0.5 / 1 / 2 / 4 cm, 12° |
| --- | ---: | ---: | ---: | ---: | --- |
| 0 / 0 | 0.87 % | 1.36 % | 2.03 % | 0 | not measured |
| 1.5 / 0 | 0 | 0 | 0.33 % | 0 | 0.96 / 0.02 / 0.01 / 0.01 |
| 1.5 / 0.5 | 0 | 0 | 0.19 % | 0.16 % | 1.00 / 0.10 / 0.04 / 0.01 |
| **1.5 / 1.0** | **0** | **0** | **0.10 %** | **0.41 %** | **1.00 / 0.46 / 0.09 / 0.03** |
| 1.5 / 1.5 (stage 1) | 0 | 0 | 0.03 % | 0.86 % | 1.00 / 0.89 / 0.17 / 0.06 |
| 1.5 / 3 | 0 | 0.03 % | 0 | 2.25 % | 1.00 / 1.00 / 0.50 / 0.22 |

At 1.5 / 1.0 across resolutions: no acne at 35 degrees at any; at 12
degrees, coarse 0 %, 0.10 % and 0.19 % and lost 0.97 %, 0.41 % and 0.13 %
at 1,024, 2,048 and 4,096; the plates' shadows lost by 1.00 / 0.47 / 0.09
for 1, 2 and 4 cm at 1,024 and 0.48 / 0.10 / 0.05 / 0.02 at 4,096. In
texels, a thin caster about three texels above its receiver keeps half its
shadow under 12-degree light and one about six texels above keeps nine
tenths, at every resolution. The smooth sphere showed no acne in any
frame; the plane none at 35 degrees once either bias is 0.5, and at most
0.07 % at 12 degrees, along the analytic boundaries. The depth bias mattered
little once the normal bias was set: the receiver's plane, compared tap by
tap, already allows for slope.

The coarse sphere's remainder at 12 degrees is not speckle: in every
setting it is the same jagged band along the facets at the terminator,
where the smooth normal says lit and the facet already faces away (the
shadow terminator of low-poly meshes); the normal bias blurs it and no
setting removes it.

**The normal bias.** It trades that band and thin casters' shadows. On the
RGBA fixture's octahedral head, the ridge report 85 reduced to 2 levels at
1.5 shows again as the bias falls: against 1.5, 1.0 changes 82 pixels of
65,536 by more than 2 levels (14 by more than 8, at most 21), 0.5 changes
290 (63, at most 38), a visible line. On AliciaSolid under the camera key
(still, face, legs and full body), depth bias 0 with normal bias 0 draws a
fine grid of acne over the face and legs; depth bias 1.5 with normal bias
0, 0.5 or 1.5 draws none, 0 leaving a faint streak where the leg turns
from the light. **1.0 is the default**: no acne measured anywhere, under
half of stage 1's thin-caster loss, and a fixture ridge close to stage 1's.
Avatars, whose views span about 2 m, have texels near 1 mm, so a 1 cm gap
is ten texels and keeps its shadow; the loss above belongs to wide scenes.

**The filter.** Radius 0 to 3 at each resolution: 0 leaves stair-stepped
edges at 1,024; 3 shows the 16 taps' grain along the edge; 1.5, stage 1's,
is smooth at every resolution and stays the default. The plane's partly lit
pixels grow with the radius (607, 1,573 and 2,896 at 0, 1.5 and 3 at
2,048).

## 3. MMD unchanged

The MMD receiver reads a layer of the array image and its debug branch
moved; its comparison and constants did not, and neither did the fit:

- **Fixture.** `toon-rgba-fixtures-gpu`'s 16 MMD self-shadow files are
  identical to stage 1's (the plain-CMake tree, built from stage 1's final
  sources). The 16 avatar files change where MToon receives, by the normal
  bias (§2).
- **Avatar.** This branch's `hydra` package (`sha256:bfaaa3f7…`, built twice
  to the same digest) in a copy of the local MMD Formation with
  `mmdImaging` 0.2.0, drawing 布伦妮 through report 63's reference-shot
  script at report 63's face, turned face, bust and full body,
  1,920 × 1,080, 4x, with `toon:shadows` on and off: 8 of 8 PNGs
  byte-identical to stage 1's and to the frames of `main` before shared
  shadows. The evidence line reads `draws_mmd_shadow=19 shadow_views=1
  draws_shadow=19`.

## 4. Cost

AliciaSolid's `alicia_VRMA_01.usda` sublayered under one to four distant
lights (and a 0.25 dome), in the `viewport-usd` viewport with `vrmImaging`,
800 × 900, 4x, `--time-step 0.25`, 240 frames, vsync off; milliseconds as
mean / p95 / p99 over 239 or 240 frames, after the gather (§1):

| Lights, resolution | GPU shadow pass | GPU work | CPU fit | CPU submit | Image |
| --- | --- | --- | --- | --- | ---: |
| 1, shadows off | 0 | 0.358 / 0.359 / 0.370 | 0.007 / 0.002 / 0.003 | 1.158 / 1.517 / 1.940 | 16 MiB |
| 1, 2,048 | 0.248 / 0.249 / 0.353 | 0.622 / 0.629 / 0.932 | 0.158 / 0.157 / 0.242 | 1.664 / 1.963 / 2.165 | 16 MiB |
| 2, 2,048 | 0.273 / 0.277 / 0.279 | 0.745 / 0.762 / 1.061 | 0.169 / 0.228 / 0.241 | 1.874 / 2.126 / 2.430 | 32 MiB |
| 3, 2,048 | 0.316 / 0.324 / 0.408 | 0.877 / 1.124 / 1.570 | 0.181 / 0.244 / 0.278 | 2.115 / 2.466 / 2.966 | 48 MiB |
| 4, 2,048 | 0.395 / 0.413 / 0.638 | 1.034 / 1.325 / 1.601 | 0.178 / 0.243 / 0.408 | 2.290 / 2.748 / 3.013 | 64 MiB |
| 4, 1,024 | 0.288 / 0.303 / 0.386 | 0.888 / 1.460 / 1.801 | 0.193 / 0.253 / 0.260 | 2.245 / 2.888 / 3.213 | 16 MiB |
| 4, 4,096 | 0.629 / 0.911 / 1.236 | 1.349 / 1.819 / 1.972 | 0.202 / 0.254 / 0.259 | 2.682 / 3.219 / 3.584 | 256 MiB |
| 4, shadows off | 0 | 0.370 / 0.375 / 0.471 | 0.008 / 0.003 / 0.006 | 1.240 / 1.642 / 1.847 | 16 MiB |

The pass grows by about 0.05 ms a view at 2,048 texels: the first view's
cost is mostly the image's clear and the pass's setup. The receivers' work
grows with the lights they read (GPU work 0.62 to 1.03 ms). Four views is
the capacity: on this machine it costs about 0.4 ms of GPU and 64 MiB at the
default resolution, and no measured scene needed more; an atlas or more
layers follow need, as the policy says. These are one machine's numbers on
one avatar, not the bounded fixtures with p95/p99 of the roadmap's
done-when condition, which are stage 3's.

## 5. Tests

| Check | What it holds |
| --- | --- |
| `toon-shadow-fit` | adds: `ShadowResolution`'s rounding; the resolution setting sizing the texel; a spot light first getting a perspective view down its axis over its cone, its planes and texel at unit distance, and leaving MMD unshadowed; a cone past the limit counted; five directional lights giving four views, each down its own light, and one counted |
| `renderer.shadow.views` (new) | two half-strength directional lights each shadowing their own side (153 in one shadow, 255 lit); the shadow debug view's mean (128); the resolution setting rebuilding the image once and a steady frame rebuilding and uploading nothing; the depth bias setting lifting a square's shadow; a spot light's perspective shadow at a fifth of its mirror point; a spot past the limit counted; five lights giving four views and one counted; MMD's debug view; no validation messages |
| `renderer.material.mmd_self_shadow`, `renderer.shadow.shared_casters`, `renderer.shadow.receivers` | unchanged, pass |
| `toon-renderer-hydra-morph-normals` | now compares with shared shadows off: it compares GPU-morphed points with points placed directly, byte for byte, and at a normal bias of 0.5 an ulp between them flipped a shadow tap where the bent surface meets its own shadow, changing 2 pixels of 16,384 by one level; `renderer.shadow.receivers` holds morphed casters to their place |

`ost build`, `ost test` and `ost validate` pass for `core` (23 of 23),
`lookdev` `hydra` (37 of 37) and `viewport-usd` (71 of 71); `ost package
--profile lookdev --intent hydra` twice gives `sha256:bfaaa3f7…` both times;
`renderer.shadow.views` is in `openstrata.renderer.yaml`; Vulkan validation
stays clean.

Two mistakes found on the way: Windows' `near` and `far` macros broke a
`ShadowView::near` field in the Hydra build (now `near_plane`); and the
measurement's first analytic shadow test let the box's slab test run
unbounded along an axis the light does not move along, which the images
showed at once.

## 6. Limits and next

- The fit and receivers handle what the scene asks of them up to four
  views; point lights, cascades, shadow colour, falloff and linking stay
  outside the policy (§10).
- The shadow terminator on coarse meshes (§2) is a property of smooth
  normals over flat facets, which the normal bias only blurs.
- The cross-family cases, MToon's re-recorded baseline frames with shadows
  on, and bounded cost fixtures with p95/p99 are stage 3's acceptance.

Captures, the measurement scenes and scripts stay in the ignored build
directory: the avatars are not redistributable.
