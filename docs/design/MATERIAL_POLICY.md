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
| MMD | `MmdMaterialAPI` applied to the `UsdShadeMaterial`; in Hydra, an `mmd/material` container on the material prim | `inputs:mmd:material:*` and `mmd:sourceIndex`, as `mmdImaging` exposes them on the Hydra material prim: `mmd/material/<field>`, `mmd/drawOrder`; `primvars:mmd:edgeScale`, `primvars:mmd:uv1` on the mesh | [`usd-mmd-plugins` material policy §4](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/MATERIAL_POLICY.md#4-canonical-material-semantics); the Hydra view, [§12.1](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/MATERIAL_POLICY.md#121-the-hydra-view-mmdimaging) |
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
registers either without the other, or neither, carries no `vrm` or `mmd`
container, and the material is PreviewSurface by §3 — a silent fall-back that the
session's composition, not this renderer, has to prevent. MMD's
stage-contract version 1 (schema-less `mmd:material:*`) is not read: the MMD
path reads version 2, which is what that repository's importer authors.

## 3. Selection: a realization is chosen, not merged

For each material, exactly one model applies, in this order:

1. `VrmMToonAPI` applied — in Hydra, `vrm/mtoon` present → **MToon**.
2. `MmdMaterialAPI` applied — in Hydra, `mmd/material` present → **MMD**.
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
| common | shading model; base colour and alpha; base texture; normal texture and scale; alpha mode and cutoff; cull mode (double-sidedness); emissive and its texture; outline enable, width, colour; sort key |
| MToon block | shade colour and texture; shading shift, its texture and contribution scale, and toony; GI equalization; MatCap; parametric rim and rim texture; outline width mode and lighting mix; UV animation and mask; render-queue offset; transparent-with-Z-write |
| MMD block | specular colour and power; ambient; the edge colour's alpha; sphere texture and mode; toon source, texture or shared index; shadow flags; vertex-colour and draw-points / lines flags; draw order; the texture, sphere and toon tints of its material morphs (§11) |

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
`worldCoordinates` is in metres, as VRM's lengths are; the renderer divides
it by the stage's `metersPerUnit`. That is stage metadata Hydra does not
carry, so the host states it as the render setting `toon:metersPerUnit`,
which is 1, VRM's unit, until it does.

MMD states its edge's size and no unit. On MMD's own frames the edge is a
share of the frame's height: a narrower field of view or a larger frame
leaves the share as it is, and it falls with the distance from the camera,
not in proportion to it. The renderer draws an edge of `edgeSize` ×
`edgeScale` as that size times (0.00108 + 0.0203 / the vertex's view depth
in MMD units) of the target's height, the depth taken as at least one MMD
unit (MAT-Q4). The edge is unlit: its colour does not follow the light
([reports 57](../reports/renderer/57-2026-10-11-mmd-outline.md) and
[58](../reports/renderer/58-2026-10-12-mat-q4-edge-width.md)). Its colour is a stored value, decoded once, as
MAT-Q3 decodes MMD's surface. A model's edges draw after its surfaces, as
MMD draws them.

## 6. Transparency and draw order

A draw is one material's part of a mesh. A mesh whose `GeomSubset`s bind
materials draws once per such subset, with that subset's material, and once
more for the faces none of them binds, with the mesh's own; the scene lists
a mesh's draws in its subsets' order. That is generic USD, not a model's
semantics, and both models' ordering below applies to draws.

- **MToon** sorts by alpha mode, then `renderQueueOffsetNumber`, and writes
  depth for transparent materials only with `transparentWithZWrite`. The
  sort key is MToon's render queue: Opaque 2000, Mask 2450, Blend with
  `transparentWithZWrite` 2501 plus the offset clamped to [0, 9], and Blend
  without it 3000 plus the offset clamped to [−9, 0]. Opaque and Mask draw
  first; transparent draws follow in queue order and, within one queue, in
  the order the scene lists them, not by distance, so an avatar's layers do
  not swap as the camera moves. A double-sided transparent surface draws
  its back faces before its front faces. A transparent material's outline
  hull draws right after its surface, as UniVRM and three-vrm draw MToon's
  outline, so a surface that writes depth hides the hull's far side; an
  opaque one's draws before, as [DESIGN_POLICY.md](DESIGN_POLICY.md) §10
  orders it. Multisampled, Mask's cut is by alpha to coverage rather than
  whole fragments ([DESIGN_POLICY.md](DESIGN_POLICY.md) §16).
- **MMD** draws in material-table order (`mmd:sourceIndex`) with alpha
  blending, because models are authored against that order. A material
  that cannot blend — its diffuse alpha 1 and its base and sphere textures
  opaque — draws with the opaque draws instead; the rest draw after them,
  writing depth as MMD does, in material-table order within a mesh. That
  differs from MMD only where an earlier translucent material covers a
  later opaque one.

The sort key is computed once, when the material is normalized, and is part
of the persistent draw packet ([DESIGN_POLICY.md](DESIGN_POLICY.md) §15).

## 7. Pipelines and parameter buffers

Seven pipelines, fixed ([DESIGN_POLICY.md](DESIGN_POLICY.md) §8):

```text
mtoon_opaque  mtoon_transparent  mtoon_outline
mmd_opaque    mmd_transparent    mmd_outline
preview_surface
```

Beside them, `mmd_shadow` draws MMD's self-shadow map, depth alone, before
the scene (§10); it shades nothing.

MToon and MMD are separate shaders, not one shader with a model switch. They
share what is truly common — skinning, morph evaluation, the texture table,
light data, the outline's hull, math helpers — as included Slang modules.

A material is a slot in its model's parameter buffer plus texture indices.
Normalizing a new material writes a slot; it never compiles a shader. A
feature a material does not use is a zero or a disabled flag in its slot, not
a pipeline variant.

## 8. Values that change at run time

UV animation takes the host's evaluation time in seconds as a frame value,
outside material slots. The viewport divides its USD time code by the
stage's `timeCodesPerSecond`; a Hydra render-pass host supplies
`toon:timeSeconds`. A new time changes the frame buffer, not any material
revision. Each texture retains its own transform after UV animation; the
mask is sampled before animation and MatCap remains view-mapped.

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
| MAT-Q1 | How `inputs:mmd:material:*` reaches the render delegate. For MToon this is answered by §2 and §8 ([report 02](../reports/renderer/02-2026-09-26-mat-q1-material-inputs.md)) | resolved 2026-10-11: the same shape for both models, a container on the Hydra material prim read from the terminal scene index. `usd-mmd-plugins` froze `mmdImaging`'s view on 2026-10-04, and every material of 41 local MMD stages selects MMD through it (§2, §3; [report 53](../reports/renderer/53-2026-10-11-mmd-material-read.md)) | agreed with `usd-mmd-plugins` before v0.4.0 |
| MAT-Q2 | MMD's shared toon ramps (`sharedToonIndex` 0–9) belong to MMD and are not redistributable, and the stage names no image for them | resolved 2026-10-11: `hydra-toon` generates its own ten ramps, under its own licence, from the colours MMD's `toon01`–`toon10` show, and maps the index to them; no MMD image is copied or shipped ([report 56](../reports/renderer/56-2026-10-11-mmd-surface.md)) | v0.4.0 |
| MAT-Q3 | MMD colours are authored as stored, with no declared colour space, and MMD shades without colour management | resolved 2026-10-11: MMD computes its terms on the values as stored and writes them undecoded and unencoded; lowering its light colour scales a model's pixels by the factor its lighting term computes on stored values, flat across brightness ([report 55](../reports/renderer/55-2026-10-11-mat-q3-mmd-colour.md)). The MMD path samples its textures without sRGB decoding, computes MMD's terms on them and the factors as authored, and converts the result from sRGB to linear once, so the host's encoding writes MMD's values. Blending stays in the linear target, so a partly transparent material approximates MMD's | v0.4.0 |
| MAT-Q4 | How wide MMD draws an edge of a given `edgeSize` × `edgeScale`: MMD states no unit, and on its own frames an edge of size 0.4 is 0.6 pixels wide from 45 MMD units and 1.57 from 9 at a 30° field of view and 1,080 pixels, neither a constant length nor a constant number of pixels ([report 57](../reports/renderer/57-2026-10-11-mmd-outline.md)) | resolved 2026-10-12: a share of the target's height, `edgeSize` × `edgeScale` × (0.00108 + 0.0203 / the vertex's view depth in MMD units). On MMD's frames a 15° field of view leaves an edge as many pixels wide as 30° does, a 2,160-pixel frame doubles it, and from 9, 18 and 30 MMD units it falls as a constant share plus one over the depth; the constants make hdToon's edge MMD's width at those cameras ([report 58](../reports/renderer/58-2026-10-12-mat-q4-edge-width.md)) | v0.4.0 |
| MAT-Q5 | What MMD's self-shadow map covers and how far behind its depth a point falls into shadow. `full.fx` compares in MMD's own map depth, 1,500 per unit and shadowed from 0.3, but MMD states neither that depth's range nor how its map follows its camera and its self-shadow distance, which the stage does not carry (§10) | provisional 2026-10-12: a 2,048-texel map fitted along the light to every MMD draw that casts or receives the self-shadow, and the comparison in MMD units, 15 per unit and shadowed from 0.3. On one model's MMD capture with self-shadow, hdToon's frame is within 4 levels on 73% of the pixels, as near as its frame without is to MMD's without, and 5 or 50 per unit move the mean difference by under 0.11 ([report 60](../reports/renderer/60-2026-10-12-mmd-self-shadow.md)). Settled when captures of a contact or grazing shadow tell the comparison apart | v0.4.0 |
| MAT-Q6 | How MMD applies a material morph's texture, sphere and toon tints to a texel. PMX states a multiply and an add for each but not how they combine, and MikuMikuDance does not say (§11) | resolved 2026-10-17: MMD does not apply them. On MMD v9.32's frames of two local models, a morph that only tints (`spa消える`, `toon白くなる`, `toon深まる`, `spa消`) leaves the frame as it was at full and half weight, and `全灰モード` and `全黒モード` change only the colours and edge they also set ([report 62](../reports/renderer/62-2026-10-17-mat-q6-morph-tints.md)). So the MMD path leaves the tints out; a host may ask for MikuMikuEffect's `full.fx` reading, which [report 61](../reports/renderer/61-2026-10-12-mmd-material-morph.md) implements and no MikuMikuEffect frame has checked (§11) | v0.4.0 |

