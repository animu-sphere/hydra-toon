# Generated native MMD combined Hydra/direct parity

Date: 2026-10-31. Windows x64, MSVC 19.51, Ninja/Release, OpenUSD 26.08,
Vulkan, NVIDIA RTX A5000 and deterministic Jolt with six-DOF. Renderer base
`efc8aeb5f1f0f10fa9d7d9d5494f6a050bc31c36` plus this change, using the installed
runtime/owner closure recorded in [report 113](113-2026-10-31-mmd-material-parity.md).
No production API, schema, dependency pin or published package changes.

## Comparison

The runtime fixture generator now additionally writes `combined.vmd`. It
combines the owner's generated keyed pendulum and signed group/direct UV and
vertex morph channels with the existing material channels. The original
`material.pmx`, `material.vmd` and synthetic texture remain unchanged.

The new opt-in `toon-renderer-mmd-combined-{cpu,gpu}` checks open that PMX/VMD
directly through the installed plugins/native session. Automatic resident
bindings cover two final joints, one vertex morph, four primary UV vertices
and 24 material inputs. Runtime alone advances physics; each display sample
asserts exactly one physics tick. Both products keep their evaluated geometry:
the resident adapter consumes the retained final snapshot, while the same
session publishes pose, morph, UV and canonical material values into USD for
UsdImaging/mmdImaging. Neither path substitutes the other's geometry or values.

Only the avatar's skinned surface is compared and drawn. UsdSkelImaging also
creates an animated skeleton diagnostic mesh, which has no runtime resident
binding. The first comparison included that unrelated diagnostic and failed
on its moving points; filtering both products to the skinned avatar surface
resolved the test setup discrepancy. No production rendering change was needed.

Over 720 ticks in three reset generations, geometry, morph/UV/tangent streams,
material subsets and canonical materials agree exactly. Joint palettes agree
within the existing `2e-6` bound; direct geometry replays exactly after Reset.
Pause holds the output, zero channels restore vertex/UV/tangent/material bases,
Release restores resident bases and Reset/Close restore source/session layers.
Retained pose, morph, UV and material values remain readable after Reset/Close.

At ticks 1/60/120/240 in every generation, 128x128 Vulkan colour bytes and
depth agree exactly with 1x and 4x MSAA, with exact image replay. At tick 60,
independently restoring skin, vertex morph, UV/tangents or materials to their
imported base changes pixels at both sample counts. Thus every output affects
the combined image. Repeat rendering uploads no geometry or pipelines and
emits no Vulkan validation messages.

## Reproduction

Use the installed DLL/plugin and Developer PowerShell setup from
[report 113](113-2026-10-31-mmd-material-parity.md#reproduction-and-verification).
First regenerate the pair in the runtime checkout:

```powershell
python tools/generate-mmd-material-fixture.py --owner <mmd-checkout> --out <generated-directory>
```

From this renderer checkout, using the existing configured material build:

```powershell
cmake -S . -B build/mmd-admission-viewport -DTOON_MMD_COMBINED_TEST_VMD=<generated-directory>/combined.vmd
cmake --build build/mmd-admission-viewport --target toon-hydra2-mmd-material-test toon-hydra2-material-test toon-avatar-state-test toon-mmd-runtime-test toon-viewport-mmd --parallel 6
ctest --test-dir build/mmd-admission-viewport -C Release --output-on-failure -R '^(toon-renderer-mmd-(combined|material)-(cpu|gpu)|toon-renderer-hydra-material(-fallback-gpu)?|toon-avatar-state(-gpu)?|toon-mmd-material-resident|toon-viewport-mmd-material|toon-mmd-runtime-cache|toon-viewport-mmd-runtime)$'
```

## Boundary

All twelve selected renderer tests passed, with no skips. The runtime's two
focused `hosts.mmd_session_materials` / `adapters.usd_pose_output` checks and
documentation checks are recorded with the integration checkpoint. A byte
comparison against the previous generator confirmed the original PMX, VMD
and texture are unchanged.

This is generated combined surface delivery and rendering evidence. It is
not private-avatar parity, representative L3 physics/contact acceptance,
broader application lifecycle or the final interactive visual gate. Sampled
texture/sphere/toon tint realization stays OFF: those coefficients have no
canonical USD representation. Additional runtime UV channels and per-vertex
colour are outside this test. The material-only comparison remains separate.
