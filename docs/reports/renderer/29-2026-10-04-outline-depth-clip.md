# Outline near/far-plane omission under animation

- Date: 2026-10-04
- Environment: Windows, MSVC 14.51, NVIDIA RTX A5000; OpenStrata 0.23.14,
  canonical CY2026 OpenUSD 26.08 `lookdev` runtime
- VRM imaging: published `vrmImaging` 0.10.0 at
  `sha256:894fd616f1414d5b393ff0d50abbf7ef18cb603562133c72e642493b48f71667`,
  with the existing local file-format/package-resolver bundles and their
  `vrmContainer` dependency
- Occasion: v0.2.0 depth visibility work following
  [report 28](28-2026-10-04-outline-frustum.md)

## Method

`OutlineBounds` tests the near and far planes as well as the four side
planes, with the existing full-width rest/joint envelope and arithmetic
margins. Pose evaluation still transforms boxes rather than skinning the
vertex array; no additional resource upload, GPU pass, query, readback or
wait is introduced. The decision uses the current frame's pose.

`ToonView` uses -w..w depth. `VulkanClipFromWorld` maps that to 0..w without
changing the volume. The hull pipeline keeps depth clamp disabled. Vulkan
performs [primitive clipping](https://docs.vulkan.org/spec/latest/chapters/vertexpostproc.html#vertexpostproc-clipping)
before rasterization, and [depth bias](https://docs.vulkan.org/spec/latest/chapters/primsrast.html#primsrast-depthbias)
offsets rasterized fragments. Thus the existing slope bias cannot restore
a primitive wholly outside a depth plane; its visible depth behavior stays
unchanged. `--outline-culling off` retains ordinary submission as a reference.

## Controlled CPU and GPU checks

CTest `toon-outline-bounds` covers all six planes, world/screen widths,
stage units, finite perspective, reversed clip depth and infinite far
projections. It retains hulls whose extrusion reaches across a plane even
when the rest points are outside. The independent 1,000-pose two-joint
oracle now samples a full extrusion sphere and checks visibility against
all six planes, with perspective/orthographic, scaled, mirrored and sheared
transforms and positive unnormalized weights. Existing unusable-transform,
skin fallback and envelope-refresh checks also pass.

`renderer.outline.depth_clip` compares 384 rigidly skinned octahedron poses
at 1x/4x MSAA: Opaque, Mask, Blend without depth writes and Blend with depth
writes; world/screen width; perspective/orthographic; near/far plane; a
crossing and return sequence. Every culled colour byte and depth float
matches ordinary submission. The check omits 64 fully clipped hulls and
retains crossing/returning hulls, including visible outline pixels.

Each sample-count run uploads points, topology and skin once, writes eight
material slots and writes 184 poses for its 192 evaluated poses. Eight
identical consecutive poses across camera changes cause no redundant pose
write. Vulkan validation reports no messages. The existing 240 side-plane
pose comparisons continue to pass.

## Hydra-fed viewport

The committed `adapters/viewport/tests/outline-depth-motion.usda` binds an
MToon material to a one-joint octahedron. Its USD animation moves across
both of the orbit camera's depth planes, then returns. The evaluation
driver uses the actual presentation path with registered `vrmImaging`:

```sh
python scripts/evaluate_outline_depth.py --viewport <viewport-usd build>/adapters/viewport/toon-viewport
```

At each of 1x and 4x, six culling-on/off capture pairs are byte-identical:

| View | Time code | Hulls with culling | Reference hulls |
| --- | ---: | ---: | ---: |
| Visible | 1 | 1 | 1 |
| Near boundary | 2 | 1 | 1 |
| Fully before near | 3 | 0 | 1 |
| Far boundary | 6 | 1 | 1 |
| Fully beyond far | 7 | 0 | 1 |
| Returned after motion | 9 | 1 | 1 |

Both boundary views differ from an outlines-off capture, establishing
visible outline coverage. The 33-frame motion run advances from time 1
through 9 in steps of 0.25, writes 33 poses with one geometry/skin upload,
and ends with exactly the initial image. It compares the final captured
image, not every intermediate presented frame. All captures exclude the
overlay and use the viewport's one-requested-frame readback checks.

The representative AliciaSolid VRMA 01 stage also retains identical
culling-on/off captures in all 12 pairs: full, close-up, panned close-up and
offscreen views at time codes 0, 29.75 and 299.75, at 1280x720 and 4x.
Submitted hull counts are respectively 15/15, 12/15, 12/15 and 0/15. All
runs retain 20 MToon/GPU-skinned draws. These views establish avatar
regression evidence; the controlled fixture establishes depth-plane
omission. No performance improvement is claimed from these capture runs.

Logs, commands, hashes and images stay local under `build/outline-depth/`;
the avatar and its captures are not committed.

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

Plain CMake and OpenStrata core each pass 5/5 CTests; the Hydra viewport
passes 31/31, including the `testusdview` host smoke test, presentation and
install-tree checks. Both OpenStrata validations pass. The viewport driver
is a separate plugin-enabled evaluation, not a CTest that silently depends
on a locally installed VRM package.

Initial build attempts needed access to the existing user runtime store.
For plain CMake, the Developer PowerShell launcher failed on a host
character-encoding issue; loading `vcvars64.bat` directly allowed the build
and all tests to pass.

## Remaining scope

Fully occluded hulls can still be submitted. The envelope can be loose, so
omission is conservative rather than maximal. Subpixel coverage variation
and representative-avatar fidelity/temporal comparisons against UniVRM or
three-vrm remain separate v0.2.0 work. This report does not close the
milestone or establish its release packaging gate.
