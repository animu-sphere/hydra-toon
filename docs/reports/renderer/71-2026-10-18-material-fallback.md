# Explicit unsupported-material fallback

- Date: 2026-10-18
- Machine: Windows x86_64, NVIDIA RTX A5000, driver 597.16
- Build: MSVC 14.51; canonical CY2026 OpenUSD 26.08 `lookdev`, combined
  `viewport-usd`; plain CMake without OpenUSD
- Scope: v0.5.0's explicit fallback for unreadable bound materials

## Behaviour

An unreadable Hydra material normalizes to opaque unlit magenta `(1, 0, 1)`.
It draws both faces and writes depth through the existing mesh pipeline.
Unknown shaders, missing or dangling surface terminals, empty material data,
malformed material containers and unsupported PreviewSurface specular
workflow take that route. Canonical MToon and MMD selection retains priority.
An unused PreviewSurface node does not rescue an unknown surface terminal.

The Vulkan material cache retains the common base colour for an unlit bound
material. Command recording pushes that colour separately for each mesh
part; a part without an available material instead uses mesh display colour.
The renderer-neutral default material is consequently opaque unlit white.
No shader, pipeline, parameter-buffer layout or dependency was added.

Unreadable graphs supply no textures or opacity. Repairing one restores its
normal surface inputs and texture references. A readable surface's
unsupported individual texture connection still retains the surface input's
constant, and a missing image still uses the texture node's fallback.

## Controlled evidence

`toon-renderer-hydra-material` checks real USD networks and retained scene
index data, including unknown/specular-workflow networks, a dangling surface
terminal, empty/malformed material data and canonical toon priority. A
textured surface becomes unsupported and releases both resident images;
repairing it restores its inputs and the shared images.

`toon-renderer-hydra-material-fallback-gpu` constructs a redistributable
in-memory USD mesh with three face ranges: an emissive PreviewSurface, an
unknown shader and an unbound face. At 120 × 64, with 1x and 4x MSAA, the
interior pixels independently match the authored emission, opaque magenta
and mesh display colour, within two 8-bit levels per RGB channel and with
alpha 255. Fallback depth is below the clear value.

Repairing the unknown shader to PreviewSurface restores its authored 0.25
Blend opacity: the expected foreground-over-clear RGB is
`(0.0625, 0.275, 0.1625)`, and depth stays clear. Reverting the shader restores
opaque magenta and depth writes. Increasing direct illumination to ten
leaves fallback and unbound colours unchanged. The two graph transitions
write the affected material slot twice in total; they retain points, topology, textures
and pipelines, and the lighting-only frame writes no material slot. No
Vulkan validation messages were recorded.

`toon-preview-surface-gpu` independently checks an unlit bound material's
back face and exact magenta RGBA, then a base-colour edit with one material
write and no geometry uploads or pipeline creation. Removing its binding
restores the mesh's display colour. These checks run at 1x and 4x MSAA.

## Verification

Both trees were rebuilt with `cmake --build`, then checked with
`ctest --test-dir <tree> --output-on-failure`:

- Combined `viewport-usd`: all 59 CTests passed, including the new fallback
  GPU check, material state checks, skinning/morph/subset checks, viewport
  presentation, `testusdview` and the install tree.
- Plain CMake without OpenUSD: all 16 CTests passed, including the expanded
  PreviewSurface GPU check and the install tree.

The builds used the MSVC developer environment. The combined test process
also put the lookdev runtime's `bin` and `lib` on `PATH` for direct viewport
launches. Changed documents' relative links and `git diff --check` passed.

## Limits

The fallback identifies unreadable materials; it does not interpret an
unknown shader, implement specular workflow or compile arbitrary graphs.
A binding whose material prim is unavailable has no renderer material and
continues to use display colour. Fallback pixels use the consumer's existing
colour encoding and output conventions; reusable RGBA output remains in
[v0.5.0](../../releases/v0.5.0.md), alongside tangent fidelity and normal
mapping. These controlled checks require no external reference renderer or
restricted asset, and no package or milestone release was published.
