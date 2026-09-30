# Scene lights and viewport material diagnostics

- Date: 2026-09-30
- Environment: Windows 11, MSVC 14.51, NVIDIA RTX A5000, Vulkan loader
  1.4.321; OpenStrata 0.23.14 and canonical CY2026 OpenUSD 26.08 `lookdev`
- VRM imaging: published `vrmImaging` 0.10.0 and its `vrmSchema`, as in
  [report 25](25-2026-09-30-mtoon-rest.md); the same local file-format and
  package resolver for the raw avatar
- Occasion: v0.2.0 scene lights, following the remaining MToon inputs

## Data and lighting

`RenderWorld` owns revisioned `ToonLight` records, copied into a reused
snapshot and draw list independently of meshes and materials. Each source
has a type, linear colour including intensity, world position and travel
direction, visibility, and basic spot shaping. Non-finite edits and zero
directions are ignored; colours are nonnegative, direction is normalized,
and cone angle and softness are bounded. Removing a light removes its
record, and re-adding a USD light gets a fresh core identity.

Hydra distant lights become directional sources, sphere lights become
points or spots when their cone angle is at most 90 degrees, and dome
lights become uniform ambient. Colour is multiplied by intensity,
`2^exposure`, diffuse and the enabled UsdLux blackbody colour. Transform
maps the source origin and local -Z travel direction into world space;
USD visibility reaches the core. Both time samples and ordinary USD edits
reach the same Sprim sync path.

The GPU frame buffer grows from 16 to 2,080 bytes at the existing descriptor
binding: two float4 header rows and four rows for each of 32 direct sources.
It holds time, material debug mode, metres per unit, uniform ambient, and
view-space light data. Every frame writes this fixed mapped buffer after
the preceding frame completes, with no allocation or descriptor changes.
Material slots remain 544 bytes and draw push constants remain 128 bytes.
Visible direct lights fill slots in core-id order; excess sources are
omitted. Every visible ambient source sums into the header independently.

For each direct source, MToon's shifted/toony N·L interpolates shade and
base colour, multiplied by incident colour. Contributions add. Points
and spots attenuate by inverse-square distance in metres, bounded at
0.01 metres; spot cone edges use a hard cut or a smooth inner/outer cosine
transition. Uniform ambient multiplies lit colour, so GI equalization has
no directional variation to equalize. Emission is added once, and rim
lighting mix uses the sum of incident direct colour and ambient. The
shared shading code also supplies the outline's lighting mix. These
follow the [MToon lighting model](https://github.com/vrm-c/vrm-specification/blob/master/specification/VRMC_materials_mtoon-1.0/README.md#lighting).

A scene with no supported lights retains the previous camera key and
0.25 uniform ambient. A rig with all lights hidden or zero strength remains
dark; it does not switch to the fallback. The viewport can force camera
lighting, scale direct and ambient independently, and inspect base colour,
mapped normals, direct illumination or ambient without material edits.
The diagnostics apply to MToon; generic fallback meshes remain unlit.

## Regression evidence

| Check | Observed result |
| --- | --- |
| `toon-render-world` | Light lifecycle, normalized direction, invalid/unchanged edits, visibility revisions and extraction; geometry and material revisions remain unchanged |
| `toon-renderer-hydra-lights` | Real UsdLux scene indices: four source kinds, colour/exposure/diffuse/temperature, transform, cone shaping, animated intensity/rotation/visibility, parameter edits at unchanged time, removal and re-addition |
| `renderer.lighting.scene` | Coloured directional shade/base split, multiple sources and ambient, one-/two-metre inverse-square point falloff, scene-unit conversion, camera translation, spot hard cut/soft penumbra, hidden-rig darkness, debug modes, emission/rim in darkness and under an actual source, all three alpha modes, and fallback restoration |
| Light/frame persistence | Light, camera and diagnostic changes add no material writes, point/topology uploads or pipeline creation; GPU validation messages remain zero |

## Viewport image route

The committed `adapters/viewport/tests/scene-lights.usda` contains an MToon
octahedron, directional key, point fill, shaped spot and dome ambient.
Eleven frames choose time codes 1 through 2 in steps of 0.1, changing only
the key's intensity and rotation. At 256x256, 4x MSAA, the final capture
equals a fresh start at time code 2 byte for byte. Time 1 and time 2 differ
at 9,557 pixels. Both have one MToon draw and one hull, one point/topology
upload and two material writes (including fallback), no textures or poses,
and one capture readback. No Vulkan validation messages are emitted.

The normal diagnostic was also captured with direct strength zero and the
overlay enabled. It shows mapped view-space normals and retains surface
alpha and outline coverage; captures exclude the overlay. This run uploads
one overlay texture and no additional scene state.

These fixtures require `vrmImaging`/`vrmSchema`, which are not regular CTest
dependencies. With their plugin paths and DLL directories registered:

```sh
<build>/adapters/viewport/toon-viewport --hidden --vsync off --overlay off \
    --usd adapters/viewport/tests/scene-lights.usda --time 1 --time-step 0.1 \
    --frames 11 --width 256 --height 256 --expect-draws 1 --expect-hulls 1 \
    --screenshot build/scene-lights-animated.ppm
<build>/adapters/viewport/toon-viewport --hidden --vsync off --overlay off \
    --usd adapters/viewport/tests/scene-lights.usda --time 2 --frames 1 \
    --width 256 --height 256 --screenshot build/scene-lights-time-2.ppm
cmake -DFIRST=<absolute static capture> -DSECOND=<absolute animated capture> \
    -P adapters/viewport/compare_captures.cmake
```

## Animated avatar

The same local AliciaSolid retargeted VRMA 01 stage as report 25 was drawn
for 120 frames, time code 0 by steps of 0.25, at 800x900 and 4x MSAA. A local
layer adds a warm directional key, blue point fill and uniform ambient,
with the source stage's time range, time rate, unit and up axis explicitly
set on the root layer. Layer metadata is necessary to preserve those
evaluation conditions when composing the stage.

Both scene and forced-camera lighting draw 20 MToon meshes, all GPU-skinned
with authored normals, five transparent surfaces and fifteen hulls. Each
uploads 20 point/topology/skin buffers, 13 materials, seven textures and
2,400 joint poses, with one final readback and no validation messages.
The forced-camera image is byte-identical to report 25's previous avatar
capture. The scene rig changes the lighting. These images, model and
motion remain local and establish integration, not a reference-renderer
reproduction claim.

## Build and validation

```sh
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
ost build --jobs auto
ost test
ost validate
```

The `viewport-usd` tests pass 27/27 and the `core` tests pass 4/4.
Both evidence validations pass, including `renderer.lighting.scene`.
The local registry uses `OST_HOME=C:/Users/snkm/.ost`.

## Limits

This is a basic avatar lighting model. Sphere radius, area integration and
UsdLux power normalization are not implemented; a sphere's intensity is
used directly as its inverse-square coefficient. Distant angular size,
rect/disk/cylinder lights, instanced lights, host `simpleLight` sources,
shadows, IES, filters and light linking are not supported. A dome samples
no environment texture, and ambient is uniform. The fixed buffer supports
32 visible direct lights, ordered by core identity rather than spatial
selection. `usdview` hosts supply `toon:metersPerUnit` for point falloff;
the viewport reads the stage unit. These limits do not assert physical
UsdLux equivalence or production IBL/shadow behavior.
