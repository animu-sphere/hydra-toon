# Controlled usdview RGBA consumer

- Date: 2026-10-19
- Machine: Windows x86_64, NVIDIA RTX A5000
- Build: MSVC 14.51, canonical CY2026 OpenUSD 26.08 `lookdev`,
  combined `viewport-usd`, Python 3.13 and the runtime's Qt bindings
- Scope: a host consumer of the premultiplied linear Hydra colour AOV in the
  [output policy](../../design/RENDER_OUTPUT_POLICY.md), following
  [Hydra report 79](79-2026-10-18-hydra-rgba.md) and
  [viewport export report 81](81-2026-10-19-foreground-png.md)

## Consumer and controls

`toon-renderer-usdview-rgba-consumer` installs the renderer in its own test
staging directory and runs `testusdview` with an original PreviewSurface
triangle, emission and opacity 1, 0.25 and 0, at actual 1x and 4x MSAA. The
existing renderer evidence confirms the sample count and completed AOV writes.
The host's selection highlighting, HUD and bounding boxes are disabled; the
test suppresses StageView's unconditional origin-axis decoration. This is a
controlled test consumer, not a new usdview feature or a renderer-side change
to the AOV convention.

With usdview colour correction disabled, its top-left Qt framebuffer contains
linear premultiplied RGBA. The test reads stored channels with `pixel()`,
avoiding `pixelColor()`'s implicit unpremultiplication. Opaque interior pixels
provide the shaded-colour control for Blend pixels; alpha-zero pixels must
have zero RGB, zero opacity must empty the image, and 4x opaque silhouettes
must contain partial coverage.

The PNG consumer divides RGB by nonzero alpha in linear space, applies the
sRGB transfer, preserves alpha and writes a straight `Format_RGBA8888` QImage
with sRGB colour-space metadata. Qt independently decodes the saved PNG;
straight channel bytes and colour-space metadata must survive, and decoding
RGB and premultiplying must reconstruct the source linear foreground.

The display consumer applies linear source-over against black, white, an
odd-cell checkerboard and an original procedural RGB8 image with distinct
rows/channels. The image's sRGB values are decoded before composition. Only
the opaque result is display-encoded. Each saved composite is decoded and
checked against the original foreground and background. Separately, the
test asks Vulkan to render the same scene over opaque black and white clears
and lets usdview apply sRGB. Every displayed RGB pixel is compared with the
foreground consumer's corresponding composite. Returning to transparent
mode must restore the byte-identical linear framebuffer.

## Measured results and native screenshot limitation

The controlled frames were 597x540. The two opaque host display comparisons
matched the consumer's RGB bytes exactly at both sample counts. Across the
four backgrounds, the maximum linear reconstruction error was below 0.00422
per channel; across all straight PNG cases it was below 0.00266. The white
Blend composite was visually inspected, with no conspicuous silhouette fringe.

The native usdview sRGB screenshot has a separate boundary problem for
transparent foreground. In the controlled Blend interior, linear stored
RGBA was `(39, 20, 11, 64)`. The native sRGB framebuffer stored
`(109, 79, 59, 64)` while Qt still labelled it premultiplied. Those RGB values
are consistent with applying the transfer to premultiplied RGB without
re-establishing encoded premultiplication. Saving through Qt produced straight
PNG `(255, 255, 235, 64)`, versus the correct conversion's
`(205, 152, 115, 64)`. The largest channel error was 120 byte levels. The
opaque control matched its expected sRGB bytes and zero-opacity output stayed
empty at both sample counts.

The native transparent screenshot is diagnostic, not a passing export path.
The test records native and expected channels/errors in
`usdview-rgba-consumer.json`; it does not require the host defect to persist.
The supported test conversion starts from the uncorrected linear framebuffer.
Neither the runtime nor hdToon's AOV representation was modified to accommodate
the native host limitation.

## Verification and limits

The combined build passed. The affected CTest selection passed 4/4: CPU render
buffers, Hydra RGBA GPU, the existing usdview smoke and the new consumer. After
adding per-background host-display error recording and the returned-foreground
check, the new consumer passed again in 18.52 seconds. Documentation and
whitespace checks passed.

This establishes one controlled Qt/Usdviewq host consumer. It does not establish
unchanged native transparent sRGB display/export, arbitrary hosts, textured
hair/normal-map/MMD-shadow acceptance, or external tangent comparisons. PNG
conversion starts from quantized linear RGBA8; low-alpha precision remains
limited. The bounded all-material fixture work and native host boundary remain
in the [milestone](../../releases/v0.5.0.md). No release was published.
