# The viewport's sample count changes while it runs: four pipelines and two targets are rebuilt, nothing is uploaded, and the image is the one a fresh start at that count draws

> Followed by [report 22](22-2026-09-30-viewport-telemetry.md): an ordinary frame is timed now, on the CPU and the GPU, where §4 found no frame timing.

- Date: 2026-09-30
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.14`; the canonical CY2026 `lookdev` runtime (OpenUSD
  26.08); `vrmImaging` 0.10.0 by its published digest through
  `ost renderer viewport --with`
  ([`ost` report 08](../ost/08-2026-09-30-v0.23.14-report-07-reverified.md))
- Occasion: the first item of
  [v0.2.0](../../roadmap/v0.2.0.md#viewport-foundation)'s viewport
  foundation, the MSAA sample count switched while the viewport runs, so
  [report 16](16-2026-09-28-msaa.md)'s baseline can be judged on one
  avatar, one camera and one window

## TL;DR

**`PresentSession::SetSamples` changes the count from the next frame on,
and the viewport's number keys 1, 2, 4 and 8 call it. A change waits for
the frame in flight and rebuilds the four scene pipelines and the
multisampled colour and depth targets. The set layouts, samplers,
descriptor sets and every mesh, material, texture and skin on the GPU
stay.** On the avatar, a change in either direction between 1, 2, 4 and 8
samples uploads nothing and recreates no swapchain. The frame after it is
the image a viewport started at that count draws, pixel for pixel. The
frame that makes the change takes 4.4 to 7.0 ms on the CPU.

## 1. What changed

- **Backend.** The scene pipelines' creation is split in two. The set
  layouts and the immutable samplers are made once per device, and the four
  pipelines (`mesh`, `mtoon_opaque`, `mtoon_transparent`, `mtoon_outline`)
  are made for a sample count over them
  (`CreateScenePipelineObjects`, `DestroyScenePipelineObjects`). Descriptor
  sets are allocated from the layouts, not the pipelines, so the material
  set with its texture table and every skin set survive another count.
- **`PresentSession::SetSamples(requested)`** records what was asked. At
  the start of the next `RenderFrame`, the device's highest count at or
  below it is chosen, as at creation (`ChooseSampleCount`). If that count
  differs from the pipelines', the session waits for the one frame in
  flight, rebuilds the pipelines, and recreates the depth image and, above 1
  sample, the transient colour samples at the swapchain's extent. The
  swapchain and its images are not touched. The session keeps the
  shaders' SPIR-V for this.
- **`PresentStatistics`** gains `sample_changes`, one per rebuild, and the
  upload counters `OffscreenStatistics` already has: topology, points,
  material slot writes, textures, skins and poses.
- **`RenderOptions`** remains fixed for an offscreen renderer's life. The
  Hydra adapter still makes another renderer for another
  `toon:msaaSamples`, which uploads the scene again.
- **`toon-viewport`.** The keys 1, 2, 4 and 8 ask for that many samples.
  The title shows the count (`4x MSAA`), a change prints
  `Samples: <n> per pixel, in a <t> ms frame`, and the last line counts
  `sample changes`. `--switch-samples N` asks for N halfway through
  `--frames`, as a key press would. The run then fails unless the count
  landed at or below N after at most one change, with no upload added since
  the request. The last frame's uploads are printed as `Uploads:`.
- **CTest `toon-viewport-samples`:** the bootstrap scene, `--samples 4
  --switch-samples 1` over 8 hidden frames.

## 2. What was run

```sh
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd        # 14 of 14
# on the avatar, for (from, to) in (4,1) (4,2) (1,4) (4,8):
ost renderer viewport --intent hydra --profile lookdev --with sha256:894fd616… -- \
    --usd <avatar.usdz> --hidden --frames 16 --vsync off \
    --samples <from> --switch-samples <to> --screenshot switch-<from>-<to>.ppm
# and for n in 1 2 4 8, without a switch:
ost renderer viewport … -- --usd <avatar.usdz> --hidden --frames 16 --vsync off \
    --samples <n> --screenshot fresh-<n>.ppm
# the frame time of a change, three runs each of 4→1, 1→4, 4→8, 8→4
```

The avatar is the converted AliciaSolid of the earlier reports, which is
not in the repository. The switch happens on the 9th of 16 frames, and the
screenshot is the 16th.

## 3. Results

Every avatar run drew 20 of 20 draws MToon, 15 of them outlined and 5
transparent. Vulkan validation reported nothing.

| Switch | Samples after | Sample changes | Swapchain recreates | Uploads after the request |
| --- | --- | --- | --- | --- |
| 4 → 1 | 1 | 1 | 0 | 0 |
| 4 → 2 | 2 | 1 | 0 | 0 |
| 1 → 4 | 4 | 1 | 0 | 0 |
| 4 → 8 | 8 | 1 | 0 | 0 |

Every run ended with the uploads of its first frame and nothing more:
topology 20, points 20, material slots 13, textures 7, skins 20, poses 20.

The last frame of each switched run against a run started at the same
count:

| Switched | Fresh | Pixels that differ |
| --- | --- | --- |
| 4 → 1 | 1 | 0 |
| 4 → 2 | 2 | 0 |
| 1 → 4 | 4 | 0 |
| 4 → 8 | 8 | 0 |

For scale, the fresh 1x and 4x frames differ in 10,779 pixels, the edges of
[report 16](16-2026-09-28-msaa.md). That report's count, 8,331, was
measured in `usdview`'s framing.

The CPU time of `RenderFrame` in the frame that makes the change, which
includes that frame's ordinary work:

| Switch | Three runs, ms |
| --- | --- |
| 4 → 1 | 4.70, 4.35, 4.50 |
| 1 → 4 | 4.53, 5.00, 5.12 |
| 4 → 8 | 6.98, 6.90, 6.95 |
| 8 → 4 | 5.99, 6.15, 6.03 |

## 4. Observations

- **The rebuild is cheap because the driver has seen the pipelines.** Each
  run starts at one count and builds the other count's pipelines once. The
  NVIDIA driver's own shader cache is warm from earlier runs, so these
  times are not those of a cold first build.
- **An ordinary frame's time is not measured yet.** The viewport has no
  frame timing; that is the next item of the viewport foundation. So the
  table does not separate the rebuild from the frame around it.
- **Waiting is allowed here.** A change waits for the frame in flight, as a
  swapchain recreation does, which is not an ordinary frame
  ([design policy §19](../../design/DESIGN_POLICY.md#19-cpu--gpu-synchronization)).

## 5. Not checked

The key presses themselves in a visible window, beyond the same
`SetSamples` call that `--switch-samples` makes. A device whose highest
count is below 8, where 8 lands on that count. A count changed during a
resize. Linux.
