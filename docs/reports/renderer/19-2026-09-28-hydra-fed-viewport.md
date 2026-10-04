# The Hydra-fed viewport: `toon-viewport --usd` draws the avatar through Hydra in its own frame loop, 20 of 20 draws MToon with `vrmImaging`, and reads nothing back

- Date: 2026-09-28
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.13`; the canonical CY2026 `lookdev` runtime (OpenUSD
  26.08) for the build; for the VRM run, the committed
  `formations/vrm-host-session/` Formation of
  [report 13](13-2026-09-28-host-session-formation.md): the canonical
  runtime `sha256:b982656c…` and the published `vrmImaging` 0.10.0
  `sha256:894fd616…`
- Occasion: [v0.2.0](../../releases/v0.2.0.md)'s viewport foundation, whose
  first item puts a representative avatar in front of `toon-viewport`, and
  [design policy §31.1](../../design/DESIGN_POLICY.md#311-open-questions)'s
  DP-Q1, which this report answers. [`ost` report
  07](../ost/07-2026-09-28-v0.23.13-a-hydra-fed-viewport.md) measured what
  `ost` gives this path before it existed

## TL;DR

**A `viewport-usd` intent builds the Hydra adapter and the viewport in one
tree, and `toon-viewport --usd <stage>` populates a render index through
UsdImaging's scene index chain with `hdToon`'s delegate, syncs it every
frame and draws the delegate's committed scene straight into the swapchain.
No Hydra render pass runs, so no frame is rendered offscreen or read back;
the one readback is a capture, which the viewport counts and checks. In the
VRM Formation, with the local viewport as the command, the avatar draws 20
meshes, all 20 MToon (5 transparent, 15 outlined, 20 skinned) from 12 MToon
materials and 7 textures: `usdview`'s numbers. Without `vrmImaging` the same
20 draws are PreviewSurface, as in `usdview`. So DP-Q1's proposed answer
holds. The viewport also gained an orbit, pan and dolly camera that frames
the scene, and `--screenshot`.**

## 1. What changed

- **Build.** `openstrata.toml` declares `viewport-usd`, setting
  `TOON_ENABLE_HYDRA2` and `TOON_ENABLE_VIEWPORT`. The top-level
  `CMakeLists.txt` now reads `OST_RENDERER_ADAPTERS` as adapters to *add*
  to what the intent's own options enable, not as the whole set, so
  `ost renderer viewport` asking for `viewport` no longer forces the Hydra
  adapter off (ost report 07, Q1). It also means Q2's sequence — a viewport
  run in the `hydra` tree, then `ost build --intent hydra` — keeps the Hydra
  adapter, since the intent's `TOON_ENABLE_HYDRA2=ON` is no longer
  overwritten.
- **Viewport.** In a build with both adapters, `adapters/viewport/hydra_scene.cpp`
  opens the stage, creates the scene indices with
  `UsdImagingCreateSceneIndices`, and inserts the final one into a render
  index created on `HdToonRenderDelegate`, as the `hdToon` skinning test
  does. Each frame calls `ApplyPendingUpdates`, enqueues the geometry
  collection, runs `SyncAll` with one task that draws nothing and carries
  the render tags `geometry` and `proxy` (usdview's default), and commits
  the delegate's render world into a reused snapshot
  (`HdToonRenderDelegate::CommitScene(FrameSnapshot&)`, new). The viewport
  extracts the draw list, sets its own camera and the stage's
  `metersPerUnit`, and renders it through its present session. OpenUSD
  stays in that one file.
- **Camera.** `adapters/viewport/camera.cpp`: an orbit camera around a
  target, turned by a left drag, panned by a middle or Shift+left drag,
  dollied by a right drag or the wheel. It frames the visible meshes'
  bounds — a skinned mesh in its bind pose — from the front for the stage's
  `upAxis`: +Z with Y up, −Y with Z up. F frames again, R returns to the
  last framing. Clip planes follow the framed sphere.
- **Capture.** `PresentSession::RequestCapture` and `TakeCapture`: the next
  presented frame's swapchain image is copied into a host buffer as well,
  and taken as RGBA8 once that frame completes; `PresentStatistics::readbacks`
  counts them. The swapchain asks for transfer-source usage where the
  surface offers it. `--screenshot <file.ppm>` writes the last of
  `--frames N`; P writes `toon-viewport-<n>.ppm`. The viewport fails if
  more frames were read back than captured.
- **sRGB presentation.** The scene pipelines write linear colour: textures
  are sampled through sRGB views, and nothing encodes the result. The
  offscreen renderer's colour AOV is linear, and `usdview`'s default colour
  correction encodes it to sRGB (`colorCorrectionMode` sRGB, Hydra's
  `colorCorrection.glslfx`). The viewport presented the same values into a
  UNORM swapchain, where the display took them as encoded: too dark and too
  saturated, which showed on the avatar's hair and skin. The swapchain now
  prefers `B8G8R8A8_SRGB` or `R8G8B8A8_SRGB`, so the hardware encodes on
  write and blends and resolves in linear; `PresentStatistics::srgb_encoded`
  says which it got. The bootstrap triangle's linear (0.8, 0.2, 0.1),
  captured at one sample, is (231, 124, 89) — sRGB's encoding exactly —
  where it was (204, 51, 26).
- **Launch record.** The viewport prints `Selected backend:`, `Device:`
  and `Presentation:`, the labels `ost renderer viewport` reads (ost
  report 07 §5), and a `Scene summary:` of the last frame in the Hydra
  host evidence's terms.
- **Tests.** `toon-viewport-capture` (the bootstrap scene, one capture) and,
  with the Hydra adapter, `toon-viewport-present-usd` (the `usdview` smoke
  stage through Hydra, `--expect-draws 1`).

The design policy described the Hydra-fed viewport as running Hydra's task
and copying the offscreen colour target to the swapchain. The viewport does
less: it runs no render pass at all, so there is no offscreen target to
copy. §31 is amended to say so.

## 2. What was run

```sh
ost renderer viewport --intent viewport-usd --profile lookdev -- \
    --hidden --frames 8 --vsync off \
    --usd adapters/hydra2/tests/usdview-smoke.usda --expect-draws 1
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
ost build --jobs auto && ost test
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra
ost renderer viewport -- --hidden --frames 8 --vsync off

