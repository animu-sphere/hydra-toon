# Live MMD display variance and device-local mesh memory

Date: 2026-10-31. Windows x64, MSVC 19.51, Ninja, `/O2 /Ob2 /DNDEBUG`, OpenUSD
26.08, Vulkan, NVIDIA RTX A5000 on driver 597.16, Ryzen 9 7950X. Continues
[report 116](116-2026-10-31-live-mmd-identity-cost.md), which left its bimodal GPU
scene and CPU submit times unexplained.

Inputs are report 116's private pair, by SHA-256: PMX `b37ed181…`, VMD `737de056…`.
The base is renderer `ef50ef5`, with runtime and physics as in that report. Assets,
instruments and raw logs remain ignored under `build/live-mmd-perf/display/`. A
local patch wrote one line per frame; it is not part of this change. `nvidia-smi`
sampled the performance state, clocks and PCIe generation every 100 ms, so a frame
can be attributed to a state up to about one sample late.

## The variance follows the PCIe link

Every buffer in `MeshCache` was host-visible system memory, read over PCIe by every
vertex in every pass. The driver moves the link between generations with its
performance state. Frames bucketed by the sampled state, before this change:

| State | Graphics MHz | Memory MHz | PCIe | GPU scene p50 ms |
| --- | ---: | ---: | --- | ---: |
| P0 | 1400–1800 | 8001 | Gen 4 | 1.19–1.20 |
| P3 | 1000–1200 | 5001 | Gen 4 | 1.20–1.21 |
| P5 | 600–1000 | 810 | Gen 2 | 4.44–4.47 |

Scene time does not follow graphics clock inside a row. It follows the link: 3.7
times slower for 3.9 times less bandwidth. 21 frames sampled at memory 8001 on Gen 2
measured 4.44 ms, though a transition sampled late could also produce them. Some
frames sampled in P5 measure about 1.2 ms in the same way. The bimodality within a
run and between runs of report 116 is the same effect, with the fraction of time in
each state differing.

## Why CPU submit moved with it

`cpu submit` is almost entirely `vkQueuePresentKHR`; buffer writes take 0.2 ms and
recording and queue submission 0.7 ms. With vsync off, present takes 2.1–2.5 ms on
Gen 4 and 7.5 ms on Gen 2, in a run whose GPU frame has a p50 of 1.8 ms and a p95 of
7.7 ms, so on this driver it tracks the GPU frame. With vsync on it also absorbs the
wait for the display, which is why 5–6 ms there is not cost.

A slow link therefore stalled the render thread. Each stall then owed more 120 Hz
ticks, which is report 116's "each display millisecond costs about two".

## Hidden is not the visible result

Report 116 measured hidden windows only. On unchanged code, a visible window already
held 60 fps for the whole 40-second motion, while a hidden one did not.

## Change

`MeshCache::Upload` now asks for device-local, host-coherent memory (the resizable
BAR heap, 23.68 GiB here) and falls back to the prior allocation if that fails. These
buffers are only written by the CPU, by `memcpy`; nothing reads them back. No shader,
binding or draw changes.

## Result

Each row is one run, alternating before and after in one session; 4x, vsync on,
zero refusals and mismatches, zero bodies after close. Windows are 640x720 hidden, and
1100x900 visible for 2,500 frames, the full motion. Timing is the final 1,024 frames
at most, as the viewport reports it; 600-frame rows use 599.

| Build | Window | Frame p50 / mean / p95 / p99 ms | GPU scene p50 / p95 ms | GPU frame p50 / p95 / p99 ms | Simulated s |
| --- | --- | --- | --- | --- | ---: |
| Before | Hidden | 17.7 / 19.2 / 24.0 / 34.1 | 1.24 / 5.57 | 2.29 / 8.04 / 8.40 | 11.5 |
| Before | Hidden | 17.8 / 19.0 / 24.0 / 33.1 | 4.43 / 5.52 | 6.64 / 7.97 / 8.41 | 11.5 |
| Before | Hidden | 16.7 / 18.3 / 23.5 / 33.3 | 1.21 / 5.65 | 1.85 / 7.92 / 8.43 | 11.0 |
| After | Hidden | 16.7 / 17.1 / 19.2 / 33.4 | 0.98 / 2.56 | 1.68 / 4.00 / 6.75 | 10.3 |
| After | Hidden | 16.7 / 17.0 / 19.4 / 30.3 | 2.18 / 4.70 | 3.56 / 6.10 / 6.77 | 10.3 |
| After | Hidden | 16.7 / 16.9 / 17.7 / 26.1 | 0.97 / 2.44 | 1.60 / 3.77 / 6.32 | 10.2 |
| Before | Visible | 16.7 / 16.7 / 16.7 / 16.8 | 1.25 / 4.54 | 1.91 / 6.84 / 7.94 | 41.8 |
| Before | Visible | 16.7 / 16.7 / 16.7 / 16.8 | 1.24 / 4.53 | 1.89 / 6.79 / 7.94 | 41.7 |
| Before | Visible | 16.7 / 16.7 / 16.7 / 16.8 | 1.25 / 4.55 | 1.91 / 6.82 / 7.90 | 41.7 |
| After | Visible | 16.7 / 16.7 / 16.7 / 17.8 | 1.42 / 2.88 | 2.13 / 4.31 / 4.43 | 41.9 |
| After | Visible | 16.7 / 16.7 / 16.7 / 16.8 | 2.44 / 2.82 | 3.72 / 4.18 / 4.34 | 41.8 |
| After | Visible | 16.7 / 16.7 / 16.7 / 16.8 | 1.45 / 2.86 | 2.16 / 4.27 / 4.40 | 41.8 |

