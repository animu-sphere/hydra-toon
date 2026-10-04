# Outline temporal sampling: translation, rotation and an animated VRM

Follow-up: [report 32](32-2026-10-04-vrm-reproduction.md) records broader
continuous capture, reference reproduction and the accepted v0.2.0 baseline.

- Date: 2026-10-04
- Environment: Windows, NVIDIA RTX A5000; existing MSVC 14.51 build,
  OpenStrata 0.23.14, canonical CY2026 OpenUSD 26.08 `lookdev` runtime
- VRM imaging: published `vrmImaging` 0.10.0 at
  `sha256:894fd616f1414d5b393ff0d50abbf7ef18cb603562133c72e642493b48f71667`,
  with the existing local file-format/package-resolver bundles and their
  `vrmContainer` dependency
- Occasion: v0.2.0 silhouette measurements following
  [report 24](24-2026-09-30-outline-stability-cost.md) and
  [report 27](27-2026-10-01-antialiasing-quality.md)

## Decision

Keep the 4x MSAA default. It reduces the sampled rotating hull's temporal
residual by 68–69% against 1x for the nominal 0.75 px row, and the avatar
close-up residual by 68%. It does not eliminate thin-outline sampling
variation. 8x reduces these residuals further but also retains error.
This evaluation changes neither the renderer nor its release baseline.

## Reproducible method

`scripts/evaluate_outline_temporal.py` uses the actual Hydra-fed viewport,
with Pillow, NumPy and externally registered VRM plugins:

```sh
python scripts/evaluate_outline_temporal.py \
    --viewport <viewport-usd build>/adapters/viewport/toon-viewport \
    --avatar <local Alicia motion stage> --output build/outline-temporal
```

Omitting `--avatar` runs only original generated geometry. A fixed black
backdrop dominates the camera bounds. Three shallow closed octahedra have
black MToon surfaces, white unlit hulls and nominal screen-width factors
of 0.5/256, 0.75/256 and 1.5/256. These labels describe the normal extrusion
in screen-height units; actual visible width also depends on the normals,
surface depth and hull depth bias. They are not constant-width line art.
Each mesh binds one joint. The geometry and authored normals stay fixed.

Nine poses translate by approximately one output pixel at the initial
distance, or rotate from 0 to 12 degrees around the viewing axis. Each is
evaluated at the initial distance and after four dolly steps, which scale
camera distance by 0.9^4. The same world translation spans more pixels
at the closer distance; the screen-width factor stays unchanged.
At 256x256, 1x/4x/8x MSAA are compared with each matching pose rendered at
1024x1024/4x. Captures are decoded from sRGB, box-averaged in linear light,
then converted to luminance. Disjoint fixed row crops contain each full
hull; their columns stay inside the black backdrop, excluding clear colour.

For actual luminance I and comparison luminance R, the spatial statistic
is mean |I−R|. The temporal statistic is the mean over consecutive pairs
and pixels of |(I[t]−I[t−1])−(R[t]−R[t−1])|. Thus an accurately reproduced
moving feature has zero residual even while its pixels change. No image
warping is used. Integrated absolute signal, its difference from comparison
signal and missing-signal fractions are separate statistics in the JSON.
Missing signal means comparison magnitude at least 0.05 but actual magnitude
below 1e-6; it counts pixels, not disappearing whole outline segments.

The comparison is finite supersampling, not analytical truth or UniVRM /
three-vrm fidelity. It retains raster error and the same hull geometry and
depth behavior. Row and whole-frame metrics include black gaps, so absolute
values are comparable within a view, not between different crop sizes.

## Controlled results

Mean temporal residual for the nominal 0.75 px hull, in 10^-3 linear luminance:

| Motion and distance | 1x | 4x | 8x |
| --- | ---: | ---: | ---: |
| Translate, initial | 2.5566 | 2.3908 | 2.1650 |
| Translate, closer | 2.3915 | 2.1512 | 1.9022 |
| Rotate, initial | 23.5639 | 7.5283 | 5.0518 |
| Rotate, closer | 18.6510 | 5.7061 | 3.7524 |

The initial-distance rotating rows, across all three nominal widths:

