# MMD sub-texture spheres: a `subTexture` sphere multiplies at the mesh's additional UV 1

- Date: 2026-10-12
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Tooling: `ost 0.23.14`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`); `usd-mmd-plugins` v0.2.0's `mmdImaging` at the
  `lookdev` profile (`sha256:39f3e24d…`, [report 53](53-2026-10-11-mmd-material-read.md));
  this branch's `toon` `hydra` package, built here
  (`sha256:481ab3c6aa93460fc12d62524cedf6f51f447a50e123d3fd27d3e73bbe3cdf35`),
  and [report 58](58-2026-10-12-mat-q4-edge-width.md)'s (`sha256:330e4e1a…`)
  to compare against; Python 3 with Pillow and NumPy for the comparisons
- Occasion: v0.4.0's sub-texture spheres, which sample the mesh's
  additional UV ([roadmap](../../releases/v0.4.0.md)).

## TL;DR

**An MMD material whose sphere mode is `subTexture` now multiplies by its
sphere texture sampled at the mesh's additional UV 1, as MikuMikuEffect's
`full.fx` does, where it used to apply nothing. The hydra adapter reads
`primvars:mmd:uv1`'s x and y, and `mmd_opaque` and `mmd_transparent` take
them as a fourth vertex stream. On a local model with an additional UV, the
frame is identical to report 58's package's, pixel for pixel, and turning
two of its materials' spheres into sub-textures changes their pixels and no
others. No local model uses the mode, so it is not compared with MMD's own
frames, though on that model's two MMD captures hdToon's frame is within
4 levels of MMD's on 75% of the pixels without MMD's self-shadow and 38%
with it.**

## 1. What MMD does

PMX's sphere mode 3 is a sub-texture: the sphere texture is drawn as an
ordinary texture through additional UV 1 instead of mapped from the normal
([`usd-mmd-plugins` material policy §8](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/MATERIAL_POLICY.md#8-sphere-textures)).
`full.fx`, which reproduces MMD's own shading and which `mmd_opaque`
follows ([report 56](56-2026-10-11-mmd-surface.md)), takes the sphere
coordinate from `TEXCOORD1`, additional UV 1's x and y, when
`use_subtexture` is set, and combines the texel as a sphere that does not
add: its colour multiplies, and its alpha multiplies the alpha. The stage
authors the channel raw, as `primvars:mmd:uv1`, a `float4` per point
([stage contract §8.3](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/STAGE_CONTRACT.md#83-uvs)),
so its origin is the image's top left, the space the sphere texture is
already sampled in.

Of the 41 local MMD models, 28 declare an additional UV, and none of
their 495 materials is in sphere mode 3: 12 spheres multiply, 79 add and
404 are disabled. A comparison with MMD's frames needs a model that uses it.

## 2. The change

- **Core.** `MeshSnapshot::sub_texture_uvs`, set by
  `RenderWorld::SetMeshSubTextureUVs` and with its own revision: additional
  UV 1's x and y, one per point, empty for a mesh without one. Setting the
  UVs a mesh already has changes nothing. The fast snapshot and the avatar
  adapter treat them as they treat the edge scales: structural.
- **Hydra.** `HdToonMesh` reads `primvars:mmd:uv1` with vertex or varying
  interpolation and keeps its x and y.
- **Pipelines.** `mmd_opaque` and `mmd_transparent` read a fourth vertex
  stream, the sub-texture UV, bound with stride 0 to (0, 0) when the mesh
  has none; `mmd_outline`'s fourth stream stays the edge scale. The vertex
  stage passes the UV to the fragment, which samples the sphere there in
  mode `kSphereSubTexture` and multiplies by it. A sub-texture's alpha
  decides, with the base texture's, whether the material can blend, as a
  sphere's does. The slot is unchanged: the mode was already in it.

## 3. Tests

- `renderer.material.mmd` (headless, GPU): a sphere texture 64 on its left
  half and 192 on its right, which the normal would sample in the middle
  and blend to 13 over an ambient of 0.5, reads 30 at additional UV 1's x
  0.75 and 4 at 0.25.
- `toon-render-world`: a sub-texture UV edit advances its own revision
  alone, and setting the same UVs changes nothing.
- `toon-renderer-hydra-geom-subsets`: the fixture's mesh carries
  `primvars:mmd:uv1`; its x and y reach the mesh, one per point, and an
  edit of it reaches the mesh without touching its points, UVs or edge
  scales.

The OpenStrata gates passed, each with `ost build`, `ost test` and
`ost validate`: `core` 15/15, `hydra` 24/24 and `viewport-usd` 55/55;
`ost package` packaged the `hydra` build twice, to the same digest.

## 4. On a local model

The local MMD Formation, repinned to each package, drew a 22-material model
with a declared additional UV (a local test model, not committed) at
1920 × 1080 from the default camera of `mmd_reference_shot.py`, with 22 MMD
draws and 13 edge hulls each time:

| Frame | Pixels differing from this branch's frame |
| --- | --- |
| report 58's package | 0 |
| this branch's, materials 10 and 16 as sub-textures of their base textures | 36,831, within those two materials |

The second frame's session layer sets the two materials' `sphereMode` to
`subTexture` and their `sphereTexture` to their base texture; their pixels
darken from a mean of (140, 109, 145) to (43, 36, 88), as a texel
multiplying itself at another UV would. The session layer has to write the
texture's resolved path: a path relative to the stage, written there,
resolves against the anonymous session layer, finds nothing and leaves the
sphere white.

## 5. The same model against MMD

The user took two captures of the same model, unmodified, in MMD v9.32 at
3840 × 2160 from centre (0, 10, 0), distance 45 and a 30° field of view, one
with MMD's self-shadow and one without it. hdToon's frame from that camera
lines up with both: the model's bounds agree to a pixel. Every material's
diffuse is 1, so the lighting term saturates and the frame is the same at
light 154 and 255. On the 562,270 pixels both captures cover opaquely:

| MMD capture | Mean difference | Within 4 levels | hdToon over by 20 or more | MMD over by 20 or more |
| --- | --- | --- | --- | --- |
| with self-shadow | 11.21 | 38% | 89,372 | 12,882 |
| without self-shadow | 5.34 | 75% | 22,805 | 17,232 |

Self-shadow alone changes 277,083 of those pixels in MMD's frames: the
hair's shadow on the forehead, a shadowed thigh, and darker hair, cape
lining and ornaments. Without it, what differs from hdToon's frame lies
along contours, where an edge or a silhouette lands a fraction of a pixel
apart, and at the skirt, which MMD's physics moved. The frame with the two
sub-texture materials of §4 is 23.36 from the self-shadow capture on
average, against 11.21: MMD draws the model's spheres as authored, and its
captures say nothing of sub-textures.

## 6. What it does not do yet

- **MMD's own frames of a sub-texture.** No local model uses one; the
  result is `full.fx`'s, not measured against MMD.
- **Self-shadow.** MMD's self-shadow, which `castSelfShadow` and
  `receiveSelfShadow` ask for, is not drawn; §5's captures are its
  reference.
- **Additional UVs 2–4**, and the UV morphs that move any of them, are not
  read.
