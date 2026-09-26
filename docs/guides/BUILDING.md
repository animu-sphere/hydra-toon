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

## The standalone viewport

```sh
ost renderer viewport -- --frames 8 --hidden
ost validate --intent renderer-viewport
```

The first run fetches GLFW. Omit the arguments after `--` for an interactive
window. The viewport builds its own tree, `build/<target>--renderer-viewport`,
and `ost validate --intent renderer-viewport` validates that tree and its
launch record.
