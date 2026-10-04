# Current-frame outline occlusion under animation

- Date: 2026-10-04
- Environment: Windows, MSVC 14.51, NVIDIA RTX A5000; OpenStrata 0.23.14,
  canonical CY2026 OpenUSD 26.08 `lookdev` runtime
- VRM imaging: published `vrmImaging` 0.10.0 at
  `sha256:894fd616f1414d5b393ff0d50abbf7ef18cb603562133c72e642493b48f71667`,
  with the existing local file-format/package-resolver bundles and their
  `vrmContainer` dependency
- Occasion: v0.2.0 outline visibility work following
  [report 29](29-2026-10-04-outline-depth-clip.md)

## Method

`OutlineOcclusion` projects small rigid opaque blockers from the current
draw list. Each candidate uses the existing rest/joint envelope, expanded
by the full world/screen width with the stage unit. All eight expanded box
corners must have positive clip `w` and lie inside the depth volume.
Their projected rectangle must be strictly inside one blocker triangle;
its nearest depth must be farther than that triangle's farthest depth.

Arithmetic margins cover float composition, vertex arithmetic and depth
remapping. Absolute matrix products preserve the size of opposing terms
when translations or matrix entries cancel. The envelope's joint/bind and
model/view composition margins are enlarged for the same reason. Raster
margins enlarge the candidate rectangle and inset the blocker edges by at
least one pixel using the actual render target extent. Positive hull depth bias
is not needed to establish the depth separation. Ties, clipped depth
geometry, unusable transforms and uncertain coverage retain the hull.

Only unlit fallback meshes and double-sided Opaque MToon meshes establish
coverage. Mask and Blend cannot, including Blend with depth writes. Any
skin binding excludes a blocker, even if the current pose would fall back
to rest geometry. A hull cannot use its own surface as a blocker. Small
rigid blockers may move and change material; they are reevaluated this
frame. There is no temporal visibility history, CPU vertex skinning, GPU
pass/query/readback, wait or additional upload.

Meshes with more than 64 triangles are excluded. Per frame, at most 256
triangles are examined and 128 retained in fixed storage. This bounds the
extra geometry work rather than walking avatar vertex arrays. The existing
`--outline-culling off` disables both frustum and occlusion decisions.
Surfaces retain their draw route, as do hulls without a coverage proof.

## Controlled CPU and GPU checks

CTest `toon-outline-occlusion` exercises full/partial coverage, extrusion,
world/screen widths, stage units, depth ties, front-side targets, material
changes, hidden/moving blockers, point/topology edits, reversed winding,
geometry work limits, skinned blocker exclusion, stale cameras, raster
extent, cancelling/singular/non-finite transforms. Its independent sampled-hull
oracle tests 2,000 deterministic translations and widths with orthographic
and perspective cameras and mirrored blockers. It omits 1,366 cases and
retains 634; every omitted hull sample is behind and inside the actual
triangle. The existing six-plane randomized skin-envelope oracle passes.

`renderer.outline.occlusion` compares 1,792 animated poses at 1x/4x MSAA:
world/screen widths, perspective/orthographic cameras, Opaque/Mask/Blend
targets with both transparent depth-write routes, rigid unlit and
double-sided Opaque blockers, Mask/Blend/single-sided/skinned fallbacks,
mirrored blockers, sideways reveals, a blocker moving away, a blocker
moving behind the target and return poses.

Every culling-on colour byte and depth float equals ordinary submission.
Each blocker configuration is crossed with all four target alpha routes.
The check omits 288 fully covered hulls and establishes visible outlines
when the blocker moves away. Each sample-count run uploads the two point
and topology buffers once. Repeated culling comparisons cause no geometry,
skin, pose, material or texture writes. Vulkan validation emits no messages.

## Hydra-fed viewport

The committed `adapters/viewport/tests/outline-occlusion-motion.usda`
animates an outlined one-joint diamond and an independent rigid opaque
blocker. The driver uses the presentation path with registered VRM imaging:

```sh
python scripts/evaluate_outline_occlusion.py --viewport <viewport-usd build>/adapters/viewport/toon-viewport
```

At each of 1x and 4x, all ten capture pairs are byte-identical:

| View | Time code | Submitted hulls | Reference hulls |
| --- | ---: | ---: | ---: |
| Covered | 1 | 0 | 1 |
| Inside blocker | 2 | 0 | 1 |
| Edge reveal | 3 | 1 | 1 |
| Blocker moved away | 4 | 1 | 1 |
| Covered again | 5 | 0 | 1 |
| Blocker behind target | 6 | 1 | 1 |
| Edge return | 7 | 1 | 1 |
| Inside return | 8 | 0 | 1 |
| Returned | 9 | 0 | 1 |
| Motion return | 1 through 9 | 0 | 1 |

All four revealed views differ from outlines-off captures. Each static
run uploads two point/topology buffers, three material slots and one skin,
with one pose write. The 33-frame motion run evaluates time 1 through 9
in steps of 0.25 and writes 29 poses; the target is stationary across four
consecutive steps while the blocker moves. It uploads no geometry or
materials during motion and ends with exactly the initial image. It
captures the last presented frame, rather than every intermediate frame.

The representative AliciaSolid VRMA 01 stage retains identical
culling-on/off captures in all 12 pairs: full body, close-up, panned
close-up and offscreen at time codes 0, 29.75 and 299.75, at 1280x720/4x.
Hull counts remain 15/15, 12/15, 12/15 and 0/15 respectively. All runs retain
20 MToon/GPU-skinned meshes, five transparent draws, 12 materials and seven
textures. Their blocker exclusion means these avatar captures establish
regression evidence, not avatar self-occlusion or a new draw reduction.

Commands, hashes, images and logs remain local under
`build/outline-occlusion/`. No model or avatar capture is committed.

## Build and validation

```sh
cmake --build build/plain-cmake
ctest --test-dir build/plain-cmake --output-on-failure
ost build --jobs auto
ost test
ost validate
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
```

Plain CMake and OpenStrata core each pass 6/6 CTests. The Hydra viewport
passes 32/32 including `testusdview`, presentation and install-tree checks;
both OpenStrata validations pass. Builds use the existing MSVC/runtime
stores outside the workspace. The plugin-enabled driver remains a separate
evaluation rather than a CTest requiring a locally installed VRM package.

## Limits and remaining quality scope

This is a bounded conservative omission path, not general occlusion
rendering. Skinned occluders, single-sided MToon blockers, coverage formed
by multiple triangles, large rigid meshes and tightly deformed envelopes
can still submit hidden hulls. No performance improvement is claimed from
these captures. The present avatar adds no occlusion candidates.

Subpixel/rotating silhouette stability, representative-avatar temporal
measurements and fidelity against UniVRM or three-vrm remain v0.2.0 work.
This report does not close the milestone or its release packaging gate.
