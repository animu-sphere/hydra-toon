# Outline side-frustum omission under animation

- Date: 2026-10-04; controlled GPU and Hydra regression evidence from the
  initial 2026-10-01 implementation run, followed by core and avatar checks
  on 2026-10-04
- Environment: Windows, MSVC 14.51, NVIDIA RTX A5000; OpenStrata 0.23.14,
  canonical CY2026 OpenUSD 26.08 `lookdev` runtime
- VRM imaging: published `vrmImaging` 0.10.0 at
  `sha256:894fd616f1414d5b393ff0d50abbf7ef18cb603562133c72e642493b48f71667`,
  with existing local file-format/package-resolver bundles and their
  `vrmContainer` dependency
- Occasion: v0.2.0 outline visibility work after
  [report 24](24-2026-09-30-outline-stability-cost.md)

## Scope and method

Omit an MToon hull only when its entire expanded bound is outside one of
the left, right, top or bottom clip planes. The surface's submission route
is unchanged. Opaque/Mask and Blend recording use the same decision.

`OutlineBounds` builds boxes over indexed rest points and the rest points
each joint influences, on points, topology or skin revisions. A current
pose transforms each populated joint box by the same joint/bind matrix
the shader reads, takes their union, then applies the weight-sum range and
the skeleton-to-mesh transform. Nonnegative blends are scaled convex
combinations; the bounds do not require weights to sum exactly to one.
This evaluates boxes rather than CPU-skinning the vertex array. An
incomplete skin follows the shader's unskinned fallback.

The expansion covers a full-width ball in view space. World width is
divided by `metersPerUnit`; screen width uses the maximum absolute clip
`w` in the bound and `abs(projection[1][1])`. A sampled RGBA8 width image
cannot exceed its full G-channel factor, so animated UVs and partially
zero images remain conservative. Missing images/UVs keep the existing
white fallback. Float-arithmetic margins enlarge the boxes and extrusion;
singular, ill-conditioned or non-finite transforms and negative/non-finite
skin weights retain the hull.

No depth-plane test is made because the hull's slope depth bias has its
own clipping implications. No occlusion test or temporal history is used.
The decision is made from this frame's pose, so moving back into view does
not depend on the previous frame's result. `DrawList::outline_frustum_culling`
and viewport `--outline-culling off` supply the ordinary-submission reference.

## Controlled checks

CTest `toon-outline-bounds` checks all four side planes, screen/world widths,
stage units, singular/non-finite transforms, pose arrival and incomplete
pose fallback, skeleton-space transforms, negative weights and points/
topology cache refresh. An independent point-by-point oracle samples
extrusion circles over 1,000 deterministic randomized two-joint poses,
with positive unnormalized weights, mirrored/scaled/sheared transforms
and perspective/orthographic projections. Every sample inside the side
planes retains its hull, and both visible and omitted poses are exercised.

Headless evidence `renderer.outline.frustum` draws 240 rigidly skinned
octahedron poses: Opaque, Mask and Blend, world and screen widths,
perspective and orthographic cameras, all four sides and five positions
per side. Each compares colour bytes and depth floats with culling disabled.
All products are identical; 48 distant hulls are omitted. Boundary poses
retain their hulls, including visible outline pixels when the surface is
outside the image. The run uploads points, topology and skin once and writes
six material slots for the six alpha/width configurations. Vulkan validation
is clean.

These controlled checks run through the shared scene recording code used
by the offscreen renderer and presentation session.

## Representative VRM

The local AliciaSolid retargeted VRMA 01 stage is captured at 1280x720,
4x MSAA, with overlay and vsync off. Each view is captured at time codes
0, 29.75 and 299.75, with side-frustum culling on and off. The binary PPMs
are byte-identical in all 12 pairs:

| Initial view | Pan in pixels | Dolly notches | Hulls with culling | Reference hulls |
| --- | --- | ---: | ---: | ---: |
| Full body | 0, 0 | 0 | 15 | 15 |
| Close-up | 0, 155 | 12 | 12 | 15 |
| Panned close-up | 100, 155 | 12 | 12 | 15 |
| Entirely outside | 3000, 155 | 12 | 0 | 15 |

All runs still list 20 MToon meshes, 20 GPU-skinned meshes with authored
normals, five transparent surfaces, 12 MToon materials and seven textures.
Each static run uploads 20 point/topology/skin buffers, 13 material slots
and seven textures, with 20 joint-buffer writes. The scene summary's
15 outline candidates therefore differ from the final submitted hull count.

Two additional close-up runs present 1,200 frames, time code 0 through
299.75 by steps of 0.25. Their final images are byte-identical, with 12
versus 15 final hull calls, the same initial uploads and 24,000 pose writes.
Each captures only its last frame. This establishes motion/update and
final-image evidence; it is not a pixelwise temporal comparison of all
1,200 frames. The runs are not a repeated isolated performance benchmark,
so no frame-time improvement is claimed.

Captures, hashes, per-run commands and logs remain local under
`build/outline-frustum/`; no model or avatar capture is committed.

## Commands and validation

```sh
ost build --jobs auto
ost test
ost validate
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
```

Core CTests pass 5/5 and the Hydra viewport CTests pass 31/31, including
the new bounds test, existing file-open/camera/sample comparisons and the
`testusdview` host smoke test. Both evidence validations pass. The GPU
frustum check is an additional evidence item within the headless test.

With the runtime, VRM plugins and their DLL dependencies registered, the
direct viewport comparisons use this command, repeating with `on`/`off`,
the times and camera offsets above:

```sh
<viewport-usd build>/adapters/viewport/toon-viewport \
    --usd <local Alicia motion stage> --hidden --frames 2 \
    --vsync off --overlay off --samples 4 --time 29.75 --expect-draws 20 \
    --camera-pan 0 155 --camera-dolly 12 --outline-culling on \
    --screenshot <local capture.ppm>
```

The motion comparison uses `--frames 1200 --time 0 --time-step 0.25` with
the same close-up and culling switch. Initial local plugin loading exposed
a missing `vrmContainer` DLL directory; adding the existing dependency's
directory to the process environment allowed all avatar comparisons to run.

## Remaining scope

Fully occluded and depth-clipped hulls can still be submitted. Joint boxes
can be loose, particularly on highly deformed meshes, so this method does
not promise maximal draw reduction. It does not address subpixel coverage
variation, real-avatar temporal error or MToon fidelity against UniVRM or
three-vrm; those remain separate v0.2.0 evaluation work.
