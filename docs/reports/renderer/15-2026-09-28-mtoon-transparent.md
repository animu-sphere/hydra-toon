# MToon transparency: Blend draws through `mtoon_transparent` in render-queue order, and the avatar's four Blend materials blend

- Date: 2026-09-28
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.13`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`) and the published `vrmImaging` 0.10.0
  (`sha256:894fd616…`) of [report 12](12-2026-09-28-published-vrmimaging.md)
- Occasion: the first item of the
  [roadmap's priority](../../roadmap/README.md#priority), MToon transparency,
  in [v0.2.0](../../releases/v0.2.0.md)

## TL;DR

**MToon's Blend alpha mode draws through a fourth pipeline,
`mtoon_transparent`, blended source-over after every opaque and Mask draw,
in MToon's render queue order, writing depth only with
`transparentWithZWrite`. A double-sided transparent surface draws its back
faces first, and a transparent material's hull draws after its surface with
its alpha.** On the avatar with `vrmImaging`, the four Blend materials, all
with `transparentWithZWrite`, are 5 of the 20 draws; blending changes 140
pixels around the face, the bangs' tips and the eyelashes' edges. Setting
those four materials to OPAQUE gives back report 14's image exactly, pixel
for pixel.

## 1. What changed

- **Core.** `IsTransparent`, `RenderQueue` and `WritesDepth` state
  [material policy §6](../../design/MATERIAL_POLICY.md#6-transparency-and-draw-order)'s
  rules. The sort key is MToon's render queue: Opaque 2000, Mask 2450, Blend
  with `transparentWithZWrite` 2501 plus `renderQueueOffsetNumber` clamped
  to [0, 9], Blend without it 3000 plus the offset clamped to [−9, 0]; the
  offset is read for MToon alone. The queue and depth writes are values,
  not structure: an edit rewrites the material's slot, and the draw's order
  and depth state are recorded from it each frame.
- **Backend.** `mtoon_transparent` is a fourth scene pipeline, created
  once, with `mtoon_opaque`'s streams, constants and dynamic state, and
  source-over blending (colour by source alpha; alpha as coverage, one plus
  one minus source alpha). Depth writes are dynamic state in every MToon
  pipeline, set per draw from the material. `mtoon_outline` blends too: a
  draw flag, `kDrawBlend`, makes its hull return the surface's alpha, and
  any other hull returns 1, so one pipeline serves both. `MeshCache::Record`
  draws:
  1. unlit draws;
  2. the opaque pass, Opaque and Mask: every hull, then every surface, as
     before;
  3. the transparent pass: the transparent draws, stably sorted by queue, so
     within one queue they keep the order the draw list gives them; each
     surface, back faces first when it is double-sided, then its hull.

  The order is by queue, not by distance, so an avatar's layers do not swap
  as the camera moves. The hull follows its surface, as UniVRM's pass order
  and three-vrm's material groups draw MToon's outline: a surface that
  writes depth then hides the hull's far side, which drawing the hull first
  would put under a see-through surface.
- **Shader.** `mtoon_transparent.slang` shades as `mtoon_opaque` does and
  returns the surface's alpha, base alpha × base texture alpha. The
  surface's vertex stage moved to `mtoon_common.slang` as `SurfaceVertex`,
  which both surface pipelines call.
- **`hdToon`.** Nothing new is read: `alphaMode`, `renderQueueOffsetNumber`
  and `transparentWithZWrite` already were. The frame evidence adds
  `draws_transparent`, and the `usdview` smoke test expects four pipelines.

## 2. What was run

```sh
ost build --jobs auto && ost test && ost validate
ost renderer viewport -- --frames 8 --hidden
ost validate --intent renderer-viewport
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra && ost validate --profile lookdev --intent hydra
ost package --profile lookdev --intent hydra
ost artifact import dist/toon/0.1.0/cy2026-windows-x86_64-py313-lookdev--hydra
# report 14's Formation, with toon at the new digest
ost formation lock formation.toml
ost formation run formation.toml -- testusdview <avatar.usdz> \
    --renderer Toon --testScript vrm_transparent_check.py
```

The new package is `sha256:176091626c92e7bc9bd37bdc0cf0dd0bff751a408de0f8c1be8574a40ff6ebe9`,
in this workstation's registry only. `vrm_transparent_check.py` takes three
shots, with `TOON_EXPECT_MTOON=12`: the avatar as authored; after clearing
`transparentWithZWrite` on every Blend material that sets it; and after
setting every Blend material's `alphaMode` to OPAQUE, both on the session
layer. It asserts four pipelines, that the edits advanced `material_writes`
by one per material edited, that `draws_transparent` fell to 0 only with
the second, and that no upload or pipeline counter moved.

## 3. Results

| Build | Result |
| --- | --- |
| `core` | `ost test` 4/4; `ost validate` passed |
| `hydra` intent, canonical `lookdev` | `ost test` 10/10; `ost validate` passed with 19 of 19 renderer assertions |
| `renderer-viewport` | 8 frames presented; `ost validate --intent renderer-viewport` passed |

The new assertion, `renderer.material.mtoon_transparent`, draws through
[report 10](10-2026-09-27-mtoon-outline.md)'s orthographic camera, where a
point at z lands at depth 0.5 − 0.5z. Lit colours saturate and the target
is UNORM, so a Blend layer of alpha 0.5 halves what is behind it and adds
half its colour. Every pixel is read at the centre unless it says
otherwise, ± 16:

| Frame | Scene | Read | Expected |
| --- | --- | --- | --- |
| 1 | red Blend 0.5 at z 0.5, created first; opaque blue at −0.5 | 128, 0, 128 | the opaque draw first, whatever the creation order |
| 2 | + green Blend 0.5 at 0.2, the same queue | 64, 128, 64 | red, then green, as the draw list orders them; green is behind red, but red writes no depth |
| 3 | green `renderQueueOffsetNumber` −1 | 128, 64, 64; depth 0.75 | green first, and neither writes depth |
| 4 | red `transparentWithZWrite` | 128, 0, 128; depth 0.25 | red in queue 2501, before green's 2999; its depth hides green |
| 5 | blue Mask, alpha 0.7, cutoff 0.5 | 128, 0, 128 | above the cutoff, whole and opaque |
| 6 | blue alpha 0.3 | 134, 13, 19 | cut away: red over the background |
| 7 | one double-sided Blend mesh: its front triangle at 0.5 samples red, its back one at 0.3, facing away and listed second, green; over opaque blue | 128, 64, 64 | back faces first; in list order it would be 64, 128, 64 |
| 8 | an octahedron, Blend 0.5 with `transparentWithZWrite`, a green unlit world outline of 0.2 | x = 0.58: 6, 140, 19 | the hull at half its alpha over the background |
| | | centre: 134, 13, 19 | the surface's depth hides the hull's far side |

Frames 3 and 4 each rewrite one slot and upload nothing; the pipelines stay
where they were across all eight, and the validation capture is empty. The
CTest `toon-render-world` checks the queue's values, the clamps, the depth
rule and that a queue or depth write edit is not structural.

The avatar, the evidence at each shot:

| Formation | Shot | `material_writes` | `draws_transparent` | point / topology uploads | `texture_uploads` | `pipelines` |
| --- | --- | --- | --- | --- | --- | --- |
| runtime + `vrmImaging` + `toon` | authored | 13 | 5 of 20 | 20 / 20 | 7 | 4 |
| | `transparentWithZWrite` cleared on 4 materials | 17 | 5 | 20 / 20 | 7 | 4 |
| | `alphaMode` OPAQUE on 4 materials | 21 | 0 | 20 / 20 | 7 | 4 |

The avatar's Blend materials are `Alicia_face_mastuge`, the eyelashes,
`_hair_trans_zwrite` with an offset of 1, `_hair_trans` and
`_other_zwrite`, all with `transparentWithZWrite`; the other eight are
OPAQUE. None is double-sided.

## 4. Observations

- **Blending is on the face.** Against report 14's image, 140 pixels
  brighten by more than 24 levels and 2 darken, at most 98 levels, all in
  the face's box: the bangs' tips fade into the skin rather than ending in
  a hard edge, and the eyelashes' anti-aliased edges, drawn as solid brown
  and salmon texels before, blend. The eye highlights stay.
- **OPAQUE gives report 14 back exactly.** With the four materials set to
  OPAQUE, the image equals report 14's authored shot in every pixel: the
  opaque pass is unchanged, and `mtoon_outline`'s blending with alpha 1 is
  exact.
- **Without `transparentWithZWrite` the bangs darken.** Clearing it darkens
  158 pixels in the bangs: those surfaces no longer write depth, so their
  hulls' far sides, drawn after them, blend over them in the outline
  colour. That is what depth writes are for; UniVRM draws the same.
- **A queue edit is a value.** Four materials' depth writes rewrote four
  slots, and four alpha modes four more, with nothing uploaded.

## 5. Not checked

Linux. The Formation without `vrmImaging`: nothing selects MToon there, so
nothing is transparent. A double-sided Blend material on a real asset: the
avatar has none. Sorting within one queue by distance, which Unity and
three.js do and this does not. Order-independent transparency. Transparent
shadows, since there are no shadows. Performance: no timing was taken; the
transparent pass binds a pipeline per surface and per hull, and a
double-sided surface draws twice.
