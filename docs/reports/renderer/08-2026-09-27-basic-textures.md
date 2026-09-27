# Basic textures: the avatar draws in its base and shade colour textures

- Date: 2026-09-27
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.13`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`) and `vrmImaging` 0.9.0 (`sha256:3043a239…`) of
  [report 06](06-2026-09-27-vrm-formation.md), unchanged
- Occasion: the Renderer Phase 1 slice after
  [report 07](07-2026-09-27-mtoon-opaque.md), which left the avatar nearly
  white because VRM carries most of a material's colour in its textures

## TL;DR

**`mtoon_opaque` samples MToon's base colour texture and shade multiply
texture, with UVs from the mesh's `st`, each through its own wrap and
`KHR_texture_transform`. On the avatar with `vrmImaging` the 12 MToon
materials sample 6 images, each decoded and uploaded once, and the avatar
draws in its own colours. A UV transform edit rewrites one parameter slot and
uploads nothing.** Uploads are recorded on the render thread, as geometry's
are.

## 1. What changed

- **Core.** `RenderWorld` owns textures: a `ToonTexture` is decoded RGBA8,
  first row at the top, and colour (sRGB) or data (linear), with its own
  revision. `ToonMaterial` references one by id through a `ToonTextureRef`
  that also carries glTF's wrap modes and `KHR_texture_transform`: the base
  texture in the common part and the shade texture in the MToon block, as
  [material policy §4](../../design/MATERIAL_POLICY.md#4-toonmaterial) lays
  them out. Which texture a material samples is structural (§8); its wrap
  and transform are values. A mesh carries `st` with its own revision.
- **Backend.** The material descriptor set gains a texture table of 128
  entries and 9 immutable samplers, one per wrap pair; a material's slot
  (now 144 bytes) names a table entry and a sampler per texture, plus each
  transform as a 2x3 affine map. Entry 0 is a white placeholder, and every
  entry points at it until a texture takes it, so the table is always valid
  and a material without a texture samples white. A texture is uploaded only
  when its revision changes: staged, copied and mipmapped by blits at the
  start of the frame that first samples it. A mesh without UVs binds an
  8-byte zero buffer with stride 0, a dynamic state, so neither case is a
  pipeline variant. The device must offer
  `shaderSampledImageArrayDynamicIndexing` and room for the table.
- **Shader.** `st` is flipped into glTF's UV space, whose origin is the
  image's top left as the image's first row is, and the transform is applied
  as glTF states it. Lit is base colour × base texture, shade is shade
  colour × shade texture, alpha is base alpha × texture alpha. Both samples
  come before a Mask discard, so their derivatives are defined.
- **`hdToon`.** The material reads `vrm/textureInfo/baseColor` and
  `vrm/textureInfo/shadeMultiply`
  ([`vrmImaging` §29](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/VRM_IMAGING_POLICY.md#29-settled-in-step-i2)):
  `file`'s resolved path, `texCoord`, `wrapS`, `wrapT` and
  `transform/{offset,rotation,scale}`. The image is decoded with `HioImage`,
  which opens it through Ar, so a path inside a `.usdz` reads as a file
  does. One texture serves every material and role that names the same path
  and encoding, and is removed when the last of them releases it. A mesh
  reads a vertex or varying `st`. The frame evidence adds
  `texture_uploads` and `textures`.

## 2. What was run

```sh
ost build --jobs auto && ost test && ost validate
ost renderer viewport -- --frames 8 --hidden
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra && ost validate --profile lookdev --intent hydra
ost package --profile lookdev --intent hydra
ost artifact import dist/toon/0.1.0/cy2026-windows-x86_64-py313-lookdev--hydra
# report 06's two Formations, with toon at the new digest
ost formation lock formation.toml
ost formation run formation.toml -- testusdview <avatar.usdz> \
    --renderer Toon --testScript vrm_material_check.py
```

The new package is `sha256:91bc5c8b52b207eec59f399fb59ceb46d3ef0ca2407a391c1c16408e6a63b021`,
in this workstation's registry only. The `testusdview` script is report 06's,
run with `TOON_EXPECT_MTOON` set to the MToon count each Formation should
show: 12 with `vrmImaging`, 0 without.

## 3. Results

| Build | Result |
| --- | --- |
| `core` | `ost test` 4/4; `ost validate` passed |
| `hydra` intent, canonical `lookdev` | `ost test` 9/9; `ost validate` passed with 15 of 15 renderer assertions |
| `renderer-viewport` | 8 frames presented, no swapchain recreation |

The new assertion, `renderer.material.mtoon_textured`, draws the bootstrap
triangle with every corner at `st` (0.25, 0.75), sampling a 2x2 sRGB
texture whose top left is red, top right green, bottom left blue and bottom
right white:

| Frame | Edit | Centre pixel |
| --- | --- | --- |
| 1 | the texture as base colour texture | red: `st`'s bottom-left origin reaches the image's top row |
| 2 | base offset (0.5, 0), a value-only edit | green; `material_writes` +1, no upload |
| 3 | the same texture as shade texture at offset (0, 0.5), shading shift -1 | blue |

`texture_uploads` is 1 across the three frames, pipelines and geometry
uploads stay where they were, and the renderer's validation capture is
empty.

`toon-renderer-hydra-material` gains a texture case on a retained scene
index: an image written by the test decodes top row first; one image named
by two roles of one material and by a second material is one texture; wrap
and transform are read; a path that does not resolve, or `texCoord` 1,
samples nothing; and once no material names the image, naming it again
decodes a new texture.

The avatar, the last frame of each run:

| Formation | `materials_mtoon` | `textures` | `texture_uploads` | `material_writes` | `draws_mtoon` | `pipelines` | topology / point uploads |
| --- | --- | --- | --- | --- | --- | --- | --- |
| runtime + `vrmImaging` + `toon` | 12 | 6 | 6 | 13 | 20 | 2 | 20 / 20 |
| runtime + `toon` | 0 | 0 | 0 | 13 | 0 | 2 | 20 / 20 |

The frame showed the avatar in its own colours: skin, eyes and hair from the
base textures, the shade colours carried by the shade textures, the face and
clothing the right way up.

## 4. Observations

- **One slot write per material, one upload per image.** 12 materials name
  6 images; each image was decoded and uploaded once over five frames.
- **The lit side still clips** under the stand-in light, as in report 07; a
  textured white now saturates the same way.
- **`HgiGL` reports 4,096 `GL error: invalid operation` lines** from
  `_DestroyDescriptorCacheItem` when `testusdview` closes. Report 07's
  package, run the same way, reports the same 4,096, so it is not this
  change; it was not investigated further.
- **Only `st` is read.** The importer writes TEXCOORD_0 to `st` and no other
  set, so a role on another set samples nothing. A face-varying `st` would
  need split vertices, which the core does not make, and is not read.
- **Sampling is trilinear.** glTF's sampler filters are not on the stage
  ([`vrmSchema` contract](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/plugins/vrmSchema/docs/SCHEMA_CONTRACT.md)),
  so every texture is sampled trilinearly, with mipmaps when the format can
  blit them.

## 5. Not checked

Linux. A texture transform other than the identity on a real asset: the
avatar states none, and the headless check covers an offset only. A texture
table that fills: past 128 images a texture samples the placeholder until an
entry frees, which no test drives. 16-bit and grey-alpha images, which the
decoder converts but no test feeds it.
