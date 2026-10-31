# Dense live MMD: identity cost and an unoptimized measurement

Date: 2026-10-31. Windows x64, MSVC 19.51, Ninja, `/O2 /Ob2 /DNDEBUG`,
OpenUSD 26.08, Vulkan, NVIDIA RTX A5000, deterministic Jolt 5.5.0 with six-DOF.
Continues [report 112](112-2026-10-31-mmd-admission.md).

Inputs are the same unmodified private pair, by SHA-256: PMX `b37ed181…`, VMD
`737de056…`. Assets, paths, instruments and raw logs remain ignored under
`build/live-mmd-perf/`. Bases: renderer `8901bd2` plus this change; runtime `658f33e`
and its [change](https://github.com/animu-sphere/usd-avatar-runtime/pull/57)
`5462833`; physics `c45e318` rebuilt optimized; the MMD package of report 112.

## Report 112 measured an unoptimized build

Report 112's renderer tree and physics package were configured with an empty
`CMAKE_CXX_FLAGS_RELEASE`, so `CMAKE_BUILD_TYPE=Release` compiled them without
`/O2` or `/DNDEBUG`. Its 11.49 ms/result conversion was not a regression from
report 108's 3.87 ms. The same source and inputs, optimized, measure 2.67–2.86
ms/result. Tick cost barely moved, because the runtime was already optimized and
Jolt itself was built separately.

## Where the tick went

A local timed copy of the runtime measured 952 ticks with no USD publication or
rendering, 11.82 ms/tick in total. Actual simulation and evaluation were small:

| Stage | ms/tick |
| --- | ---: |
| State validation, mostly one mesh string per UV record | 3.74 |
| Joint identity search, pre and post | 2.91 |
| UV morph evaluation and identity matching | 1.22 |
| Material morph evaluation and identity search | 1.05 |
| Jolt step with kinematic targets and body reads | 1.11 |
| Pose evaluation, IK and physics feedback | 0.45 |

At 8.33 ms budget per 120 Hz tick, 11.8 ms could never catch up: every display
request paid the eight-tick catch-up limit, about 95 ms.

## Change

The [runtime change](https://github.com/animu-sphere/usd-avatar-runtime/pull/57)
owns the tick and its
[evidence](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/reference/CAPABILITY_MATRIX.md#dense-live-mmd-tick-identity-cost-evidence-2026-10-31):
4.06–4.12 ms/tick, with every value and identity over all 952 ticks hashing
identically to the prior code.

Here, `Valid` and `AvatarStateAdapter::Apply` treat a repeated mesh address as
bytes already read in that scan. Bound UV slots keep an owner string only at the
start of a run. Bytes are compared again at each new address and each bound owner
boundary, so renames, reorders and duplicates still refuse; values are still
scanned. This is a comparison inside one call, not a cross-frame pointer shortcut,
and a producer that does not share addresses only loses the saving.

## Displayed result

Bounded hidden runs: 640x720, 4x, vsync on, zero refusals and mismatches, zero
bodies after close. Before is optimized `main`; after is this change with the
runtime change. Each row is one run.

| Build | Frames | ms/tick | ms/result | Frame p50 / mean / p95 ms | GPU scene p50 ms | Ticks | Simulated / wall s |
| --- | ---: | ---: | ---: | --- | ---: | ---: | --- |
| Before | 120 | 11.94 | 2.86 | 104.5 / 109.5 / 180.4 | 4.45 | 952 | 7.9 / 13.0 |
| Before | 120 | 11.79 | 2.67 | 105.8 / 109.4 / 119.7 | 4.48 | 952 | 7.9 / 13.0 |
| After | 120 | 4.99 | 1.52 | 17.3 / 19.4 / 28.6 | 1.88 | 288 | 2.4 / 2.3 |
| After | 120 | 4.88 | 1.44 | 17.0 / 18.7 / 25.9 | 1.93 | 278 | 2.3 / 2.2 |
| After | 600 | 5.02 | 1.49 | 25.6 / 26.0 / 45.4 | 4.45 | 1880 | 15.7 / 15.6 |
| After | 600 | 5.03 | 1.39 | 16.7 / 19.6 / 32.8 | 1.78 | 1416 | 11.8 / 11.7 |

The fixed clock now holds wall time, with no retained backlog, instead of falling
to 0.6x. This is real-time simulation, not a steady 60 fps. Display varies between
about 38 and 60 fps, with p99 frames of 47–73 ms, and two identical 600-frame runs
differ by 6 ms mean. GPU scene time is bimodal across runs, near 1.8 or 4.5 ms, and
`cpu submit` moves with it; the cause is not established. Viewport ticks cost
more than the 4.1 ms bench, which runs alone. A hidden 640x720 window, the first
16 seconds of one motion and one avatar are not representative acceptance.

## Regression evidence

All 87 CTests configured in this tree pass against the changed runtime, including
`toon-avatar-state` and its GPU case, resident MMD runtime/material checks, and
the generated [combined](114-2026-10-31-mmd-combined-parity.md) and
[material](113-2026-10-31-mmd-material-parity.md) comparisons with exact 1x/4x
colour and depth. The dense adapter case adds a whole run renamed through one
shared address, an adjacent duplicate through a repeated address, and one address
standing for both bound owners. Removing the repeated-address order check fails
`toon-avatar-state`.

[Report 117](117-2026-10-31-vulkan-device-local-meshes.md) traces the bimodal GPU and submit times to the PCIe link.

Remaining measured cost is unassigned in the
[roadmap](../../roadmap/later.md#not-yet-in-a-milestone). Each display millisecond
now costs about two, since a longer frame owes proportionally more ticks.
