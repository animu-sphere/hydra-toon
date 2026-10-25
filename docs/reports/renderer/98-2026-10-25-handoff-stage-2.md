# GPU handoff stage 2: the product ring and the reference consumer

- Date: 2026-10-25
- Machine: Windows 11 x86_64, NVIDIA RTX A5000, driver 597.16; MSVC 14.51 /
  Visual Studio 18, CMake 4.4.3
- Tools: `slangc` from Vulkan SDK 1.4.350.0; the Khronos validation layer
  of Vulkan SDK 1.3.290.0, the registered one; `ost` 0.23.14
- Source: branch `claude/v080-handoff-stage-2` from `main` at `33760fd`
- Occasion: stage 2 of the [v0.8.0 roadmap](../../roadmap/v0.8.0.md), under
  the [GPU handoff policy](../../design/GPU_HANDOFF_POLICY.md) §4–§9

## TL;DR

**Stage 2 is in. A renderer on a lent device hands its RGBA product off on
the GPU: `RenderHandoff` draws into a slot of a ring no consumer holds and
hands it off in `SHADER_READ_ONLY_OPTIMAL` with the policy's descriptor,
reading nothing back; consumers acquire the latest frame and release it with
their own timeline semaphore and value, which the slot's next frame waits
for on the GPU. The reference consumer, on the lent device and queue,
samples the frames with the backend's composite shaders and equals the CPU
composite of the readback product byte for byte, for both RGBA fixtures at
1x and 4x, through capture, two consumers of one frame, a full ring, a GPU
wait for an unreached release, a resize and a recreated device, with
validation and synchronization validation clean. Beside readback, a frame
of AliciaSolid at 3,840 × 2,160 is ready 0.94 ms after the render call
starts instead of 6.57 ms, the call takes 0.18 ms instead of 6.57 ms, and
the GPU frame drops from 3.23 ms to 0.71 ms without the copies to host
memory.**

## 1. The interface

`<toon/vulkan_handoff.hpp>` gained, beside stage 1's lent device:

| Declaration | Contents |
| --- | --- |
| `HandoffOptions` | `slots`, two to four, three by default (HOST-Q1) |
| `HandoffImage` | The §6 descriptor: encoded image and view, extent, format, slot, generation; the interpretation strings as `ColorProduct` spells them; the timeline semaphore and value; layout and queue family; frame, scene revision (`DrawList::source_revision`), late sample and input times; render start and submit |
| `HandoffStatus` | `Rendered`, `NoFreeSlot`, `DeviceLost`, `Failed` |
| `HandoffRenderer` | An `OffscreenRenderer` with `RenderHandoff`, `Acquire`, `Release`, `RequestCapture`, `TakeCapture` and `HandoffStatistics` |
| `CreateHandoffRenderer` | Stage 1's checks, and the ring size refused outside two to four |

A frame's GPU time is known only after it completes, which a handoff call
does not wait for, so it is not in the descriptor: the next render call
reads the frame's timestamps, and `OffscreenStatistics::gpu_frame` names the
frame `gpu_frame_ms` belongs to. The policy's §6 says so now.

## 2. The renderer

`Render` split into what both paths share — `Prepare` (the previous frame,
targets, uploads, the late sample), `Record` (the shadow and scene passes
into a given colour target) and `Submit` (after the previous frame and any
release waits) — and what follows the pass: the readback's copies, or the
slot's transition to `SHADER_READ_ONLY_OPTIMAL`, through a copy when a
capture was requested. A handoff frame draws its colour into the slot,
resolved there at 4x, and keeps its depth internal.

The ring (`backend/vulkan/handoff_ring.hpp`, `vulkan_handoff.cpp`) keeps the
current generation's slots and the retired ones:

- **Choosing a slot.** A slot no consumer holds and not the latest frame's,
  drawn longest ago; the latest frame's own slot only when nothing else is
  free; none when all are held, and the call reports `NoFreeSlot`.
- **Release.** Each release lowers the slot's holders and adds the
  consumer's semaphore and value, one per semaphore at the highest value,
  waited for at the colour output stage, where the slot's transition is.
  Each render call first forgets every value already reached, so a
  consumer's semaphore is not touched after that; teardown waits for those
  not reached. `Release` documents the lifetime.
- **Generations.** A new extent creates a new set of slots and retires the
  old; a retired slot is destroyed at a render call once released, its
  values reached and not the latest frame's.
- **Capture.** The next handoff frame copies its slot into a capture buffer
  in host-cached memory before the transition; `TakeCapture` waits for that
  frame.
- **Device loss.** `VK_ERROR_DEVICE_LOST` from a submit or a wait marks the
  renderer lost; every later frame reports `DeviceLost`.

## 3. The reference consumer

