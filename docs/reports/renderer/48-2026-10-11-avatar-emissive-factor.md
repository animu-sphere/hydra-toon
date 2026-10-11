# Runtime `emissiveFactor` scaled as the delegate realizes it

- Date: 2026-10-11
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Runtime: canonical CY2026 OpenUSD 26.08 lookdev; `usd-avatar-runtime`
  `b8c05c1` (experimental C ABI revision 4) with its VRM USD bindings, as in
  [report 47](47-2026-10-11-avatar-real-providers.md); `vrmSchema` 0.10.0;
  `vrmImaging` 0.10.0 lookdev package
- Scope: `emissiveFactor` in the optional adapter's material mappings, in
  [v0.3.0](../../roadmap/v0.3.0.md#evaluated-state-adapter-and-representative-avatar-evidence)

## The gap

A VRM expression's `emissionColor` bind lands on the canonical
`inputs:vrm:material:emissiveFactor`, a `color3f`. The runtime publishes that
input unscaled. The delegate's emissive colour is `emissiveFactor` times
`emissiveStrength` (`KHR_materials_emissive_strength`). Writing the runtime
value straight into `ToonMaterial::emissive` would differ from the Hydra path
whenever the strength is not 1. So the host matcher left the input unmapped
and reported it.

## Change

- **Binding scale.** `Toon::AvatarMaterialBinding` has a `scale`, 1 by
  default. The adapter multiplies each component it writes by it in single
  precision, the way the delegate multiplies the factor. `Bind` refuses a
  non-finite scale. Release still restores the scene's baseline, which the
  delegate has already scaled.
- **Resident strength.** `HdToonReadMaterial` returns the strength it scaled
  emission by. Each `HdToonResidentMaterial` describes it as
  `emissive_strength`, as of the material's last sync, including value-only
  syncs. A material that is not MToon describes 1.
- **Matching.** `HdToonCanonicalMaterialInputs` maps
  `inputs:vrm:material:emissiveFactor` to `Emissive`. `HdToonMatchAvatarTargets`
  gives an `Emissive` binding the resident material's strength as its scale.
  Every other field keeps a scale of 1.
- **Fixture.** `avatar-provider.usda`'s face material has an `emissiveFactor`
  of (0.1, 0.05, 0.02) and an `emissiveStrength` of 2. Its `happy` expression
  binds `emissionColor` to (0.3, 0.1, 0.05).

The provider host's oracle authors each published material value on its
canonical input as before. UsdImaging and the delegate then apply the
authored strength, so the oracle needs no change.

## Evidence

| Check | Result |
| --- | --- |
| `toon-avatar-state` | A 2.5 scale writes (0.3, 0.7, 0.1) × 2.5 exactly. Release restores the scaled baseline. An infinite scale is refused at `Bind` |
| `toon-renderer-hydra-material` | The MToon material authored with strength 2 describes 2; glTF-only and plain materials describe 1 |
| `toon-renderer-hydra-skinning` | `emissiveFactor` binds to `Emissive` with the described 2.5 as its scale, and shade colour beside it with 1. An unknown input is still reported |
| `toon-renderer-hydra-avatar-providers-{bone,expression}` | 5 joints, 6 shapes, 4 material inputs, 7 expressions; 2 skins, 6 morphs, 4 material fields matched; 18 results each, palettes and weights within 0, every bound `ToonMaterial` equal to the oracle's |
| `toon-renderer-hydra-avatar-providers-gpu` | 36 images, colour step 0, depth 0, no flipped pixels; after the first result, 3 joint, 16 weight and 5 material writes and no static upload |

One mutation was caught. A matcher that bound emission with a scale of 1
failed the skinning check, and the provider check failed at `happy=1`:
"material 2 differs from the delegate's normalization of the same values".

`AliciaSolid.usdz` (local, not committed) was run again. Its expressions bind
no material colours, so it publishes no material inputs. It gave report 47's
results: 38 results and 76 images, colour step 0, depth 0, 60 joint and
84 weight writes, no static upload.

## Commands and limits

The configuration is report 47's. `ctest --test-dir build/avatar-providers`
passes 28 of 28. The consumer trees without the providers pass too: Hydra,
25 of 25; Vulkan without Hydra, 18 of 18; neither Vulkan nor Hydra, 13 of 13.

No local avatar binds a material colour, so material binds on a real avatar
remain unexercised. The scale is the strength as of the last sync. An
animated `emissiveStrength` updates it, but the host reads it only when it
matches; a later strength change needs the match redone. Still open: real
motion providers in the composition, runtime results in the dedicated
viewport, results arriving after extraction, the runtime's own Hydra
publication, and producer-to-present latency.
