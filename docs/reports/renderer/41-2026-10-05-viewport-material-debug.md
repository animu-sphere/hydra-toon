# Viewport evaluated material diagnostics

- Date: 2026-10-05
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Runtime: canonical CY2026 OpenUSD 26.08 lookdev
- Scope: the evaluated-material-value and override-activity viewport diagnostics
  in [v0.3.0](../../roadmap/v0.3.0.md)

## Panel and value route

The Materials panel lists resident renderer material ids, bound mesh ids,
shading model, alpha mode, sidedness and effective normalized values. MToon
colours, scalars and texture transforms can be edited. Texture identities and
pipeline structure remain display-only. Colours are linear numeric values;
non-finite numeric edits are discarded. PreviewSurface values are read-only
because the current fallback renders mesh display colour rather than a
PreviewSurface network.

Per-material release and release-all restore the latest scene values. Edits
use `HydraScene`'s existing delegate override route and the viewport's late
commit, without USD authoring or requesting Hydra sync. The UI reads every
effective value from the current snapshot and retains no override copy.

`MaterialSnapshot::parameters_overridden` exposes override activity even
when its values equal the scene. Activation and release in that case advance
neither scene nor parameter revisions. Old snapshots retain their original
flags. Material structure changes invalidate the override; removal and scene
replacement discard its state.

## Regression evidence

`toon-viewport-material-debug-state` uses an original, committed triangle
with a bound material and no format-plugin dependency. It exercises equal
activation/release, rejected ids and structural edits, duplicate updates,
late application after extraction, repeated release, replacement with reused
numeric ids, and material removal through an in-memory shared-layer edit.
The fixture is never saved. Six late frames retain one Hydra sync before
the deliberate removal; static arrays and revisions remain unchanged.

`toon-viewport-material-debug-gpu` runs those six frames at 96x96 and 4x
MSAA on one persistent renderer. Only setting different values and restoring
the scene write the material buffer, once each after initial preparation.
No subsequent point, topology, skin, pose, morph, weight or texture writes,
pipeline creation or target allocations occur. Vulkan validation reports
zero messages. Non-background depth is required, and all colour/depth pairs
match the baseline exactly. This fixture uses the PreviewSurface fallback;
unchanged pixels establish retained geometry, not a measured MToon edit.

`toon-expression-state` additionally checks material activity flags on
equal values, old snapshots, restoration after underlying scene changes and
structural invalidation. The existing `toon-expression-gpu` independently
verifies visible MToon value changes against ordinary scene setters across
opaque, Mask and Blend routes. This run verifies the programmatic host route
and compiles the panel; it does not claim an interactive panel inspection or
new representative-avatar evidence.

## Build and validation

```sh
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
cmake --build build/morph-no-vulkan --config Release
ctest --test-dir build/morph-no-vulkan -C Release --output-on-failure
```

The existing runtime store was selected explicitly for OpenStrata. A plain
CTest attempt without runtime activation could not start six viewport cases
because their OpenUSD DLLs were absent from the search path; the OpenStrata
test command supplies that environment. Sandbox execution restrictions also
required ordinary execution permissions for Ninja and OpenUSD-linked tests.

The viewport-usd build, all 53 CTests (including usdview and the install
tree), and completion-bound OpenStrata validation pass. Artifact integrity
is an explained skip because this change creates no release package.
The plain-CMake build without OpenUSD/Vulkan and all 11 CTests pass.
`git diff --check` and documentation relative-file-link checks pass.

Source-owner expression names, canonical material-input mappings, runtime
frame/binding identity and producer-clock integration remain in v0.3.0.
This diagnostic change supplies no new evaluation or display-latency evidence.
