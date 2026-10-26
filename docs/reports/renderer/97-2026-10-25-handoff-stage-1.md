# GPU handoff stage 1: the lent device and the readback baseline

- Date: 2026-10-25
- Machine: Windows 11 x86_64, NVIDIA RTX A5000, driver 597.16; MSVC 14.51 /
  Visual Studio 18, CMake 4.4.3
- Tools: `slangc` from Vulkan SDK 1.4.350.0; the Khronos validation layer
  of Vulkan SDK 1.3.290.0, the registered one; `ost` 0.23.14
- Source: branch `claude/v080-handoff-stage-1` from `main` at `df77e39`
- Occasion: stage 1 of the [v0.8.0 roadmap](../../releases/v0.8.0.md), under
  the [GPU handoff policy](../../design/GPU_HANDOFF_POLICY.md) and the
  placement of [PROJECT_LAYOUT](../../architecture/PROJECT_LAYOUT.md)
  (PR #126)

## TL;DR

**Stage 1 is in. `<toon/vulkan_handoff.hpp>` publishes the device
requirements and creates the offscreen renderer on a device the host lends;
a device lacking a requirement is refused with the item named. A lent and
an owned device initialize the same renderer, which never destroys a lent
device. A test host that creates its device from the requirements finds both
RGBA fixtures' colour and depth byte-identical to an owned device's over
three backgrounds, a resize and back, at 1x and 4x, with validation and
synchronization validation clean and nothing left on the device when it is
destroyed. Synchronization validation, on for the first time, found a real
hazard in texture mip generation and two cross-frame reports, both fixed.
Measuring the readback baseline found that the renderer read its products
from uncached memory: a 1,920 × 1,080 render call of a 130-triangle scene
took 73 ms. Read from host-cached memory it takes 1.6 ms, which `hdToon`
gains too. The baseline on a canonical scene, AliciaSolid and a kitchen with
two avatars is recorded below.**

## 1. The public boundary

`include/toon/vulkan_handoff.hpp` includes `vulkan_backend.hpp` and adds
three things, with handles as `std::uintptr_t` and enumerations as numbers,
as `vulkan_present.hpp` does; it joined the core-boundary check's list.

| Declaration | Contents |
| --- | --- |
| `VulkanHandoffRequirements()` | `api_version` `VK_API_VERSION_1_3`; `queue_flags` graphics; required `timelineSemaphore`, `dynamicRendering`, `synchronization2`, `shaderSampledImageArrayDynamicIndexing`; optional `alphaToOne`; colour `R8G8B8A8_UNORM`, depth and shadow `D32_SFLOAT`; no device extension |
| `LentVulkanDevice` | Encoded instance, physical device, device and queue; the queue family; the instance's `apiVersion`; the features the host enabled |
| `CreateOffscreenRenderer(LentVulkanDevice, shaders, …)` | The offscreen renderer on that device, or `Fail` with what it lacks |

Vulkan cannot report which features a created device enabled, so the host
states them, in the same `VulkanDeviceFeatures` structure the requirements
use. The renderer checks the statement, the instance version, the physical
device's support (the same `SupportsSceneFeatures` an owned device passes,
which names the missing feature), the product formats and that the queue's
family draws. `alphaToOne` is used only when the host enabled it and the
device has it. The policy's §2 now says so.

## 2. One renderer, two devices

`VulkanOffscreenRenderer::Initialize` received a device instead of creating
one. Creating the instance and choosing and creating the device moved to
`CreateDevice`, which only `CreateOffscreenRenderer` without a device calls;
`backend/vulkan/vulkan_handoff.cpp` checks a lent device and calls the same
`Initialize` through `CreateLentOffscreenRenderer` in the private
`vulkan_scene.hpp`. Command pool, timeline semaphore, pipelines, caches,
targets and readback buffers are the renderer's either way.

Teardown differs in two places. On a lent device the renderer waits for its
own last frame on its timeline rather than `vkDeviceWaitIdle`, since the
host's other queues are not its to wait for, and it does not destroy the
device. A lent device's validation messages go to the host's instance, so
the renderer's statistics say that instead of counting them.

`OffscreenStatistics` gained what §8 asks of the renderer: `gpu_timing` and
`gpu_frame_ms`, from two timestamps around each frame's commands, read after
the CPU's wait for the frame, and `readback_bytes`. The WebGPU backend sets
`readback_bytes` too, from its 256-byte copy pitch.

## 3. Lent equals owned