`toon-handoff-gpu`'s consumer creates its own timeline semaphore, command
buffer, descriptor set and pipeline from the backend's `composite` SPIR-V,
the viewport's fullscreen source-over, and an RGBA8 target it copies to the
CPU. Each composite waits for the frame's value at the fragment stage and
for its own previous composite, and signals its timeline once its read and
copy have executed; that value is what it releases with. Over black, white
and a black-and-white checkerboard every composited value is an exact
multiple of 1/255, so the CPU composite of the readback product predicts it
byte for byte.

On a ring of three, for 1x and 4x:

| Check | Result |
| --- | --- |
| Avatar and `mmd-shadow` fixtures, transparent clear, three backgrounds, 256 × 256 | Every composite equal to the readback product's; descriptor fields as §6 states; no bytes read back |
| GPU time | A handed-off frame's time reported by the next call, under its frame number |
| Capture | Byte-identical to the readback product and to what the consumer sampled; one capture per request |
| Two consumers of one frame | Both composites exact; the slot held until the second release; a third release refused |
| Full ring | Three acquisitions hold three slots; the next call is `NoFreeSlot`, draws nothing and keeps the latest frame; the held frame is unchanged; after releases the next frame goes into a released slot |
| Unreached release | Released with a host semaphore's value 1 before it is signalled; the frame drawn next into that slot waits for it on the GPU (one release wait counted); the host signals after the submit; the frame is exact |
| Resize | 320 × 180 starts generation 2 with three slots retired; a held generation-1 frame still composites exactly; once released, the next call destroys the three |
| Ring size | One and five slots refused |
| Recreated device | A second host device and renderer hand off an exact frame |

Validation and synchronization validation report nothing, and destroying
each host device reports nothing left on it.

## 4. What validation found on the way

- **Transitions after a handoff frame.** A readback frame ends its depth
  target with a copy; a handoff frame leaves it after the depth tests. The
  next frame's transition from `UNDEFINED` had no source stage, so it was
  not chained to the wait for the previous frame, and synchronization
  validation reported write-after-write. The depth transitions now follow
  the tests, the resolve and the copy, the colour transitions the colour
  output and the copy.
- **A consumer gone before the renderer.** The first run destroyed the
  consumers, then the renderer, whose teardown waited on their destroyed
  semaphores and crashed. Reached values are now forgotten at each render
  call, the lifetime is in `Release`'s contract, and the test's consumers
  outlive the renderer.
- **The consumer's own readback.** Its copy into the same buffer each
  composite was reported against the previous one despite the wait for its
  own timeline; a copy-to-copy barrier before it orders them on the queue.
- **The GPU wait untested.** With consumers reading back before the next
  render call, every release value was reached and forgotten, so no frame
  waited on the GPU; the host-signalled release above exercises it.

## 5. The handoff beside readback

`toon-handoff-measure` now renders each configuration on one handoff
renderer, readback first, then handoff. A handoff frame is acquired and
released by a consumer that reads nothing; the host waits for its value on
the CPU only to time when it is ready. Sixty warmup frames, 120 measured;
p50 / p95 / p99 in milliseconds. The stages and session are report 97's.

