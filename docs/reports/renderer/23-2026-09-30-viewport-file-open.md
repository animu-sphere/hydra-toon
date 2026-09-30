# Native file open in the standalone viewport

- Date: 2026-09-30
- Environment: Windows, MSVC 14.51, NVIDIA RTX A5000, OpenUSD 26.08
  `lookdev` runtime, `ost` 0.23.14
- Reference: hydra-merlin's Native File Dialog Extended 1.3.0 integration,
  pinned at `3cd252a8f7ca32419b1ca235c2990ba6a0ecba7c`

## Change

Hydra builds expose `Open File...` and Ctrl+O. The native chooser has USD,
VRM and PMX filters and returns a UTF-8 path to the existing OpenUSD stage
reader. The host prepares and syncs a replacement before changing its active
scene. Successful opens frame the new scene with its up axis and unit and
request `PresentSession::ResetScene`. The next render clears the old mesh,
texture and material cache entries after the GPU frame completes, preserving
the presentation session and overlay. Failed opens retain the scene and show
the error. The parser and format registration remain owned by external
OpenUSD plugins.

Windows uses `wmain` and converts its arguments to UTF-8, matching the dialog.
The initial regression run exposed code-page loss in narrow command-line
arguments for a Japanese filename; the subsequent run passed with this fix.

## Verification

`ost build --profile lookdev --intent viewport-usd --jobs auto` and
`ost test --profile lookdev --intent viewport-usd` passed, 23 of 23 tests.
Seven new tests compare captures from a fresh start with captures after an
in-session open: bootstrap to USD, USD to USD with overlapping mesh ids and
initial revisions, and a failed open retaining the previous scene. The
replacement changes geometry, colour, up axis and metres per unit and has a
Japanese filename. All three image comparisons matched byte for byte.
Each bounded rendering run also checks Vulkan validation and readback counts.

The non-Hydra viewport was rebuilt and presented eight hidden frames through
`ost renderer viewport -- --hidden --frames 8 --vsync off`, exiting 0.

The visible Hydra viewport was exercised through computer-use: the button
opened the native chooser with the combined USD/VRM/PMX filter. Opening a raw
VRM without its format plugin displayed an error and retained the bootstrap
scene. Ctrl+O also opened the chooser.

For the user-requested Seed-san VRM, the existing packaged file reader had a
`usd` target and `--with` correctly refused it in the `lookdev` session. The
local `usdVrmFileFormat` and `usdVrmPackageResolver` DLLs were already built
against the OpenUSD 26.08 ABI. A session using those local bundles and the
published `vrmImaging` digest from the VRM Formation opened the raw file in
a bounded hidden run and then through the native chooser in the visible
viewport. The hidden run reported 21 meshes, all skinned with authored normals,
13 MToon draws, 8 PreviewSurface materials, 10 MToon materials and 9 textures;
it presented eight frames, with no readback or Vulkan validation failure.
The visible window was left displaying the requested model. The model and
its captures are not included in the repository.

PMX ingestion, Linux native dialogs and high-DPI monitors were not exercised.
