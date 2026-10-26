# GPU handoff stage 4: acceptance

- Date: 2026-10-25
- Machine: Windows 11 x86_64, NVIDIA RTX A5000, driver 597.16, a
  3,840 × 2,160 display at 100 % scale; MSVC 14.51 / Visual Studio 18,
  CMake 4.4.3
- Tools: `slangc` 2026.8 from Vulkan SDK 1.4.350.0; the Khronos validation
  layer of Vulkan SDK 1.3.290.0, the registered one; `ost` 0.23.14
- Source: branch `claude/v080-handoff-stage-4` from `main` at `5875d73`;
  Tessera pinned at `c999a27`
- Occasion: stage 4 of the [v0.8.0 roadmap](../../releases/v0.8.0.md), the
  [evidence](../../design/GPU_HANDOFF_POLICY.md#10-evidence) of the GPU
  handoff policy

## TL;DR

**Stage 4 is in, and the milestone's done-when condition holds on this
machine, every piece of evidence rerun from one commit. The reference
consumer equals the CPU composite of the readback product byte for byte for
both RGBA fixtures at 1x and 4x through capture, two consumers of one frame,
a full ring, a GPU wait for an unreached release, a resize and a recreated
device, and now also on rings of two and four slots, with validation and
synchronization validation silent. The Tessera host's presented frames, at
1x as well as 4x, are within one sRGB step of the composite of the product
the consumers sample, 99.2–99.8 % of pixels exactly, and look right. Beside
readback, over 300 frames, the handoff removes every byte read back and the
CPU wait: AliciaSolid at 3,840 × 2,160 is ready 0.91 ms after the call
starts instead of 6.36 ms, and the GPU frame falls rather than rises. The
kitchen's GPU-frame tails of reports 98 and 99 are not the handoff's: one
event a few seconds into each run slows the CPU and GPU together for about
0.15 s and lands in whichever series is running then.**

## 1. The evidence and where it is

| Policy §10 asks for | Here |
| --- | --- |
| The reference consumer's exact comparisons over the RGBA fixtures | §2 |
| Resize, holding, several consumers, capture, device loss (by a destroyed and recreated device) | §2 |
| Validation output | §2, §4, §5 |
| The §8 measurements | §3 |
| The Tessera host's frames inspected and compared with the reference consumer's | §4 |

The four trees were built with `ost build` from the branch, whose only code
change is the ring-size check below. The runs cover `shadow-cost.usda`,
AliciaSolid (`alicia_VRMA_01.usda`) and the local Kitchen_set holding
AliciaSolid and YYB Miku, as in reports 97–99.

## 2. The reference consumer

`toon-handoff-test` (CTest `toon-handoff-gpu`) on its own lent device, with
validation and synchronization validation on its instance:

| Check | 1x | 4x |
| --- | --- | --- |
| Lent device's products equal an owned device's, both fixtures | equal | equal |
| Consumer composites over black, white and a checkerboard equal the CPU composite of the readback product, both fixtures, 256 × 256 | equal | equal |
| §6 descriptor fields; no bytes read back by handoff frames | complete; 0 | complete; 0 |
| Capture equals the readback product and what the consumer sampled; one per request | equal | equal |
| Two consumers of one frame; held until the second release; a third release refused | exact | exact |
| Three slots held; the next call `NoFreeSlot`, draws nothing, keeps the latest frame; the next frame after releases goes into a released slot | exact | exact |
| A release value reached only after the next frame into its slot is submitted; that frame waits on the GPU | 1 wait, exact | 1 wait, exact |
| Resize to 320 × 180: generation 2, three slots retired; a held generation-1 frame stays exact; the three destroyed once released | exact | exact |
| Rings of two and four slots: that many acquisitions fill the ring, the next call draws nothing, the released slot takes the next frame, exact | **new**, exact | **new**, exact |
| Rings of one and five slots refused | refused | refused |

After both sample counts the host destroys its device, which reports
nothing left on it, then creates a second device and renderer, which hand
off an exact frame: the recovery policy §7 prescribes for a lost device,
whose loss cannot be forced. Validation reports no message on either
device.

The ring-size check is new in this stage. Report 98 refused rings of one
and five but drove only the default of three, while HOST-Q1 lets a host ask
for two to four; the check runs the full-ring path on the other two sizes.

## 3. The handoff beside readback

`toon-handoff-measure` lends its own device without validation (policy §8)
and renders each configuration on one handoff renderer, readback first,
then handoff; a handoff frame is acquired and released by a consumer that
reads nothing, and the host waits for its value on the CPU only to time
when it is ready. 300 measured frames each; p50 / p95 / p99 in
milliseconds; vsync does not apply, nothing is presented.

| Stage | Extent | Samples | Path | Render call | GPU frame | Producer to ready | Bytes read back |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: |
| `shadow-cost` | 1,920 × 1,080 | 1x | readback | 1.57 / 1.73 / 2.07 | 0.75 / 0.77 / 1.04 | 1.57 / 1.73 / 2.07 | 16,588,800 |
| | | | handoff | 0.04 / 0.05 / 0.09 | 0.12 / 0.12 / 0.12 | 0.19 / 0.21 / 0.27 | 0 |
| | 3,840 × 2,160 | 1x | readback | 5.86 / 6.51 / 6.79 | 2.85 / 3.37 / 3.45 | 5.86 / 6.51 / 6.79 | 66,355,200 |
| | | | handoff | 0.04 / 0.07 / 0.07 | 0.34 / 0.35 / 0.35 | 0.42 / 0.45 / 0.79 | 0 |
| | 1,920 × 1,080 | 4x | readback | 1.60 / 1.74 / 2.11 | 0.80 / 0.80 / 1.07 | 1.60 / 1.74 / 2.11 | 16,588,800 |
| | | | handoff | 0.04 / 0.06 / 0.09 | 0.17 / 0.17 / 0.17 | 0.24 / 0.27 / 0.35 | 0 |
| | 3,840 × 2,160 | 4x | readback | 6.15 / 6.81 / 7.01 | 3.02 / 3.56 / 3.69 | 6.15 / 6.81 / 7.01 | 66,355,200 |
| | | | handoff | 0.04 / 0.07 / 0.09 | 0.51 / 0.52 / 0.93 | 0.60 / 0.65 / 1.04 | 0 |
| AliciaSolid | 1,920 × 1,080 | 1x | readback | 2.12 / 2.53 / 2.75 | 1.20 / 1.47 / 1.62 | 2.12 / 2.53 / 2.75 | 16,588,800 |
| | | | handoff | 0.15 / 0.18 / 0.26 | 0.56 / 0.58 / 0.97 | 0.76 / 0.82 / 1.20 | 0 |
| | 3,840 × 2,160 | 1x | readback | 6.36 / 7.00 / 7.18 | 3.24 / 3.77 / 3.91 | 6.36 / 7.00 / 7.18 | 66,355,200 |
| | | | handoff | 0.15 / 0.18 / 0.26 | 0.71 / 0.72 / 1.12 | 0.91 / 0.97 / 1.33 | 0 |
| | 1,920 × 1,080 | 4x | readback | 2.23 / 2.65 / 2.96 | 1.27 / 1.54 / 1.68 | 2.23 / 2.65 / 2.96 | 16,588,800 |
| | | | handoff | 0.16 / 0.19 / 0.35 | 0.63 / 1.03 / 1.05 | 0.83 / 1.42 / 1.68 | 0 |
| | 3,840 × 2,160 | 4x | readback | 6.59 / 6.76 / 6.98 | 3.43 / 3.45 / 3.58 | 6.59 / 6.76 / 6.98 | 66,355,200 |
| | | | handoff | 0.16 / 0.19 / 0.32 | 0.91 / 0.92 / 0.92 | 1.11 / 1.15 / 1.28 | 0 |
| Kitchen_set with both avatars | 1,920 × 1,080 | 1x | readback | 10.71 / 14.02 / 29.19 | 7.95 / 8.39 / 19.91 | 10.71 / 14.02 / 29.19 | 16,588,800 |
| | | | handoff | 1.85 / 2.64 / 3.70 | 7.16 / 7.45 / 7.72 | 9.24 / 10.11 / 11.48 | 0 |
| | 3,840 × 2,160 | 1x | readback | 15.22 / 16.05 / 16.43 | 9.95 / 10.10 / 10.38 | 15.22 / 16.05 / 16.43 | 66,355,200 |
| | | | handoff | 1.80 / 2.30 / 2.56 | 7.27 / 7.57 / 7.60 | 9.27 / 9.79 / 10.26 | 0 |
| | 1,920 × 1,080 | 4x | readback | 11.53 / 12.72 / 13.24 | 7.89 / 8.76 / 9.31 | 11.53 / 12.72 / 13.24 | 16,588,800 |
| | | | handoff | 1.86 / 2.72 / 3.99 | 7.24 / 7.86 / 8.17 | 9.49 / 10.31 / 11.46 | 0 |
| | 3,840 × 2,160 | 4x | readback | 15.95 / 18.97 / 30.49 | 10.60 / 10.89 / 11.00 | 15.95 / 18.97 / 30.49 | 66,355,200 |
| | | | handoff | 1.82 / 2.55 / 3.29 | 7.53 / 8.14 / 8.26 | 9.77 / 10.49 / 11.08 | 0 |

The small stages warm up for 60 frames, the kitchen for 400 (below).

- **Policy §8 holds.** Every handoff series reads nothing back, and its
  render call is the CPU's draw preparation, not a wait: 0.04–0.16 ms on
  the small stages and 1.8 ms for the kitchen's 1,811 draws. The GPU frame
  falls instead of rising by the ring's barriers, since readback frames
  also copy colour and depth into host memory: by 0.6 ms at 1,920 × 1,080
  and 2.5 ms at 3,840 × 2,160 on the small stages, by 0.7–3.1 ms on the
  kitchen. Against report 98, the handoff's render call and GPU frame p50s
  agree within 0.03 ms on the small stages; readback's render call differs
  by up to 0.3 ms there and 1.7 ms on the kitchen.
- **The kitchen's tails.** With the 60 warmup frames of the other stages,
  the kitchen's first handoff series, 1,920 × 1,080 at 1x, reached
  13.3 / 17.1 ms at p95 / p99 of GPU frame, and the Tessera host's handoff
  runs did the same (as in report 99), while readback stayed below 9 ms.
  Four repeats put the tail in that series every time. Frame by frame, it
  is one burst of about fourteen consecutive frames, about 0.15 s, in which
  the GPU frame rises to 12–15 ms and the render call, normally 1.8 ms, to
  11–14 ms; CPU and GPU slow together, which nothing in the handoff path
  does. With 400 warmup frames the handoff series start after the burst,
  which falls in the first readback series instead (its GPU p99 of
  19.9 ms above, and of the host's readback runs in §5), and every handoff
  series is clean. It is an event a few seconds into each process, of a
  cause not identified here (the driver's background work is a
  candidate); report 98's tails, on either path, fit the same event. The
  last readback series' render-call p99 of 30.5 ms, with a steady GPU
  frame, is a CPU-side stall of its own. The p50s were unaffected
  throughout.

## 4. The Tessera host's frames

`toon-tessera-host --smoke` (report 99 §3) at 1,280 × 720, now at 1x as
well as 4x: validation and synchronization validation on; four frames
captured as they are handed off, the capture being byte-identical to what
consumers sample (§2), and the presented swapchain image compared with the
CPU composite of that product over the host's clear colour, outside the
panel; a resize to 960 × 540 between the first two, then a click on the
panel's switch to readback and another back.

| Stage | Samples | Frame | Extent | Generation | Pixels compared | Exact | Largest difference |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: |
| `shadow-cost` | 1x | handoff | 1,280 × 720 | 1 | 860,478 | 858,987 | 1 |
| | | handoff, resized | 960 × 540 | 2 | 457,278 | 456,408 | 1 |
| | | readback | 960 × 540 | 2 | 457,278 | 456,408 | 1 |
| | | handoff again | 960 × 540 | 2 | 457,278 | 456,408 | 1 |
| | 4x | handoff | 1,280 × 720 | 1 | 860,478 | 858,967 | 1 |
| | | handoff, resized | 960 × 540 | 2 | 457,278 | 456,372 | 1 |
| | | readback | 960 × 540 | 2 | 457,278 | 456,372 | 1 |
| | | handoff again | 960 × 540 | 2 | 457,278 | 456,372 | 1 |
| AliciaSolid | 1x | handoff | 1,280 × 720 | 1 | 860,478 | 858,519 | 1 |
| | | handoff, resized | 960 × 540 | 2 | 457,278 | 456,115 | 1 |
| | | readback | 960 × 540 | 2 | 457,278 | 456,115 | 1 |
| | | handoff again | 960 × 540 | 2 | 457,278 | 456,115 | 1 |
| | 4x | handoff | 1,280 × 720 | 1 | 860,478 | 857,593 | 1 |
| | | handoff, resized | 960 × 540 | 2 | 457,278 | 455,516 | 1 |
| | | readback | 960 × 540 | 2 | 457,278 | 455,516 | 1 |
| | | handoff again | 960 × 540 | 2 | 457,278 | 455,516 | 1 |
| Kitchen_set with both avatars | 1x | handoff | 1,280 × 720 | 1 | 857,916 | 852,724 | 1 |
| | | handoff, resized | 960 × 540 | 2 | 454,716 | 451,886 | 1 |
| | | readback | 960 × 540 | 2 | 454,716 | 451,886 | 1 |
| | | handoff again | 960 × 540 | 2 | 454,716 | 451,886 | 1 |
| | 4x | handoff | 1,280 × 720 | 1 | 857,916 | 852,470 | 1 |
| | | handoff, resized | 960 × 540 | 2 | 454,716 | 451,214 | 1 |
| | | readback | 960 × 540 | 2 | 454,716 | 451,214 | 1 |
| | | handoff again | 960 × 540 | 2 | 455,997 | 452,495 | 1 |

`B8G8R8A8_SRGB`, scale 1. Every run reports no validation message, 15
frames handed off, six slots created and three destroyed and two clicks
taken. The 4x rows repeat report 99's counts exactly on `shadow-cost` and
AliciaSolid; the kitchen's move by a few hundred pixels with the width of
the panel's timing text, as report 99 noted.

**Against the reference consumer.** The reference consumer composites into
an 8-bit linear target and equals the CPU composite byte for byte (§2); the
host blends in Tessera's pipeline, in float, and its sRGB attachment
encodes, so the CPU composite here is source-over in float encoded to sRGB
and rounded, and one step is the host's limit. Both are measured against
the composite of the same product, the bytes every consumer of a frame
samples.

**Inspected.** The presented frames show what the stages hold: the
`shadow-cost` casters and their shadows on the receiver, AliciaSolid's
toon shading and outlines, both avatars in the kitchen; the panel's path,
extent, sample count and generation follow the resize and the clicks. At
pixel scale the avatar's silhouette over the host's blue-grey clear has no
dark or light fringe, which a premultiplied product drawn as straight
alpha would leave, and 1x frames show the aliasing 4x removes. The panel's
read-back figure on a captured handoff frame is the capture's colour copy,
four bytes per pixel; a readback frame's adds depth, eight in all.

## 5. The Tessera host's timings

`--measure` without validation, presenting with `IMMEDIATE` at
1,920 × 1,080; readback first, then handoff, on one renderer; 300 measured
frames, the warmup as in §3; p50 / p95 / p99 in milliseconds. *Host frame*
is the whole loop iteration.

| Stage | Samples | Path | Host frame | Render call | GPU frame | Bytes read back |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| `shadow-cost` | 1x | readback | 2.81 / 4.92 / 6.77 | 1.59 / 2.49 / 4.11 | 0.77 / 1.16 / 1.47 | 16,588,800 |
| | | handoff | 0.51 / 0.62 / 0.81 | 0.06 / 0.07 / 0.07 | 0.13 / 0.13 / 0.13 | 0 |
| | 4x | readback | 2.89 / 3.54 / 3.86 | 1.65 / 2.19 / 2.48 | 0.82 / 1.25 / 1.52 | 16,588,800 |
| | | handoff | 0.53 / 0.68 / 1.00 | 0.06 / 0.07 / 0.09 | 0.18 / 0.18 / 0.19 | 0 |
| AliciaSolid | 1x | readback | 3.37 / 3.81 / 4.23 | 2.14 / 2.55 / 2.90 | 1.20 / 1.41 / 1.63 | 16,588,800 |
| | | handoff | 0.98 / 1.27 / 1.56 | 0.18 / 0.19 / 0.22 | 0.63 / 0.68 / 1.08 | 0 |
| | 4x | readback | 3.50 / 3.93 / 4.10 | 2.27 / 2.70 / 2.83 | 1.29 / 1.64 / 1.73 | 16,588,800 |
| | | handoff | 1.01 / 1.25 / 1.67 | 0.18 / 0.19 / 0.26 | 0.67 / 0.69 / 1.07 | 0 |
| Kitchen_set with both avatars | 1x | readback | 12.54 / 14.40 / 32.25 | 11.15 / 12.71 / 28.73 | 7.95 / 8.67 / 19.77 | 16,588,800 |
| | | handoff | 9.68 / 10.79 / 12.00 | 1.93 / 2.59 / 3.35 | 7.45 / 8.03 / 8.09 | 0 |
| | 4x | readback | 12.40 / 13.51 / 32.88 | 11.08 / 11.92 / 29.77 | 7.87 / 8.59 / 19.86 | 16,588,800 |
| | | handoff | 9.70 / 10.44 / 11.55 | 1.90 / 2.40 / 3.13 | 7.23 / 7.98 / 8.13 | 0 |

The host's frame is 3.4–5.5 times shorter with the handoff on the small
stages and 2.7–2.9 ms shorter on the kitchen, as in report 99; with 400 warmup
frames the kitchen's handoff tails are gone and the burst sits in readback.

## 6. Checks

| Tree | CTest |
| --- | --- |
| `core` | 24/24, `toon-handoff-gpu` with the ring sizes among them |
| `lookdev--hydra` | 39/39, `toon-handoff-measure-gpu` among them |
| `lookdev--tessera-usd` | 40/40, `toon-tessera-host-smoke` among them |
| `core--webgpu` | 53/53; the WebGPU backend keeps readback |

## 7. Done when, and next

The [roadmap's condition](../../releases/v0.8.0.md#acceptance) holds: one
foreground product reaches an intended host, the Tessera host, without CPU
readback; the reference consumer matches the readback composite exactly
through resize, held slots, several consumers and capture; the Tessera
host's frames are checked against the composite it matches; and readback
is measured beside the handoff. Next is the v0.8.0 release.
