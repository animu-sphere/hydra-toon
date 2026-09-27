# Inverted-hull outline: `mtoon_outline` draws the avatar's outlines, and an outline edit rewrites one slot

- Date: 2026-09-27
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.13`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`) and `vrmImaging` 0.9.0 (`sha256:3043a239…`) of
  [report 06](06-2026-09-27-vrm-formation.md), unchanged
- Occasion: the last Renderer Phase 1 item of
  [design policy §25](../../design/DESIGN_POLICY.md#25-implementation-phases),
  after [report 09](09-2026-09-27-gpu-skinning.md)

## TL;DR

**An MToon draw whose material asks for an outline also draws its inverted
hull through `mtoon_outline`: each skinned vertex moves out along its normal
by MToon's width, in world units or as a ratio of the screen height, times
the outline width texture's G, and the hull is drawn with its front faces
culled in the outline colour. On the avatar with `vrmImaging`, 8 of the 12
MToon materials ask for a 0.5 mm world-coordinates outline, and 15 of the 20
draws add a hull. Widening one material's outline, or turning it off, is a
value-only edit that rewrites one parameter slot and uploads nothing.** At
the default full-body framing the avatar's outline is about a pixel wide.

## 1. What changed

- **Core.** `ToonMaterial`'s MToon block carries the outline width texture,
  whose identity is structural like every texture's; the outline's mode,
  width, colour and lighting mix stay values
  ([material policy §8](../../design/MATERIAL_POLICY.md#8-values-that-change-at-run-time)).
  `HasOutline` says whether a material's draws add a hull: an MToon material
  whose width mode is not None, with a width above zero.
- **Backend.** A third scene pipeline, `mtoon_outline`, created once beside
  the other two, with `mtoon_opaque`'s vertex streams, constants and dynamic
  state. The material set is now visible to the vertex stage too, since the
  width is applied there, and a material's slot grew from 144 to 224 bytes:
  the outline colour and lighting mix, the width, the width mode, and the
  width texture's table entry, sampler and transform. The last push constant
  word carries `|projection[1][1]|` for the screen-coordinates width, so the
  constants stay 128 bytes. The hulls are recorded before the surfaces, as
  design policy §10 orders them, with front faces culled whatever the
  material's double-sidedness, as MToon states.
- **Shader.** `mtoon.slang`'s draw constants, parameter slot, stand-in
  lights and surface shading moved to `mtoon_common.slang`, which
  `mtoon.slang` and the new `mtoon_outline.slang` include. The outline's
  vertex stage skins the point and normal, then moves the point by the width
  along the view-space normal, carried back into the mesh's space through the
  transposed normal matrix, so the move is a view-space length under any
  scale the transform or skin has. worldCoordinates is that length;
  screenCoordinates scales it by 2 · w / |projection[1][1]|, which makes it
  that ratio of the screen height at the vertex's depth, under a perspective
  or an orthographic camera
  ([material policy §5](../../design/MATERIAL_POLICY.md#5-outline)). The
  width texture is sampled at mip 0. The fragment stage shades the hull as
  the surface and returns `outlineColorFactor` × lerp(1, that shading,
  `outlineLightingMixFactor`), as the MToon specification defines the mix,
  after the same Mask test.
- **`hdToon`.** The material reads `vrm/textureInfo/outlineWidthMultiply` as
  data, so it is a texture of its own even when a colour role names the same
  image. The host frame evidence adds `draws_outline`.

## 2. What was run

```sh
ost build --jobs auto && ost test && ost validate
ost renderer viewport -- --frames 8 --hidden
ost validate --intent renderer-viewport
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra && ost validate --profile lookdev --intent hydra
ost package --profile lookdev --intent hydra
ost artifact import dist/toon/0.1.0/cy2026-windows-x86_64-py313-lookdev--hydra
# report 09's two Formations, with toon at the new digest
ost formation lock formation.toml
ost formation run formation.toml -- testusdview <avatar.usdz> \
    --renderer Toon --testScript vrm_outline_check.py
```

The new package is `sha256:8397030d7095b289053831ecca2343bc71b1eef9c4f0bb725f598c24f39fd846`,
in this workstation's registry only. `vrm_outline_check.py` takes three
shots: the avatar as authored; after tripling the first outlined material's
`inputs:vrm:mtoon:outlineWidthFactor` on the session layer; and after setting
its `outlineWidthMode` to `none`. It asserts that each edit advanced
`material_writes` by one and no upload or pipeline counter, and that the last
one lowered `draws_outline`.

## 3. Results

| Build | Result |
| --- | --- |
| `core` | `ost test` 4/4; `ost validate` passed |
| `hydra` intent, canonical `lookdev` | `ost test` 10/10; `ost validate` passed with 17 of 17 renderer assertions |
| `renderer-viewport` | 8 frames presented, no swapchain recreation; `ost validate --intent renderer-viewport` passed |

The new assertion, `renderer.material.mtoon_outline`, draws an octahedron of
radius 0.5 through an orthographic camera, where it is the diamond
|x| + |y| ≤ 0.5, bound to an MToon material with a red lit colour, a green
outline and a lighting mix of 0. It reads the pixel at x = 0.58 on the centre
row, outside the surface and inside a hull of width 0.2:

| Frame | Outline | Rim pixel | Centre pixel |
| --- | --- | --- | --- |
| 1 | worldCoordinates, 0.2 | green | red: the hull stays behind its surface |
| 2 | none | background | red |
| 3 | screenCoordinates, 0.1 of the height | green: the same 0.2 under this camera | red |
| 4 | as 3, with a 1×1 width texture whose G is 0 | background: the vertex stage sampled it | red |

`material_writes` is 1, 2, 3 across the first three frames, the texture is
uploaded once in the fourth, and points, topology and pipelines stay where
they were; the validation capture is empty. `renderer.frame.persistence` now
requires the three scene pipelines, created once, and the `usdview` smoke
test asserts three pipelines.

The CTest `toon-renderer-hydra-material` gives the textured material an
`outlineWidthMultiply` role naming the image its base colour role names: it
becomes a second texture, decoded as data.

The avatar, the evidence at each shot:

| Formation | Shot | `draws_outline` | `material_writes` | point / topology uploads | `texture_uploads` | `pipelines` |
| --- | --- | --- | --- | --- | --- | --- |
| runtime + `vrmImaging` + `toon` | authored | 15 of 20 | 13 | 20 / 20 | 6 | 3 |
| | `Alicia_body` width × 3 | 15 | 14 | 20 / 20 | 6 | 3 |
| | `Alicia_body` mode `none` | 13 | 15 | 20 / 20 | 6 | 3 |
| runtime + `toon` | authored | 0 | 13 | 20 / 20 | 0 | 3 |

Without `vrmImaging` every material is PreviewSurface, so nothing asks for an
outline. With it, `Alicia_body`, `_body_wear`, `_wear`, `_face`, `_hair`,
`_hair_trans_zwrite`, `_hair_wear` and `_hair_trans` author a
worldCoordinates outline of 0.0005; `_eye`, `_eye_white`, `_face_mastuge` and
`_other_zwrite` author none. The stage's `metersPerUnit` is 1.

## 4. Observations

- **At full-body framing the outline is about a pixel.** 0.5 mm on an avatar
  some 400 pixels tall is a fraction of a pixel; against report 09's bind
  shot, 351 pixels change by more than 24 levels, all within the avatar's
  bounds. A diagnostic shot with every width × 20 showed a continuous rim
  along the hair, ribbon, arms and sleeves, darker than the surface beside it
  since the materials mix lighting into the outline colour, with no cracks
  or spikes.
- **Derived smooth normals suit the hull.** Normals are derived from the
  points, never read (Renderer Phase 1's stand-in), so a hard edge an asset
  authors does not split the hull open.
- **Widening an outline is a value.** The 3× shot changed 97 pixels and
  wrote one slot; so did turning it off, which dropped the two draws bound to
  `Alicia_body` from the outline pass without touching the draw list.
- **The avatar samples no outline width texture.** `texture_uploads` stays
  at report 08's 6, so no material names an `outlineWidthMultiply` image; the
  texture path is covered by the headless check alone.
- **The `hydra` build tree had lost its header dependencies.** `ninja -t
  deps` reported `#deps 0` for 14 objects, the core libraries' among them, so
  the core header change did not recompile `extraction.cpp.obj`, and the
  post-build `toon-headless` segfaulted on the old `ToonMaterial` layout.
  Deleting those objects and building again fixed it; the `core` tree kept
  its dependencies.

## 5. Not checked

Linux. A screenCoordinates outline on a real asset, or under a perspective
camera: the avatar authors only worldCoordinates, and the headless check's
camera is orthographic, where w is 1. An outline width texture on a real
asset. A mirrored transform, for which the hull culls the other winding. A
stage not in metres. Performance: no timing was taken; the outline pass runs
the vertex stage a second time for 15 of the avatar's 20 draws.
