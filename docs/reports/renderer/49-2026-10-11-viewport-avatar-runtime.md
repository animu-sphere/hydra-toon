# Runtime results in the dedicated viewport, arriving after extraction

- Date: 2026-10-11
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Runtime: canonical CY2026 OpenUSD 26.08 lookdev; `usd-avatar-runtime`
  `b8c05c1` (experimental C ABI revision 4) with its VRM USD bindings, as in
  [report 47](47-2026-10-11-avatar-real-providers.md); `vrmSchema` 0.10.0;
  `vrmImaging` 0.10.0 lookdev package; `usdVrmFileFormat` 0.10.0 for the
  `.vrm` avatar
- Scope: the dedicated viewport, late arrival and runtime-frame diagnostics
  of [v0.3.0](../../roadmap/v0.3.0.md)

## The gap

Report 47 drove the runtime's real VRM providers through the host matcher on
offscreen renderers, one result per comparison. The dedicated viewport had
never drawn a runtime result. Nothing had shown a result published after a
frame's extraction reaching that frame, or the viewport presenting a stream
of results while the runtime resets and the scene is replaced. The panels
showed renderer ids, not the runtime frame or the source names behind them.

## Change

`toon-viewport-avatar` is the viewport built with an avatar runtime host
(`adapters/viewport/avatar_runtime.*`). It is built only with
`TOON_AVATAR_RUNTIME_PROVIDERS` and the Hydra adapter, from the same sources
as `toon-viewport`, and is not installed. `--avatar-runtime` turns the host
on for the `--usd` stage.

- **Providers and binding.** The host reads the avatar at the stage's
  default prim with the runtime's VRM expression and LookAt bindings, as
  report 47's host does, and registers the owner evaluator. It matches the
  first published result to the delegate's resident targets with
  `HdToonMatchAvatarTargets` and binds `Toon::AvatarState`. Unmatched
  outputs stay unbound and are listed.
- **Input.** The host generates every expression channel, a rolling wave
  with several expressions active at once, and a world gaze point circling
  in front of the head. Each input is stamped with the steady clock when it
  is produced, mapped to runtime seconds with scale 1. The owner arbitrates
  expressions and gaze; the host does not.
- **Producer.** At `--avatar-rate N`, a thread evaluates N results per
  second. Each retained result goes to a one-slot mailbox, replacing an
  untaken one. At `--avatar-rate 0`, the frame loop publishes one result per
  frame, after extraction, so captures repeat.
- **Two application points.** The frame loop applies the newest result over
  the extracted scene. The late read applies it again over `ReadFast`, after
  the GPU and acquire waits. A result published in between reaches the frame
  only at the late read. Neither point evaluates, syncs Hydra or authors USD.
- **Input times.** Each result's retained source samples set the frame's
  expression and look-at input times, mapped back to the steady clock. The
  existing latency series then measure them to submit. Pose input is left
  unmeasured: no pose provider runs.
- **Lifetime.** `--avatar-reset-every N` resets the instance to a new
  generation every N results. Opening a replacement stage binds a new
  runtime to it, in a new binding epoch, before the old scene is released.
- **Diagnostics.** An `Avatar runtime` overlay panel shows the applied
  result's instance, generation, frame, layout and binding epoch, publication
  counts, how the layout matched, unmatched outputs and owner diagnostics. It
  lists the source channels with their expression or LookAt, and each bound
  shape and canonical material input with its resident slot or field and its
  value in that result.

`run_avatar_providers.cmake` now takes an optional stage and checks its
output against `EXPECT` patterns, for the viewport tests.

## Evidence

`avatar-provider.usda`, the fixture of reports 47 and 48:

| CTest | Run | Result |
| --- | --- | --- |
| `toon-viewport-avatar-runtime-late` | hidden, 48 frames, bone gaze, rate 0, reset every 20 | 49 results, 48 late reads each applying a result published after that frame's extraction, none held; 2 resets to generation 3; 2 skins, 6 morphs and 4 material fields bound, no mismatch; after the first frame 47 joint, 47 weight and 25 material writes, no static upload; 1 Hydra sync |
| `toon-viewport-avatar-runtime-thread` | hidden, 120 frames, expression gaze, rate 240, replaced halfway | the replacement binds in epoch 2 with no mismatch; 120 late reads applied, none rejected |

Two local avatars, which are not redistributable and are not committed, run
hidden with vsync on for 600 frames at a 120-per-second producer:

| Avatar | Bound | Results | Late | Writes after the first frame | Expression input to submit | Publication to late read |
| --- | --- | --- | --- | --- | --- | --- |
| `AliciaSolid.usdz`, bone gaze | 20 skins, 46 morphs, no mismatch | 1,243 | 596 of 600 frames | 11,920 joint, 2,089 weight, no static upload | p50 3.385, p95 8.175 ms | p50 2.470, p95 7.173 ms |
| `Seed-san.vrm`, expression gaze, reset every 500 | 21 skins, 48 morphs, no mismatch | 1,234, 2 resets to generation 3 | 596 of 600 frames | 1,788 weight, no static upload | p50 2.002, p95 9.443 ms | p50 1.186, p95 8.663 ms |

Both had 1 Hydra sync. The frame interval was 16.6 ms at p50. A generated
input reached runtime publication in 0.19 ms at p50 on both. The owner's
diagnostics were passed through and counted: `VRM_LOOKAT_WARNING` on every
`AliciaSolid` result, and `VRM_ADAPTER_GAZE_PRECEDENCE`,
`VRM_EXPRESSION_SUPPRESSED` and `VRM_EXPRESSION_WARNING` on `Seed-san`,
where the generated wave also drives the look expressions the gaze owns.

All timings here are generated-source timings: the input is made by the
host, not captured. Submit is measured; present API return is reported for
pose and camera input only, so expression and gaze stop at submit.

## Commands and limits

The configuration is report 47's with `-DTOON_ENABLE_VIEWPORT=ON`:

```powershell
cmake --build build/avatar-viewport
ctest --test-dir build/avatar-viewport   # 61 of 61
# With the providers' DLLs on PATH and vrmSchema and vrmImaging registered:
toon-viewport-avatar --usd <avatar> --avatar-runtime [--avatar-rate 120]
    [--avatar-lookat <prim>] [--avatar-reset-every N]
```

All 61 CTests of that tree pass, the two new ones included.

The threaded fixture test presents 120 frames in well under a second
without vsync, so few results arrive between extraction and the late read
there. The late count is checked in the rate-0 test and was measured on the
avatars. Still open: real motion providers in the composition, captured
source timing, the present-API endpoint for expression and gaze input, and
draw packet and pipeline comparisons with the oracle.
