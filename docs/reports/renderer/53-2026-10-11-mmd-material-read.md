# `hdToon` selects MMD from `mmdImaging`'s Hydra view, and every material of 41 local MMD stages reads as MMD

> Followed by [report 54](54-2026-10-11-geom-subset-bindings.md): `GeomSubset` material bindings, so the 20-material model is 20 draws, one per material.

> Followed by [report 55](55-2026-10-11-mat-q3-mmd-colour.md): MAT-Q3 decided, so MMD's textures will be sampled undecoded, not as the sRGB colour §1 reads them as.

- Date: 2026-10-11
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Tooling: `ost 0.23.14`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`); `usd-mmd-plugins` v0.2.0's `mmdImaging`, carrying
  `mmdSchema`, built and packaged on this workstation at the `lookdev`
  profile; this branch's `toon` `hydra` package, built here
- Occasion: v0.4.0 starts with MAT-Q1 agreed with `usd-mmd-plugins`
  ([roadmap](../../releases/v0.4.0.md)). That repository froze its Hydra view,
  `mmdImaging`, on 2026-10-04 in the shape `vrmImaging` has, so that this
  renderer reads both models by one path
  ([its material policy §12.1](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/MATERIAL_POLICY.md#121-the-hydra-view-mmdimaging)).
  This report reads it.

## TL;DR

**Rule 2 of the selection
([material policy §3](../../design/MATERIAL_POLICY.md#3-selection-a-realization-is-chosen-not-merged))
now holds: a material prim with `mmd/material` and no `vrm/mtoon` is MMD,
and its fields are normalized into `ToonMaterial`'s common part and a new
MMD block. A time move of one `mmd/material/<field>` reaches the slot by the
value-only route MToon uses. With `mmdImaging` in the session, every one of
the 495 materials in 41 local MMD stages selects MMD; with `mmdSchema` alone,
none does. In `usdview`, `hdToon` reports 20 MMD materials on a 20-material
model. MAT-Q1 is resolved. Nothing draws through an MMD pipeline yet, and the
model draws as one mesh in its display colour, because `hdToon` reads no
`GeomSubset` binding.**

## 1. The change

- **`ToonMaterial`** gains an MMD block: specular colour and power, ambient,
  the edge's alpha, the sphere texture and mode, the toon source with an
  individual texture or a shared index, the shadow flags, the vertex-colour,
  point and line flags, and the draw order. Diffuse RGBA is the common base
  colour and alpha, the base texture the common one, and `drawEdge`,
  `edgeSize` and the edge's RGB the common outline request
  ([material policy §4, §5](../../design/MATERIAL_POLICY.md#4-toonmaterial)).
  Colours are kept as authored: how they are interpreted is MAT-Q3.
- **`HdToonReadMaterial`** reads `mmd/material/<field>` and `mmd/drawOrder`
  after `vrm/mtoon` is found absent. The three texture slots are present only
  when authored, so an absent slot samples nothing. The base and sphere
  slots are always read; the toon slot only when `toonSource` is
  `individual`, and it samples clamped to the edge. `sharedToonIndex` is
  read only when `toonSource` is `shared`, so it stays -1 otherwise.
  Textures are decoded as sRGB colour, as the stage's `colorSpace` says.
- **Texture roles** grow from nine to eleven: the sphere and toon textures
  follow MToon's nine. A change of either texture's identity is structural.
- **Value-only changes.** A dirtied `mmd` locator without `material` is a
  value-only change, as a `vrm` one is, and the delegate re-reads the
  material in `Update()`
  ([material policy §8](../../design/MATERIAL_POLICY.md#8-values-that-change-at-run-time)).
- **Evidence.** `TOON_HYDRA_EVIDENCE` and the viewport's scene summary add
  `materials_mmd`; the viewport overlay counts MMD materials.

## 2. Tests

`toon-renderer-hydra-material`, with a retained scene index spelling
`mmdImaging`'s locators, now also checks that:

- every field of a fully authored contribution lands where §1 says,
  `drawOrder` included;
- an individual toon texture is sampled and clamped, and a shared index
  beside it is ignored;
- `vrm/mtoon` wins over `mmd/material` on one prim;
- a diffuse colour changed behind `mmd/material/diffuseColor` leaves the
  Sprim clean under emulation and reaches the material in the next sync,
  keeping its texture.

`toon-render-world` checks that the sphere and toon textures' identities are
structural and their transforms are not. The OpenStrata gates passed:
`core` 14/14, `hydra` 22/22, `viewport-usd` 53/53.

## 3. Local stages

The 15 local models of `usd-mmd-plugins`' reports, exported to USDZ by its
`mmd_export` at stage-contract version 2, are 41 files with their props. None
is committed. Each was copied to an ASCII path, because a CP932 command line
cannot carry their names
([`usd-mmd-plugins` opening guide](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/guides/opening.md)),
and read through UsdImaging's scene indices into `hdToon` by
`toon-hydra2-material-test --stage`:

```sh
# in usd-mmd-plugins
ost plugin build plugins/mmdSchema --profile lookdev
ost plugin build plugins/mmdImaging --profile lookdev
ost plugin test plugins/mmdImaging --profile lookdev
ost plugin package plugins/mmdImaging --profile lookdev
ost artifact import plugins/mmdImaging/dist/plugins/mmdImaging/0.2.0/cy2026-windows-x86_64-py313-lookdev
ost plugin run plugins/mmdImaging --profile lookdev -- \
    toon-hydra2-material-test.exe --stage <model.usdz>
