# Native MMD viewport admission diagnostics

Date: 2026-10-31. Windows x64, MSVC 19.51.36256, Ninja/Release, CMake 4.4,
OpenUSD 26.08, Vulkan, NVIDIA RTX A5000, deterministic Jolt 5.5.0 with six-DOF.

## Ownership and package composition

The viewport copies the installed native session's `DampingNormalizations()`
and `InertJoints()` records into owned host status. The Avatar runtime panel
and final log show original/effective damping coefficients with attribute paths,
and inert joint paths. Successful construction/Reset replaces the records;
Stop clears them. The host does not interpret PMX coefficients, classify joints,
rewrite source USD or implement a solver. Semantics remain with the
[runtime session](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/architecture/MMD_SESSION.md)
and [MMD owner](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/PHYSICS_INTEGRATION.md).

Bases: renderer `27174bd5de33eaf781ee7882338fe470af13732a` plus this change;
runtime `6e921e891f73a655b857a2323057a42a5b568e65`;
MMD `b6f6d43f2817eff923e50f744cf326ad0329fbeb`;
physics `c45e3181201bc9e9a62569892604f6a31b225f61` with the locally rebuilt
source-damping package closure. All consumers use the same Python-enabled
OpenUSD installation. This is local installed composition; no published package
or dependency-pin update is claimed.

## Generated verification

The five selected CTests pass: `toon-mmd-runtime-cache`,
`toon-mmd-runtime-admission`, `toon-viewport-mmd-runtime`,
`toon-mmd-material-resident` and `toon-viewport-mmd-material`.
The admission check was rerun successfully after extending it to both lower
and upper damping normalization bounds.

The new check uses the generated morph PMX/mixed VMD with anonymous test-only
opinions: a copied kinematic anchor, a standard inert joint and damping -2/2.
It checks exact attribute paths and original/effective -2/0 and 2/1 values,
240 resident ticks across two generations, exact joint/morph/UV reset replay,
Pause, changed-admission Reset refresh, retained status ownership, Stop/restart,
and unchanged source/root and authored session layers. Other selected tests
cover cached/uncached tangents and revisions, material output, GPU playback,
scene replacement and teardown.

Configure installed dependencies and generated input paths as in the
[viewport guide](../../guides/BUILDING.md#live-mmd-physics-in-the-dedicated-viewport).
In Developer PowerShell, the measured local build/test sequence was:

```powershell
cmake --build build/mmd-admission-viewport --config Release --target toon-mmd-admission-test toon-mmd-runtime-test toon-viewport-mmd --parallel 6
ctest --test-dir build/mmd-admission-viewport -C Release -R 'toon-mmd-runtime-(cache|admission)|toon-viewport-mmd-runtime|toon-mmd-material-resident|toon-viewport-mmd-material' --output-on-failure
```

The compiler environment was initialized through `Microsoft.VisualStudio.DevShell`
and `Enter-VsDevShell -VsInstallPath ...`, with `_CL_=/Z7`. The local launcher
script's Visual Studio discovery failed on JSON encoding; direct module entry
worked. Initial package lookup needed explicit TBB/OpenSubdiv configuration
directories from the selected OpenUSD installation. A test-only C++ layer-export
call was corrected before the passing run.

## Unmodified private GPU intake

The original private pair has SHA-256 identities:

- PMX: `b37ed18115891c296b0955515edd004067e446acd9268dec8a548215afb30403`.
- VMD: `737de0562a388d7ec91492826de995e6f3dde6ac8d5ddca784785029c2388c92`.

The native viewport opens the PMX directly and uses the original VMD, without
damping/joint opinions, motion filtering or experimental spring opt-ins.
With matching DLL/plugin environment, run:

```text
toon-viewport-mmd --usd <original.pmx> --mmd-physics --avatar-motion <original.vmd> --play --hidden --frames 120 --width 640 --height 720 --samples 4 --vsync on
```

The bounded run returns zero: 120 presented frames, 952 physics ticks, one
Hydra sync, one resident skin, 73 morph bindings, one UV mesh and 624 material
inputs. It reports zero refusals/mismatches, 58 damping normalizations and 10
inert joints; teardown reports zero bodies. Both input hashes are unchanged.
Assets, machine-local paths and raw diagnostics remain ignored.

Measured runtime cost is 11.39 ms/tick and resident conversion 11.49 ms/result;
119 conversions and 119 reuses. Mean GPU scene time is 6.71 ms. These are a
single bounded run with retained catch-up backlog, not a controlled comparison
or real-time claim.

A separate visible 1100x900/4x session was opened for the user's review. The
window shows live playback with 265 bodies. The user confirmed the visible
result on 2026-10-31. This closes the scoped unmodified-input viewport visual
review; no additional manual playback-control or full-motion checks are inferred.

This evidence does not establish full-motion coverage, private Hydra/direct
numeric or pixel parity, representative material quality, licensed fixed-scene
L3 acceptance or broader application lifecycle acceptance. The remaining gates
belong to the [MMD roadmap](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/roadmap/current.md).
