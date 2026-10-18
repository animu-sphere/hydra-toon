# PreviewSurface normal mapping on shared tangent frames

- Date: 2026-10-18
- Machine: Windows x86_64, NVIDIA RTX A5000, driver 597.16
- Build: MSVC 14.51; canonical CY2026 OpenUSD 26.08 `lookdev`, combined
  `viewport-usd`; plain CMake without OpenUSD; optional avatar state without Vulkan
- Scope: v0.5.0's PreviewSurface normal input and shared tangent-frame shading

## Behaviour

The Hydra material reader consumes constant and time-sampled `normal3f`
PreviewSurface inputs and direct RGB `UsdUVTexture` connections, including
the existing `st` reader, one `UsdTransform2d`, scale/bias, encoding and image
fallback. The image reuses `ToonMaterial::normal_texture`; the sixth
`preview.inputs` record describes its sampling, and `preview.normal` is the
signed constant, default `(0,0,1)`. The Vulkan PreviewSurface slot is 640 bytes.
Normal constants, scale/bias and UV edits are parameter changes; image
identity remains structural. Late snapshots reject non-finite normal values.

PreviewSurface uses the same fourth tangent stream, skin/morph evaluation,
model/view reflection handling and fragment orthogonalization as MToon.
The shared `TangentMappedNormal` accepts a decoded vector and its +Y
orientation. PreviewSurface's +Y follows bottom-left `st`'s +V and texture
scale/bias performs any [0,1] decoding. MToon retains its own XY normal scale,
texture decoding and opposite bitangent. Sampling transforms do not rotate
the frame. All PreviewSurface implicit texture samples and frame derivatives
precede Mask discard.

Authored frames take precedence over the derivative fallback and can map
PreviewSurface constants or fallback samples without mesh UVs. A missing
image uses its texture fallback before scale/bias. A zero mapped vector or
unusable frame retains the vertex normal. CPU-deformed meshes without a
matching tangent output continue to use derivatives.

## Controlled evidence

`toon-preview-surface-gpu` compares full colour/depth images with independent
CPU-baked normal directions at 1x and 4x MSAA. It checks signed constants,
mirrored and rotated derivative frames, a four-direction normal image's
bottom-left origin, transformed sampling without frame rotation, arbitrary
scale/bias, primvar fallback coordinates without UVs, missing images, zero
vectors and degenerate UVs. A UV edit writes one material slot while retaining
texture uploads, geometry and pipelines.

`toon-tangent-gpu` adds 24 PreviewSurface CPU-normal comparisons at each
sample count, alongside the existing 24 MToon comparisons. The cases combine
both handedness signs, joint/model reflections, nonuniform scales, two
weighted joints, position/normal morph offsets and Opaque/Mask/Blend.
Constant UVs cannot supply the tested authored frame. Every RGBA byte stays
within two levels; pose and tangent edits retain point, topology, skin and
morph uploads. MToon's analytic back-facing hull checks also pass.

`toon-renderer-hydra-material` checks real USD constants and time samples,
raw normal-image sharing with other data roles, scale/bias, transformed UVs,
missing images, graph repair, unsupported UVs and normal disconnection.
OpenUSD 26.08's UsdImaging does not publish an input's authored constant
while connected: an unsupported normal connection consequently retains the
schema default `(0,0,1)`. Disconnecting publishes and restores that constant.

`toon-renderer-hydra-primvars-gpu` adds a real USD normal graph with a texture
fallback, time-sampled scale and indexed face-varying tangent seams. Its
Vulkan image agrees with independently supplied CPU-normal vertices at both
sample counts, within two byte levels. Repeated draws retain static resources.
All these GPU checks report zero Vulkan validation messages.

## Verification

Built the CMake trees and ran `ctest --test-dir <tree> --output-on-failure`:

- Combined `viewport-usd`: 61/61 passed, including Hydra, viewport, `testusdview`
  and installed consumers.
- Plain CMake without OpenUSD: 18/18 passed.
- Optional avatar state, Release without Vulkan/OpenUSD: 14/14 passed.

The builds used the explicit MSVC developer environment. The initial
PowerShell developer-shell launcher failed to parse its installation metadata;
loading `VsDevCmd.bat` supplied the compiler environment. Direct viewport
tests initially lacked runtime DLLs; the final combined run included the
lookdev runtime's `bin` and `lib` on `PATH`.

The pre-existing no-Vulkan tree had a corrupt localized include-prefix cache
and missed header dependencies, leaving old material-layout objects behind.
A clean rebuild removed those stale objects before its final tests. Its
installed-consumer compiler probes used process-local `_CL_=/Z7`, as in
report 72. No compiler installation or tracked build configuration changed.

## Limits

These are controlled GPU and CPU-reference checks, not equivalence to an
external renderer or a glTF tangent generator. MikkTSpace generation,
external authored/generated tangent comparisons and the PBR reference-renderer
checks remain in the v0.5.0 roadmap. Tangent morph deltas, other UV sets,
arbitrary normal graphs and floating-point textures are not added. Reusable
premultiplied RGBA output and compositing remain separate milestone work.
No restricted asset or capture is committed, and no release was published.
