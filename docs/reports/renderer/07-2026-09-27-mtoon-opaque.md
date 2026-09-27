# `mtoon_opaque` draws: meshes bind their material, and every avatar mesh with a VRM material is shaded as MToon

> Followed by [report 08](08-2026-09-27-basic-textures.md): the base and shade colour textures, so the avatar is no longer nearly white.

- Date: 2026-09-27
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.13`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`) and `vrmImaging` 0.9.0 (`sha256:3043a239…`) of
  [report 06](06-2026-09-27-vrm-formation.md), unchanged
- Occasion: the first Renderer Phase 1 slice. Until now `ToonMaterial` was
  read and normalized ([reports 03](03-2026-09-26-material-sprim.md),
  [04](04-2026-09-26-material-value-route.md)) but nothing drew with it

## TL;DR

**A mesh now binds its Hydra material, and a mesh whose material selected
MToon draws through `mtoon_opaque`: MToon's lit / shade split with shading
shift and toony, GI equalization and emission, from a parameter slot, with
no textures. On the avatar with `vrmImaging`, all 20 draws go through
`mtoon_opaque`; without it, none do. A value-only material edit rewrites one
slot and nothing else.** The lighting is a stand-in: one key light fixed to
the camera and a uniform ambient.

## 1. What changed

- **Core.** `MeshSnapshot` carries `material` (0 for none) and smooth vertex
  normals with their own revision, derived from the points and topology at
  commit, as Storm derives them for a skinned mesh or one that authors none.
  Triangles are counter-clockwise from the front. `DrawList` carries every
  material of the snapshot.
- **Backend.** Two scene pipelines, created once: the unlit mesh pipeline,
  for meshes that bind no MToon material, and `mtoon_opaque`
  (`shaders/mtoon.slang`). A material is a 64-byte slot in one host-visible
  storage buffer ([material policy §7](../../design/MATERIAL_POLICY.md#7-pipelines-and-parameter-buffers));
  a slot is written only when its material's parameters revision changes,
  and the buffer grows by doubling, keeping every slot. Cull mode (from
  double-sidedness) and front face (from a mirroring transform) are dynamic
  state, so neither is a pipeline variant. Normals reach the shader in view
  space through the inverse transpose of the model-view 3x3.
- **`hdToon`.** A mesh syncs `DirtyMaterialId` and binds by path; a binding
  is resolved again when a material prim under that path is created or
  destroyed, so the order Hydra syncs them in does not matter. A left-handed
  mesh's triangles are reversed. The frame evidence adds `material_writes`,
  `draws` and `draws_mtoon`.
- **Hosts.** The backend takes a `SceneShaders` set instead of one
  vertex/fragment pair, and the build copies every compiled shader next to
  each host.

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

The new package is `sha256:5b723bac5e7a4002feca29d14adc46562e4e1063ea8d167cd19855cb6ed69497`,
in this workstation's registry only. The `testusdview` script is report 06's:
one shot, then the last `TOON_HYDRA_EVIDENCE` line.

## 3. Results

| Build | Result |
| --- | --- |
| `core` | `ost test` 4/4; `ost validate` passed |
| `hydra` intent, canonical `lookdev` | `ost test` 9/9; `ost validate` passed with 14 of 14 renderer assertions |
| `renderer-viewport` | 8 frames presented, no swapchain recreation |

The new assertion, `renderer.material.mtoon_opaque`, renders the bootstrap
triangle bound to an MToon material with a red lit colour and a blue shade
colour on its own renderer. Facing the key light, the centre pixel is red;
after a shading shift of -1, a value-only edit, it is blue. Across the two
frames `material_writes` goes from 1 to 2 while pipelines, point uploads and
topology uploads stay where they were, and the renderer's validation capture
is empty. The triangle is back-face culled, so the check also fixes the
winding convention: counter-clockwise under the OpenGL clip convention is
front-facing after the Vulkan clip transform's y flip.

`renderer.frame.persistence` now requires both scene pipelines, created once,
across its 1,000 frames.

The avatar, the last frame of each run:

| Formation | `pipelines` | `materials_mtoon` | `material_writes` | `draws` | `draws_mtoon` | topology / point uploads |
| --- | --- | --- | --- | --- | --- | --- |
| runtime + `vrmImaging` + `toon` | 2 | 12 | 13 | 20 | 20 | 20 / 20 |
| runtime + `toon` | 2 | 0 | 13 | 20 | 0 | 20 / 20 |

`material_writes` is 13 in both: 12 VRM materials and the fallback material,
each written once, and never again over five frames. The frame showed the
avatar whole and correctly culled, in its shade colours on the side away from
the light.

## 4. Observations

- **The avatar is nearly white.** VRM carries most of a material's colour in
  its textures; the factors alone are mostly white. Textures are the next
  Phase 1 slice.
- **The lit side clips.** A white base colour under a unit key light plus
  0.25 ambient exceeds 1 and saturates in the RGBA8 target. That is MToon's
  formula under this stand-in lighting, not a defect of the shading; real
  lights and exposure are later work.
- **Blend materials draw opaque**, through `mtoon_opaque`, until
  `mtoon_transparent` exists. Mask materials test their constant alpha
  against the cutoff, which without a texture keeps or drops the whole
  material.
- **The interactive `usdview` shows the same.** Opened through the
  Formation (`ost formation run formation.toml -- usdview <avatar.usdz>
  --renderer Toon`), the avatar draws white with pale blue and pink shade
  colours at its edges, and the shading turns with the camera. Its HUD read
  about 17 FPS; that is not a measurement of this change, and every frame is
  still read back to the CPU.
- **Authored normals are not read.** Smooth normals are derived from the
  points, as Storm does for a skinned mesh; a hard edge an asset authors is
  lost.

## 5. Not checked

Linux. A mirrored transform on a real stage (the front-face flip is exercised
by no test). A material created after its mesh binds it, in a live session:
the rebinding path is covered by reasoning, not by a test.
