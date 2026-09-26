---
status: proposed
owner: hydra-toon
---

# Material policy

> Status: **proposed**, 2026-09-26. Nothing here is implemented. It becomes
> binding piece by piece as Renderer Phase 1 (MToon opaque), Phase 4 (MMD) and
> Phase 5 (PreviewSurface) land. It expands
> [DESIGN_POLICY.md](DESIGN_POLICY.md) §8 and wins over it on materials.

## 1. Purpose

Three material models reach this renderer: VRM's MToon, MMD's material, and
`UsdPreviewSurface`. Inside the renderer, all three draw through a small fixed
set of pipelines. This document says how each is read, what the renderer turns
it into, and what stays specific to one model.

## 2. Inputs

| Model | Recognized by | Read from | Owner |
| --- | --- | --- | --- |
| MToon | `VrmMToonAPI` applied to the `UsdShadeMaterial` | `inputs:vrm:material:*`, `inputs:vrm:mtoon:*`, `inputs:vrm:textureInfo:<slot>:*` | [`usd-vrm-plugins` material policy §6](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/MATERIAL_ARCHITECTURE_POLICY.md#6-canonical-vrm-material-semantics) |
| MMD | `MmdMaterialAPI` applied to the `UsdShadeMaterial` | `inputs:mmd:material:*`; `mmd:sourceIndex`; `primvars:mmd:edgeScale`, `primvars:mmd:uv1` | [`usd-mmd-plugins` material policy §4](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/MATERIAL_POLICY.md#4-canonical-material-semantics) |
| PreviewSurface | neither API applied; a `UsdPreviewSurface` reached from the material's surface output | the ordinary Hydra material network | OpenUSD |

The attribute names and their meaning belong to the owners and are not
restated here. MMD's stage-contract version 1 (schema-less `mmd:material:*`)
is not read: MMD rendering is Renderer Phase 4, after that repository's
version 2 became what its importer authors.

## 3. Selection: a realization is chosen, not merged

For each material, exactly one model applies, in this order:

1. `VrmMToonAPI` applied → **MToon**.
2. `MmdMaterialAPI` applied → **MMD**.
3. Otherwise → **PreviewSurface**, from the material's surface network.
4. Nothing readable → the fallback material.

A VRM or MMD material also carries `/preview` and `/mtlx` graphs authored by
its format repository. When rule 1 or 2 applies they are ignored: they are
portable approximations derived from the same semantics, and reading them
would make the toon look depend on a realization rather than the source
([integration scope §1](INTEGRATION_SCOPE_POLICY.md#1-the-rule)).

## 4. `ToonMaterial`

`ToonMaterial` is renderer-private. It is not a USD schema and never becomes
one here ([integration scope §4](INTEGRATION_SCOPE_POLICY.md#4-dependency-rules)
rule 4).

```cpp
enum class ToonShadingModel { PreviewSurface, MToon, MMD };
```

It has a small common part, which every pipeline reads the same way, and one
model-specific block:

| Part | Holds |
| --- | --- |
| common | shading model; base colour and alpha; base texture; alpha mode and cutoff; cull mode (double-sidedness); emissive; outline enable, width, colour; sort key |
| MToon block | shade colour and texture; shading shift and toony; GI equalization; MatCap; parametric rim and rim texture; outline width mode and lighting mix; UV animation and mask; render-queue offset; transparent-with-Z-write |
| MMD block | specular colour and power; ambient; sphere texture and mode; toon source, texture or shared index; shadow flags; vertex-colour and draw-points / lines flags |

Only what is truly the same goes into the common part. MMD-only concepts —
sphere modes, sub-texture UVs, shared toon slots, shadow flags, per-vertex edge
scale — stay in the MMD block and are not approximated into MToon terms, and
the same holds the other way. A later common abstraction is extracted from
two working paths, not designed ahead of them.

## 5. Outline

Both models state an outline request; the renderer decides how to draw it.
The initial method for both is inverted hull
([DESIGN_POLICY.md](DESIGN_POLICY.md) §10).

| | MToon | MMD |
| --- | --- | --- |
| enabled | `outlineWidthMode` ≠ `none` | `drawEdge` |
| width | `outlineWidthFactor` × `outlineWidthMultiply` texture, in world or screen units by mode | `edgeSize` × `primvars:mmd:edgeScale` |
| colour | `outlineColorFactor`, mixed with lighting by `outlineLightingMixFactor` | `edgeColor` (RGBA) |

`outlineWidthMode` is an MToon semantic, not an instruction to use a
particular technique: `screenCoordinates` is honoured by scaling the hull
offset in clip space, not by switching to a screen-space outline.

## 6. Transparency and draw order

- **MToon** sorts by alpha mode, then `renderQueueOffsetNumber`, and writes
  depth for transparent materials only with `transparentWithZWrite`.
- **MMD** draws in material-table order (`mmd:sourceIndex`) with alpha
  blending, because models are authored against that order.

The sort key is computed once, when the material is normalized, and is part
of the persistent draw packet ([DESIGN_POLICY.md](DESIGN_POLICY.md) §15).

## 7. Pipelines and parameter buffers

Seven pipelines, fixed ([DESIGN_POLICY.md](DESIGN_POLICY.md) §8):

```text
mtoon_opaque  mtoon_transparent  mtoon_outline
mmd_opaque    mmd_transparent    mmd_outline
preview_surface
```

A material is a slot in its model's parameter buffer plus texture indices.
Normalizing a new material writes a slot; it never compiles a shader. A
feature a material does not use is a zero or a disabled flag in its slot, not
a pipeline variant.

## 8. Values that change at run time

The canonical inputs of both models are varying, so a runtime can override or
time-sample them: a VRM expression's material binds
([`usd-vrm-plugins` §6.7](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/MATERIAL_ARCHITECTURE_POLICY.md#67-expressions-change-semantic-slots-not-shader-inputs)),
an MMD material morph
([`usd-mmd-plugins` §11](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/MATERIAL_POLICY.md#11-material-morphs)).

A changed value updates that material's parameter slot and nothing else
([DESIGN_POLICY.md](DESIGN_POLICY.md) §14). It does not re-select the model,
rebuild the draw packet or touch the pipeline. Only a change to which API is
applied, a texture's identity, the alpha mode or double-sidedness is
structural.

## 9. Open questions

| Id | Question | Proposed answer | Resolve by |
| --- | --- | --- | --- |
| MAT-Q1 | How the Material interface inputs `inputs:vrm:*` and `inputs:mmd:material:*` reach the render delegate. Hydra's material network is built from what the surface terminals connect to, so an input no realization connects to may never arrive; the format repositories describe different paths ([integration scope §6](INTEGRATION_SCOPE_POLICY.md#6-cross-repository-observations)) | Measure on OpenUSD 26.08 before choosing. Candidates: what already arrives through the material network; a UsdImaging API-schema adapter per API, registered by schema name so no link to the format repository is needed; a `hydra-toon` scene index reading the prim's data sources. One path for both models is preferred. Where an adapter lives (here, or `mmdImaging` in `usd-mmd-plugins`) is agreed with its owner | Renderer Phase 1 |
| MAT-Q2 | MMD's shared toon ramps (`sharedToonIndex` 0–9) belong to MMD and are not redistributable, and the stage names no image for them | `hydra-toon` ships its own ramp set whose terms allow redistribution, mapped by index | Renderer Phase 4 |
| MAT-Q3 | MMD colours are authored as stored, with no declared colour space, and MMD shades without colour management | Decide, and record, how the MMD path interprets them, against reference renders from MMD itself | Renderer Phase 4 |
