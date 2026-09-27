---
status: proposed
owner: hydra-toon
---

# Material policy

> A model's part of this policy binds once that model draws; what draws is the
> [capability matrix](../reference/CAPABILITY_MATRIX.md). It expands
> [DESIGN_POLICY.md](DESIGN_POLICY.md) §8 and wins over it on materials.

## 1. Purpose

Three material models reach this renderer: VRM's MToon, MMD's material, and
`UsdPreviewSurface`. Inside the renderer, all three draw through a small fixed
set of pipelines. This document says how each is read, what the renderer turns
it into, and what stays specific to one model.

## 2. Inputs

| Model | Recognized by | Read from | Owner |
| --- | --- | --- | --- |
| MToon | `VrmMToonAPI` applied to the `UsdShadeMaterial`; in Hydra, a `vrm/mtoon` container on the material prim | `inputs:vrm:material:*`, `inputs:vrm:mtoon:*`, `inputs:vrm:textureInfo:<slot>:*`, as `vrmImaging` exposes them on the Hydra material prim: `vrm/material`, `vrm/mtoon`, `vrm/textureInfo/<role>` | [`usd-vrm-plugins` material policy §6](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/MATERIAL_ARCHITECTURE_POLICY.md#6-canonical-vrm-material-semantics); the Hydra view, [imaging policy §28.1](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/VRM_IMAGING_POLICY.md#281-the-vrm-locator-hierarchy) |
| MMD | `MmdMaterialAPI` applied to the `UsdShadeMaterial` | `inputs:mmd:material:*`; `mmd:sourceIndex`; `primvars:mmd:edgeScale`, `primvars:mmd:uv1`; the Hydra view is `mmdImaging`'s, which does not exist yet (MAT-Q1) | [`usd-mmd-plugins` material policy §4](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/MATERIAL_POLICY.md#4-canonical-material-semantics) |
| PreviewSurface | neither API applied; a `UsdPreviewSurface` reached from the material's surface output | the ordinary Hydra material network | OpenUSD |

The attribute names and their meaning belong to the owners and are not
restated here.

The canonical values reach the renderer only through the format repository's
UsdImaging adapter, never through the material network: a realization does
not connect to them, so the network never carries them
([renderer report 02](../reports/renderer/02-2026-09-26-mat-q1-material-inputs.md)).
`hdToon` therefore reads the Hydra material prim's own data sources, from the
render index's terminal scene index, in the locator hierarchy the adapter's
owner froze. It links neither the schema nor the adapter. A session that
registers either without the other, or neither, carries no `vrm` container,
and the material is PreviewSurface by §3 — a silent fall-back that the
session's composition, not this renderer, has to prevent. MMD's
stage-contract version 1 (schema-less `mmd:material:*`) is not read: the MMD
path reads version 2, which is what that repository's importer authors.

## 3. Selection: a realization is chosen, not merged

For each material, exactly one model applies, in this order:

1. `VrmMToonAPI` applied — in Hydra, `vrm/mtoon` present → **MToon**.
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

MToon and MMD are separate shaders, not one shader with a model switch. They
share what is truly common — skinning, morph evaluation, the texture table,
light data, the outline's hull, math helpers — as included Slang modules.

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
([DESIGN_POLICY.md](DESIGN_POLICY.md) §14). A value-only change is a dirtied
`vrm/<group>/<field>` locator with nothing under `material`, which scene
index emulation translates to no dirty bit on a classic `HdMaterial`
([report 02](../reports/renderer/02-2026-09-26-mat-q1-material-inputs.md)):
`hdToon` takes it from the terminal scene index it observes
(`HdRenderDelegate::SetTerminalSceneIndex`, `Update`), not from `Sync`. It does not re-select the model,
rebuild the draw packet or touch the pipeline. Only a change to which API is
applied, a texture's identity, the alpha mode or double-sidedness is
structural.

## 9. Open questions

| Id | Question | Proposed answer | Resolve by |
| --- | --- | --- | --- |
| MAT-Q1 | How `inputs:mmd:material:*` reaches the render delegate. For MToon this is answered by §2 and §8 ([report 02](../reports/renderer/02-2026-09-26-mat-q1-material-inputs.md)) | The same shape for `mmdImaging` — a container on the Hydra material prim, read from the terminal scene index — whose Hydra view is `usd-mmd-plugins`' to define, so that one read path serves both models | agreed with `usd-mmd-plugins` before v0.4.0 |
| MAT-Q2 | MMD's shared toon ramps (`sharedToonIndex` 0–9) belong to MMD and are not redistributable, and the stage names no image for them | `hydra-toon` ships its own ramp set whose terms allow redistribution, mapped by index | v0.4.0 |
| MAT-Q3 | MMD colours are authored as stored, with no declared colour space, and MMD shades without colour management | Decide, and record, how the MMD path interprets them, against reference renders from MMD itself | v0.4.0 |
