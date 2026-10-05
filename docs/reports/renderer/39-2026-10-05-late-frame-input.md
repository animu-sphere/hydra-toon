# Evaluated late-frame input and latency endpoints

- Date: 2026-10-05
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Runtime: canonical CY2026 OpenUSD 26.08 lookdev; OpenStrata 0.23.14
- Scope: evaluated late latching and API-endpoint telemetry for v0.3.0

## Sampling and dependency boundary

Both Vulkan sessions accept a `LateFrameSource` callback. They wait for the
previous frame and acquire the presentation image, prepare structural mesh,
texture and material resources, then read the host's latest evaluated
snapshot before joint/weight/material writes and command recording. A host
owns its callback lifetime, synchronization, resource revisions and producer
clock mapping. Renderer input contains evaluated joint matrices, subshape
weights, normalized material values and camera matrices. There is no link
to a format repository or expression/look-at evaluator.

`ApplyFastSnapshot` validates the complete candidate before changing draw
storage. Changed draw membership, bindings, slow arrays/revisions, palette
or weight counts, material structure and textures reject the sample. The
ordinary extracted frame is then rendered; the rejection count and reason
are visible. Non-finite fast values are similarly rejected. A callback
failure with an error fails the frame; no sample with no error retains the
ordinary frame. This API does not retain an earlier host sample on behalf
of the host.

The viewport late callback commits its already evaluated Hydra delegate,
without applying pending USD edits, moving time or syncing Hydra, and takes
its current orbit camera. Morph debug edits made after extraction can reach
the current submit. USD playback still evaluates before extraction. This is
the renderer receiver, not a live source-produced MotionPose/gaze host.

## State and independent late-frame comparison

`toon-late-frame-state` checks in-place packet storage, shared rest arrays,
pose, signed weights, material values, camera and input timestamps. Changed
visibility, slow revisions, weight counts, material structure and rest array
identity are rejected without partial mutation. Non-finite camera, material,
mesh colour and light inputs are rejected. Rolling variance, timestamp units,
absent inputs and repeated unchanged input response counts are checked.

`toon-late-frame-gpu` holds its extracted input constant. Its source produces
new evaluated values inside the late callback, after extraction and GPU
waits. Eighteen frames cycle positive, zero and negative weights while
changing the joint palette, MToon colour and camera. An independent session
extracts the latest source normally. At 96x96 and 4x MSAA, every RGBA and
depth array agrees exactly.

After initialization, each frame writes one joint buffer, one weight buffer
and one material slot. Topology, points, skin, morph targets and textures
are not uploaded again; pipelines and targets are not recreated. An extra
structural source edit is rejected, uploads no new points and gives the
ordinary extracted frame's exact image. All Vulkan validation counts are
zero. Offscreen timestamps satisfy input <= latch <= buffers <= submit;
there is no offscreen present timestamp.

`toon-viewport-latency` runs 16 deterministic morph frames through the real
presentation path. All 16 late reads apply, none reject, and the sole mesh
retains one topology, point, skin and target upload and one initial pose and
material write. Sixteen changing weights produce sixteen weight writes.
Exported JSON endpoints are positive and ordered through presentation API
return. Pose response counts are sixteen; the unchanged camera counts once.

## Representative avatar pose measurements

Two runs of the local Alicia VRMA motion stage each present 1,200 frames at
1280x720, 4x MSAA, vsync off, overlay off, with no captures. Explicit USD time
starts at zero and advances 0.1 time code per presented frame. Summary
windows retain the final 1,024 samples, excluding startup. The avatar has
twenty GPU-skinned/authored-normal MToon draws, five transparent draws,
fifteen outlined draws, thirteen material slots and seven textures.

Each run reports these final counters:

| Counter | Total |
| --- | --- |
| Topology / rest-point / skin uploads | 20 / 20 / 20 |
| Material writes / texture uploads | 13 / 7 |
| Pose writes | 24,000, twenty per frame |
| Morph target uploads / weight writes | 6 / 6, initial only |
| Late reads applied / rejected | 1,200 / 0 |
| Swapchain recreations / sample changes / readbacks | 0 / 0 / 0 |