`toon-handoff-gpu` (`validation/handoff_test.cpp`) creates its instance with
the validation layer, synchronization validation enabled through
`VkValidationFeaturesEXT`, and its own debug messenger counting what the
renderer's own instance counts. It picks the first device with a graphics
queue and the required features, creates it from the requirements with
`alphaToOne` where offered, and lends it.

- **Refusals.** Lent devices claiming no `synchronization2`; no
  `timelineSemaphore` and no dynamic indexing; a 1.2 instance; a queue
  family past the last; and no queue are each refused with `Fail` and the
  item named.
- **Comparisons.** For 1x and 4x, an owned and a lent renderer on the same
  GPU (vendor, device and driver compared) render the avatar and
  `mmd-shadow` RGBA fixtures of [render output policy §6](../../design/RENDER_OUTPUT_POLICY.md#6-evidence-boundary-and-additional-outputs),
  now shared through `validation/rgba_fixture_scenes.hpp`, at 256 × 256,
  320 × 180 and 256 × 256 again, each over transparent, black and white
  clears. All 36 colour and depth products are byte-identical, with the
  same interpretation metadata, 8 bytes per pixel read back and a measured
  GPU time. Pipelines created, texture and topology uploads and target
  allocations are equal.
- **Teardown.** With every renderer gone the host destroys the device; the
  layer reports no object left on it, and the messenger counts no message.

The owned renderer's instance has the validation layer too and counts no
message.

## 4. What synchronization validation found

Neither renderer instance had enabled synchronization validation before.
On the lending host it reported three hazards.

- **Mip generation (fixed, real).** `TextureCache::RecordUploads` moves
  every level of a new texture from `UNDEFINED` to `TRANSFER_DST_OPTIMAL`
  with the copy stage as destination, then copies level 0 and blits each
  level from the one above. The blits write at the blit stage, which the
  transition was not ordered before: a write-after-write hazard on levels
  1 and below. The transition's destination is now copy and blit.
- **Across frames (fixed, by an explicit wait).** Frame N's readback copies
  are ordered only before the host's reads; frame N+1 transitions the same
  images from `UNDEFINED` with no source stage and copies into the same
  buffers. The renderer waits for frame N on the CPU before recording N+1,
  which orders them, but this layer reported write-after-read on the
  transition and write-after-write on the copies: it did not take the host's
  timeline wait as completing frame N. Each submission now also waits on
  the GPU for the previous frame's value, already signalled, which orders
  the frames on the queue itself. A queue a host shares wants that order
  stated anyway, and the next stage's release semaphores join the same
  wait.

With both changes the run is clean at 1x and 4x.

## 5. The readback baseline

`toon-handoff-measure` (`adapters/hydra2/tests/handoff_measure.cpp`) opens a
stage through UsdImaging and `hdToon`'s delegate, syncs it once, frames the
committed draw list with the viewport's orbit camera for the stage's up
axis, and renders on a device it lends itself, without the validation layer
(policy §8 now says so). Each configuration renders warmup frames, then
120 measured ones, recording the render call's CPU time, the GPU frame time,
the bytes read back, and the latency from the call's start to the product
being ready. On the readback path the product is ready when the call
returns, so that latency equals the call's time; `submit_to_ready_ms`, from
the submit inside the call to its return, isolates the wait and the copy.

### 5.1 Uncached readback memory

The first run, ten warmup frames, read:

| Stage | Extent | Samples | CPU call p50 | GPU frame p50 | Submit to ready p50 |
| --- | --- | --- | ---: | ---: | ---: |
| `shadow-cost` | 1,920 × 1,080 | 1x | 73.04 ms | 0.78 ms | 72.89 ms |
| `shadow-cost` | 3,840 × 2,160 | 1x | 296.84 ms | 6.21 ms | 296.63 ms |
| AliciaSolid | 1,920 × 1,080 | 1x | 74.80 ms | 1.22 ms | 74.48 ms |
| kitchen | 1,920 × 1,080 | 1x | 103.88 ms | 28.51 ms | 100.88 ms |

Nearly the whole call followed the submit, at about 230 MB/s whatever the
scene. The readback buffers were allocated as every host buffer is,
host-visible with coherent preferred, which on this driver is uncached
system memory: right for what the CPU writes, slow for what it reads.
`CreateHostBuffer` takes the preferred properties now, and the colour and
depth readback buffers ask for host-cached and coherent memory. The 1x
runs above are four of the twelve; every one was 73–340 ms.

### 5.2 The baseline

With cached readback memory and sixty warmup frames (§5.3), p50 / p95 / p99
in milliseconds; 16,588,800 bytes read back per frame at 1,920 × 1,080 and
66,355,200 at 3,840 × 2,160, colour and depth:

| Stage | Extent | Samples | CPU render call | GPU frame | Submit to ready |
| --- | --- | --- | ---: | ---: | ---: |
| `shadow-cost` (17 draws, 130 triangles) | 1,920 × 1,080 | 1x | 1.60 / 1.93 / 2.23 | 0.77 / 0.95 / 1.06 | 1.54 / 1.84 / 2.14 |
| | 3,840 × 2,160 | 1x | 6.00 / 6.46 / 6.70 | 2.85 / 3.26 / 3.29 | 5.91 / 6.39 / 6.54 |
| | 1,920 × 1,080 | 4x | 1.66 / 2.13 / 2.25 | 0.80 / 0.96 / 1.06 | 1.60 / 2.02 / 2.13 |
| | 3,840 × 2,160 | 4x | 6.28 / 6.76 / 7.34 | 3.02 / 3.30 / 3.42 | 6.16 / 6.66 / 7.16 |
| AliciaSolid `alicia_VRMA_01.usda` (21 draws, 32,306 triangles) | 1,920 × 1,080 | 1x | 2.18 / 2.47 / 2.77 | 1.20 / 1.34 / 1.64 | 2.00 / 2.27 / 2.59 |
| | 3,840 × 2,160 | 1x | 6.63 / 7.17 / 7.60 | 3.23 / 3.62 / 3.91 | 6.37 / 6.86 / 7.21 |
| | 1,920 × 1,080 | 4x | 2.30 / 2.80 / 2.91 | 1.27 / 1.56 / 1.75 | 2.10 / 2.58 / 2.71 |
| | 3,840 × 2,160 | 4x | 6.88 / 7.27 / 7.70 | 3.44 / 3.81 / 3.98 | 6.63 / 7.04 / 7.40 |
| Kitchen_set with AliciaSolid and YYB Miku (1,811 draws, 799,386 triangles) | 1,920 × 1,080 | 1x | 11.39 / 12.08 / 12.71 | 7.84 / 8.48 / 8.67 | 8.83 / 9.32 / 9.54 |
| | 3,840 × 2,160 | 1x | 16.18 / 17.23 / 17.93 | 10.05 / 10.65 / 10.88 | 13.20 / 14.00 / 14.33 |
| | 1,920 × 1,080 | 4x | 11.13 / 11.79 / 11.87 | 7.88 / 8.54 / 8.70 | 8.82 / 9.35 / 9.55 |
| | 3,840 × 2,160 | 4x | 16.35 / 17.15 / 18.04 | 10.17 / 10.85 / 10.96 | 13.35 / 13.99 / 14.64 |

What the handoff should remove is visible: beyond the GPU frame, the
readback path spends about 0.8 ms at 1,920 × 1,080 and 3 ms at
3,840 × 2,160 waiting for the frame and copying 16.6 or 66.4 MB, and the
GPU frame itself includes the copies into host memory. The kitchen's CPU
time before the submit, about 2.5 ms, is draw preparation the handoff does
not change. Every stage ran in the kitchen-and-avatars session's plugins
(VRM and MMD); AliciaSolid and the kitchen are local data.

### 5.3 GPU clocks

The first cached run used ten warmup frames, and the kitchen's first
configuration measured 12.8 ms of GPU frame at 1x against 7.9 ms at 4x.
Run again with 4x first and sixty warmup frames, both took 7.8–7.9 ms:
the GPU had not finished raising its clocks. The baseline above used sixty
warmup frames throughout, and the building guide says to keep the warmup
long. The uncached run's GPU times are inflated the same way, by frames
spaced 73–340 ms apart.

## 6. Checks

| Tree | CTest |
| --- | --- |
| `core` | 24/24, `toon-handoff-gpu` and `toon-renderer-core-boundary` with the new header among them |
| `lookdev--hydra` | 39/39, `toon-handoff-measure-gpu` (`skinning.usda`, 320 × 180, 1x and 4x, four frames) among them |
| `core--webgpu` | 53/53 |

The RGBA fixture capture (`toon-rgba-fixtures-gpu`) now includes the shared
scenes header and passes unchanged.

## 7. Next

Stage 2 adds the product ring of policy §4, the §6 descriptor, readiness and
release semaphores, held slots, resize generations, several consumers,
capture and the device-loss status, and the reference consumer's exact
comparisons, to `toon-handoff-test`; `toon-handoff-measure` gains the
handoff path beside this baseline.
