# PreviewSurface constant inputs and basic PBR

- Date: 2026-10-17
- Machine: Windows 11 x86_64, NVIDIA RTX A5000, driver 597.16;
  MSVC 14.51 / Visual Studio 18
- Runtime: canonical CY2026 OpenUSD 26.08 `lookdev` for Hydra and the
  viewport; plain CMake needs no OpenUSD
- Scope: the first material-interoperability increment of
  [v0.5.0](../../roadmap/v0.5.0.md)

## Material input

`HdToonReadMaterial` follows the universal network's surface terminal to a
`UsdPreviewSurface` node and reads its constant or time-sampled
`diffuseColor`, `metallic`, `roughness`, `emissiveColor`, `opacity` and
`opacityThreshold` values. Absent inputs use PreviewSurface defaults.
Emission above one is preserved. Metallic, roughness, opacity and threshold
are clamped to the unit interval; non-finite scalar inputs take their defaults.

A positive opacity threshold selects Mask; otherwise opacity below one
selects Blend. An unreadable or unsupported terminal, including the specular
workflow, retains the mesh's display-colour fallback. A disconnected
PreviewSurface node does not select the PBR realization. Canonical MToon and
MMD containers continue to take precedence over the network.

`toon-renderer-hydra-material` now constructs real in-memory USD networks,
passes them through UsdImaging and the render index, and checks default and
authored inputs, colour and alpha-mode changes at two time codes, unsupported
terminal selection and the toon models' priority. No format plugin or
restricted asset is needed.

## Vulkan realization

`preview_surface.slang` is the ninth fixed scene pipeline, using the shared
surface vertex stage. It uses GGX distribution, Smith visibility and Schlick
Fresnel for metallic/roughness direct lighting, diffuse uniform ambient and
linear emission. Its model-specific parameter slot is 48 bytes, binding 7 of
the shared material set; the shared layout requires ten storage buffers
including the skin/morph set. A value edit rewrites the existing slot.

Opaque and surviving Mask fragments return alpha one and write depth;
Mask uses a hard threshold. Blend returns material opacity and uses the
existing source-over blend state without depth writes. This increment
retains the existing opaque scene clear and output conventions.

`toon-preview-surface-gpu` checks at both 1x and 4x:

- Linear emission channels against specified values.
- Dielectric and coloured-metal direct lighting against the analytic
  normal-incidence BRDF, within two RGBA8 levels.
- Mask discard and exact threshold equality, and whether depth is written.
- Blend colour against foreground-over-background and no depth write;
  Opaque ignoring material opacity.
- Single-sided culling and double-sided back faces, unreadable-material
  fallback, value-only slot writes and no static-resource recreation.
- No Vulkan validation messages.

The viewport's material-debug GPU test also verifies that a real bound
PreviewSurface responds to late colour/emission overrides, returns to its
original image exactly on release, and keeps depth, resident resources and
Hydra sync count unchanged. The material panel displays metallic and
roughness as read-only values.

## Verification

The Hydra-only tree was rebuilt from clean objects and its CTests passed
after updating the `usdview` smoke test's fixed pipeline count to nine.
The final combined `viewport-usd` tree passed all 56 CTests, including
`testusdview`, the new GPU check, late material overrides, presentation,
sample switching and install-tree checks. CTest was launched with the
runtime's `bin` and `lib` on `PATH`, as required for the Hydra-linked
viewport executable even in tests that open no stage.

The plain-CMake tree, rebuilt from clean objects without OpenUSD, passed
all 16 CTests, including the new 1x/4x GPU check and install-tree checks.

Builds used `cmake --build` with the MSVC developer environment; tests used
`ctest --test-dir <build tree> --output-on-failure`. This was an implementation
check, not the milestone release gate or package publication.

## Remaining evidence

Texture connections, UV transforms, normal mapping, face-varying mesh data,
shared authored/generated tangents and reference-renderer comparisons remain
in v0.5.0. No Seed-san comparison is claimed here. Mask alpha-to-coverage,
transparent RGBA output, premultiplication and compositing over selectable
backgrounds also remain. Environment reflections and arbitrary graph
evaluation are outside this basic fallback.
