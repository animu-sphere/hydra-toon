# Both avatar sessions from their packages

Date: 2026-11-01. Windows x64/MSVC Release, Vulkan, NVIDIA RTX A5000, and the `usd` runtime for the
first time. Third-party models, motions and captures remain local and ignored, and the private MMD models
are reported by number.

## Data flow

No session code changed. `toon-viewport-vrm` now builds against the version
`adapters/viewport/avatar-session-pins.json` names, and `scripts/pull_avatar_session.py` pulls that release's
session and owners by digest through `ost`. `toon-viewport-mmd` built from the prefix of an `avatarMmdSession`
archive with a deterministic Jolt SDK, as the
[package boundary](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/architecture/MMD_SESSION.md#package-boundary)
requires. Plugins are the owners' published bundles, checked against their release's `SHA256SUMS`.

Both hosts moved from the `lookdev` runtime to `usd`, the runtime these packages are built for.
`physicsUsd` compares the OpenUSD headers it finds with its own and refuses `lookdev`, so the MMD host has to,
and the VRM host now matches.

## VRM, from the published pin

`avatarVrmSession` 0.3.0 (`sha256:0871943a`), `vrmImaging` and the importers at 0.12.0, AliciaSolid and `VRMA_01`.

| Check | Result |
| --- | --- |
| `toon-vrm-runtime-resident` | passes: exact cached and uncached streams, exact reset replay, pause, late delivery, stop/restart and loop; face with 17 expressions and 46 morph targets |
| 120 presented frames with springs and face | 121 ticks, 0 mismatches and refusals, 120 late reads published |

## MMD, from a package of `usd-avatar-runtime` at `main`

An `avatarMmdSession` archive (`sha256:76ee74bc`) of what v0.4.0 is to release, on `usd-physics-plugins` 0.1.2 and
`usd-mmd-plugins` 0.3.2. It is not published, so this host has no pin yet.

| Check | Result |
| --- | --- |
| `toon-mmd-runtime-cache`, `toon-mmd-runtime-admission`, generated | pass |
| `toon-viewport-mmd-runtime`, generated | passes: 90 presented frames, reset and stage replacement, closed with no bodies left |
| 41 private PMX files with one private VMD, 300 presented frames at 720x720 and vsync | 24 draw; the other 17 are props with no rigid bodies, refused as `No MMD rigid bodies in this stage` |

For the 24 that draw: 597 to 612 ticks, 0 refusals and 0 mismatches, one Hydra sync, up to 624 material
inputs, and no bodies left after close. The largest costs were 4.9 ms a tick in the runtime and 1.0 ms a
result for resident conversion. Last-frame captures were viewed together at low resolution: all 24 draw with no obvious
breakage. This does not judge how physics looks, and pixels were not compared numerically.

## Limits

Windows only. The private VMD names bones and morphs that many of the models lack, so those keys are dropped,
as the owner reports, and such a model shows less of its motion. The bounded runs do not show motion over a long
take. No material value here is compared with the owner, and Hydra/direct agreement remains open.
