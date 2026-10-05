# Building and testing

Every command on this page has been run in this repository; the latest run is
[renderer report 01](../reports/renderer/01-2026-09-26-phase0-mesh-camera.md).
What each build contains is
[PROJECT_LAYOUT.md §5](../architecture/PROJECT_LAYOUT.md#5-build-intents-and-runtime-profiles),
and what it was measured on is
[SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md).

## Prerequisites

- A Vulkan SDK that provides Vulkan 1.3 and `slangc` (bundled from 1.3.296).
  Without it the build still succeeds, and the GPU checks report `SKIP`.
- CMake 3.24 or newer, Ninja, and a C++20 compiler. On Windows, `ost` loads
  the MSVC environment itself; for plain CMake, use a developer shell with the
  compiler on `PATH`.

## Plain CMake — no OpenUSD

From the repository root, build and run the default tests:

```sh
cmake -S . -B build/plain-cmake -G Ninja
cmake --build build/plain-cmake
ctest --test-dir build/plain-cmake --output-on-failure
```

The options are `TOON_ENABLE_VULKAN` (default `ON`), `TOON_ENABLE_HYDRA2`,
`TOON_ENABLE_VIEWPORT` and `TOON_BUILD_TESTS` (default `ON`). Plain CMake does
not require `ost`; optional adapters need their own dependencies. A plain-CMake
tree can be validated with `ost validate --build-dir build/plain-cmake`, which
does not claim `ost` built it.

## OpenStrata — no OpenUSD

With `ost` 0.23.8 or newer (`ost --version`), run:

```sh
ost build --check
ost build --jobs auto
ost test
ost validate
```

`ost build` runs `toon-headless`, which renders the bootstrap triangle scene
1,000 times on one persistent renderer and writes `build/<target>/renderer-report.json`. `ost validate` reads
it; the Hydra assertions are `SKIP` in this build by design.
`renderer.install_tree` is `SKIP` after `ost build` and passes once `ost test`
has run `toon-renderer-install-tree`, which installs the project, runs the
installed `toon-headless --install-tree` and merges its verdict into the
report.

## The Hydra adapter

The adapter needs a real OpenUSD imaging runtime. This repository is measured
with OpenStrata's canonical OpenUSD 26.08 `lookdev` runtime, which includes
`usdview`, pulled by digest:

```sh
ost artifact pull oci://ghcr.io/animu-sphere/openstrata-runtime-cy2026-lookdev@sha256:b840ed4690aa39d4582bc03c0a09fa7bab717216635b55a4eb718482dbfb196b \
    --expect-artifact sha256:b982656c07dd9147973e3c7197d9b050ee48dd1ac291988f7e6025c76c4a785b
ost runtime pull cy2026 --profile lookdev \
    --from-artifact sha256:b982656c07dd9147973e3c7197d9b050ee48dd1ac291988f7e6025c76c4a785b
ost runtime validate cy2026 --profile lookdev
```

That is the Windows x86_64 leaf; the Linux leaf's digests are in OpenStrata's
[v0.23.11 verification report](https://github.com/animu-sphere/open-strata/blob/main/docs/reports/2026-09-26-v0.23.11-renderer-formations.md#canonical-runtime-evidence),
and its
[adoption guide](https://github.com/animu-sphere/open-strata/blob/main/docs/guides/adopt-a-renderer-project.md#2-adopt-a-digest-pinned-runtime)
covers adopting other runtimes. `ost runtime list` shows what is already in
the local store.

```sh
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra
ost validate --profile lookdev --intent hydra
```

`ost test` includes `toon-renderer-usdview-host`, which opens `testusdview`
on the installed smoke scene for a few seconds and keeps
`usdview-first-frame.png` and `usdview-stable-update.png` under
`build/<target>--hydra/adapters/hydra2/usdview-install/`.

`ost` pins the `core` runtime in `strata.lock` and the `lookdev` one in
`strata.openstrata-cy2026-<os>-<arch>-py313-lookdev.lock`; a `lookdev` build
leaves `strata.lock` alone. Both files are local and ignored by Git.

`ost package --profile lookdev --intent hydra` packages the adapter's build
as a `renderer` component, which an OpenStrata Formation composes into a
`usdview` session
([renderer report 06](../reports/renderer/06-2026-09-27-vrm-formation.md)).

`ost renderer view` opens `usdview` interactively on the same install. It has
not been run in this repository yet.

## The VRM host session

[`formations/vrm-host-session/`](../../formations/vrm-host-session/) is the
`usdview` session that draws a VRM stage with MToon: the `lookdev` runtime
above, `usd-vrm-plugins`' `vrmImaging` and this repository's published `toon`
package, each pinned by digest. Nothing is built. On Windows x86_64, with the
runtime pulled as above and a host Python 3.13:

```sh
ost artifact pull oci://ghcr.io/animu-sphere/usd-vrm-plugins@sha256:84dbb7e550c55d249798f7288066de3589c383a75a35a6268328a497f147dda8     --expect-artifact sha256:894fd616f1414d5b393ff0d50abbf7ef18cb603562133c72e642493b48f71667
ost artifact pull oci://ghcr.io/animu-sphere/hydra-toon@sha256:9c4f37a04ca44fb01c1427408019d7a2c9cd6371e93b0b1cacd728f387f08f0c     --expect-artifact sha256:797a038d9d6d199de18a20e4dd1a986ceac51de07eada51a9896837269c9f140
cd formations/vrm-host-session
ost formation doctor formation.toml
ost formation run formation.toml
```

The published v0.2.0 package is verified on the RTX A5000 in
[renderer report 33](../reports/renderer/33-2026-10-04-published-v020.md).
The declared command opens `testusdview` on the committed
`material-probe.usda` and asserts that its VRM material selected MToon; it
fails if `vrmImaging` is missing. To draw an avatar instead, override the
command and give the avatar's MToon material count:

```sh
TOON_EXPECT_MTOON=12 ost formation run formation.toml --     testusdview <avatar.usdz> --renderer Toon --testScript vrm_material_check.py
```

Hydra does not carry a stage's `metersPerUnit`, so a stage whose unit is
not the metre states it through the render setting `toon:metersPerUnit`, 1
by default; MToon's world-coordinates outline width is in metres, and this
setting turns it into the stage's units. In a `testusdview` script,
`appController._stageView.SetRendererSetting("toon:metersPerUnit", 0.01)`
sets it for a stage of centimetres
([renderer report 17](../reports/renderer/17-2026-09-28-outline-meters-per-unit.md)).

UV animation uses seconds. The standalone viewport converts its selected
USD time code by the stage's `timeCodesPerSecond` automatically. In a
`usdview` host, supply the same value through the renderer setting
`toon:timeSeconds` when selecting a USD time; without it UV animation stays
at time zero. A time update rewrites no material slot. The controlled
viewport comparison and remaining MToon texture checks are recorded in
[renderer report 25](../reports/renderer/25-2026-09-30-mtoon-rest.md).

`TOON_HYDRA_EVIDENCE` and `TOON_HYDRA_IMAGE` keep the frame evidence and the
image; unset, they go to a temporary directory the check prints
([renderer report 13](../reports/renderer/13-2026-09-28-host-session-formation.md)).

usdSkelImaging hides a skinned mesh's authored normals from Hydra unless
`USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS=1` is set before the stage is
imaged, and `hdToon` then derives smooth normals per mesh. On an avatar
split into several meshes, such as bangs whose tips are a separate Blend
mesh, those differ where the meshes meet and show as a line. Set it in the
session's environment for the authored normals; the published v0.1.0 `toon`
package reads none, so this needs a later one:

```sh
USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS=1 ost formation run formation.toml --     usdview <avatar.usdz> --renderer Toon
```

The setting is process-wide, so Storm in the same session reads it too. The
standalone viewport sets it itself ([renderer report 20](../reports/renderer/20-2026-09-28-authored-normals.md)).

## The standalone viewport

```sh
ost renderer viewport -- --frames 8 --hidden
ost validate --intent renderer-viewport
```

The first run fetches GLFW and Dear ImGui. Omit the arguments after `--` for an interactive
window. `--samples N` sets the MSAA samples per pixel, 4 by default; 1 turns
anti-aliasing off. In a Hydra host, the render setting `toon:msaaSamples`
does the same. The viewport builds its own tree, `build/<target>--renderer-viewport`,
and `ost validate --intent renderer-viewport` validates that tree and its
launch record.

Left drag orbits, a middle or Shift+left drag pans. Holding the right button
and dragging right zooms in; dragging left zooms out. The wheel dollies;
F frames the scene and R returns to the last framing.
`--screenshot <file.ppm>` writes the last of `--frames N` frames, and P
writes the next frame to `toon-viewport-<n>.ppm`. The keys 1, 2, 4 and 8
set the MSAA samples per pixel while the viewport runs; the title shows the
count, and `--samples N` sets the first.

For repeatable close-ups, `--camera-pan X Y` applies an initial drag in
window pixels and `--camera-dolly N` applies wheel notches, positive toward
the target. Pan is applied after startup framing, then dolly; `F`, `R` and
opening another file retain ordinary framing. Sample-count changes keep
this view. The no-Hydra presentation check used:

```sh
ost renderer viewport -- --hidden --frames 8 --vsync off \
    --camera-pan 13 -7 --camera-dolly 2
```

An overlay shows the frame interval, the CPU and GPU time of each part of
the frame, the draw calls and the uploads, as mean, p95 and max over the
last 1,024 frames; `O` hides it and `--overlay off` leaves it out. A capture
never contains it. Every run ends with a `Timing:` line per quantity. Read
GPU times with `--vsync off`: under vsync the GPU idles between frames and
times the same work several times longer
([renderer report 22](../reports/renderer/22-2026-09-30-viewport-telemetry.md)).

The Outlines checkbox or `--outlines off` omits hulls without rewriting
materials or re-uploading geometry. `Hull draws:` and the overlay count
the actual hull calls, including those in the transparent part;
`--expect-hulls N` fails when the last frame differs. Compare total GPU
`work` with outlines on and off: the GPU overlaps passes, so the first
pass that fetches geometry also carries that cost.

In a Hydra-fed viewport, expand **Morphs**, then a mesh, to inspect its
evaluated subshape weights. Drag a weight or Ctrl+click its field to enter
a value; signed weights are supported. An edit takes effect on the next
frame and holds that mesh's whole evaluated weight array while playback
continues underneath it. **Release override** resumes the latest scene
weights; **Release all overrides** does so for every mesh. Slots are indexed
renderer subshapes, including any mapped inbetweens, rather than source
expression names. No USD values are authored. The Uploads panel shows the
small morph-weight writes; the Animation panel shows Hydra sync count.
Source expression mapping and material-value controls are separate work.

Hull submission also omits meshes whose full expanded outline is outside
one of the six clip planes, using the current skinned pose. It keeps
boundary silhouettes, including a surface outside the image whose hull
reaches into it. World widths use the stage unit; screen widths include
the projection and vertex depth. The bounds are conservative for width
textures, so partially zero maps and animated UVs retain their full-width
envelope. Hulls crossing the near/far planes are retained; fully clipped
hulls are omitted before submission.

Small rigid opaque meshes can also hide hulls. One triangle must cover the
entire expanded hull, including raster margins, and its farthest depth
must be nearer than the hull's nearest depth. Unlit fallback meshes and
double-sided Opaque MToon surfaces can establish coverage; Mask, Blend,
skinned and single-sided MToon meshes cannot. The current pose, camera,
blocker transform and material are used each frame. Combined triangle
coverage and avatar self-occlusion are not tested.

`--outline-culling off` disables frustum and occlusion omission for a reference
capture; the default is `on`. Keep the time, camera and samples identical
when comparing it with `on`. `Hull draws:` counts submitted hulls after
omission, whereas the scene summary counts materials requesting a hull.
The comparison rewrites no material and changes no uploads.
[Renderer report 28](../reports/renderer/28-2026-10-04-outline-frustum.md)
records controlled colour/depth checks and representative VRM captures.
[Renderer report 29](../reports/renderer/29-2026-10-04-outline-depth-clip.md)
adds near/far-plane checks and a skinned viewport return sequence.
[Renderer report 30](../reports/renderer/30-2026-10-04-outline-occlusion.md)
adds current-frame opaque occlusion and reveal/return checks.

With `vrmImaging` registered in the runtime/plugin environment, compare
the committed depth-plane fixture at 1x/4x MSAA:

```sh
python scripts/evaluate_outline_depth.py --viewport <viewport-usd build>/adapters/viewport/toon-viewport
```

The driver checks MToon/GPU-skinning selection, hull counts, uploads,
boundary outline coverage and culling-on/off image equality. Captures,
logs and a command/hash summary go to `build/outline-depth/`, or `--output`.

With the same plugin environment, evaluate opaque occlusion at 1x/4x:

```sh
python scripts/evaluate_outline_occlusion.py --viewport <viewport-usd build>/adapters/viewport/toon-viewport
```

The driver checks every authored reveal/return pose against ordinary hull
submission, visible outline pixels, sample counts and uploads. A 33-frame
run additionally checks the returning image and pose-only updates; it
captures its final frame. Evidence goes to `build/outline-occlusion/`.

The overlay's `Lighting and material` section selects scene lighting or
the camera key, adjusts direct and ambient strength separately, and moves
the fallback key direction in view space. `Material view` shows the MToon
surface, base colour, mapped normals, incident direct light or uniform
ambient. These controls change frame values without editing material
slots. The equivalent capture options are `--lighting scene|camera`,
`--direct-strength N`, `--ambient-strength N` (both in [0, 4]), and
`--material-view surface|base|normal|direct|ambient`.

Scene lighting reads USD distant lights, sphere lights as points or spots
with ShapingAPI cones, and dome lights as uniform ambient. A scene with no
supported lights uses the camera key plus ambient; hiding all authored
lights leaves it dark. Up to 32 visible direct lights are drawn in core-id
order, with all ambient lights summed. This is basic lighting: sphere area
and normalization, dome textures, shadows and light linking are not read.
In `usdview`, point attenuation uses `toon:metersPerUnit`; the viewport
reads the stage's unit automatically. [Renderer report 26](../reports/renderer/26-2026-09-30-scene-lights.md)
records the tested controls and scene route.

### A USD stage in the viewport

The `Skeleton` panel lists each USD skeleton's joint names, parents and
evaluated world positions in stage units. Enable `Show bones and joints` to
draw them through surfaces, and `Joint labels` for names. Selecting a joint
row highlights its marker and label. The display follows the selected USD
time, including mapped animation and missing-joint rest fallback; it does
not show external late pose overrides. Bones crossing clip planes are
clipped before projection. Diagnostics never author USD or request Hydra
sync. Closing the panel with screen display disabled stops the diagnostic
reads. `O` hides all overlays, and screenshots remain overlay-free.
Disable diagnostics when measuring the renderer's baseline motion cost.

In a build with Hydra, `Open File...` on the overlay or Ctrl+O opens the
native file chooser, including when starting on the bootstrap scene. The
filters include USD (`usd`, `usda`, `usdc`, `usdz`), VRM and PMX; the selected
file is opened through OpenUSD, so source formats require their registered
file-format plugins in the launch environment. The chooser also permits
other files supported by those plugins. A successful open frames the new
scene using its up axis and unit; cancellation keeps the scene, and a
failed open shows an error and keeps it too. Ctrl+O works with the overlay
hidden. `O` still toggles the overlay.

`--switch-file <file> --frames N`, with N at least 2, exercises the same
scene replacement halfway through a bounded run. The file-open CTests
compare its final capture with a fresh start on that scene, including a
Japanese filename, changed geometry and a failed open.

With `ost` 0.23.14 or newer, `ost renderer viewport --intent hydra` builds
the Hydra adapter and the viewport into a tree of its own,
`build/<target>--hydra--renderer-viewport`, where `--usd` draws a stage
through Hydra in the viewport's own frame loop
([renderer report 19](../reports/renderer/19-2026-09-28-hydra-fed-viewport.md)).
A VRM avatar selects MToon only with `vrmImaging` in the process, which
`--with` adds by the digest the VRM Formation pins
([`ost` report 08](../reports/ost/08-2026-09-30-v0.23.14-report-07-reverified.md)):

```sh
ost renderer viewport --intent hydra --profile lookdev \
    --with sha256:894fd616f1414d5b393ff0d50abbf7ef18cb603562133c72e642493b48f71667 \
    -- --usd <avatar.usdz>
ost validate --profile lookdev --intent hydra--renderer-viewport
```

Without `--with`, every VRM material draws as PreviewSurface. The digest
must be in the local registry: pull it as
[the VRM host session](#the-vrm-host-session) does.

For repeatable animation evaluation, `--time T` sets an initial USD time
code and `--time-step S` advances by S time codes per presented frame.
Without `--time`, stepping starts at the stage's start time. Without playback
or either time option, an animated stage stays at its start, and a static
stage at Default.
This is a fixed sequence, independent of wall time, with no looping;
time codes outside the authored range use USD's sampling behavior. Both
options require `--usd`; opening a replacement restarts its sequence.
An unchanged selected time skips Hydra sync after initialization.
For example, this committed fixture moves a joint and returns to its
initial pose in nine frames:

```sh
ost renderer viewport --intent viewport-usd --profile lookdev -- \
    --usd adapters/viewport/tests/outline-motion.usda \
    --time 1 --time-step 0.25 --hidden --frames 9 --vsync off --overlay off
```

For interactive playback, open a stage with a finite authored start/end range
and use the overlay's `Animation` panel. `Play` / `Pause` and Space toggle
playback. `Time code` seeks within the range; `Start`, `< Step` and `Step >`
seek to the beginning or by one time code. Manual seeks pause. `Speed`
multiplies the stage's time-code rate by 0.05–4; `Loop` wraps at the end,
while disabling it stops on the endpoint. Playing again from that endpoint
restarts at the beginning. Playback defaults to paused, speed 1 and looping.
A successful file open resets the transport with the new stage's range and
the launch settings; cancellation or a failed open preserves it.

`--play` starts wall-clock playback, `--playback-speed N` chooses its multiplier
and `--loop on|off` chooses its endpoint behavior. `--play` requires `--usd`
and an authored animation range. It cannot be combined with `--time-step`;
that option keeps its deterministic per-presented-frame evaluation and
disables interactive transport controls. File-dialog and minimized-window
waits do not advance the wall clock. Paused frames still process pending USD
changes; camera-only frames need no Hydra sync. `Hydra syncs` in the panel and
the final `Animation:` log show this boundary.

The measured regression runs paused, deterministic and wall-clock morph
captures, checking sync counts, static uploads and paused image equality:

```sh
ctest --test-dir build/cy2026-windows-x86_64-py313-lookdev--viewport-usd \
    --output-on-failure -R 'toon-playback|toon-viewport-playback'
```

[Renderer report 37](../reports/renderer/37-2026-10-05-viewport-playback.md)
records this run. Wall-clock playback evaluates the USD scene before
extraction; it does not latch a motion sample at GPU submission.

For a VRM stage with the material and file plugins registered, use the same
time options with `--samples 1`, `4` or `8` and `--outlines on` or `off`.
[Renderer report 24](../reports/renderer/24-2026-09-30-outline-stability-cost.md)
records that comparison. Increasing MSAA reduces sampled silhouette
variation; it does not provide temporal filtering.

The AA evaluation driver requires Python with Pillow and a built Hydra
viewport, with the VRM imaging plugin paths and DLL directories registered
as for the runs above:

```sh
python scripts/evaluate_antialiasing.py \
    --viewport <viewport-usd build>/adapters/viewport/toon-viewport \
    --avatar <local Alicia motion stage> --output build/aa-quality
```

Omitting `--avatar` runs only the generated thin-feature fixture. All runs
write local captures, comparison sheets, logs and `summary.json` under the
output directory. The driver requires actual 1x/2x/4x/8x support and MToon
imaging; it fails on a sample-count downgrade. The default avatar sequence
is 1,200 frames from time code 0 by steps of 0.25; `--frames` must exceed
1,024 for the repeated benchmarks to exclude warm-up. The default close-up
pan `(0, 155)`, dolly `12`, times `0`, `29.75`, `299.75` and expected 20
draws fit the tested Alicia motion stage; use `--close-pan X Y`,
`--close-dolly N` and `--avatar-draws N` for another avatar, and ensure its
authored time range covers the chosen sequence. The generated fixture
contains original geometry and textures. Model and avatar captures stay
local. [Renderer report 27](../reports/renderer/27-2026-10-01-antialiasing-quality.md)
records the 4x baseline decision and the limits of its spatial and temporal
measurements.

The outline temporal evaluation additionally requires NumPy. With the same
viewport and plugin environment, run:

```sh
python scripts/evaluate_outline_temporal.py \
    --viewport <viewport-usd build>/adapters/viewport/toon-viewport \
    --avatar <local Alicia motion stage> --output build/outline-temporal
```

Omit `--avatar` for only the generated GPU-skinned thin hulls. Translation
and rotation at two distances are evaluated at 1x/4x/8x against 4x MSAA
at four times the linear resolution. Repeated and returning poses must
match exactly. The optional avatar uses five time codes, 29 through 30
by 0.25, at 1280x720 with a 2560x1440 comparison, and isolates its signed
outline contribution using outlines-on/off pairs. `--avatar-time`,
`--avatar-step`, `--avatar-phases`, `--avatar-draws`, `--close-pan X Y` and
`--close-dolly N` adapt the sequence and view to another asset. Captures,
logs, contact sheets, hashes, commands and `summary.json` remain local.
These are finite sampling comparisons; [renderer report 31](../reports/renderer/31-2026-10-04-outline-temporal.md)
defines the statistics and records remaining temporal limitations.

Opening a raw `.vrm` also needs `usdVrmFileFormat` and
`usdVrmPackageResolver` for embedded textures; `vrmImaging` alone handles
material imaging, not file ingestion. With those local bundles already built
against the same OpenUSD runtime, start on the bootstrap scene and use
`Open File...`:

```sh
ost renderer viewport --intent hydra --profile lookdev \
    --with ../usd-vrm-plugins/plugins/usdVrmFileFormat \
    --with ../usd-vrm-plugins/plugins/usdVrmPackageResolver \
    --with sha256:894fd616f1414d5b393ff0d50abbf7ef18cb603562133c72e642493b48f71667
```

This local-bundle composition opened the requested raw VRM in
[renderer report 23](../reports/renderer/23-2026-09-30-viewport-file-open.md).
Packaged bundles must match the session's target; a `usd` package is not a
`lookdev` package.

`ost test` does not take the workflow tree, so the viewport's Hydra CTest,
`toon-viewport-present-usd`, runs in the `viewport-usd` intent, which builds
the same two adapters:

```sh
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
```

In that tree, `ost renderer viewport --intent viewport-usd` picks the
viewport among every executable of that name, and the `usdview` host test
installs a copy under `adapters/hydra2/usdview-install/`, which it may
launch instead; after a change, run `ost test` there before looking.

## Local VRM reproduction comparison

With the `viewport-usd` build, Pillow/NumPy and the runtime/VRM plugin
environment registered, install the reference packages under ignored build
output and start the local comparison page:

```sh
npm install --prefix build/vrm-reproduction/reference --ignore-scripts \
    --no-audit --no-fund three@0.180.0 @pixiv/three-vrm@3.5.5
python scripts/evaluate_vrm_reproduction.py \
    --viewport <viewport-usd build>/adapters/viewport/toon-viewport \
    --avatar <local VRM 0.x> --avatar <local VRM 1.0> \
    --reference-root build/vrm-reproduction/reference/node_modules
```

Open the printed loopback URL and press `Capture reference set`. It loads
local model bytes through three-vrm with the exported viewport camera;
captures and metadata are saved locally. `--reuse` serves existing viewport
captures after checking the asset hashes/order. The reference evaluates the
raw rest pose and normalizes Lambert light intensity, not animated springs,
constraints or expressions. Models, captures and npm modules are not committed.

For the matching host capture, use the current installed `hdToon`, the same
runtime and VRM plugin paths, and the host Python interpreter. Set
`USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS=1`, then for each generated case:

```sh
# Export these environment variables using your shell's syntax.
TOON_COMPARISON_CAMERA=<output>/avatar-0-full-camera.json
TOON_HYDRA_IMAGE=<output>/avatar-0-full-usdview.png
TOON_HYDRA_EVIDENCE=<output>/avatar-0-full-usdview-evidence.log
TOON_EXPECT_MTOON=<expected material count>
TOON_EXPECT_DRAWS=<expected mesh count>
python <runtime>/bin/testusdview <same local avatar> --renderer Toon \
    --testScript scripts/vrm_reproduction_usdview.py
```

This driver fixes the physical framebuffer and session camera and suppresses
host decorations. An interactive renderer session can instead be composed
by `ost renderer view <stage> --profile lookdev --with <VRM plugin>`, adding
the same file-format, resolver and imaging plugins listed above. The OST
renderer command supplies `hdToon`; `ost plugin view` is the plugin-focused
alternative, not a prerequisite for a composed renderer session.

Once all reference and usdview images exist, run
`python scripts/evaluate_vrm_reproduction.py --summarize`. It writes
`summary.json` and a comparison sheet, reporting foreground encoded-sRGB
differences without a universal fidelity threshold.

Continuous outline evaluation captures each presented frame, rather than
starting a new process for every pose:

```sh
python scripts/evaluate_outline_sequence.py \
    --viewport <viewport-usd build>/adapters/viewport/toon-viewport \
    --avatar <local motion stage 1> --avatar <local motion stage 2>
```

Defaults are 61 frames, USD time codes 29 through 89, 20 MToon draws, 4x
MSAA, full/close views at 640×720. Use `--time`, `--step`, `--frames` and
`--draws` for another stage. Preview GIF duration assumes 30 time codes per
second; evidence is indexed by USD time code. Each outlines-on frame must
match an independent repeat, and geometry must upload only once.
`--capture-sequence DIR` and `--camera-output FILE` require bounded
`--frames N`; a sequence and `--screenshot` are mutually exclusive. Captures
omit the overlay and perturb timing, so use separate uncaptured performance
runs. [Report 32](../reports/renderer/32-2026-10-04-vrm-reproduction.md)
records the accepted v0.2.0 baseline and its limitations.

## Late evaluated input and latency

A host registers `LateFrameSource` through `OffscreenRenderer::SetLateFrameSource`
or `PresentSession::SetLateFrameSource`. Its external rig/expression evaluator
consumes the source owner's motion semantics and publishes renderer values.
The callback reads those already evaluated values after GPU/acquire waits
and structural preparation, before fast writes and recording. It must not
perform USD authoring or scene sync there.

Start each candidate from the same scene snapshot used for ordinary
extraction. Keep its mesh/material order, bindings, static array identities,
slow revisions, joint count and subshape count; update only evaluated fast
values and their normal per-resource revisions. New topology, targets,
textures or material structure go through ordinary scene processing and
extraction. Replace stored candidates when replacing the scene.

For example, a host can store a complete evaluated snapshot under a mutex:

```cpp
struct LatestEvaluatedFrame {
  std::mutex mutex;
  std::optional<Toon::FrameSnapshot> frame;
} latest;

session->SetLateFrameSource({
    [](void* context, Toon::FrameSnapshot& out, std::string&) {
      auto& input = *static_cast<LatestEvaluatedFrame*>(context);
      std::lock_guard lock(input.mutex);
      if (!input.frame) return false;
      out = *input.frame;
      return true;
    }, &latest});
```

The publisher takes the same mutex when replacing `frame`. Keep `latest`
alive until unregistering with `SetLateFrameSource({})`, and keep evaluation
outside the lock. Return the current snapshot even when unchanged: false
with an empty error selects that frame's ordinary extracted input, not a
previously accepted late sample. False with an error fails the frame.
Structural or non-finite candidates fall back atomically; inspect
`late_samples_rejected` and `late_rejection` in the session statistics.

Set `snapshot.inputs.pose`, `.expression`, `.look_at` and `.camera` to the
appropriate input-update times in `Toon::SteadyNanoseconds()` units. Map a
producer clock explicitly; leave absent timestamps zero. The built-in USD
host records time-selection input, not sensor-production time. Its late
commit and current orbit camera require no additional Hydra sync.

Export an uncaptured timing run with:

```sh
toon-viewport --usd <animated stage> --hidden --frames 1200 \
  --time 0 --time-step 0.1 --vsync off --overlay off \
  --telemetry-output <timing.json>
```

The live Latency panel and JSON expose input-to-buffer/submit/present-API
response times and CPU joint/morph/material write costs. Summaries cover
1024 samples with p95/p99, population variance and standard deviation. Each
input update contributes its first completed frame once; missing input
series have count zero. Per-frame JSON timestamps remain available for age
analysis. `present_endpoint` is `vkQueuePresentKHR_return` and
`display_time_measured` is false: use external display instrumentation for
scanout latency. [Report 39](../reports/renderer/39-2026-10-05-late-frame-input.md)
records the receiver regression and representative avatar measurements.

In a USD viewport session, expand `Materials` to inspect effective normalized
values, renderer material ids and the meshes bound to them. Expand a material
to see its model, alpha mode, sidedness and override activity. For MToon,
drag colour/scalar or texture-transform fields, or Ctrl+click to enter a
numeric value. Colours are linear. `Release material override` and
`Release all material overrides` resume the latest evaluated scene values.
These controls use the same late commit as the Morphs panel, so edits require
no USD authoring or Hydra sync. Model, alpha mode, sidedness and texture ids
are display-only. PreviewSurface values are read-only while that fallback
draws mesh display colour. Runtime expression names and source-owner mappings
are not supplied by these renderer-id diagnostics.
