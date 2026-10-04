# The viewport times its frames and shows it: CPU and GPU time per part, draw and upload counts on a Dear ImGui overlay that a screenshot leaves out, 0.04 ms of CPU and 0.01 ms of GPU a frame

- Date: 2026-09-30
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.14`; the canonical CY2026 `lookdev` runtime (OpenUSD
  26.08); `vrmImaging` 0.10.0 by its published digest through
  `ost renderer viewport --with`; Dear ImGui 1.92.8 at
  `8936b58fe26e8c3da834b8f60b06511d537b4c63`, the revision `hydra-merlin`'s
  viewport pins
- Occasion: the next item of
  [v0.2.0](../../releases/v0.2.0.md)'s viewport foundation: frame time, CPU
  and GPU timing and renderer statistics on screen
  ([design policy §24](../../design/DESIGN_POLICY.md#24-profiling),
  [§31](../../design/DESIGN_POLICY.md#31-evaluation-hosts))

## TL;DR

**Every frame the present session writes GPU timestamps between the parts
of the frame, and the viewport times its own steps: Hydra sync, extraction,
the overlay, and the present session's wait and submit. It keeps the last
1,024 frames and shows mean, p95 and max on a Dear ImGui overlay, with the
frame interval's plot, the draw calls of each part of the scene pass and the
uploads. ImGui stays in the viewport. The viewport turns ImGui's draw data
into a plain `OverlayDrawList`, and the backend draws that in a pass of its
own after any capture is copied.** On the avatar at 4x MSAA with vsync off,
a frame's GPU work is 0.132 ms and the frame interval 0.687 ms. The overlay
adds 0.044 ms of CPU and 0.013 ms of GPU. A capture with the overlay on is
the capture with it off, pixel for pixel. Under vsync the GPU times the
same work at about ten times as long, so GPU times are read with vsync off.

## 1. What changed

- **Backend: timestamps.** When the queue writes timestamps, a present
  session writes ten a frame, into a query pool reset at the start of the
  frame. They mark the frame's start, the end of the uploads, the swapchain
  image ready, the ends of the scene pass's unlit draws, opaque MToon
  hulls, opaque MToon surfaces and transparent draws, the end of the pass
  with its resolve, the end of a capture's copy, and the end of the
  overlay. The next `RenderFrame` reads them once it has waited for the
  frame. The waits are the ones one frame in flight already has, so reading
  them adds no wait. `PresentStatistics` gains `gpu_timing`, `gpu`
  (`PresentGpuTimes`, milliseconds per part) and `gpu_frame`, the number of
  the frame they belong to.
- **Backend: counts and CPU split.** `MeshCache::Record` counts the draw
  calls of each part, the triangles and the pipeline binds into a
  `SceneRecord`, and writes the three timestamps inside the pass.
  `PresentStatistics` gains `draws` (`PresentDrawCounts`), which includes the
  overlay's draws and vertices, and `overlay_texture_uploads`. It also gains
  `cpu_wait` and `cpu_submit`: `RenderFrame`'s time waiting for the frame in
  flight and for a swapchain image, and its other time.
- **Backend: the overlay pass.** `OverlayDrawList`
  ([`include/toon/overlay.hpp`](../../../include/toon/overlay.hpp)) is plain
  data. It holds vertices in framebuffer pixels with sRGB-encoded RGBA8
  colour, 16-bit indices, commands with a clip rectangle and a texture id,
  and RGBA8 textures with a revision. `RenderFrame` takes one. Its
  `OverlayRenderer` has one pipeline (`shaders/overlay.slang`,
  single-sampled, no depth, source over). The pipeline decodes the vertex
  colour to linear when the swapchain encodes to sRGB. A texture is uploaded
  only when its revision changes and released when it leaves the list.
  Vertices and indices go into host-visible buffers that grow. The pass
  loads the swapchain image after the scene pass or after the capture's
  copy. A screenshot never contains the overlay.
- **Viewport.** `overlay.cpp` is the only file that includes ImGui. It
  feeds ImGui the window's pointer and wheel events, lays out a "Telemetry"
  window, and copies the draw data and textures into the `OverlayDrawList`.
  The copy follows ImGui 1.92's protocol for a renderer with
  `ImGuiBackendFlags_RendererHasTextures`, so the font atlas can grow. It
  uses neither of ImGui's platform or renderer backends. A press or wheel
  turn over the overlay does not move the camera. Text and spacing scale
  with the monitor's content scale. The overlay's MSAA buttons call
  `SetSamples`, as the number keys do. `telemetry.cpp` keeps each quantity
  over the last 1,024 frames. `O` shows and hides the overlay,
  `--overlay off` turns it off, and the run ends with a `Draw calls:` line
  and one `Timing:` line per quantity.
- **Build.** Dear ImGui 1.92.8, pinned by revision, is fetched with the
  viewport and compiled into it as `toon-viewport-imgui`: the core and its
  tables and widgets only. Its licence is installed under
  `share/toon/licenses/imgui`.
- **CTests.** `toon-viewport-capture-no-overlay` takes
  `toon-viewport-capture`'s screenshot with `--overlay off`.
  `toon-viewport-capture-without-overlay` then requires the two files to be
  the same bytes, and skips where the captures skipped.

## 2. What was run

```sh
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd        # 16 of 16
ost build && ost test                                   # core, 4 of 4
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra               # 10 of 10
ost renderer viewport -- --frames 8 --hidden --vsync off
ost validate --intent renderer-viewport                 # passed
ost validate --profile lookdev --intent hydra--renderer-viewport   # passed
# on the avatar, for n in 1 2 4 8, and at 4 with --overlay off and with
# --vsync on:
ost renderer viewport --intent hydra --profile lookdev --with sha256:894fd616… -- \
    --usd <avatar.usdz> --hidden --frames 1200 --vsync off --samples <n> \
    --screenshot <n>.ppm