# The avatar, without vrmImaging: the runtime alone.
ost renderer viewport --intent viewport-usd --profile lookdev -- \
    --usd <avatar.usdz> --hidden --frames 8 --vsync off \
    --width 800 --height 900 --screenshot plain.ppm
# With vrmImaging: the committed Formation, the local viewport as command.
cd formations/vrm-host-session
ost formation run formation.toml -- <build>/adapters/viewport/toon-viewport.exe \
    --usd <avatar.usdz> --hidden --frames 8 --vsync off --expect-draws 20 \
    --width 800 --height 900 --screenshot vrm.ppm
```

The avatar is `AliciaSolid.usdz`, the one reports 06–18 use; it is not
redistributable and not in the repository.

## 3. Results

| Build or run | Result |
| --- | --- |
| `viewport-usd` intent, canonical `lookdev` | `ost test` 13/13, including `toon-viewport-present`, `-capture` and `-present-usd`; `ost validate` passed |
| `core` | `ost test` 4/4 |
| `hydra` intent | `ost test` 10/10 |
| `renderer-viewport` (no Hydra) | 8 frames presented, `Scene: bootstrap`, 0 frames read back |
| smoke stage through Hydra | 1 draw, 1 material (the fallback, PreviewSurface); 8 frames, 0 read back |

The avatar, from the last frame's summary:

| Run | Draws | MToon | Transparent | Outlined | Skinned | Materials MToon / PreviewSurface | Textures | Frames read back |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| runtime only | 20 | 0 | 0 | 0 | 20 | 0 / 13 | 0 | 1 (the screenshot) |
| Formation with `vrmImaging` | 20 | 20 | 5 | 15 | 20 | 12 / 1 | 7 | 1 (the screenshot) |

The MToon row is `usdview`'s at every column that both count
([report 18](18-2026-09-28-outline-depth-bias.md): 20 MToon, 5 transparent,
15 outlined, 20 skinned draws; 12 MToon materials). The one PreviewSurface
material with `vrmImaging` is the delegate's fallback, which no mesh binds.
The screenshots show the avatar from the front, whole, centred; with
`vrmImaging` in MToon with its outlines, without it as flat grey. Taken
again after the swapchain turned sRGB, the MToon row is unchanged and the
hair and skin are lighter and less saturated, as their textures are; the
background, the clear colour, is encoded too and so lighter.

## 4. Observations

- **`ost renderer viewport` can launch a copy.** It picks the viewport
  among every file named `toon-viewport.exe` under the build tree. In the
  `viewport-usd` tree, the `usdview` host test installs the whole project
  into `adapters/hydra2/usdview-install/`, viewport included, and one run
  launched that copy instead of `adapters/viewport/toon-viewport.exe`. It
  was the copy of the previous `ost test`, before a change to the render
  tags, and drew 21 meshes where the build drew 20. The Formation run above
  names the build's executable directly. This is `ost`'s to decide
  (`pick_built_executable` in `renderer.rs`); an `ost` report has not been
  written.
- **A render index synced with no task syncs every render tag.**
  `SyncAll` gathers render tags from its tasks, and with none it syncs
  every rprim in the collection. Before the render-tag task, the avatar
  with `vrmImaging` drew 21 meshes, the 21st unskinned; with it, 20. Which
  prim the 21st was is not identified here.
- **Framing is by a sphere.** The camera fits the bounds' circumscribed
  sphere to the vertical field of view, so a standing avatar in a tall
  window leaves room at the sides and above and below.

## 5. Not checked

Interactive use: the camera was driven only by framing, in hidden windows,
and orbit, pan, dolly and the P key were not exercised on screen. A stage
with animation, which is shown at its start time; playback is
[v0.3.0](../../roadmap/v0.3.0.md)'s. A Z-up stage. A surface format other
than the A5000's, which a capture refuses unless it is 8-bit RGBA or BGRA.
Picking and a reference check, the other readbacks design policy §31
allows. The `usd` runtime profile, Linux and macOS.
