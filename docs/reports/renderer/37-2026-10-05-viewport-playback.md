# Viewport playback and idle Hydra sync

- Date: 2026-10-05
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Runtime: canonical CY2026 OpenUSD 26.08 lookdev
- Scope: the animation playback controls and camera-only idle path of
  [v0.3.0](../../roadmap/v0.3.0.md)

## Transport and scene boundary

The viewport has wall-clock play/pause, range seeking, one-time-code steps,
speed from 0.05 to 4 and looping. Space toggles playback; the Animation
panel exposes the remaining controls. Manual seeks pause and clamp to the
authored range. Non-looping playback stops at the end, and another play
restarts at the beginning. Seconds advance by the stage's time-code rate.
File-dialog and minimized-window waits reset the clock; a successful scene
replacement resets transport state using the new range and launch settings.
Failed opens retain the active state.

`--play`, `--playback-speed` and `--loop` configure wall-clock playback.
`--time-step` retains its deterministic per-presented-frame, unclamped USD
sampling and disables interactive transport; combining it with `--play`
fails before window creation. Playback defaults to paused. Static stages
have no transport range and retain their previous Default-time behavior.

The viewport observes added, removed, dirtied and renamed terminal scene
prims. Each update still applies pending USD changes and commits the world;
Hydra sync runs only on initialization, changed time or a scene-index notice.
Equal explicit times are ignored. Camera and debug values remain in the draw
list, outside this sync boundary. The overlay and final log expose sync count.

## State and persistent GPU checks

`toon-playback-state` compiles without OpenUSD, Vulkan or windowing. It
checks time-code-rate conversion, speed, pause, clamped manual seek/step,
non-looping endpoints, replay, exact loop endpoints and multi-loop elapsed
intervals, scene reset and invalid/overflowing input. Explicit initial
capture times outside the range retain their existing sampling semantics.

`toon-viewport-playback` uses the committed sparse, GPU-skinned morph stage
for three 16-frame runs with 4x MSAA and FIFO presentation:

| Route | Hydra syncs | Weight writes | Static uploads |
| --- | --- | --- | --- |
| Paused at time code 2 | 1 | 1 | One each: topology, points, material, skin, pose, morph targets; no textures |
| Deterministic time code 2, step 0 | 1 | 1 | Same |
| Wall clock, speed 0.25, loop enabled | 17, including startup | 16 | Same |

The paused and deterministic PPM captures match byte for byte. Playback
changes only morph weights after the first frame; no rest geometry, target,
skin or material re-upload is allowed. Every run checks clean Vulkan
validation. The command requests a 96x96 window; this Windows/GLFW session
captures 120x96 because of the native minimum window width.

Logs and captures are under
`build/cy2026-windows-x86_64-py313-lookdev--viewport-usd/adapters/viewport/`
as `playback-{paused,deterministic,playing}.{log,ppm}`. Wall-clock times and
images vary between runs; the test requires motion and fixed static upload
counters rather than an exact elapsed time. The conflict check rejects
`--play --time-step 1`.

The existing nine-frame joint and morph return sequences still pass their
upload counts and image comparisons. File replacement, failed-open retention,
MSAA switching and overlay-free capture regressions also pass.

## Reproduction and limits

```sh
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
```

Build, all 43 CTests and OpenStrata validation pass, including the usdview
host and install-tree checks. Artifact integrity is an explained skip
because this run packages no release.

The existing OpenUSD-free `core--renderer-viewport` tree also builds
`toon-viewport` and `toon-playback-test`; its presentation smoke and playback
state checks both pass. No OpenUSD dependency was introduced into that route.

This is USD scene playback before extraction, not late motion latching.
The transport state and actual playback/capture path are automated; individual
mouse interactions with the new panel were not exercised in this run.
Representative-avatar expression mapping, direct viewport expression
controls, look-at, skeleton diagnostics and input-to-display latency remain
outside this change.
