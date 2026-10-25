# GPU handoff stage 3: the Tessera host

> The kitchen's handoff tails in §4 are one event a few seconds into each run, not the handoff's: [report 100](100-2026-10-25-handoff-acceptance.md#3-the-handoff-beside-readback).

- Date: 2026-10-25
- Machine: Windows 11 x86_64, NVIDIA RTX A5000, driver 597.16, a
  3,840 × 2,160 display at 100 % scale; MSVC 14.51 / Visual Studio 18,
  CMake 4.4.3
- Tools: `slangc` 2026.8 from Vulkan SDK 1.4.350.0; the Khronos validation
  layer of Vulkan SDK 1.3.290.0, the registered one; `ost` 0.23.14
- Source: branch `claude/v080-handoff-stage-3` from `main` at `7b0d5f4`;
  Tessera pinned at `c999a27`, its `main` with the change below merged
- Occasion: stage 3 of the [v0.8.0 roadmap](../../roadmap/v0.8.0.md), under
  the [GPU handoff policy](../../design/GPU_HANDOFF_POLICY.md) §9–§11 and the
  [project layout](../../architecture/PROJECT_LAYOUT.md#3-where-new-code-goes)

## TL;DR

**Stage 3 is in. Tessera binds an image as premultiplied (its pull request
[#116](https://github.com/animu-sphere/tessera/pull/116), HOST-Q2), and
`toon-tessera-host`, built by the new `tessera-usd` intent with Tessera
fetched at that commit, draws an avatar through the GPU handoff and a
Tessera panel into one swapchain image in one render pass. Its presented
frames equal the CPU composite of the same frame's product within one 8-bit
sRGB step everywhere outside the panel, 99.2–99.8 % of pixels exactly, on
`shadow-cost`, AliciaSolid and the kitchen with both avatars, through a
resize to a new ring generation, a click on the panel's switch to the
readback path and back, under validation and synchronization validation
with no message. At 1,920 × 1,080 the host's frame takes 0.50 ms instead of
2.91 ms on `shadow-cost`, 0.98 ms instead of 3.55 ms on AliciaSolid and
11.4 ms instead of 14.3 ms on the kitchen, reading back nothing.**

## 1. Tessera's premultiplied images

Tessera's image pipeline sampled straight alpha only, while the product is
premultiplied (policy §4). As HOST-Q2 settled, the change is Tessera's:
`bind_image(handle, view, sampler, ImageAlpha)` and `CpuImage::alpha` take
`straight` (the default) or `premultiplied`. A premultiplied binding draws
through its own image pipelines, whose colour source factor is one, and
tint alpha also scales its RGB, so a tinted image stays premultiplied; the
binding fixes the alpha, so a batch keyed by image handle never mixes the
two. Tessera's scalar CPU backend implements the same rule. Its evidence is
Tessera's own (support matrix, *Premultiplied image alpha evidence*): the
existing GPU-versus-CPU scene keeps its recorded errors exactly, and a
premultiplied texture stays within one sRGB step of the CPU on both of
Tessera's draw paths at four scales.

## 2. The host

`adapters/tessera/` follows the layout #130 placed:

| File | Does |
| --- | --- |
| `src/main.cpp` | Window and Win32 messages; instance, surface and device through `lending_host.hpp` with the surface and swapchain extensions; swapchain, render pass and the frame loop; `--smoke` and `--measure` |
| `src/panel.*` | The panel: a JSON v1 document with the path, extent, sample count, generation, timings and read-back bytes, and a switch; Tessera's styles, layout, paint and hit test; placeholder text |
| `src/compare.*` | The presented image against the CPU composite of the product, and PPM output |

`validation/lending_host.hpp` gained `Presenting`: instance and device
extensions, a call once the instance exists (the host creates its surface
there) and a queue-family filter for presentation. The renderer is told of
none of them and enables none. `adapters/hydra2/tests/stage_scene.hpp` is
the stage through UsdImaging into `hdToon`'s delegate, now shared by the
measurement host.

A frame, with one host frame in flight:

1. Wait for the previous frame's fence, retire it in Tessera, and unbind the
   Tessera handles of an older ring generation.
2. Acquire a swapchain image. On a new extent, unbind every slot's handle
   first: the new generation's `RenderHandoff` may destroy old slots.
3. `RenderHandoff` (or `Render` on the readback path), `Acquire` the frame,
   and bind its slot's view once per slot and generation as a premultiplied
   Tessera image with a nearest sampler: the product is drawn at its own
   extent, so each pixel reads its texel.
4. In one render pass cleared to the background, Tessera draws the product
   as one full-window `DrawImage`, then the panel's draw list. On the
   readback path, the `ColorProduct` is first copied into a host image of
   the product's format.
5. Submit, waiting for the acquired image at colour output and the frame's
   timeline value at the fragment stage, signalling the host's own timeline
   value; `Release` the frame with that value; present.

The renderer's background is transparent, so its product is the foreground
alone, composited by Tessera over the host's clear colour.

## 3. Frames against the reference composite

`--smoke` turns on validation and synchronization validation, renders, and
for four frames requests a capture with the frame (`RequestCapture` and
`TakeCapture` on the handoff path, the `ColorProduct` itself on readback),
reads the presented swapchain image back, and compares it with the CPU
composite of the product over the clear colour: source-over of
premultiplied linear RGB, encoded to sRGB and rounded, as the swapchain's
sRGB attachment stores it. The panel's border box, one pixel wider on each
side, is left out. Between the frames the window is resized, and a click
posted through the window procedure at a pixel Tessera's hit test puts on
the switch selects readback, then handoff again. Report 98 showed the
reference consumer equal to the same composite byte for byte; this host
blends in Tessera's pipeline, in float, before the sRGB encode, so one step
is its limit.

| Stage | Frame | Extent | Generation | Pixels compared | Exact | Largest difference |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| `shadow-cost` | handoff | 1,280 × 720 | 1 | 860,478 | 858,967 | 1 |
| | handoff, resized | 960 × 540 | 2 | 457,278 | 456,372 | 1 |
| | readback | 960 × 540 | 2 | 457,278 | 456,372 | 1 |
| | handoff again | 960 × 540 | 2 | 457,278 | 456,372 | 1 |
| AliciaSolid | handoff | 1,280 × 720 | 1 | 860,478 | 857,593 | 1 |
| | handoff, resized | 960 × 540 | 2 | 457,278 | 455,516 | 1 |
| | readback | 960 × 540 | 2 | 457,278 | 455,516 | 1 |
| | handoff again | 960 × 540 | 2 | 457,278 | 455,516 | 1 |
| Kitchen_set with both avatars | handoff | 1,280 × 720 | 1 | 856,635 | 851,189 | 1 |
| | handoff, resized | 960 × 540 | 2 | 455,997 | 452,495 | 1 |
| | readback | 960 × 540 | 2 | 455,997 | 452,495 | 1 |
| | handoff again | 960 × 540 | 2 | 455,997 | 452,495 | 1 |

4x MSAA, `B8G8R8A8_SRGB`, scale 1. Every run reports no validation message,
15 frames handed off, six slots created and three destroyed (the first
generation's after the resize) and two clicks taken. The handoff and
readback frames of one extent agree pixel for pixel in the counts, as they
should: the same product drawn by the same pipeline. The committed
`skinning.usda` runs the same smoke at 640 × 360 as the CTest
`toon-tessera-host-smoke`. The table's runs used Tessera at `e06fb01`; the
pinned merge commit has the same tree, and AliciaSolid's and the kitchen's
smokes rerun on it pass the same way, with the kitchen's counts moving by a
few hundred pixels as the panel's timing text changes width, and by one
exact pixel.

## 4. The handoff beside readback in the host

`--measure` runs without the validation layer and presents with
`IMMEDIATE`, so the frame loop is not paced by the display (with FIFO every
frame takes 16.66 ms and the GPU clocks fall between frames). Readback
first, then handoff, on one renderer; 60 warmup frames and 300 measured;
p50 / p95 / p99 in milliseconds. *Host frame* is the whole loop iteration:
the fence wait, acquire, render call, panel, recording, submit and present.

| Stage | Samples | Path | Host frame | Render call | GPU frame | Bytes read back |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| `shadow-cost` | 1x | readback | 2.91 / 3.78 / 4.08 | 1.63 / 2.29 / 2.57 | 0.76 / 0.96 / 1.45 | 16,588,800 |
| | | handoff | 0.50 / 0.66 / 0.97 | 0.06 / 0.07 / 0.08 | 0.13 / 0.13 / 0.13 | 0 |
| | 4x | readback | 2.96 / 3.98 / 4.22 | 1.68 / 2.60 / 2.87 | 0.82 / 1.30 / 1.61 | 16,588,800 |
| | | handoff | 0.53 / 0.75 / 1.13 | 0.07 / 0.07 / 0.08 | 0.18 / 0.18 / 0.19 | 0 |
| AliciaSolid | 1x | readback | 3.55 / 4.34 / 4.83 | 2.22 / 2.84 / 2.95 | 1.19 / 1.66 / 1.80 | 16,588,800 |
| | | handoff | 0.98 / 1.41 / 1.62 | 0.26 / 0.28 / 0.29 | 0.56 / 0.81 / 1.19 | 0 |
| | 4x | readback | 3.64 / 4.49 / 4.89 | 2.32 / 3.02 / 3.33 | 1.27 / 1.73 / 1.89 | 16,588,800 |
| | | handoff | 1.08 / 1.57 / 1.72 | 0.27 / 0.28 / 0.33 | 0.64 / 0.90 / 1.28 | 0 |
| Kitchen_set with both avatars | 1x | readback | 14.32 / 16.32 / 19.98 | 12.63 / 14.37 / 18.11 | 8.20 / 9.34 / 13.29 | 16,588,800 |
| | | handoff | 11.41 / 13.99 / 20.71 | 3.55 / 4.17 / 9.76 | 7.49 / 10.96 / 13.52 | 0 |
| | 4x | readback | 14.86 / 16.49 / 19.07 | 13.14 / 14.50 / 17.44 | 8.42 / 9.22 / 13.06 | 16,588,800 |
| | | handoff | 11.14 / 15.33 / 25.88 | 3.18 / 4.36 / 13.32 | 7.66 / 11.66 / 13.23 | 0 |

All at 1,920 × 1,080. The GPU frame is the renderer's own, as report 98's;
Tessera's pass is not in it.

- **The host feels what §8 measured.** The render call and the GPU frame
  match report 98's measurement host on the same stages within the run's
  variance, and the host's frame follows: on the two small stages it is
  the render call, Tessera's few draws and presentation, five times shorter
  than with readback. On the kitchen the GPU frame dominates either way, so
  the handoff saves the 2.9 ms readback wait and copy, not more.
- **Kitchen's tails** again show in p95/p99 on both paths, as in report 98.

## 5. Found on the way

- **A raw string.** The panel's JSON ended its raw string literal early at
  `(Space)"`; it uses a named delimiter.
- **The capture barrier.** The smoke's copy of the presented image first
  named colour-attachment writes at the transfer stage, which validation
  rejects; the render pass's dependency into transfer already makes them
  available, so the layout barrier names no source access.
- **A crowded Tessera fixture.** In Tessera, the premultiplied images first
  joined the existing mixed GPU-versus-CPU scene and overlapped its rotated
  and glyph edges, one or two steps past its tolerance at bright pixels;
  they have a scene of their own (Tessera's evidence).

## 6. Checks

| Tree | CTest |
| --- | --- |
| `lookdev--tessera-usd` | 40/40, `toon-tessera-host-smoke` and `toon-handoff-measure-gpu`, now on the shared `stage_scene.hpp`, among them; `toon-renderer-install-tree` passes and the install manifest names no Tessera file |

Tessera's own Debug and Release suites (67 checks each, the native menu
smoke excepted) pass on its branch; its CI runs on the pull request.

## 7. Next

Stage 4, acceptance: the policy's [evidence](../../design/GPU_HANDOFF_POLICY.md#10-evidence)
in one report, then the v0.8.0 release.
