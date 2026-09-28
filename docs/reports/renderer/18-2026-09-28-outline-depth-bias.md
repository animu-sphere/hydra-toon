# The hull's depth bias: a hull as thin as nothing loses to the surface it outlines

- Date: 2026-09-28
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.13`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`) and the published `vrmImaging` 0.10.0
  (`sha256:894fd616…`) of [report 12](12-2026-09-28-published-vrmimaging.md),
  unchanged
- Occasion: [v0.2.0](../../roadmap/v0.2.0.md)'s outline stability item,
  after [report 17](17-2026-09-28-outline-meters-per-unit.md), whose §4
  found a hull thinner than the depth buffer resolves fighting its surface

## TL;DR

**`mtoon_outline` now rasterizes with a depth bias of one resolution step
plus the triangle's depth slope, away from the camera. A hull and a surface
at one depth were a tie that the hull won, since it draws first and the
test is `LESS`; now the surface wins it. A new headless shot draws exactly
that tie and failed before the bias. On the avatar in a stage of
centimetres with `toon:metersPerUnit` left at 1, 2,026 of report 17's
2,399 pixels that changed by more than 24 levels return to the surface,
and no pixel there got darker; what remains is the outlines that vanish,
as they must at that width. In the avatar's own stage of metres, 102 pixels
change by more than 24 levels, 93 of them from outline colour to surface:
specks on the hair, chest, hands and skirt frills where a correct 0.5 mm
hull had also been winning. No counter changes: the bias is pipeline
state, fixed for the renderer's life.**

## 1. What changed

- **Backend.** A scene pipeline description can ask for a depth bias;
  `mtoon_outline` alone does, with a constant factor of 1 and a slope
  factor of 1 and no clamp, which would need the `depthBiasClamp` feature.
  Depth grows away from the camera in this renderer, so a positive bias
  pushes back. The bias is static state: no dynamic state, draw constant or
  slot changes.
- **Headless.** `renderer.material.mtoon_outline` takes a sixth shot: a
  double-sided quad at z = 0.25 that faces away from the orthographic
  camera, so its back face shows and its hull's visible faces lie behind
  it, with a world-coordinates outline of 1 pm. The quad's coordinates round
  that move away, so the hull lands at the surface's depth. The check
  requires the centre pixel to be the surface's red, not the outline's
  green.

Why a bias and not a move of the hull in view space: where hull and
surface overlap on screen, the hull's visible faces are meant to lie behind
the surface, and the tie is only lost to rounding. The smallest step the
depth buffer resolves at that depth, which is what the constant factor is,
settles a tie and nothing more; the slope factor covers triangles seen at a
grazing angle, whose interpolated depths round further apart. A fixed
view-space push would instead be a length in the scene's unit, and would
hide a hull that legitimately lies just in front of another mesh.

## 2. What was run

```sh
ost build --jobs auto              # the new shot, before the bias: fails
ost build --jobs auto && ost test && ost validate
ost renderer viewport -- --frames 8 --hidden
ost validate --intent renderer-viewport
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra && ost validate --profile lookdev --intent hydra
ost package --profile lookdev --intent hydra
ost artifact import dist/toon/0.1.0/cy2026-windows-x86_64-py313-lookdev--hydra
# report 17's Formation, with toon at the new digest
ost formation lock formation.toml
TOON_UNITS_SHOTS=-m=- ost formation run formation.toml -- \
    testusdview <avatar.usdz> --renderer Toon --testScript vrm_units_check.py
TOON_UNITS_SHOTS=-cm-unset=-,-cm=0.01 ost formation run formation.toml -- \
    testusdview alicia-cm.usda --renderer Toon --testScript vrm_units_check.py
```

The new package is `sha256:1137bc1cb9e182d4db04b6742c3804f2226bcb777f0d7518444f31ea070a7bd9`,
in this workstation's registry only. `alicia-cm.usda` and
`vrm_units_check.py` are report 17's, unchanged, with
`TOON_EXPECT_MTOON=12`.

## 3. Results

| Build | Result |
| --- | --- |
| `core`, the new shot without the bias | `renderer.material.mtoon_outline` fails: *"a hull at its surface's depth must lose the depth test to it"* |
| `core` | `ost test` 4/4; `ost validate` passed; `renderer.antialiasing.msaa` still counts 152 mixed pixels around the outlined diamond and 71 along the Mask cut |
| `hydra` intent, canonical `lookdev` | `ost test` 10/10; `ost validate` passed |
| `renderer-viewport` | 8 frames presented at 4 samples, no swapchain recreation; `ost validate --intent renderer-viewport` passed |

The avatar's evidence is report 17's at every shot: `scene_revision` 2, 2
and 3; 4 pipelines; 20 point, topology and skin uploads; 13 material
writes; draws 20 MToon, 5 transparent, 15 outlined, 20 skinned.

The images, leaving out usdview's axes (2,280 pixels):

| Images | Pixels that differ | by more than 24 levels | largest difference |
| --- | --- | --- | --- |
| report 17 → now, metres | 289 | 102 | 92 |
| report 17 → now, centimetres at 0.01 | 289 | 102 | 92 |
| report 17 → now, centimetres at 1 | 2,195 | 2,002 | 194 |
| report 17: metres → centimetres at 1 | 3,661 | 2,399 | 194 |
| now: metres → centimetres at 1 | 1,417 | 390 | 109 |
| now: metres → centimetres at 0.01 | 6 | 0 | 6 |

Of report 17's 2,399 pixels, 2,026 now lie within 24 levels of the stage
of metres. Of the 2,002 pixels that the bias changes at 1 by more than 24
levels, every one got brighter: outline colour gave way to the surface.

## 4. Observations

- **The fight was there at the right width too.** In metres, 93 of the 102
  pixels that change by more than 24 levels got brighter: single pixels of
  outline colour on the hair, the chest, the hands, the skirt's frills and
  the feet, where a 0.5 mm hull at the camera's distance already rounded
  to its surface's depth. They are the kind of speck
  [report 16](16-2026-09-28-msaa.md) saw where the hull shows through;
  whether they are the same pixels was not checked.
- **Between meshes, the surface now wins a near tie as well.** The other 9
  lie where the ribbon meets the top of the head, and got darker: the
  hair's hull, all but touching the ribbon, had covered it, and now the
  ribbon shows. The bias makes no distinction between a hull's own surface
  and another mesh's, so a hull that is closer to another surface than one
  depth step loses to it.
- **What is left at 1 in centimetres is the width.** The 390 pixels that
  still differ by more than 24 levels from the stage of metres are the
  outlines that vanish at 0.0005 cm; report 17's blotches across the skirt
  are gone.
- **The scaled stage agrees more closely.** With the unit right, the
  stages of metres and centimetres now differ in 6 pixels by at most 6
  levels, where report 17 found 7 by up to 20: some of that residue was the
  same fight, decided differently by the other transform's rounding.

## 5. Not checked

Another GPU vendor's depth bias, which Vulkan defines for a float depth
buffer by the primitive's largest exponent, so the step it takes is the
implementation's. A perspective close-up where the slope factor dominates.
The rest of the outline stability item: the width under animation,
thin-outline aliasing and flicker, how the width texture is sampled, and
the hull's cost.
