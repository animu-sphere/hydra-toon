# Viewport skeleton and joint diagnostics

- Date: 2026-10-05
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Runtime: canonical CY2026 OpenUSD 26.08 lookdev; OpenStrata 0.23.14
- Scope: the v0.3.0 viewport skeleton/joint diagnostic requirement

## Evaluated hierarchy and display

The Skeleton panel reads composed USD skeleton queries only while the panel
or its optional screen display is active. It lists skeleton paths, joint
names, parent names and evaluated world origins in stage units. UsdSkel
maps animation order and fills missing animated joints from their rest
transforms. Origins come from joint world transforms, not the mesh's
bind-relative skinning palette. Scene-index notices invalidate cached
queries; each scene instance owns its diagnostic cache.

Bones and joint markers draw through surfaces in the viewport overlay.
Joint labels are optional; selecting a table row highlights that joint and
its name. The ordinary orbit camera projects world positions with the
USD/OpenGL clip convention, converting Y up to top-left screen coordinates.
Segments are clipped against all six homogeneous planes before perspective
division. Non-finite or clipped joint positions produce no marker. The
overlay keeps the existing screenshot contract: captures omit it.

These values describe the selected USD time. External evaluated late pose
overrides are not represented by this USD diagnostic query, and instance
proxies are not traversed. Diagnostic evaluation is CPU work while enabled;
this run does not claim its cost on every avatar or a motion performance
improvement.

## State, geometry and GPU evidence

`toon-viewport-skeleton-debug-state` uses a committed three-joint fixture
with nonidentity bind transforms, a translated SkelRoot, a rotated skeleton,
reordered animated joints and one joint absent from the animation. Joint
names/parents and world origins agree with hand-computed values at two times
and on returning to the first time. Repeated reads and fast commits retain
the render-world revision and resident point/index/influence/palette arrays,
and do not increase Hydra's sync count.

`toon-viewport-skeleton-debug-gpu` renders before and after those diagnostic
reads: all three 96x96/4x colour/depth pairs agree exactly. Non-background
depth and changing animation images are required, so empty renders cannot
satisfy the comparison. The diagnostic reads cause no point, topology,
skin, pose, material, texture, morph or weight writes, pipeline creation or
target allocation. Vulkan validation reports zero messages. In-memory
shared-layer transform edits refresh world origins, and skeleton removal
removes its diagnostic entry; the fixture file is never saved.

`toon-skeleton-projection` covers center/top-left projection, invalid
extents/non-finite values, offscreen and behind-camera rejection, clipped
side-plane segments, a bone crossing the near plane from behind the camera,
and the viewport's Y-up/Z-up cameras before and after orbit/pan. It has no
OpenUSD or Vulkan dependency.

## Interactive viewport verification

The actual panel was operated with the committed morph fixture: expand the
panel and skeleton, enable markers, and select the root row. The root marker
appeared over the surface and selection produced a highlighted marker/name.

The local Alicia VRMA motion stage was then opened at 1280x900 and 4x MSAA
with the same published vrmImaging 0.10.0 and local owner raw-VRM ingestion
libraries used in [report 39](39-2026-10-05-late-frame-input.md). The panel
listed 128 joints under the composed skeleton; its scrollable table exposed
the hierarchy and world coordinates. Enabling diagnostics while paused
retained one Hydra sync. Playback changed the avatar pose, table coordinates
and overlaid bones together; the paused motion pose remained aligned.
The final scene summary retained 20 GPU-skinned/authored-normal MToon draws,
12 MToon materials and seven textures. This is visual diagnostic evidence,
not a new source-produced expression/gaze or display-latency claim.

Machine-local logs are `build/skeleton-{preview,avatar}.{log,err}`. The model,
motion and screen images remain untracked.

## Build and validation

```sh
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
cmake --build build/morph-no-vulkan --config Release
ctest --test-dir build/morph-no-vulkan -C Release --output-on-failure
```

The viewport-usd build, all 51 CTests (including usdview and the install
tree), and completion-bound OpenStrata validation pass. Artifact integrity
is an explained skip because this change creates no release package.
The plain-CMake build without OpenUSD/Vulkan and all 11 CTests pass.
`git diff --check` and documentation relative-file-link checks pass.

Source-produced expression/gaze integration and fast/slow evidence,
expression mapping/material-value diagnostics, producer clock integration
and actual display timing remain in the v0.3.0 roadmap.
