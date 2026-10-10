# Real runtime providers through the host matcher

- Date: 2026-10-11
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Runtime: canonical CY2026 OpenUSD 26.08 lookdev; `usd-avatar-runtime`
  `b8c05c1` (experimental C ABI revision 4), its VRM USD bindings and VRM
  registration adapter installed from the sibling working tree; `vrmSchema`
  0.10.0 and `vrmRig` owner installs; motion owners 0.5.4; `vrmImaging`
  0.10.0 lookdev package; `usdVrmFileFormat` 0.10.0 for the `.vrm` avatar
- Scope: the external host's real-provider matching and the Hydra/direct
  comparison in
  [v0.3.0](../../roadmap/v0.3.0.md#evaluated-state-adapter-and-representative-avatar-evidence)

## The gap

Report 46 matched layouts that followed the runtime binders' conventions,
built from UsdSkel by the check itself. Whether the layouts the runtime's
providers actually publish match the delegate's description, and whether the
fast result then equals what Hydra draws from the same values, needed a host
that links the providers.

## Change

`toon-hydra2-avatar-provider-test` (`adapters/hydra2/tests/`) is that host.
It is built only with `TOON_AVATAR_RUNTIME_PROVIDERS`, which also needs the
optional runtime consumer and finds the runtime's `vrm_expression_usd` and
`vrm_lookat_usd` components. It is not installed.

- **Providers.** The runtime's `ExpressionBinding` and `LookAtBinding` read
  the avatar's applied VRM schemas. Their registration adapter is the only
  evaluator of a real runtime instance, which starts from the bindings'
  baseline. The host gives every expression a scalar channel and the gaze a
  world point; it evaluates nothing itself.
- **Matching.** The first published result goes through
  `HdToonMatchAvatarTargets` with the canonical material inputs. Every
  output must match, and `Bind` must accept it, before any frame is
  compared.
- **Frames.** Rest, each expression alone at 1 and 0.5, a mix of three with
  a gaze point, gaze alone the other way, and rest after a reset to
  generation 2.
- **Oracle.** Each result is also authored into the session layer: joints as
  the skeleton's rest transforms, shape weights on a SkelAnimation that
  animates no joint, and material values on their canonical inputs.
  UsdImaging and usdSkelImaging then compose it. The oracle is only for this
  check; no renderer path authors USD per frame. A SkelAnimation's `half3`
  scales lost 2.4e-4 on one avatar's near-unit joint scales, so joints use
  `matrix4d` rests. At rest, the converted joints must give back the
  authored rest transforms within 1e-5. That checks the oracle's reading of
  the runtime's root placement.
- **Comparison.** For each result, the fast adapter's output over the bound
  scene must match the oracle's Hydra commit. Each bound palette must agree
  within 1e-4 relative, each bound mesh's subshape weights within 1e-6, and
  each bound material's whole `ToonMaterial` exactly. The fast output must
  also keep every static array and revision of the bound scene. With
  `--shaders`, both outputs are drawn on two offscreen renderers, from +Z and
  from -Z, framing the morphed meshes at 256x256. A pixel may differ by one
  8-bit step and by 1e-4 of the -1..1 depth range. At most 16 pixels per
  view (0.025%) may differ by more, where an edge's coverage flips. The fast
  renderer must upload no static resource after its first frame.

A host must state the stage's unit on each commit, as `toon-viewport` does.
Hydra carries none, and the fast adapter converts runtime metres with it.
Without it, the first run's direct palettes were off by the whole placement.

`avatar-provider.usda` is a committed VRM-schema avatar in centimetres,
placed 20 cm off the origin. Its face is skinned in a reversed joint subset
and has a shape with an inbetween. Its eyes are skinned to their own joints.
An expression drives the face's MToon shade colour and its base RGB and
alpha. There are two LookAt prims, bone and expression, and the check selects
one with `--lookat`.

## Evidence

The fixture, through `vrmImaging` so its materials are MToon:

| Case | Layout | Matched | Results |
| --- | --- | --- | --- |
| bone gaze | 5 joints, 6 shapes, 3 material inputs, 7 expressions | 2 skins, 6 morphs (1 inbetween slot), 3 material fields | 18; palettes within 0, weights within 0; 2 posed, 15 weighted, 3 recoloured |
| expression gaze | same | same | 18; 0 posed, 16 weighted, 3 recoloured |
| bone gaze, GPU | same | same | 36 images, colour step 0, depth 0, no flipped pixels; 16 results differ from the rest images; after the first result, 3 joint, 16 weight and 5 material writes and no static upload |

Bone gaze must pose a bound palette. Expression gaze must weight shapes
without posing one. The material outputs must bind and change a material.

Two mutations were each caught at the first frame that exercises them.
Binding joints in palette position instead of by token failed at rest, with
1.0 relative palette error on the face. Mapping `shadeColorFactor` to the
rim colour failed at `happy=1`.

Two local avatars, which are not redistributable and are not committed:

| Avatar | Layout | Matched | Results |
| --- | --- | --- | --- |
| `AliciaSolid.usdz` (`C6220B68…`) | 128 joints, 46 shapes, 0 material inputs, 17 expressions | 20 skins, 46 morphs | 38; palettes within 1.7e-24, weights within 0; 2 posed, 25 weighted. 76 images: colour step 0, depth 0; 26 results differ from the rest images; 60 joint and 84 weight writes, no static upload |
| `Seed-san.vrm` (`624D0D55…`), through `usdVrmFileFormat` | 128 joints, 48 shapes, 0 material inputs, 18 expressions | 21 skins, 48 morphs | 40; palettes within 2.4e-7, weights within 0; 0 posed, 36 weighted. 80 images: 40 flipped pixels, at most 1 in a view; largest depth difference 0.0055; 99 weight writes, no static upload |

Every runtime output of both avatars matched a resident target. Neither
avatar's expressions bind material colours, so material matching on a real
avatar is still unexercised. On `Seed-san.vrm` no result changed the images:
its textures do not resolve through the format bundle in this session, so
the faces draw flat. The provider reported `VRM_LOOKAT_WARNING` for a missing
`offsetFromHeadBone` and `VRM_EXPRESSION_SUPPRESSED` for owner arbitration.
Both are the owner's diagnostics, counted and passed through.

## Commands and limits

```powershell
cmake -S . -B build/avatar-providers -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DTOON_ENABLE_HYDRA2=ON -DTOON_ENABLE_AVATAR_STATE=ON `
  -DTOON_AVATAR_RUNTIME_PROVIDERS=ON `
  -DTOON_AVATAR_RUNTIME_INCLUDE_DIR=<usd-avatar-runtime>/include `
  -DAvatarRuntime_DIR=<runtime prefix>/lib/cmake/AvatarRuntime `
  -DvrmSchema_DIR=<vrmSchema prefix>/lib/cmake/vrmSchema `
  -DvrmRig_DIR=... -DmotionCore_DIR=... -DmotionRetarget_DIR=... -DmotionUsd_DIR=... `
  -DTOON_VRM_IMAGING_RESOURCES=<vrmImaging package>/plugin/resources/vrmImaging
cmake --build build/avatar-providers
ctest --test-dir build/avatar-providers   # 28 of 28
# With the providers' DLLs on PATH and vrmSchema and vrmImaging registered:
toon-hydra2-avatar-provider-test <avatar> --shaders build/avatar-providers/backend/vulkan/shaders
```

All 28 CTests of that tree pass, including
`toon-renderer-hydra-avatar-providers-{bone,expression,gpu}`. The existing
consumer tree without the option still passes its 25.

The oracle authors USD. It shows that the fast route and UsdImaging agree on
the same values. It is not the runtime's Hydra publication, which does not
exist yet. Still open: real motion providers in the composition, runtime
results in the dedicated viewport, results arriving after extraction,
`emissiveFactor`, and producer-to-present latency.
