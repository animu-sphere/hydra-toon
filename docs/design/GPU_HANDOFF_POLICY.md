---
status: accepted
owner: hydra-toon
---

# GPU handoff policy

> The v0.8.0 host-integration RFC, accepted on 2026-10-25. It realizes the
> logical contract of [RENDER_OUTPUT_POLICY.md §7](RENDER_OUTPUT_POLICY.md#7-gpu-texture-handoff)
> for one integration, an in-process Vulkan image handoff on a device the
> host owns, and wins over it on that integration. What the product contains
> stays with the render output policy (§1–§5 there); frames and
> synchronization follow [DESIGN_POLICY.md §17–§19](DESIGN_POLICY.md#17-vulkan-frame-architecture)
> and its [KPIs](DESIGN_POLICY.md#23-performance-kpis); placement follows
> [PROJECT_LAYOUT.md](../architecture/PROJECT_LAYOUT.md); what the host owns
> follows [INTEGRATION_SCOPE_POLICY.md](INTEGRATION_SCOPE_POLICY.md).
> Implementation status belongs to the
> [capability matrix](../reference/CAPABILITY_MATRIX.md); the milestone's
> acceptance is recorded in the [v0.8.0 release record](../releases/v0.8.0.md).
> Stage 1, the lent device and the readback baseline, is
> [report 97](../reports/renderer/97-2026-10-25-handoff-stage-1.md); stage 2,
> the ring and the reference consumer,
> [report 98](../reports/renderer/98-2026-10-25-handoff-stage-2.md); stage 3,
> the Tessera host, [report 99](../reports/renderer/99-2026-10-25-handoff-stage-3.md);
> and stage 4, the §10 evidence,
> [report 100](../reports/renderer/100-2026-10-25-handoff-acceptance.md).

## 1. Purpose and decisions

The renderer's reusable RGBA product reaches the CPU today wherever it
leaves the renderer: the offscreen renderer reads it back into a
`ColorProduct`, and `hdToon` copies that into its `HdRenderBuffer`. Only the
standalone viewport consumes it on the GPU, inside the renderer. This policy
lets a host application sample the product on the GPU without per-frame CPU
readback, keeping readback as an explicit, measured fallback. Three
decisions were taken by the project on 2026-10-25 and shape the rest:

- **Same-device Vulkan image.** The handoff is in-process: the renderer draws
  on a Vulkan device the host created and lends it, and gives the host the
  product image, its view and a timeline-semaphore value to wait for, with
  every field §7 requires. No external memory, shared handle or second API
  is involved (§2–§6).
- **Tessera is the first consumer.** Tessera's optional Vulkan module already
  borrows a host device and samples a host `VkImageView` in
  `SHADER_READ_ONLY_OPTIMAL` ([Tessera rendering](https://github.com/animu-sphere/tessera/blob/main/docs/architecture/rendering.md#implemented-vulkan-primitive-boundary)).
  A host that draws an avatar and Tessera UI in one window is the shape a
  Mimikuri application, one of Tessera's intended consumers, would take; its
  own requirements are not yet available. A reference consumer in this repository
  carries the exact acceptance; the Tessera host demonstrates it (§9).
- **Vulkan only.** The WebGPU backend keeps CPU readback; a WebGPU handoff,
  cross-process sharing and Hgi interop are later integrations built on the
  same logical contract (§12).

## 2. Device ownership

The host owns the Vulkan instance, physical device, logical device and
queue, and their lifetime. It lends them to the renderer when creating it;
the renderer owns everything it creates on that device — command pool,
pipelines, caches, shadow and product images, its timeline semaphore — and
destroys them before the host may destroy the device.

- **Requirements.** The renderer publishes what it needs before the host
  creates the device: Vulkan 1.3, a graphics queue family, the scene
  features `SupportsSceneFeatures` checks today (timeline semaphores, dynamic
  rendering, Synchronization2, dynamic sampled-image indexing among them),
  `alphaToOne` as optional, and the colour/depth/shadow formats. A host
  device missing one is refused with the missing item named, as an owned
  device is today. Vulkan cannot report which features a created device
  enabled, so the host states them when it lends the device; the renderer
  checks that statement and the physical device's support. The renderer
  enables no extension the host did not.
- **Queue.** The host gives one queue and its family. The renderer submits
  to it only inside its own render call, on the caller's thread; the host
  does not submit to the same queue concurrently, which Vulkan's external
  queue synchronization requires. A host that wants concurrency lends a
  second queue of the same family.
- **Owned devices stay.** `CreateOffscreenRenderer` and the present session
  keep creating their own device; a borrowed device is an additional entry
  point, and the scene code below it is the same.

## 3. Public boundary

Public headers carry no Vulkan types, as
[`PresentSurfaceProvider`](../../include/toon/vulkan_present.hpp) already
does: handles travel encoded in `std::uintptr_t` and enumerations as their
numeric values. The handoff entry point and its descriptor live in a new
public header of the Vulkan backend, `include/toon/vulkan_handoff.hpp`,
added to PROJECT_LAYOUT before code. The core library and the Hydra adapter
do not see it. The boundary check keeps `vulkan.h` out of every public
header.

## 4. Product images

- **Contents.** The handed-off image is the reusable product of
  [render output policy §1–§5](RENDER_OUTPUT_POLICY.md#1-product-and-consumers):
  linear sRGB stored in `R8G8B8A8_UNORM`, linear transfer, premultiplied
  alpha, single-sampled (the resolve target at 4x), origin at the top left.
  It is byte-identical to the `ColorProduct` the same frame reads back. No
  display encoding, unpremultiplication or flip happens on the producer.
- **Ring.** The renderer keeps a small ring of product slots, each an image
  with `COLOR_ATTACHMENT`, `SAMPLED` and `TRANSFER_SRC` usage and one view.
  A frame renders into a slot no consumer holds; the depth used to draw it
  stays internal. The ring has three slots unless the host asks for two to
  four when it creates the renderer (HOST-Q1, §11).
- **Layout.** A handed-off slot is in `SHADER_READ_ONLY_OPTIMAL`, made so by
  the frame's own commands. Consumers sample it in that layout and must not
  transition it. The renderer moves it back to an attachment layout only
  after it is released (§5).
- **Queue family.** Producer and consumers use the same queue family in
  v0.8.0; no queue-family ownership transfer is defined. A host lending
  queues of different families is refused.

## 5. Synchronization and ownership

- **Readiness.** The renderer's existing timeline semaphore carries every
  frame's completion: frame N signals value N. The descriptor names the
  semaphore and N; a consumer waits for N before the stage that samples the
  image. The CPU waits for nothing of frame N (design policy §19); as every
  frame does, the next render call waits for frame N before reusing what it
  wrote, which is the one frame in flight below.
- **Release.** A consumer releases a slot by giving the renderer its own
  timeline semaphore and the value it signals once its last read of that
  slot has executed. The renderer makes its next use of the slot wait for
  that value on the GPU, at the colour output stage where its transition
  is; a value already reached when a render call starts is forgotten, and
  so the consumer's semaphore must stay valid only until then, or until the
  renderer is destroyed, whose teardown waits for values not yet reached.
  Several consumers each release; the slot returns to the ring only when
  every consumer that acquired it has. A consumer that did not read the
  slot releases it without a semaphore.
- **Holding.** A consumer may hold the latest slot across several producer
  frames. When every slot is held, the next render call does not overwrite
  one: it reports that no slot is free and draws nothing, so the host
  decides between waiting and releasing. The renderer never blocks on a
  consumer's CPU.
- **Frames in flight.** One frame in flight stays the low-latency default
  (design policy §18); the ring exists for consumers' reads, not for more
  producer frames.

## 6. Identity, timing and interpretation

Every handoff descriptor carries:

| Field | Value |
| --- | --- |
| Resource | Encoded `VkImage` and `VkImageView`, extent, `VkFormat`, slot index and ring generation |
| Interpretation | Colour space linear sRGB, transfer linear, alpha premultiplied, origin top left |
| Readiness | Encoded timeline `VkSemaphore` and the value N to wait for |
| State | `SHADER_READ_ONLY_OPTIMAL`, the queue family index |
| Identity | Frame number N, the render world's scene revision, the late sample applied, if any |
| Timing | Producer steady-clock nanoseconds at render start and submit, in the clock [DESIGN_POLICY §23](DESIGN_POLICY.md#23-performance-kpis) names; the GPU time, when timestamps exist, is known only once the frame completes, so the renderer's statistics carry it with its frame number after the next render call |

Consumers needing straight alpha or display encoding convert at their own
boundary ([render output policy §5](RENDER_OUTPUT_POLICY.md#5-hydra-and-viewport-boundaries)).

## 7. Resize, device loss, consumers and capture

- **Resize.** A render call at a new extent starts a new ring generation.
  Slots of the old generation are destroyed only after their release; the
  descriptor's generation tells a consumer its view has changed.
- **Device loss.** A render call that observes `VK_ERROR_DEVICE_LOST`
  reports it as its own status and hands nothing off. The renderer does not
  recover: the host destroys it, recreates its device and a new renderer,
  and re-acquires. Scene resources are rebuilt from the render world.
- **Several consumers.** Any number of consumers on the same device may
  acquire the same slot; each releases independently (§5). Consumers on
  another device or process are outside v0.8.0 (§12).
- **Capture.** The explicit readback stays: a capture request makes the
  next handoff frame copy its slot on its way to consumers, and taking the
  capture waits for that frame and returns a `ColorProduct` byte-identical
  to what consumers sample. Screenshots and the CPU fallback use it;
  ordinary handoff frames read nothing back.

## 8. Readback fallback and measurement

The CPU product path remains supported and is the fallback where a host
cannot lend a device or a backend cannot hand off. Its cost is measured, not
assumed: at 1,920 × 1,080 and 3,840 × 2,160, 1x and 4x, on a canonical scene
and an avatar, both paths report CPU render-call time, GPU frame time, bytes
read back and producer-to-ready latency as p50/p95/p99, beside each other.
The handoff must remove the readback bytes and the CPU wait for completion
and must not raise GPU frame time beyond the measured cost of the ring's
barriers.

The measurement host lends its own device, without the validation layer, so
both paths draw on the same device and the layer's cost is not counted as
the renderer's. The fallback is measured as it should run: its readback
buffers are in host-cached memory, which the CPU reads at memory speed
rather than through uncached system memory.

## 9. Consumers

- **Reference consumer.** A test host in this repository creates the
  device, lends it, acquires each frame's slot, waits, samples it into its
  own target composited source-over onto a background, releases, and reads
  that target back. Its image equals the CPU composite of the readback
  product over the same background exactly, at 1x and 4x, through resize,
  several consumers, held slots and capture, with Vulkan validation and
  synchronization validation clean.
- **Tessera host.** A Win32 example host draws an avatar through the
  handoff and Tessera UI through `tessera::vulkan` into one swapchain image,
  binding the product with `bind_image` as a premultiplied image and
  retiring it with Tessera's frames. It is an example in this repository
  behind an opt-in intent with a pinned Tessera (HOST-Q2 and HOST-Q3, §11).
- **Not consumers here.** `hdToon` in `usdview` keeps CPU readback: the
  canonical `lookdev` runtime ships HgiGL only, so a zero-copy Hydra path is
  Vulkan-to-OpenGL interop, a later integration (§12). The standalone
  viewport keeps compositing inside the renderer.

## 10. Evidence

Acceptance records, in renderer reports, the reference consumer's exact
comparisons over the [RGBA fixtures](RENDER_OUTPUT_POLICY.md#6-evidence-boundary-and-additional-outputs),
the resize/hold/several-consumer/capture/device-loss behaviours (device
loss by a destroyed-and-recreated device, as forcing a real loss is not
reproducible), validation output, the §8 measurements, and the Tessera
host's frames inspected and compared with the reference consumer's.

## 11. Open questions

| ID | Question | Proposal |
| --- | --- | --- |
| HOST-Q1 | Ring depth: two slots (one rendering, one held) or three (a consumer may hold one while another is pending), and whether a host may choose | Settled 2026-10-25: three slots by default, so two consumers holding different slots leave one to render into; a host may ask for two to four when it creates the renderer, two suiting a single consumer that wants the memory back |
| HOST-Q2 | How premultiplied alpha meets Tessera's straight-alpha image pipeline: a per-image premultiplied flag in Tessera, or a conversion pass in the Tessera host | Settled 2026-10-25: Tessera gains a per-image premultiplied option on `bind_image`, blending such an image as premultiplied, through a change in Tessera's repository. The host binds the product as it is, without a conversion pass whose unpremultiplication would lose 8-bit precision at low alpha and bleed at edges, so its frames stay comparable with the reference consumer |
| HOST-Q3 | Where the Tessera host lives: an example in this repository behind an opt-in intent with a pinned Tessera, or an example in Tessera's repository consuming a published `toon` package | Settled 2026-10-25: an example in this repository behind its own opt-in intent, with Tessera pinned to a commit. The handoff API ships first in v0.8.0, so a host consuming a published `toon` package could not be part of this milestone's acceptance |

## 12. Outside this policy

A WebGPU handoff; cross-process or cross-API sharing (Vulkan external memory
to Direct3D 11 shared handles, Spout, OpenGL memory objects); Hgi interop and
a zero-copy `hdToon` AOV; queue-family ownership transfer; depth, normal or
identity AOV handoff; consumers on other devices; Linux. Each becomes its own
integration when a consumer asks for it, reusing §4–§7's meanings.
