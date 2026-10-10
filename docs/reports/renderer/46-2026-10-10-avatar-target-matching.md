# Runtime-to-resident target matching

> Followed by [report 47](47-2026-10-11-avatar-real-providers.md): the layouts the runtime's real VRM providers publish now match, through a host that links them.

- Date: 2026-10-10
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Runtime: canonical CY2026 OpenUSD 26.08 lookdev; local sibling working
  tree's experimental runtime C ABI revision 4 headers
- Scope: the host-side matching of runtime target identities to the
  delegate's resident description in
  [v0.3.0](../../roadmap/v0.3.0.md#evaluated-state-adapter-and-representative-avatar-evidence)

## The gap

Report 43 described the Hydra identities behind resident slots, and report 44
let a morph binding list a shape's inbetween slots. The pairing between them
was still written by hand in each check. The runtime assigns that pairing to
a host on the renderer's side
([resident target matching](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/architecture/OUTPUT_PATHS.md#resident-target-matching)),
and because the description is not installed, the host code has to be built
with the renderer.

## Change

`HdToonMatchAvatarTargets` (`adapters/hydra2/src/avatar_binding.hpp`) takes a
runtime layout, `DescribeResidentTargets`' result and a host table of
canonical material inputs. It returns `Toon::AvatarBindings` and a list of
mismatches, each naming the runtime or resident identity and the reason.

- **Skins.** A skin is built for each GPU-skinned palette whose skeleton path
  the runtime publishes. Each palette joint token is looked up in that
  skeleton's runtime joints, so the runtime's joint order and the mesh's
  `skel:joints` order can differ. The described inverse binds and placement
  are carried into the skin. A palette entry the runtime does not publish, or
  one the skeleton does not name, leaves that mesh unbound and is reported. A
  runtime skeleton that drives no resident skin is also reported. A mesh
  whose skeleton the runtime does not publish keeps its scene pose.
- **Morphs.** A runtime `(mesh path, skel:blendShapes token)` takes that
  shape's described primary slot, and its inbetween slots at their weights.
- **Materials.** A runtime `(material path, input)` binds to the resident
  material at that path, using the field the host's table gives the input.
  `HdToonCanonicalMaterialInputs` lists the VRM canonical Material attributes
  that the delegate's own normalization copies one-to-one. `emissiveFactor` is
  left out because the delegate scales it by `emissiveStrength`.
- **Visibility.** A visibility target binds to the resident mesh with that
  path.

Nothing here interprets a format, expression or humanoid role. The matcher
treats the identities as opaque strings, and `Bind` still checks the scene's
values. The code builds as `toon-hydra2-avatar-binding`, but only when the
optional runtime consumer is enabled. It is not installed. `hdToon`,
`toon-hydra2-runtime`, core and backends gain no dependency on it.

## Evidence

`toon-renderer-hydra-skinning`, built with the consumer, now gets every
binding from the matcher:

- **Inbetweens.** Report 44's inbetween parity now uses matched bindings:
  `lift` matches slot 1 with its inbetween at slot 0, position 0.5, and
  `tilt` and `plain` match slots 2 and 3. The weights still equal
  usdSkelImaging's exactly at all 12 steps.
- **Skins.** The runtime publishes `/Root/Skel` in skeleton order, `base` then
  `base/tip`. The match binds only the linear quad, with joints `{1, 0}`
  (the mesh's reversed palette); the CPU-skinned dual quaternion quad gets
  nothing. The fast adapter's palette from a metre-unit pose equals
  usdSkelImaging's within 1e-5, both before and after a 2-unit skeleton
  translation that the runtime carries in the root joint.
- **Mismatches.** A layout with a missing palette joint, a skeleton that is
  not in the stage, an unknown shape, a shape on the CPU-skinned mesh,
  `emissiveFactor`, a material path that does not exist and an unknown
  visibility target reports exactly these 8 subjects, in a fixed order. The
  shape, `shadeColorFactor` and visibility outputs that do match are still
  bound.
- **Mutation.** With joints bound in palette position instead of by token,
  the fixture fails.

With `--stage`, the check also builds a runtime layout from the stage the
way the runtime's USD binders read it. The layout takes skeleton paths and
joint tokens with metre-unit locals, each root carrying the skeleton's
placement, and every skinned mesh's `skel:blendShapes` tokens. It is built
from UsdSkel, not from the delegate. The check requires no mismatches and a
successful `Bind` and `Apply`, and compares every matched palette with the
committed one. On the local AliciaSolid USDZ, which is not redistributable
and is not committed:

```text
authored rest runtime layout: 128 joints and 158 blend shapes bound as 20 skins and 158 morphs (0 inbetween slots)
rotated rest runtime layout: 128 joints and 158 blend shapes bound as 20 skins and 158 morphs (0 inbetween slots)
```

All 2,560 palette entries match within a relative 1e-4 in both passes.
AliciaSolid's palettes use skeleton order, so the palette-position mutation
does not change them. A mutation that binds each token to the next runtime
joint fails at the first palette entry.

## Commands and limits

```powershell
cmake --build build/avatar-state-hydra
ctest --test-dir build/avatar-state-hydra --output-on-failure
# With the lookdev runtime's bin and lib directories on PATH:
toon-hydra2-skinning-test --stage <AliciaSolid.usdz>
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
```

All 25 CTests of the Hydra build with the consumer pass.

The stage layout reproduces the runtime binders' identity conventions; it
does not come from them. Matching the layouts that real
`usd-avatar-runtime` providers publish still needs a host that links them.
Still open as well: material matching on an MToon avatar, binding
`emissiveFactor`, rendering runtime results in the dedicated viewport,
Hydra/direct parity and latency.
