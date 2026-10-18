# Viewport RGBA product compositing

- Date: 2026-10-18
- Machine: Windows x86_64, NVIDIA RTX A5000
- Build: MSVC 14.51; canonical CY2026 OpenUSD 26.08 `lookdev`, combined
  `viewport-usd`; plain CMake without Vulkan/OpenUSD
- Scope: the viewport consumer of the
  [output policy](../../design/RENDER_OUTPUT_POLICY.md), following
  [Hydra report 79](79-2026-10-18-hydra-rgba.md)

## Change and interpretation

The presentation session draws the scene into an independent RGBA8 UNORM
attachment, with transparent zero clear, linear sRGB and premultiplied alpha.
MSAA averages colour and alpha into that attachment. The scene pipelines and
coverage semantics are shared with the offscreen renderer; the swapchain
format no longer determines the scene attachment format. Sample changes keep
the resolved foreground image, while resize rebuilds its attachments and
updates the compositor descriptor after completion of the previous frame.

A fullscreen GPU pass reads the resolved product and applies
`foreground.rgb + background.rgb * (1 - foreground.a)`, writing alpha one.
Composition happens in linear space before an sRGB swapchain attachment
encodes it. A non-sRGB UNORM fallback retains linear transfer. Solid colour
and a two-colour checkerboard use linear-sRGB values, independently of scene
lighting or materials. The existing default background remains
`(0.05, 0.10, 0.15)`. UI draws after composition and capture.

`PresentSession::SetCompositeBackground` rejects non-finite or out-of-range
colours and zero checker size, keeping the last accepted background. The
viewport's Background panel changes both colours and checker size;
`--background solid|checkerboard` and `--background-color R G B` select the
initial preview. Background changes require no scene upload or pipeline
rebuild. GPU telemetry has a separate `composite` interval.

`RequestCapture`/`TakeCapture` retain opaque as-presented capture without UI.
`RequestForegroundCapture`/`TakeForegroundCapture` independently request the
top-left premultiplied linear RGBA8 product, without background or UI. Both
readbacks carry explicit transfer/alpha metadata and only run when requested;
ordinary frames stay on the GPU. `readbacks` counts each consumed product.
This is CPU readback, not the later external GPU texture-handoff API.

Because presentation now always produces transparent foreground first,
multisample presentation requires optional Vulkan `alphaToOne`, as transparent
offscreen output does. An unsupported creation or sample-change request
returns an explicit error; 1x remains available. The shader uses Slang's
`SV_VulkanVertexID` so the fullscreen triangle does not add a
`shaderDrawParameters` device-feature requirement.

## Controlled evidence

`toon-viewport-composite-gpu` uses original triangle geometry and constant
materials at actual 1x and 4x MSAA, through a hidden GLFW presentation surface.
It checks:

- Empty zero-RGBA foreground and no readback on ordinary frames.
- Byte-identical viewport foreground and separately rendered offscreen
  products for MToon, MMD and PreviewSurface, with Opaque, Mask and Blend.
- MToon/MMD opaque and translucent hull-only foreground, including pixels
  without a visible surface, and empty output when hulls are disabled.
- Exact foreground preservation across black, white, arbitrary solid colour
  and checkerboard backgrounds. Odd-size checker cells exercise boundaries.
- Every presented pixel against independently calculated linear source-over,
  followed by sRGB transfer when selected, within 1.6 byte levels per RGB
  channel; presented alpha is always one. Partial MSAA silhouettes are
  required, and Mask coverage is retained in the offscreen equality checks.
- Invalid background rejection, retained scene upload counters, and captures
  without the conspicuous UI triangle drawn after them.
- 4x-to-1x sample change, resize to 128x112, and scene-cache reset, retaining
  foreground/offscreen equality throughout. Vulkan validation stays clean.

The four saved Blend composites were assembled into a local comparison sheet
and visually inspected: the translucent triangle and silhouette blend into
each selected background without a conspicuous fringe. The CLI checkerboard
capture was also inspected. These synthetic captures are not avatar-quality
acceptance evidence.

## Verification and remaining boundaries

The combined build passed. Its complete CTest selection passed 64/64,
including Hydra, usdview, viewport, offscreen RGBA and installed consumers.
After adding the hull-only cases, the affected presentation selection passed
7/7 again. The test process put the lookdev runtime's `bin` and `lib` on
`PATH`. The CLI checkerboard capture presented four frames with one requested
readback. The existing plain-CMake Release tree without Vulkan/OpenUSD rebuilt
and passed 14/14 state, boundary, evidence and installed-consumer checks.
A separate fresh configuration failed in CMake's compiler probe with MSVC
C1902 (PDB manager mismatch), before project compilation; the configured
Release tree supplied the no-Vulkan verification. Documentation and whitespace
checks passed.

The sRGB attachment and `alphaToOne`-supported paths were GPU-validated here.
Non-sRGB surfaces and devices without `alphaToOne` remain unverified. This
does not establish host display/export or PNG conversion, an image-background
consumer, the full textured hair/normal-map/MMD-shadow bounded fixture set,
or external tangent comparisons. Those remain in the
[milestone](../../roadmap/v0.5.0.md). No release was published.
