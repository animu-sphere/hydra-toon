# Building and testing

Every command on this page has been run in this repository; the latest run is
[ost report 02](../reports/ost/02-2026-09-26-v0.23.7-report-01-reverified.md).
What each build contains is
[PROJECT_LAYOUT.md §5](../architecture/PROJECT_LAYOUT.md#5-build-intents-and-runtime-profiles),
and what it was measured on is
[SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md).

## Prerequisites

- `ost` 0.23.7 or newer (`ost --version`).
- A Vulkan SDK that provides Vulkan 1.3 and `slangc` (bundled from 1.3.296).
  Without it the build still succeeds, and the GPU checks report `SKIP`.
- CMake 3.24 or newer, Ninja, and a C++20 compiler. On Windows, `ost` loads
  the MSVC environment itself.

## The default build — no OpenUSD

```sh
ost build --check
ost build --jobs auto
ost test
ost validate
```

`ost build` runs `toon-headless`, which renders the bootstrap frame 1,000
times and writes `build/<target>/renderer-report.json`. `ost validate` reads
it; the Hydra assertions are `SKIP` in this build by design, and so is
`renderer.install_tree`, even after its CTest passes
([report 02 §3](../reports/ost/02-2026-09-26-v0.23.7-report-01-reverified.md#3-p3--rendererinstall_tree-on-core)).

## The Hydra adapter

The adapter needs a real OpenUSD imaging runtime. This repository was measured
with an OpenUSD 26.08 `lookdev` runtime the workstation had already adopted;
`ost runtime list` shows what is available, and OpenStrata's
[adoption guide](https://github.com/animu-sphere/open-strata/blob/main/docs/guides/adopt-a-renderer-project.md#2-adopt-a-digest-pinned-runtime)
covers adopting one.

```sh
ost runtime list
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra
ost validate --profile lookdev --intent hydra
```

`ost test` includes `toon-renderer-usdview-host`, which opens `testusdview`
on the installed smoke scene for a few seconds and keeps
`usdview-first-frame.png` and `usdview-stable-update.png` under
`build/<target>--hydra/adapters/hydra2/usdview-install/`.

The `lookdev` build rewrites `strata.lock` to pin the `lookdev` runtime, and
the next plain `ost build` restores the `core` pin. Do not commit the
`lookdev` rewrite
([report 02 §4](../reports/ost/02-2026-09-26-v0.23.7-report-01-reverified.md#4-observation--stratalock-follows-the-last-profile-built)).

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

## Plain CMake

The project is an ordinary CMake project. `ost` adds the runtime prefix, the
generator and the evidence bookkeeping. The options are `TOON_ENABLE_VULKAN`
(default `ON`), `TOON_ENABLE_HYDRA2`, `TOON_ENABLE_VIEWPORT` and
`TOON_BUILD_TESTS` (default `ON`). A plain-CMake tree is validated with
`ost validate --build-dir <dir>`, which does not claim `ost` built it.