| Stage | Extent | Samples | Path | Render call | GPU frame | Producer to ready | Bytes read back |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: |
| `shadow-cost` | 1,920 × 1,080 | 1x | readback | 1.64 / 1.95 / 2.54 | 0.77 / 0.77 / 0.95 | 1.64 / 1.95 / 2.54 | 16,588,800 |
| | | | handoff | 0.04 / 0.07 / 0.12 | 0.14 / 0.14 / 0.14 | 0.22 / 0.25 / 0.30 | 0 |
| | 3,840 × 2,160 | 1x | readback | 6.03 / 7.11 / 7.62 | 2.85 / 3.61 / 3.88 | 6.03 / 7.11 / 7.62 | 66,355,200 |
| | | | handoff | 0.04 / 0.06 / 0.08 | 0.34 / 0.35 / 0.35 | 0.42 / 0.45 / 0.47 | 0 |
| | 1,920 × 1,080 | 4x | readback | 1.63 / 1.78 / 1.97 | 0.80 / 0.80 / 0.94 | 1.63 / 1.78 / 1.97 | 16,588,800 |
| | | | handoff | 0.05 / 0.08 / 0.14 | 0.17 / 0.17 / 0.17 | 0.26 / 0.30 / 0.35 | 0 |
| | 3,840 × 2,160 | 4x | readback | 6.20 / 6.68 / 6.88 | 3.02 / 3.22 / 3.44 | 6.20 / 6.68 / 6.88 | 66,355,200 |
| | | | handoff | 0.05 / 0.07 / 0.08 | 0.51 / 0.52 / 0.81 | 0.61 / 0.64 / 0.93 | 0 |
| AliciaSolid | 1,920 × 1,080 | 1x | readback | 2.21 / 2.41 / 3.81 | 1.19 / 1.24 / 1.49 | 2.21 / 2.41 / 3.81 | 16,588,800 |
| | | | handoff | 0.17 / 0.21 / 0.31 | 0.56 / 0.57 / 0.58 | 0.78 / 0.82 / 0.92 | 0 |
| | 3,840 × 2,160 | 1x | readback | 6.57 / 6.98 / 7.13 | 3.23 / 3.38 / 3.64 | 6.57 / 6.98 / 7.13 | 66,355,200 |
| | | | handoff | 0.18 / 0.20 / 0.32 | 0.71 / 0.72 / 0.97 | 0.94 / 1.07 / 1.65 | 0 |
| | 1,920 × 1,080 | 4x | readback | 2.45 / 3.68 / 3.87 | 1.28 / 2.12 / 2.25 | 2.45 / 3.68 / 3.87 | 16,588,800 |
| | | | handoff | 0.19 / 0.20 / 0.33 | 0.63 / 0.64 / 0.94 | 0.86 / 0.91 / 1.28 | 0 |
| | 3,840 × 2,160 | 4x | readback | 6.89 / 7.48 / 8.34 | 3.43 / 3.82 / 3.98 | 6.89 / 7.48 / 8.34 | 66,355,200 |
| | | | handoff | 0.18 / 0.20 / 0.21 | 0.91 / 0.91 / 0.92 | 1.13 / 1.16 / 1.28 | 0 |
| Kitchen_set with both avatars | 1,920 × 1,080 | 1x | readback | 11.41 / 20.41 / 21.16 | 8.05 / 16.73 / 17.17 | 11.41 / 20.41 / 21.16 | 16,588,800 |
| | | | handoff | 2.17 / 2.68 / 2.94 | 7.17 / 7.49 / 7.85 | 9.41 / 10.24 / 10.54 | 0 |
| | 3,840 × 2,160 | 1x | readback | 16.90 / 41.88 / 49.73 | 10.58 / 21.73 / 22.70 | 16.90 / 41.88 / 49.73 | 66,355,200 |
| | | | handoff | 2.15 / 2.98 / 3.30 | 7.21 / 11.42 / 12.07 | 9.55 / 14.66 / 17.43 | 0 |
| | 1,920 × 1,080 | 4x | readback | 11.07 / 11.88 / 12.22 | 7.85 / 8.28 / 8.81 | 11.07 / 11.88 / 12.22 | 16,588,800 |
| | | | handoff | 2.11 / 3.30 / 8.31 | 7.24 / 14.76 / 15.23 | 9.76 / 17.41 / 19.74 | 0 |
| | 3,840 × 2,160 | 4x | readback | 16.46 / 30.98 / 61.69 | 9.98 / 16.32 / 21.18 | 16.46 / 30.98 / 61.69 | 66,355,200 |
| | | | handoff | 2.15 / 2.77 / 3.00 | 7.43 / 7.73 / 8.29 | 9.69 / 10.39 / 11.01 | 0 |

- **Policy §8 holds.** The handoff reads nothing back and its render call
  waits for nothing of its own frame: the call is the CPU's draw
  preparation, 0.04–0.19 ms here and 2.1 ms for the kitchen's 1,811 draws.
  The GPU frame does not rise beyond the ring's barriers; it falls, since
  readback frames also copy colour and depth into host memory: by 0.6 ms at
  1,920 × 1,080 and 2.5 ms at 3,840 × 2,160 on the two small scenes.
- **Ready sooner.** Producer to ready is the call plus the GPU frame and a
  CPU wake-up of about 0.05 ms; against readback it saves the wait, the
  copies and the 16.6 or 66.4 MB memcpy.
- **Kitchen's tails.** Several kitchen series have p95/p99 two or three
  times their p50 in GPU frame time, on either path and either sample
  count, while the others do not; one run of 120 frames on a desktop shows
  that variance, not something either path does. The p50s are steady.

## 6. Checks

| Tree | CTest |
| --- | --- |
| `core` | 24/24, `toon-handoff-gpu` with the reference consumer among them |
| `lookdev--hydra` | 39/39, `toon-handoff-measure-gpu` now timing both paths |
| `core--webgpu` | 53/53; the WebGPU backend only shares the statistics' new field |

## 7. Next

Stage 3: Tessera's per-image premultiplied option in its own repository,
the pinned Tessera and the opt-in intent here, and a Win32 host drawing an
avatar through the handoff and Tessera UI in one swapchain image, checked
against the reference consumer, with §8's measurements beside readback.