The source stage animates the body palette, not facial expression weights
or material values. This therefore measures representative avatar pose
updates; the changing expression/material receiver is established by the
synthetic late-frame comparison above.

| Quantity, milliseconds | Run 1 mean / p95 / stddev | Run 2 mean / p95 / stddev |
| --- | --- | --- |
| CPU frame interval | 1.504 / 1.760 / 0.131 | 1.565 / 1.811 / 0.108 |
| CPU Hydra evaluation | 0.393 / 0.436 / 0.025 | 0.408 / 0.473 / 0.040 |
| GPU work, excluding image wait | 0.311 / 0.315 / 0.002 | 0.309 / 0.313 / 0.003 |
| Selected USD time -> buffers written | 0.534 / 0.584 / 0.070 | 0.561 / 0.632 / 0.041 |
| Selected USD time -> submit return | 0.924 / 1.014 / 0.078 | 0.975 / 1.067 / 0.057 |
| Selected USD time -> present API return | 1.501 / 1.757 / 0.131 | 1.562 / 1.807 / 0.108 |
| CPU joint writes, all twenty meshes | 0.057 / 0.057 / 0.063 | 0.057 / 0.058 / 0.001 |

Run 1 includes a 2.054 ms joint-write outlier. These CPU costs include joint
packing, buffer upload/flush and rebinding when needed; morph costs cover
weight upload/flush, and material costs cover slot encoding and shared
material-buffer flush. They exclude the frame/light buffer. GPU per-pass
times overlap and are not independently additive. This is measured cost on
one device, not a general latency guarantee or a before/after speedup claim.

The environment composes published `vrmImaging` 0.10.0 (artifact
`sha256:894fd616f1414d5b393ff0d50abbf7ef18cb603562133c72e642493b48f71667`)
and its vrmSchema with locally staged owner raw-VRM ingestion libraries.
Their DLL SHA-256 values are:

- UsdVrmFileFormat: `d8a5f96ef8f701d60a959b4d523762b1cc3684c534633d67224baeef7cd021bc`
- UsdVrmPackageResolver: `c6edf27efa52596ca8eb0063511462d2ebfa9c51f1307ef16a434ce92f380968`

An initial environment omitted the package resolver and sampled missing
textures. That run is excluded from these numbers. The two reported reruns
load all seven textures.

## Telemetry interpretation and reproduction

Host timestamps use monotonic nanoseconds; zero means absent input. A host
must explicitly map any producer clock before passing timestamps. Response
summaries take the first completed frame for an input update, so a static
camera's growing sample age does not inflate response latency. JSON retains
per-frame timestamps, write costs, nearest-rank p95/p99, population variance
in ms squared and standard deviation. Missing input series have count zero,
and the panel displays them as unavailable.

The endpoint is `vkQueuePresentKHR` return, not scanout or photon time.
`display_time_measured` is false. USD time selection timestamps are not
sensor-production timestamps. Source-produced expression/gaze integration,
actual display measurement, expression mapping/material diagnostics and
skeleton/joint diagnostics remain roadmap work.

```sh
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
ost renderer viewport --profile core -- --hidden --frames 8 --vsync off
```

All 48 viewport-usd CTests and OpenStrata validation pass, including the
Hydra/usdview host and install tree. Artifact integrity is an explained skip:
this change creates no release package. Plain CMake, without OpenUSD or
Vulkan, builds and passes all ten CTests. The OpenUSD-free GPU viewport
builds/presents and passes eight focused late-frame, boundary, playback,
presentation and capture checks. `git diff --check` passes.

With matching owner file-format, package-resolver, imaging, schema and DLL
paths registered, each avatar run is:

```sh
toon-viewport --usd <local Alicia motion stage> --hidden \
  --width 1280 --height 720 --samples 4 --frames 1200 \
  --vsync off --overlay off --time 0 --time-step 0.1 --expect-draws 20 \
  --telemetry-output <output.json>
```

Local logs/JSON are `build/late-latch/avatar-motion-{1,2}.{log,json}`. The
presentation regression exports `adapters/viewport/latency.{log,json}` in
the viewport-usd build tree. Assets and machine-local captures stay untracked.
