# The MMD session from its published pin

Date: 2026-11-01. Windows x64/MSVC Release, Vulkan, NVIDIA RTX A5000, `usd` runtime. Third-party models, motions and
captures remain local and ignored, and the private MMD models are reported by number.

`usd-avatar-runtime` v0.4.0 is published, so `adapters/viewport/avatar-session-pins.json` now names `avatarMmdSession`
0.4.0 (`sha256:bd8b1b83`) and its owners by digest, and `toon-viewport-mmd` requires exactly that version.
`scripts/pull_avatar_session.py mmd` pulled all of them through `ost`. The host was built in a new directory with a
local deterministic Jolt 5.5.0 SDK, which the package checks, and with the published `usd-mmd-plugins` 0.3.2 bundles.

| Check | Result |
| --- | --- |
| `toon-mmd-runtime-cache`, `toon-mmd-runtime-admission`, `toon-viewport-mmd-runtime`, generated | pass |
| The 24 private models with rigid bodies from [report 119](119-2026-11-01-avatar-sessions-from-packages.md), one private VMD, 300 presented frames at 720x720 and vsync | all draw: 597 to 635 ticks, 0 refusals and 0 mismatches, one Hydra sync, no bodies left after close |

The largest costs were 5.5 ms a tick in the runtime and 1.0 ms a result in resident conversion. Report 119's 4.9 ms
came from a local build of the session, and this package is built on the hosted runner. The difference was not
investigated.

## Limits

Report 119's limits still hold, and this adds no judgement of how physics looks. This run checks refusals and
mismatches and was not captured or watched. `avatar-session-pins.json` keeps the VRM host at 0.3.0, which its check
used; moving it to 0.4.0 needs that check again.