Simulation holds wall time in every row; a run's simulated seconds are its frames at
its frame rate. Tick cost stays 3.9–4.2 ms and result conversion 1.0–1.1 ms. Median
buffer writes are unchanged at 0.21 ms; their p95 in visible runs rises from 0.30–0.31
to 0.63 ms, and the hidden pair that was compared stays at 0.33–0.34 ms.

Scene time on Gen 4 falls from 1.20 to 0.15 ms. Because the frame is now lighter, the
driver spends about half of each visible run in P8, at 200–400 MHz and Gen 1, where
scene p50 is 2.2–2.6 ms. A median can therefore rise while the tail falls, as in the
rows above, and GPU scene remains bimodal, now by clock. Visible scene p95 is under 2.9
ms. In one hidden run p95 stays at 4.70 ms, from frames sampled in P8, so low power
states can still reach the former Gen 2 time.

AliciaSolid, 61,896 skinned triangles, 640x720 hidden, 4x, vsync off, two runs each,
agrees with [report 94](94-2026-10-25-webgpu-acceptance.md#4-performance):

| Build | GPU shadow p50 ms | GPU scene p50 ms | GPU frame p50 ms | CPU submit p50 ms |
| --- | ---: | ---: | ---: | ---: |
| Before | 0.245 | 0.300–0.310 | 0.565–0.577 | 1.11–1.13 |
| After | 0.025–0.026 | 0.069 | 0.113 | 0.65–0.66 |

## An unexplained cost

Visible windows gain margin, not frame rate, and they miss vsync somewhat more often.
Per-frame traces of three full runs each, 7,410 frames per build, 18 ms and 25 ms being
this table's chosen thresholds. The first after run's trace was overwritten by a later
local run, so its row is the second and third after runs above and a fourth, whose
summary matches them (GPU frame p95 4.19 ms, frame p99 16.8 ms):

| Build | Frames over 18 ms | Over 25 ms | 32–35 ms | Longest ms |
| --- | ---: | ---: | ---: | ---: |
| Before | 23 | 4 | 0 | 44.6 |
| After | 39 | 9 | 6 | 33.4 |

The six 33 ms frames spend 21–23 ms in present with about 10 ms of Hydra time. Five were
sampled in P8 or P5 beside a 2–4 ms GPU frame; one, early in its run and sampled in P0,
had a 0.34 ms GPU frame. A low power state is a plausible cause, not an established one;
no run held the clocks high, since that needs a global driver setting. Late frames stay
under 0.6% of each build's total, and all four of the before frames over 25 ms come from
one run. The cost was accepted for the margin and hidden result above, and remains
[in the roadmap](../../roadmap/later.md#not-yet-in-a-milestone).

## Regression evidence

All 87 CTests configured in this tree pass, including GPU parity, resident MMD runtime
and material checks and the generated combined and material comparisons with exact
1x/4x colour and depth.

For the fallback, a temporary edit treated every device-local allocation as failed,
destroying it and allocating again. All 87 tests pass with it, and AliciaSolid returns
to the before figures: shadow 0.243, scene 0.302 and frame 0.566 ms. That exercises the
path, not a real small BAR heap, which this machine does not have. A heap that fills
serves later buffers from system memory, so only part of an avatar gains.

No automated test observes the selected memory type: removing the preference would
fail nothing, and only cost measurement would show it.

## Limits

One GPU, driver and machine. Other vendors, a 256 MiB BAR and a representative
multi-avatar scene were not measured. Only mesh buffers moved; frame, material and
shadow records stay in system memory. The driver's state choice was observed, not
controlled.
