# Tessera v0.6.0 dedicated viewport UI

Date: 2026-11-05. Windows x86_64, MSVC 14.51.36231, Release, OpenUSD 26.08
through the existing `lookdev` runtime; NVIDIA RTX A5000, Vulkan driver value
2504261632, Vulkan SDK 1.4.350.0. Native presentation scale was 1.

## Scope

The opt-in, non-installed `toon-tessera-host` moved from a small overlay to a
dedicated scene pane and scrollable Inspector using Tessera v0.6.0,
`f264eba5cbb0ff7920ba8441e7761030d9a6df92`. The public authoring API composes
SplitPane, Grid statistics and a Stack camera toolbar. The host still lends
its Vulkan device, samples premultiplied handoff slots and releases them on
its own timeline; the alternate path uploads readback products.

The renderer's extent/aspect now follows the scene pane. Its image is placed
on integral physical pixel boundaries, independently of logical UI coordinates.
The comparison maps the captured product into that scene region and masks
only the toolbar, rather than comparing UI pixels to renderer output.

The main `toon-viewport` / ImGui path, installed renderer boundary, USD scene
semantics and runtime providers were not changed.

## Commands and results

The existing configured build retained its compiler/runtime/Slang setup.
From a Visual Studio developer shell:

```powershell
cmake -S . -B build\cy2026-windows-x86_64-py313-lookdev--tessera-usd
cmake --build build\cy2026-windows-x86_64-py313-lookdev--tessera-usd --target toon-tessera-host toon-tessera-panel-test --parallel 8
ctest --test-dir build\cy2026-windows-x86_64-py313-lookdev--tessera-usd -R '^toon-tessera-(panel|host-smoke(-1x)?)$' --output-on-failure --no-tests=error
```

Configure/build passed. All three CTests passed, without skips:

| Check | Evidence |
| --- | --- |
| `toon-tessera-panel` | Split layout, button activation across several telemetry frames, no release-only activation, outside release/capture cancellation, retained stationary hover, compatible splitter capture, keyboard adjustment/activation, resize clamping and focus reveal |
| Window-free logical scale coverage | 640 x 360 divided by 1, 1.25, 1.5 and 2; positive scene bounds, scrolling and keyboard access retained; a 120 x 80 logical window also retained a scene region |
| CPU comparison contract | Offset scene region, padded source rows, RGBA/BGRA paths, toolbar exclusion and rejection of an out-of-window region |
| `toon-tessera-host-smoke` | 4x MSAA, seven captures, maximum sRGB channel difference 1, zero validation messages |
| `toon-tessera-host-smoke-1x` | 1x MSAA, seven captures, maximum sRGB channel difference 0, zero validation messages |

Each native smoke used the committed `adapters/hydra2/tests/skinning.usda`,
starting at 640 x 360 and resizing to 480 x 270. Captures covered handoff,
camera zoom/reset, divider dragging, resized handoff, readback and return to
handoff. Native input also checked keyboard activation/reset, camera isolation
from Inspector drag/wheel input, button presses spanning a rendered frame,
and capture loss. The first 4x capture was visually inspected.

The measurement executable was also run in its runtime environment:

```powershell
& build\cy2026-windows-x86_64-py313-lookdev--tessera-usd\adapters\tessera\toon-tessera-host.exe --shaders build\cy2026-windows-x86_64-py313-lookdev--tessera-usd\backend\vulkan\shaders --tessera-shaders build\cy2026-windows-x86_64-py313-lookdev--tessera-usd\adapters\tessera\tessera-shaders --stage adapters\hydra2\tests\skinning.usda --width 640 --height 360 --samples 4 --measure build\cy2026-windows-x86_64-py313-lookdev--tessera-usd\adapters\tessera\workspace-measure.json --frames 8 --warmup 4
```

Both paths completed and wrote valid JSON. The window remained 640 x 360;
`render_width`/`render_height` were 352 x 360. Ordinary readback reported
1,013,760 bytes/frame (RGBA8 plus float depth, eight bytes per scene pixel);
handoff reported zero. This short run verified output/accounting, not a new
performance baseline.

## Consumer limits and upstream follow-ups

- Placeholder ASCII shaping remains enabled; native fonts/localization, scene
  editing and playback were outside this slice.
- Native DPI other than scale 1 and other GPUs/platforms were not exercised.
  The four-scale coverage above is window-free logical layout/input evidence.
- Ordinary button snapshots are retained while pressed because
  PointerDispatcher capture is tree-identity scoped. Telemetry labels pause
  until release; viewport resize cancels the press. SplitPaneDispatcher keeps
  live compatible capture independently.
- A compiled reproduction against the pinned Tessera core produced one action
  for Down/Up on one snapshot and zero after an identical UiTree replacement.
  The compatible-capture enhancement was filed as
  [animu-sphere/tessera#153](https://github.com/animu-sphere/tessera/issues/153).
- Inspector scrolling uses wheel input and focus reveal, without a draggable
  scrollbar. The concrete authoring/input request was filed as
  [animu-sphere/tessera#154](https://github.com/animu-sphere/tessera/issues/154).
