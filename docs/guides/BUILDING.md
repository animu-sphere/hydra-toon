# Building and testing

Every command on this page has been run in this repository; the latest run is
[renderer report 01](../reports/renderer/01-2026-09-26-phase0-mesh-camera.md).
What each build contains is
[PROJECT_LAYOUT.md §5](../architecture/PROJECT_LAYOUT.md#5-build-intents-and-runtime-profiles),
and what it was measured on is
[SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md).

## Prerequisites

- A Vulkan SDK that provides Vulkan 1.3 and `slangc` (bundled from 1.3.296).
  Without it the build still succeeds, and the GPU checks report `SKIP`.
- CMake 3.24 or newer, Ninja, and a C++20 compiler. On Windows, `ost` loads
  the MSVC environment itself; for plain CMake, use a developer shell with the
  compiler on `PATH`.

## Plain CMake — no OpenUSD

From the repository root, build and run the default tests:

```sh
cmake -S . -B build/plain-cmake -G Ninja
cmake --build build/plain-cmake
ctest --test-dir build/plain-cmake --output-on-failure
```

The options are `TOON_ENABLE_VULKAN` (default `ON`), `TOON_ENABLE_HYDRA2`,
`TOON_ENABLE_VIEWPORT` and `TOON_BUILD_TESTS` (default `ON`). Plain CMake does
not require `ost`; optional adapters need their own dependencies. A plain-CMake
tree can be validated with `ost validate --build-dir build/plain-cmake`, which
does not claim `ost` built it.

## OpenStrata — no OpenUSD

With `ost` 0.23.8 or newer (`ost --version`), run:

```sh
ost build --check
ost build --jobs auto
ost test
ost validate
```

`ost build` runs `toon-headless`, which renders the bootstrap triangle scene
1,000 times on one persistent renderer and writes `build/<target>/renderer-report.json`. `ost validate` reads
it; the Hydra assertions are `SKIP` in this build by design.
`renderer.install_tree` is `SKIP` after `ost build` and passes once `ost test`
has run `toon-renderer-install-tree`, which installs the project, runs the
installed `toon-headless --install-tree` and merges its verdict into the
report.

## The Hydra adapter

The adapter needs a real OpenUSD imaging runtime. This repository is measured
with OpenStrata's canonical OpenUSD 26.08 `lookdev` runtime, which includes
`usdview`, pulled by digest:

```sh
ost artifact pull oci://ghcr.io/animu-sphere/openstrata-runtime-cy2026-lookdev@sha256:b840ed4690aa39d4582bc03c0a09fa7bab717216635b55a4eb718482dbfb196b \
    --expect-artifact sha256:b982656c07dd9147973e3c7197d9b050ee48dd1ac291988f7e6025c76c4a785b
ost runtime pull cy2026 --profile lookdev \
    --from-artifact sha256:b982656c07dd9147973e3c7197d9b050ee48dd1ac291988f7e6025c76c4a785b
ost runtime validate cy2026 --profile lookdev
```

That is the Windows x86_64 leaf; the Linux leaf's digests are in OpenStrata's
[v0.23.11 verification report](https://github.com/animu-sphere/open-strata/blob/main/docs/reports/2026-09-26-v0.23.11-renderer-formations.md#canonical-runtime-evidence),
and its
[adoption guide](https://github.com/animu-sphere/open-strata/blob/main/docs/guides/adopt-a-renderer-project.md#2-adopt-a-digest-pinned-runtime)
covers adopting other runtimes. `ost runtime list` shows what is already in
the local store.

```sh
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra
ost validate --profile lookdev --intent hydra
```

`ost test` includes `toon-renderer-usdview-host`, which opens `testusdview`
on the installed smoke scene for a few seconds and keeps
`usdview-first-frame.png` and `usdview-stable-update.png` under
`build/<target>--hydra/adapters/hydra2/usdview-install/`.

`ost` pins the `core` runtime in `strata.lock` and the `lookdev` one in
`strata.openstrata-cy2026-<os>-<arch>-py313-lookdev.lock`; a `lookdev` build
leaves `strata.lock` alone. Both files are local and ignored by Git.

`ost package --profile lookdev --intent hydra` packages the adapter's build
as a `renderer` component, which an OpenStrata Formation composes into a
`usdview` session
([renderer report 06](../reports/renderer/06-2026-09-27-vrm-formation.md)).

`ost renderer view` opens `usdview` interactively on the same install. It has
not been run in this repository yet.

## The VRM host session

[`formations/vrm-host-session/`](../../formations/vrm-host-session/) is the
`usdview` session that draws a VRM stage with MToon: the `lookdev` runtime
above, `usd-vrm-plugins`' `vrmImaging` and this repository's published `toon`
package, each pinned by digest. Nothing is built. On Windows x86_64, with the
runtime pulled as above and a host Python 3.13:

```sh
ost artifact pull oci://ghcr.io/animu-sphere/usd-vrm-plugins@sha256:84dbb7e550c55d249798f7288066de3589c383a75a35a6268328a497f147dda8     --expect-artifact sha256:894fd616f1414d5b393ff0d50abbf7ef18cb603562133c72e642493b48f71667
ost artifact pull oci://ghcr.io/animu-sphere/hydra-toon@sha256:a14c583fcd7a6b29c07b955808cbfc631820786589567618a3156182dbc91a93     --expect-artifact sha256:265328f06d0fcbdc718b459011f321c769dcf423881f8b67a12321254d728c29
cd formations/vrm-host-session
ost formation doctor formation.toml
ost formation run formation.toml
```

The declared command opens `testusdview` on the committed
`material-probe.usda` and asserts that its VRM material selected MToon; it
fails if `vrmImaging` is missing. To draw an avatar instead, override the
command and give the avatar's MToon material count:

```sh
TOON_EXPECT_MTOON=12 ost formation run formation.toml --     testusdview <avatar.usdz> --renderer Toon --testScript vrm_material_check.py
```

Hydra does not carry a stage's `metersPerUnit`, so a stage whose unit is
not the metre states it through the render setting `toon:metersPerUnit`, 1
by default; MToon's world-coordinates outline width is in metres, and this
setting turns it into the stage's units. In a `testusdview` script,
`appController._stageView.SetRendererSetting("toon:metersPerUnit", 0.01)`
sets it for a stage of centimetres
([renderer report 17](../reports/renderer/17-2026-09-28-outline-meters-per-unit.md)).

`TOON_HYDRA_EVIDENCE` and `TOON_HYDRA_IMAGE` keep the frame evidence and the
image; unset, they go to a temporary directory the check prints
([renderer report 13](../reports/renderer/13-2026-09-28-host-session-formation.md)).

usdSkelImaging hides a skinned mesh's authored normals from Hydra unless
`USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS=1` is set before the stage is
imaged, and `hdToon` then derives smooth normals per mesh. On an avatar
split into several meshes, such as bangs whose tips are a separate Blend
mesh, those differ where the meshes meet and show as a line. Set it in the
session's environment for the authored normals; the published v0.1.0 `toon`
package reads none, so this needs a later one:

```sh
USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS=1 ost formation run formation.toml --     usdview <avatar.usdz> --renderer Toon
```

The setting is process-wide, so Storm in the same session reads it too. The
standalone viewport sets it itself ([renderer report 20](../reports/renderer/20-2026-09-28-authored-normals.md)).

## The standalone viewport

```sh
ost renderer viewport -- --frames 8 --hidden
ost validate --intent renderer-viewport
```

The first run fetches GLFW. Omit the arguments after `--` for an interactive
window. `--samples N` sets the MSAA samples per pixel, 4 by default; 1 turns
anti-aliasing off. In a Hydra host, the render setting `toon:msaaSamples`
does the same. The viewport builds its own tree, `build/<target>--renderer-viewport`,
and `ost validate --intent renderer-viewport` validates that tree and its
launch record.

Left drag orbits, a middle or Shift+left drag pans, a right drag or the
wheel dollies; F frames the scene and R returns to the last framing.
`--screenshot <file.ppm>` writes the last of `--frames N` frames, and P
writes the next frame to `toon-viewport-<n>.ppm`.

### A USD stage in the viewport

The `viewport-usd` intent builds the Hydra adapter and the viewport into one
tree, where `--usd` draws a stage through Hydra in the viewport's own frame
loop ([renderer report 19](../reports/renderer/19-2026-09-28-hydra-fed-viewport.md)):

```sh
ost renderer viewport --intent viewport-usd --profile lookdev -- --usd <stage>
```

`ost renderer viewport` launches the viewport in the runtime's environment
alone, so a VRM avatar draws with every material PreviewSurface. For MToon,
run the built viewport as the command of the VRM Formation, which adds
`vrmImaging`:

```sh
cd formations/vrm-host-session
ost formation run formation.toml --     ../../build/cy2026-windows-x86_64-py313-lookdev--viewport-usd/adapters/viewport/toon-viewport.exe     --usd <avatar.usdz>
```

`ost renderer viewport` picks the viewport among every executable of that
name in the tree, and the `usdview` host test installs a copy under
`adapters/hydra2/usdview-install/`, which it may launch instead; after a
change, run `ost test` before looking, or name the build's executable as
above.