| Width | 1x | 4x | 8x |
| --- | ---: | ---: | ---: |
| 0.5 px | 20.4727 | 8.1412 | 5.3261 |
| 0.75 px | 23.5639 | 7.5283 | 5.0518 |
| 1.5 px | 29.2652 | 9.2860 | 6.0155 |

Wider hulls affect more pixels, so the larger absolute residual does not
establish worse perceived quality. In the 0.75 px initial translating row,
missing-signal fractions are 68.29%, 15.39% and 5.73% at 1x/4x/8x.
The corresponding integrated signal ranges are 10.000, 7.274 and 0.773
equivalent full-intensity pixels. Rotation's ranges are 50.000, 7.070 and
2.636, while its supersampled comparison also varies by about 1.33 pixels.
Spatial smoothing, pixelwise temporal error and constant integrated signal
are distinct properties. In particular, MSAA's reduction in the translating
row's temporal residual is much smaller than in rotation.

All twelve 17-frame forward/return runs and twelve independent repeat runs
end byte-identically to their initial capture. Each static run uploads four
point/topology buffers, five material slots and three skins, and writes three
poses. A round trip writes 51 poses and uploads no additional geometry,
skins or material slots. Each controlled frame draws four MToon meshes,
three through GPU skinning, and submits three hulls. These checks establish
state determinism; they do not remove the measured sampling variation.

## Representative VRM

The local AliciaSolid retargeted VRMA 01 stage is sampled at time codes
29, 29.25, 29.5, 29.75 and 30. Full-body and close-up views use 1280x720
at 1x/4x/8x, compared with 2560x1440/4x. The close-up uses pan (0,155)
and dolly 12; the comparison doubles the pixel pan to preserve the view.

Each pose and sample count captures outlines on and off. The **signed**
linear-luminance difference isolates the outline's contribution, including
dark outlines and surface pixels it covers. The temporal statistic uses
that signed field; integrated signal uses its magnitude. It is not a
material-independent segmentation of the silhouette. Texture mip selection,
alpha sampling and surface/hull coverage can still affect the comparison.

Mean temporal residual, again in 10^-3 linear luminance:

| View | 1x | 4x | 8x |
| --- | ---: | ---: | ---: |
| Full body | 0.5691 | 0.3566 | 0.2457 |
| Close-up | 2.2068 | 0.7146 | 0.5012 |

The full-body reduction at 4x is 37%, and the close-up reduction is 68%.
8x reduces their 4x residuals by another 31% and 30%. Those percentages
describe these five poses, not an avatar-wide temporal guarantee. All runs
retain 20 MToon/GPU-skinned draws with authored normals, five transparent
surface draws, 12 MToon materials and seven textures. Each independent
process uploads 20 point/topology buffers, 13 material slots, seven textures
and 20 skins, and writes 20 poses. These static captures do not establish
the avatar's upload behavior during continuous playback.

## Verification and limits

There are 248 successful viewport capture runs: 168 controlled and 80 VRM.
Each requests exactly one readback and retains the requested sample count;
no Vulkan validation messages are reported. All image hashes, commands,
logs, generated stages, contact sheets and full statistics remain local
under `build/outline-temporal/`. No avatar, model or capture is committed.

Plain CMake CTest passes 6/6. The existing Hydra viewport build passes 32/32,
including `testusdview`, presentation, image comparisons and install-tree
checks. The first direct viewport CTest invocation lacked its OpenUSD DLL
search paths; rerunning with the existing runtime environment passes.
No C++ or shader source changes, rebuild or OpenStrata revalidation are
claimed. Analytical moving-feature, dropped-pixel, signed-dark-outline and
linear-light averaging checks pass for the measurement helpers.

The nine controlled quality captures and five avatar captures are independent
processes. Only the controlled forward/return runs exercise intermediate
presented frames in one process, and capture their final frame. This is not
all-frame capture instrumentation, temporal filtering, subjective flicker
validation or a latency benchmark. Broader avatar motions and assets,
world-width outlines, near-edge view changes and fidelity against UniVRM or
three-vrm remain outside this report. The v0.2.0 temporal-quality and VRM
reproduction items remain open.
