# Native MMD canonical material parity

Date: 2026-10-31. Windows x64, MSVC 19.51, Ninja/Release, CMake 4.4,
OpenUSD 26.08, Vulkan, NVIDIA RTX A5000, deterministic Jolt with six-DOF.
Renderer base `6d660d0b985f71d200653dc5d1da275fa1ba9dd9` plus this change;
installed runtime source `6e921e891f73a655b857a2323057a42a5b568e65` and the
local owner/physics closure from [report 112](112-2026-10-31-mmd-admission.md).
No published package or dependency-pin change is claimed.

## Defect and correction

The generated live material comparison found that an evaluated diffuse alpha
below one produced Blend through the resident snapshot adapter but Opaque
through canonical USD/Hydra. Canonical numeric values agreed, but normalized
material structure differed. Hydra now classifies diffuse alpha consistently
with the direct path. Direct overrides also classify an opaque result from a
translucent authored baseline, while release restores that baseline.

The Vulkan/WebGPU backend's existing texture-alpha classification and MMD
material-table order remain authoritative for drawing; this corrects the
normalized material descriptor rather than adding another texture policy.
Existing material tests cover initial translucent normalization and value-only
restoration to opaque. State tests additionally cover a translucent baseline,
an alpha-above-one override and release.

## Generated composed comparison

`toon-renderer-mmd-material-cpu` uses the runtime-generated two-material
PMX/VMD from `tools/generate-mmd-material-fixture.py` in the runtime repository.
It opens the PMX directly through the installed MMD plugins and creates the
installed native session with USD publication enabled. All 24 material inputs
automatically match resident targets. Each final snapshot reaches the fast
adapter over immutable imported bases; the same session independently
publishes its six canonical inputs per material into its temporary USD layer,
which UsdImaging/mmdImaging reads back through the renderer delegate.

The check compares exact normalized material values and identities at every
tick, over 720 physics ticks in three reset generations, including negative
and group/direct material channels. Sampled tint multiply/add coefficients
are excluded from USD equality: they have no canonical USD representation.
Their direct mapping and GPU realization retain [report 111](111-2026-10-29-live-mmd-materials.md)'s
separate tests. Pause holds the result; Reset restores USD and resident bases;
replay is exact; Close leaves root/session layers unchanged and retained
material results readable.

`toon-renderer-mmd-material-gpu` also compares exact colour bytes and depth
at 128x128, with 1x and 4x MSAA, at ticks 1/60/120/240 in each generation.
Both products hold geometry at the imported baseline to isolate material
delivery from physics/pose rounding. Each keeps its own material values and
the original two material subsets. Sampled tint realization is OFF. The
check requires visible pixel changes from canonical morphs at both sample
counts, exact reset image replay and frame-zero appearance at zero channels.
Repeated rendering uploads no geometry or pipelines and emits no Vulkan
validation messages. This is generated material parity, not combined live
pose/UV/physics image parity or representative private-avatar appearance.

## Reproduction and verification

Use the installed dependency, DLL and plugin setup in the
[viewport guide](../../guides/BUILDING.md#live-mmd-physics-in-the-dedicated-viewport).
Set `TOON_MMD_MATERIAL_TEST_PMX`, `TOON_MMD_MATERIAL_TEST_VMD` and
`TOON_MMD_RUNTIME_PLUGINS` to the generated pair and installed plugin
resources. From Developer PowerShell with the existing configured build:

```powershell
cmake -S . -B build/mmd-admission-viewport -DTOON_MMD_MATERIAL_PROVIDER_TEST=ON
cmake --build build/mmd-admission-viewport --target toon-hydra2-mmd-material-test toon-hydra2-material-test toon-avatar-state-test toon-mmd-runtime-test toon-viewport-mmd --parallel 6
ctest --test-dir build/mmd-admission-viewport -C Release --output-on-failure -R '^(toon-renderer-mmd-material-(cpu|gpu)|toon-renderer-hydra-material(-fallback-gpu)?|toon-avatar-state(-gpu)?|toon-mmd-material-resident|toon-viewport-mmd-material|toon-mmd-runtime-cache|toon-viewport-mmd-runtime)$'
```

All ten selected tests passed, with no skips. The new CPU and GPU comparisons
also passed individually. Earlier attempts exposed test setup mistakes
(an omitted canonical identity table and rounding below the native clock's
first tick) before reaching the material-structure defect. The local build's
localized MSVC include prefix did not track edited headers; touching the
including translation unit ensured those changes were compiled. No generated
build metadata is committed.

Representative material appearance, private Hydra/direct parity, sampled-tint
Hydra representation, additional UV channels and MMD L3 acceptance remain
outside this checkpoint.