```

The avatar is the converted AliciaSolid of the earlier reports, which is
not in the repository, at 1280x720. Each run keeps its last 1,024 frames,
which leaves out the first 176 and their uploads. A visible window was also
opened on the bootstrap scene and captured from the desktop, to look at the
overlay itself.

## 3. Results

Every avatar run drew 20 of 20 draws MToon. Vulkan validation reported
nothing. Each frame recorded the same draw calls: 13 opaque hulls, 15
opaque surfaces and 7 transparent draws (5 surfaces and 2 hulls), 61,896
triangles and 7 pipeline binds. With the overlay on, the overlay added 2
draws, and its font atlas was uploaded twice at the start and not again.

The CPU, mean milliseconds, vsync off unless stated:

| Run | Interval | Hydra sync | Extraction | Overlay | Wait | Submit |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1x | 0.704 | 0.009 | 0.001 | 0.049 | 0.030 | 0.614 |
| 2x | 0.684 | 0.008 | 0.001 | 0.045 | 0.027 | 0.602 |
| 4x | 0.687 | 0.008 | 0.001 | 0.044 | 0.028 | 0.606 |
| 8x | 0.708 | 0.008 | 0.001 | 0.045 | 0.028 | 0.625 |
| 4x, overlay off | 0.618 | 0.008 | 0.001 | 0.000 | 0.025 | 0.583 |
| 4x, vsync on | 16.670 | 0.013 | 0.001 | 0.077 | 16.228 | 0.347 |

The frame interval's p50, p95 and p99 at 4x: 0.676, 0.749 and 0.919 ms;
under vsync, 16.655, 18.514 and 19.788 ms.

The GPU, mean milliseconds:

| Run | Unlit | Outline | Opaque | Transparent | Resolve | Overlay | Work |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1x | 0.002 | 0.094 | 0.003 | 0.002 | 0.000 | 0.012 | 0.113 |
| 2x | 0.002 | 0.097 | 0.004 | 0.002 | 0.011 | 0.013 | 0.129 |
| 4x | 0.015 | 0.085 | 0.006 | 0.002 | 0.012 | 0.013 | 0.132 |
| 8x | 0.056 | 0.045 | 0.013 | 0.002 | 0.014 | 0.013 | 0.144 |
| 4x, overlay off | 0.004 | 0.095 | 0.006 | 0.002 | 0.012 | 0.000 | 0.119 |
| 4x, vsync on | 0.294 | 0.922 | 0.051 | 0.021 | 0.109 | 0.093 | 1.491 |

"Work" is the frame without the wait for its swapchain image, which was
0.003 ms with vsync off. The uploads and the capture's copy were 0.000 ms
in the mean. Under vsync, work's p50 is 1.016 ms and its p95 2.774 ms.

The 4x capture with the overlay on and the one with it off differ in 0
pixels. So do the two bootstrap captures of the CTest.

## 4. Observations

- **A part's time is not its pipeline's cost.** The timestamps sit
  between draws on a GPU that overlaps them. A part's time runs from the
  previous part's last draw finishing to its own last draw finishing. Work
  no draw owns lands in the first part that waits for it. At 8 samples,
  0.056 ms appears in `unlit`, which draws nothing, and is most likely the
  pass's clear. `outline` holds 0.09 ms at 1, 2 and 4 samples, so it is not
  fill. The hulls are the first draws to read the meshes, which live in
  host-visible memory
  ([capability matrix](../../reference/CAPABILITY_MATRIX.md), geometry
  upload ⚠️). The surfaces that read the same meshes next take 0.003 to
  0.006 ms. That points at fetching the geometry rather than at the hull,
  but it is not shown here. A device-local copy of the geometry
  ([design policy §20](../../design/DESIGN_POLICY.md#20-asset-upload))
  would show it. Until then, `work` is the number to compare, and the parts
  say where time went only in that sense.
- **Vsync slows the GPU, not the work.** Under FIFO the same frame takes
  1.0 ms of GPU at p50 instead of 0.13. The GPU sits idle for most of each
  16.7 ms and is presumably clocked down when a frame arrives. GPU times are
  therefore compared with `--vsync off`. Under vsync the CPU's interval is
  the display's, and `wait` carries it.
- **The overlay is cheap and leaves the image alone.** It adds 0.044 ms of
  CPU and 0.013 ms of GPU at 4x. The interval grows by 0.069 ms against
  `--overlay off`. Its pass follows the capture's copy, and the captures
  match byte for byte.
- **Submit is most of the CPU frame.** With vsync off, 0.6 of the 0.69 ms
  is `RenderFrame` after its waits: cache updates, recording, the submit
  and an immediate-mode present. It is not broken down further yet. The
  CPU render thread stays under design policy §23's 2 ms.
- **Hydra sync is 0.008 ms** because the stage does not move. Animation
  playback will give it something to measure.

## 5. Not checked

A monitor whose content scale is not 1. Pointer input on the overlay beyond
what the code routes: the visible run was looked at, not clicked. A queue
that writes no timestamps, where the GPU table says so instead. Material
and light debug controls, which move constants to per-frame parameters and
are designed with scene lights. Linux.
