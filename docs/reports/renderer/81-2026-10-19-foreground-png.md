# Foreground PNG consumer and viewport organization

- Date: 2026-10-19
- Machine: Windows x86_64, NVIDIA RTX A5000
- Build: MSVC 14.51, canonical CY2026 OpenUSD 26.08 `lookdev`;
  combined `viewport-usd`, optional avatar-provider viewport and plain CMake
  without Vulkan/OpenUSD
- Scope: the PNG consumer boundary in the
  [render output policy](../../design/RENDER_OUTPUT_POLICY.md), following
  [viewport report 80](80-2026-10-18-viewport-compositing.md)

## Change and interpretation

`--foreground-output FILE.png` exports the independent foreground product
from the last of a bounded `--frames N` run. It uses the existing
`RequestForegroundCapture`/`TakeForegroundCapture` boundary, without preview
background or UI. It may accompany an opaque as-presented PPM screenshot;
that requests two readbacks. Ordinary frames still request neither.

The consumer accepts complete top-left RGBA8 products whose metadata states
linear sRGB and premultiplied alpha. It divides RGB by nonzero alpha in
linear space, clamps to [0,1], applies the sRGB transfer to RGB, and preserves
the alpha byte. Alpha-zero pixels become zero RGB. The result is straight
sRGB RGBA8 with a PNG sRGB chunk using perceptual intent. The source payload
and its metadata remain unchanged. Padded source rows are supported; invalid
metadata, incomplete extents, encoder size limits and output failures raise
errors. File paths use UTF-8, including on Windows.

PNG compression uses the unmodified vendored `stb_image_write` v1.16;
the pinned source, MIT license option and attribution live in
`third_party/stb/`. No build-time download or new runtime library is needed.
The viewport installs its license/attribution alongside the existing licenses.

The adapter now keeps implementation/private headers in `src/` and fixtures,
test sources and check drivers in `tests/`. Root CMake owns dependencies,
the two executable variants and installation; test registration lives in
`tests/CMakeLists.txt`. A shared source list keeps the ordinary and provider
viewport variants aligned. Fixture paths and executable names are retained;
test executables and artifacts now live in the build's viewport `tests/`
directory. `main.cpp` retains the frame loop while CLI parsing/validation and
capture/export live in `arguments.*` and `capture.*`. Validation consumers and
the structural documentation follow the new paths.

## Controlled evidence

`toon-foreground-png-state` exercises a 256x3 synthetic product with padded
rows, all alpha byte values, dark colours, nonzero RGB at zero alpha and
distinct channel/row patterns. It checks source isolation, incompatible
metadata, incomplete payloads and failed file output. The independent
`toon-foreground-png-decode` Python standard-library decoder checks the
written PNG's chunk CRCs, sRGB intent, dimensions, reconstructed pixels,
alpha preservation and linear-space source-over reconstruction over black,
white and arbitrary grey. Encoded RGB must agree within half a byte;
recomposition differs by less than 0.005 per linear channel. A Japanese
filename exercises UTF-8 output.

`toon-viewport-foreground-png` drives the actual viewport at 1x and 4x over
black, white, an arbitrary solid colour and checkerboard. Independently
decoded PNG foreground bytes remain equal across all four backgrounds;
the paired presented PPMs all differ. Empty alpha and opaque pixels remain,
4x retains partial silhouette alpha, and zero-alpha RGB is zero. UI is enabled
in these runs. Each process reports exactly two readbacks for its two requested
products. Window-system minimum extents may enlarge the requested small window;
the test uses the actual PNG dimensions. Unbounded foreground export is rejected
before window creation.

## Verification and remaining boundaries

The combined `viewport-usd` build passed, including its complete 68-test CTest
selection. This includes the existing three-material-family offscreen and
compositor checks, Hydra AOVs, usdview, playback, diagnostics, file replacement
and installed consumers. The avatar-provider viewport also built, with its
three runtime checks and four foreground checks passing (7/7). The existing
plain-CMake Release tree without Vulkan/OpenUSD rebuilt and passed 16/16,
including the encoder and independent decoder checks. The standalone viewport
without OpenUSD also rebuilt and passed its affected 13-test selection.
Unpaired foreground exports of the bootstrap and USD morph fixture each
completed four frames with exactly one readback. The lookdev runtime's
`bin` and `lib` were on the test process's `PATH`.

The first new decode runs exposed two test harness assumptions: the Windows
test executable used narrow argv for a UTF-8 path, and the CLI test assumed
the window system would accept a 96-pixel requested width. The harness now
converts wide argv and checks the actual capture extent; both checks pass.

This establishes the viewport PNG consumer, not another host's consumption
of the Hydra AOV. PNG conversion starts from quantized linear RGBA8;
unpremultiplication cannot recover colour precision lost at low alpha.
The full textured hair/normal-map/MMD-shadow fixture set, image-background
compositing, external host display/export conventions and tangent comparisons
remain in the [milestone](../../releases/v0.5.0.md). Documentation and whitespace
checks passed. The bootstrap PNG was visually inspected. No release was published.