ost plugin run plugins/mmdSchema --profile lookdev -- \
    toon-hydra2-material-test.exe --stage <model.usdz>
```

`ost plugin test` passed 13 checks, the adapter's registration among them.
The package is `sha256:39f3e24d66599012c5683b21c5d8afa7f4050d25806b9b6ca9b838725f39ac70`.

| Session | Files | Materials | MMD |
| --- | --- | --- | --- |
| `mmdImaging` (carrying `mmdSchema`) | 41 | 495 | 495 |
| `mmdSchema` alone | 3 checked | 24 | 0 |

The schema alone applies the API, but no adapter contributes `mmd`, so the
materials are PreviewSurface, as
[material policy §2](../../design/MATERIAL_POLICY.md#2-inputs) says such a
session falls back.

On one 20-material model, every material's line agrees with the same stage
read through `pxr.Usd`: 8 individual toon textures and 12 without a ramp, one
`add` sphere and 19 disabled, 9 edges drawn, and draw orders 0 to 19 in
material order.

## 4. The `usdview` session

A local Formation, untracked because it pins unpublished packages, composes
the runtime, `mmdImaging` and the `toon` package:

```toml
[runtime]
artifact = "sha256:b982656c07dd9147973e3c7197d9b050ee48dd1ac291988f7e6025c76c4a785b"

[[components]]
id = "mmdImaging"
kind = "plugin"
artifact = "sha256:39f3e24d66599012c5683b21c5d8afa7f4050d25806b9b6ca9b838725f39ac70"

[[components]]
id = "toon"
kind = "renderer"
artifact = "sha256:75083469a8a56d6bfc328758947c7e5a88ffc3fa7702c66678c5e96519138a48"
```

`ost formation lock` and `doctor` passed. `testusdview` with
`mmd_material_check.py`, the VRM host session's check asserting
`materials_mmd` instead, passed on the 20-material model with
`TOON_EXPECT_MMD=20`:

```text
renderer: HdToonRendererPlugin
materials_preview=1 materials_mtoon=0 materials_mmd=20 draws=1 draws_mtoon=0 draws_skinned=1
```

The PreviewSurface material is the render index's fallback material.

## 5. What it does not do yet

- **Draw MMD.** An MMD material is not MToon, so its mesh takes the path a
  PreviewSurface mesh takes: its display colour, unlit. `mmd_opaque`,
  `mmd_transparent` and `mmd_outline` come next.
- **`GeomSubset` bindings.** The importer authors one mesh per model and
  binds each material to a face subset of it
  ([`usd-mmd-plugins` stage contract §8.1](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/STAGE_CONTRACT.md#81-one-mesh-material-subsets)).
  `hdToon` reads only a mesh's own binding, so the model is one draw, and
  its 20 materials are synced but sampled by nothing. Per-subset draws are
  generic USD, not MMD; v0.4.0 needs them before any MMD material can draw.
- **MAT-Q2 and MAT-Q3.** A shared toon index is read and has no ramp to
  sample, and MMD's colours are carried as authored without a decision on
  their interpretation.
