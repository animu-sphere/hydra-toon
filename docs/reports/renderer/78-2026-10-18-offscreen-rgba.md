# Offscreen premultiplied RGBA

- Date: 2026-10-18
- Machine: Windows x86_64, NVIDIA RTX A5000, driver 597.16
- Build: MSVC 14.51; canonical CY2026 OpenUSD 26.08 `lookdev`, combined
  `viewport-usd`; plain CMake without OpenUSD
- Scope: the first offscreen boundary of the
  [RGBA output policy](../../design/RENDER_OUTPUT_POLICY.md)

## Change and interpretation

`OffscreenRenderer::SetBackground` accepts `BackgroundSettings` with Opaque
or Transparent mode and finite linear RGB in [0,1]. Opaque clears to that
colour and alpha one. Transparent ignores the colour and clears all four
channels to zero. The default remains opaque `(0.05, 0.10, 0.15, 1)`.
Invalid settings retain the previous setting. Settings affect the next
frame without changing lighting, material values or GPU resource identity.

`ColorProduct` readback is top-left RGBA8 UNORM, premultiplied in the linear
sRGB working space. `color_space` retains its existing `linear` value;
`transfer_function` is `linear` and `alpha_convention` is `premultiplied`.
There is no tone mapping or HDR product: attachment channels clamp to
[0,1]. Material parameters and shaders still supply straight colours;
the existing source-alpha colour blending applies opacity once, while
alpha uses `As + Ad * (1-As)`. Opaque samples write alpha one; multisample
resolve averages colour and alpha together. No depth-derived alpha is used.

MToon's Mask shader returns sample coverage for alpha-to-coverage. That
value previously also became stored sample alpha, applying coverage twice
in the resolved product's alpha. Devices supporting Vulkan `alphaToOne`
now enable it for this pipeline: it runs after coverage selection and
stores alpha one on selected samples without changing their colour.
Blend and outline pipelines keep their independent source-over alpha.

`alphaToOne` remains an optional device feature. Without it, this pipeline
preserves the opaque clear's alpha by disabling alpha writes. Selecting
transparent output with more than one actual sample fails with an explicit
message, retaining the previous clear. Transparent 1x and opaque output
remain available. The RTX run exercised the supported branch; the
unsupported-feature branch was inspected but not GPU-validated.

The shared scene pipeline also applies this sample-alpha correction to
presentation. Present captures identify their transfer as `srgb` or `linear`
according to the selected swapchain format, with `opaque` alpha.

## Controlled evidence

`toon-rgba-output-gpu` runs original triangle and back-facing hull geometry
at actual 1x and 4x MSAA. Constant emission in MToon/PreviewSurface and
independently decoded MMD ambient colour give analytic RGB references.
Checks include:

- Default and transparent empty clears, untouched depth, and rejection of
  invalid mode, non-finite and out-of-range colour without changing settings.
- Opaque, quarter-opacity Blend and zero-opacity surfaces for all three
  material families, checking interior and every silhouette pixel's
  premultiplied colour against its resolved alpha.
- Partial silhouette samples at 4x; MToon Mask cutoff equality at 1x and
  partial alpha-to-coverage at 4x; discarded Mask alpha zero. Coverage is
  checked against colour so storing coverage twice fails the check.
- Two overlapping Blend surfaces, with an independent source-over RGB and
  accumulated-alpha calculation at the interior.
- Opaque and translucent MToon/MMD hulls without a visible surface,
  checking their independent foreground alpha and premultiplied RGB.
- Recomposition of the transparent product over black, white and arbitrary
  solid linear RGB against separately rendered opaque-background images,
  within two UNORM levels per RGB channel. Background changes retain scene
  depth and all static resource/upload counters; returning to Transparent
  reproduces the foreground bytes exactly despite another stored clear colour.

The readback's extent, layout and interpretation metadata are also checked.
Vulkan validation reported no messages.

## Verification and remaining boundaries

The MSVC developer environment built both trees. Plain CMake CTest passed
19/19; combined `viewport-usd` passed 62/62, including Hydra, GPU, usdview,
viewport presentation, headless evidence and installed consumers. The
combined test process put the lookdev runtime's `bin` and `lib` on `PATH`.
`python scripts/check_docs.py` and `git diff --check` passed.

This is an offscreen API and numerical regression, not completion of
v0.5.0. Hydra still uses the default opaque setting, and its host alpha
convention/conversion remains to be established. The viewport still renders
directly into the swapchain; it does not consume a reusable foreground
product. PNG straight-alpha conversion, checkerboard/image compositing,
textured hair, normal-map and MMD self-shadow RGBA fixtures and bounded
tangent comparison remain outside this run. The
[remaining milestone](../../roadmap/v0.5.0.md) retains those acceptance gates.
No release was published.
