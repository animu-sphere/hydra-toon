# PreviewSurface texture inputs and USD UV transforms

- Date: 2026-10-18
- Machine: Windows x86_64, NVIDIA RTX A5000, driver 597.16
- Build: MSVC developer environment; canonical CY2026 OpenUSD 26.08
  `lookdev` for the combined `viewport-usd` build; plain CMake without OpenUSD
- Scope: ordinary USD PreviewSurface image inputs in v0.5.0

## Input normalization

The universal surface terminal still selects `UsdPreviewSurface`, after
canonical MToon and MMD selection. The bounded network reader follows direct
`UsdUVTexture` connections for diffuse/emissive RGB and scalar
metallic/roughness/opacity R, G, B or A. A connected value replaces the surface
constant. Each input carries its own scale, bias and missing-image fallback.

Colour space comes from `sourceColorSpace`, including the asset-parameter
`colorSpace` metadata UsdImaging produces from it. `raw` and `sRGB` are
explicit; `auto` asks Hio about the image. Identical path/encoding pairs share
the existing image cache; the same file used as sRGB and raw has two entries.
The Vulkan image view decodes sRGB RGB before scale/bias and shading, while
alpha stays linear. Texture transforms and scale/bias do not change image
identity.

Coordinates come from `UsdPrimvarReader_float2` for `st`, optionally through
one `UsdTransform2d`, or a constant float2. USD's bottom-left coordinates are
converted into the existing texture-reference convention, including its
degrees-to-radians rotation and translation after the V flip. Missing mesh
UVs use the primvar reader's fallback coordinates; missing images use the
texture node's RGBA fallback. Repeat, clamp and mirror wrapping are supported,
including Hio sampler metadata; absent metadata defaults to repeat.

These node definitions follow the installed OpenUSD shader definitions and
the [OpenUSD PreviewSurface specification](https://openusd.org/release/spec_usdpreviewsurface.html).

## Vulkan realization and checks

The fixed PreviewSurface pipeline and shared image table are retained. Its
material slot grows from 48 to 528 bytes: the existing three float4 values
and five 96-byte texture-input records. The core PreviewSurface block owns
metallic/roughness/opacity texture references; diffuse/emissive reuse the
common references. Image identity changes are structural. UV transforms,
scale/bias and fallback are values, including on the validated late-frame
path.

`toon-renderer-hydra-material` writes a controlled one-pixel PNG and imports
a real in-memory USD graph through UsdImaging. It checks five connected
inputs, independent channels, sRGB/raw cache sharing, wrap modes, scale/bias,
fallback, and transformed coordinates at two times. A subsequent transform
edit changes only the material parameters revision; a missing image keeps
the texture fallback, and an unsupported UV set leaves the surface constant.
The test applies queued stage changes as the viewport does.

`toon-preview-surface-gpu`, at 1x and 4x, checks:

- Emission textures replacing zero emission, and scale/bias applied after
  linear or sRGB sampling.
- A packed image driving independent RGB base colour, B metallic and G
  roughness, against the analytic normal-incidence BRDF.
- Linear A opacity through textured Mask discard, exact cutoff equality and
  Blend, including depth writes.
- Missing-image fallback, missing-UV primvar fallback and constant coordinates.
- Bottom-left UV orientation and rotation/translation using a four-colour
  image with known texel centres.
- A value or UV edit rewriting one material slot without image/geometry
  uploads or pipeline creation, and no Vulkan validation messages.

Core state checks cover the three added texture identities and value-only
UV edits. Late-frame checks reject invalid channels, non-finite texture
parameters and structural texture changes before mutating the draw list.

## Verification

The combined `viewport-usd` tree passed all 56 CTests, including real USD
material networks, 1x/4x texture checks, the existing MToon/MMD GPU checks,
late material overrides, presentation, sample switching, `testusdview` and
the install-tree check. CTest ran with the lookdev runtime's `bin` and `lib`
on `PATH` for the Hydra-linked executables.

The plain-CMake tree without OpenUSD passed all 16 CTests, including the
expanded GPU check and the install-tree check. Both trees were rebuilt with
`cmake --build`; verification used `ctest --test-dir <tree> --output-on-failure`.
Changed documents' relative paths and `git diff --check` also passed. This
was an implementation check, not a milestone release or package publication.

## Limits and follow-up

Normal maps and the shared authored/generated tangent foundation,
face-varying mesh data and reference-renderer comparisons remain in
[v0.5.0](../../releases/v0.5.0.md). The reader does not evaluate arbitrary
graphs, other UV sets, chained transforms or black-border wrapping. Such
unsupported texture connections retain the surface constant. The existing
loader stores RGBA8, including downconversion of 16-bit integer images;
floating-point/HDR images are not supported. Mask remains a hard cutoff;
transparent-background RGBA and premultiplication remain separate work.

This establishes no glTF PBR or Seed-san reference equivalence. Those cases
move to the later [MaterialX work](../../roadmap/later.md#gltf-pbr-through-materialx).