## 10. MMD's self-shadow

MMD's self-shadow is MMD's semantics, asked for by a material's
`castSelfShadow` and `receiveSelfShadow`, not a general shadow system
([DESIGN_POLICY.md](DESIGN_POLICY.md) §26, §32). The renderer draws it as
MikuMikuEffect's `full.fx` does, while it is on: on unless the host turns
it off, as MMD's own switch is (`DrawList::mmd_self_shadow`, the render
setting `toon:mmdSelfShadow`).

- **The map.** Each frame, before the scene, a depth map is drawn from the
  light MMD's shading reads, the scene's first direct light, when it is
  directional; from a point or spot light, or with no direct light, there
  is no map and nothing is shadowed. Every MMD draw whose material casts
  draws into it, both faces, no texel's alpha cutting it away, as `full.fx`
  draws its map.
- **What it covers.** The map looks along the light at the box around every
  MMD draw that casts or receives, so it does not move with the camera.
  MMD's own map follows its camera and its self-shadow distance, which the
  stage does not carry (MAT-Q5).
- **A receiver.** A material that receives is shaded as `full.fx`'s
  self-shadow pass shades it: its colour, the lighting term with its
  textures and sphere, is not toned by the ramp, and where the light is
  cut — by the map, or by the surface turning from the light over the first
  third of N·L — it falls to that colour times the ramp's far-side texel,
  without the specular. A point the map does not cover is lit. A material
  that does not receive is shaded as §5–§7 and
  [MAT-Q3](#9-open-questions) describe, ramp and all.
- **How far behind.** A point falls into shadow from 0.02 MMD units behind
  what the map saw first and wholly by 0.087, `full.fx`'s form on a depth in
  MMD units (MAT-Q5).

`groundShadow`, MMD's shadow on its floor, is not drawn: a stage has no
floor for it.

## 11. MMD's material morphs

A material morph is MMD's semantics: the stage keeps it declarative, on its
morph prim, and a runtime evaluates it
([`usd-mmd-plugins` §11](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/MATERIAL_POLICY.md#11-material-morphs)).
The renderer draws what the evaluation leaves; it reads no morph prim and
weighs no morph.

- **Values with a canonical input.** Diffuse, specular, specular power,
  ambient, edge colour and edge size reach the material's own values, as a
  runtime's opinion or time samples on the canonical inputs, which the
  value-only path reads (§8), or as an evaluated override
  (`RenderWorld::SetMaterialParametersOverride`). A morph that takes a
  diffuse alpha below 1 moves the material to the transparent draws, which
  is structural (§6).
- **Tints.** The texture, sphere and toon tints have no canonical input;
  they modulate a texture's contribution, which only an MMD-aware renderer
  draws. The MMD block carries each as the evaluated multiply and add, the
  product of every multiply morph's `lerp(1, value, weight)` and the sum of
  every add morph's `value × weight`, and an evaluated override sets them.
  They are values in the material's slot, never structural.
- **How a tint applies.** MMD draws no tint (MAT-Q6), so by default
  neither does the MMD path. A host that asks for MikuMikuEffect's
  `full.fx` reading, a per-frame switch (`DrawList::mmd_morph_tints`,
  `toon:mmdMorphTints`), gets the base texel, the sphere texel and the
  toon ramp's texels, its far-side one included, each tinted as `full.fx`
  tints them: the colour moves from what the texture adds when it
  contributes nothing toward texel × multiply + add, by the sum of the two
  alphas, and the alpha is kept. A material without the texture is not
  tinted. The switch changes no material slot.
