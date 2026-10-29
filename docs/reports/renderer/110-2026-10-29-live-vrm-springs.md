# Live VRM clip and spring motion in the viewport

Date: 2026-10-29. Windows x64/MSVC Release, OpenUSD 26.08, Vulkan,
NVIDIA RTX A5000. Installed runtime `vrm_session`, VRM owner 0.10.0 and motion
0.5.4 packages, with contract-v2 importer/schema resources. Third-party avatar
and clip data and captured images remain local and ignored.

## Data flow and controls

Optional `toon-viewport-vrm` consumes the runtime's
[native VRM session](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/architecture/VRM_SESSION.md).
Motion sampling, retargeting and spring evaluation stay in their owner adapters.
Runtime owns the seed at tick zero, 60 Hz ticks, a four-tick catch-up budget,
retained backlog and reset. The renderer matches the final pose to resident skin
palettes and applies the retained snapshot before extraction and at the late read.
No USD pose authoring, baked clip or per-frame Hydra sync is used.

Play/Pause/Reset/Stop and Space control the native session. Reset immediately
seeds the clip's first animated pose and pauses. Stop releases the session and
restores authored output; Play constructs a new session. With `--loop on`, the
first request after reaching the clip end explicitly resets spring history and
the runtime clock before replay. Overshoot/backlog is cleared at that declared
reset boundary; no negative step or velocity continuity is implied. `--loop off`
holds the clip's last pose while springs continue. `--secondary-motion off`
selects motion-only construction. USD seek/step/speed and generated gaze options
are refused for this native route.

## Evidence

`toon-vrm-runtime-resident`, using local AliciaSolid and VRMA_01, checks:

- Spring-enabled output changes actual resident skin palettes relative to
  motion-only output.
- Cached and uncached conversions agree exactly across 120 ticks in each of two
  reset generations; reset replay also agrees exactly.
- Paused time and repeated delivery/late reads perform no extra tick; renderer
  baseline commits preserve camera input and invalidate cached conversions.
- Reset/stop/restart and loop-wrap recovery succeed without refusal/mismatch.

AliciaSolid was drawn for 120 presented frames at 1000x1000 / 4x MSAA with the
original VRM and VRMA_01: 128 joints, 20 resident skins, 119 completed ticks,
zero mismatches/refusals and one Hydra sync. All late reads applied successfully.
After the first frame, static uploads were zero; pose uploads carried animation.
The bounded run's mean GPU scene time was 0.364 ms and total GPU work 0.636 ms.
These are scoped timings, not cross-platform performance acceptance.

Seven selected regressions pass: snapshot consumer state/GPU, Hydra skinning,
native VRM resident composition, viewport presentation/capture and USD playback.
The actual 1440x1000 window displayed the avatar, clip motion and spring-driven
hair/skirt motion at approximately 60 fps with vsync. Pause held tick 649 and its
pose across observations; Reset displayed paused tick zero; Play resumed. The
user confirmed visual acceptance of this Alicia preview.

This session selects pose only: expression/gaze channels and node constraints
are not evaluated. Scaled spring rigs retain the owner's admission limits.
Real-avatar Hydra/direct numerical and pixel parity, arbitrary seek, seamless
loop velocity continuity, dynamic in-place spring toggling and broader avatar
coverage remain open. Existing shader/material appearance is unchanged.
