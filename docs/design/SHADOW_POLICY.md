---
status: accepted
owner: hydra-toon
---

# Shadow policy

> The v0.6.0 shadow RFC, accepted on 2026-10-21. It expands
> [DESIGN_POLICY.md](DESIGN_POLICY.md) §32 and §35 and wins over it on shared
> shadows. How each material family *receives* stays
> [MATERIAL_POLICY.md §14](MATERIAL_POLICY.md#14-shadows-across-material-families),
> and MMD's own self-shadow semantics stay
> [§10](MATERIAL_POLICY.md#10-mmds-self-shadow); this page owns the light,
> caster, view and resource contracts they read. Implementation status
> belongs to the [capability matrix](../reference/CAPABILITY_MATRIX.md);
> the milestone's acceptance is recorded in the
> [v0.6.0 release record](../releases/v0.6.0.md).

## 1. Purpose and decisions

Shared shadows let MToon, MMD and PreviewSurface cast onto and receive from
each other in one scene, without one family's look following another's.
Two decisions were taken by the project on 2026-10-21 and shape the rest:

- **One map per shadowing light, drawn by one shared caster pass.** MMD's
  camera-following self-shadow map becomes the shared view of the first
  directional light; MMD receivers keep reading it with material
  policy §10's comparison and filter. This resolves
  [MAT-Q7](MATERIAL_POLICY.md#9-open-questions) (§6).
- **On by default, the fallback key included.** Shared shadows follow
  `UsdLux`'s `shadow:enable`, whose fallback is true, and the camera key
  that stands in for a missing rig casts as MMD's self-shadow already does.
  MToon's representative looks gain cast shadows; their references are
  recorded again as the new baseline rather than counted as regressions
  (§8).

## 2. Lights

A shadowing light is a `ToonLight`
([design policy §32](DESIGN_POLICY.md#32-scene-lighting)) that is visible,
direct, directional or spot, and whose shadow is enabled. Its identity is its
`LightId`; its transform is the light's own. Point lights do not cast in
v0.6.0 (cube maps are outside the milestone).

| Input | Source | v0.6.0 |
| --- | --- | --- |
| Shadow enable | `UsdLuxShadowAPI` `inputs:shadow:enable` through `HdLightTokens->shadowEnable`; true when unauthored | read, a new `ToonLight` value |
| Shadow colour, distance, falloff, falloff gamma | `UsdLuxShadowAPI` | not read; a matrix gap |
| Shadow linking | `UsdLuxLightAPI` `collection:shadowLink` | not read; every caster casts for every shadowing light |
| Fallback key | the camera key used when a scene has no supported light, or scene lights are off for debugging | casts, directional |

Shadowing lights are taken in the draw list's light order up to a fixed
count, four (`kShadowViewCapacity`); the rest light without shadows. The
count is a backend capacity, measured in the second stage (§9,
[report 86](../reports/renderer/86-2026-10-21-shared-shadow-stage-2.md)),
and the overflow is reported, not silent. So is a spot light whose cone
is wider than 80 degrees from its axis, which one perspective view
cannot cover and a cube map would (§10). MMD keeps reading only the scene's
first direct light and casts only when that light is directional, as
material policy §10 says; when that light's shadow is off, MMD's receivers
are not shadowed either.

## 3. Settings

Render settings, per-frame values in the draw list like `toon:mmdSelfShadow`,
never material or geometry edits. The viewport carries the same controls.

| Setting | Default | Meaning |
| --- | --- | --- |
| `toon:shadows` | `true` | non-MMD casters draw into the shared views and non-MMD materials receive |
| `toon:mmdSelfShadow` | `true` | unchanged: MMD materials receive by material policy §10; MMD casters with `castSelfShadow` draw into the views whenever a view is drawn |
| `toon:shadowResolution` | `2048` | texels on a side of every view, a power of two from 512 to 4096 |
| `toon:shadowDepthBias` | `1.5` | how far behind the view a receiver must lie, in texels of the view, times one plus its slope from the light (at most 4); 0 to 16 |
| `toon:shadowNormalBias` | `1.0` | how far a receiver moves along its normal as it turns from the light, in texels of the view; 0 to 16 |
| `toon:shadowFilterRadius` | `1.5` | the radius of the 16-tap PCF disc, in texels of the view; 0 to 8 |

The bias and filter defaults were measured in stage 2
([report 86](../reports/renderer/86-2026-10-21-shared-shadow-stage-2.md)):
no acne at 1,024 to 4,096 texels on its measurement scenes and avatar,
most of a thin caster's shadow kept from about six texels above its
receiver under grazing light, and edges neither stepped nor grainy.

MMD receivers ignore the bias and filter settings: their depth window,
slope allowance and six-texel disc are MMD's fitted semantics
([MAT-Q5](MATERIAL_POLICY.md#9-open-questions)), not tunables. With
`toon:shadows` off and `toon:mmdSelfShadow` on, the frame is today's MMD
self-shadow exactly.

## 4. Casters and receivers

Visibility ([design policy §35](DESIGN_POLICY.md#35-shared-scene-services))
decides what casts and receives; a material's flags are values in its slot,
not pipeline variants
([MATERIAL_POLICY.md §7](MATERIAL_POLICY.md#7-pipelines-and-parameter-buffers)).

| Family | Casts | Receives |
| --- | --- | --- |
| MMD | when `castSelfShadow`: both faces, no texel alpha cut, as material policy §10 draws its map | when `receiveSelfShadow` and `toon:mmdSelfShadow`, from the first direct light's view only |
| MToon | Opaque, faces as its double-sidedness; Mask, cut at its cutoff by base colour alpha | every shadowing light, when `toon:shadows` |
| PreviewSurface, unbound display colour | Opaque and Mask as MToon | every shadowing light, when `toon:shadows` |
| Unreadable-material fallback | Opaque | no; it is unlit |

Blend surfaces and outline hulls (MToon's and MMD's edges) do not cast.
Invisible prims neither cast nor receive. Every caster draws at the
position its surface draws: static, GPU-skinned, GPU-morphed or CPU
fallback points, from the same deformed vertex buffers, so a caster can
never disagree with what the camera sees of it.

## 5. Views and resources

Ownership follows design policy §35: the core decides, the backend draws.

- **Core** (`render-extraction`). Shadow settings are per-frame values in
  the draw list (`DrawList::shadows`, `DrawList::mmd_self_shadow`). Which
  materials cast and receive (`ShadowRoleOf`), the selection of shadowing
  lights and the fit of each light's view are the core's `ShadowFit`,
  which keeps each mesh's posed bounds (`OutlineBounds::SceneBounds`,
  `ScenePieces`) and fits backend-neutral `ShadowView`s from the draw list
  each frame, so WebGPU (v0.7.0) reuses them unchanged. Each view carries
  its light, its clip-from-world transform (depth in [0, 1] from the
  light, y up), its depth range in scene units and its texel size in
  scene units.
- **Backend**. A D32 two-dimensional array image at the set resolution,
  one layer per view, which grows when a frame draws more views than it
  has layers and is rebuilt alone when the resolution changes; the caster
  pipeline; the caster pass, recorded once per frame before the scene for
  every view that has a receiver, each into its layer; the view parameter
  buffer; barriers. Each light entry in the frame's light buffer names its
  view or none.
- **Adapters**. The Hydra and fast adapters supply lights, their shadow
  enable and the settings as scene data and own no shadow resource or pass.

A directional view is fitted as material policy §10 fits MMD's map today,
generalised from MMD draws to all casters and receivers: along the light it spans the box
around every caster and receiver; across it, what the camera sees of the
receivers' posed pieces, cut to the frustum, in steps of an eighth of an
octave and moving in whole texels, so edges hold still as the camera
moves; with no receiver in view it spans the whole box. A spot view is the
light's own perspective over its cone, padded as a directional view is,
its near and far planes fitted to the same boxes and the near plane no
closer than a thousandth of the far one, so the depth keeps its
precision. A view with no receiver is not drawn. An atlas follows only
measured need.

Each receiver keeps the units its comparison is defined in: MMD converts
the view's scene-unit depth into MMD units as it does now; MToon and
PreviewSurface compare in texels of the view where the point is, a spot
view's growing with the distance from the light. MMD's six-texel edge is
in texels of its view, so another resolution changes its width, as a
mixed scene's wider fit does (§6).

## 6. MAT-Q7 and MMD's migration

MMD receivers read the shared view of the first direct light, drawn by the
shared pass, with material policy §10's comparison and filter unchanged;
there is no second MMD map. Consequences:

- **An MMD-only scene is unchanged.** Its casters, receivers and fit are
  exactly today's, so the map and frame match the current self-shadow
  output; the existing `mmd_self_shadow` GPU check and the evidence of reports
  [60](../reports/renderer/60-2026-10-12-mmd-self-shadow.md),
  [63](../reports/renderer/63-2026-10-17-self-shadow-quality.md) and
  [66](../reports/renderer/66-2026-10-17-mat-q5-shadow-depth.md) remain the
  regression baseline, and the migration lands before any new receiver.
- **Non-MMD casters reach MMD receivers** through the same map, and MMD
  casters reach MToon and PreviewSurface receivers.
- **In a mixed scene the fit widens** to every family's seen receivers, so
  an MMD receiver's texels can cover more of the scene than they would
  alone; its edge stays six texels of its view. This is accepted.
- `DrawList::mmd_self_shadow`, `toon:mmdSelfShadow` and
  `draws_mmd_shadow` keep their meanings; a new per-view draw count joins
  the evidence statistics.

## 7. Receiving

The families' receive semantics are
[MATERIAL_POLICY.md §14](MATERIAL_POLICY.md#14-shadows-across-material-families);
in terms of their current shading:

- **MToon**: a light's visibility scales its ramped shading term before the
  shade/lit mix, so shadowed light gives the shade colour, as the shift and
  toony ramp would where the surface turns away. The rim's incident sum
  is not shadowed: three-vrm's shadow scales the shading term alone,
  `getShading`'s `shading *= shadow`, and its rim sums the light unshadowed
  (settled in stage 1, [report 85](../reports/renderer/85-2026-10-21-shared-shadow-stage-1.md)).
- **PreviewSurface**: visibility scales the light's direct diffuse and
  specular response.
- **MMD**: material policy §10, from the first direct light's view only.

Ambient lights are never shadowed.

## 8. Fast path, cost and evidence

- **Parity.** The Hydra and fast adapters reach the same views and
  results
  ([design policy §34](DESIGN_POLICY.md#34-evaluated-avatar-input-and-transport-parity)).
  A pose-only update moves casters through the deformed
  buffers and the bounds' joint boxes and reuploads no geometry; the
  existing upload counters stay at zero for it.
- **Cost.** CPU fit time and GPU time of the caster pass per view, with
  p95 and p99 as well as the mean, and the image's memory, on bounded
  fixtures naming GPU, resolution, samples, vsync and scene
  ([design policy §23](DESIGN_POLICY.md#23-performance-kpis)).
- **Looks.** MMD's representative frames must not change. MToon's
  representative avatar frames are recorded again with shadows on as the
  new reference, beside their shadows-off frames, which must not change.
- **Debug.** A material debug view of each receiver's visibility
  (`MaterialDebug::Shadow`: the mean over the lights that have a view, or
  MMD's self-shadow, 1 elsewhere) and a viewport preview of each view's
  depth, neither writing material slots.

## 9. Stages

1. **One directional view.** Move the fit into the core; draw the first
   shadowing directional light's view (or the key's) with the shared
   caster pass; migrate MMD (§6) with its regression baseline; add MToon
   and PreviewSurface receivers with fixed provisional bias and filter;
   `toon:shadows`; settle MToon's rim question.
2. **More lights and controls.** Up to the measured count of directional
   and spot views; resolution, bias and filter settings; the debug views.
3. **Acceptance.** The cross-family cases and done-when condition of the
   [v0.6.0 milestone](../releases/v0.6.0.md)
   ([report 87](../reports/renderer/87-2026-10-21-shared-shadow-acceptance.md)).

## 10. Outside this policy

Point-light cube shadows, cascades, contact and advanced soft shadows,
shadow catchers, ray tracing, IBL, translucent or coloured shadows,
`UsdLux` shadow colour/distance/falloff and shadow linking. Each is taken
up by its own trigger in the [later roadmap](../roadmap/later.md).
