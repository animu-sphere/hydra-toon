# Changelog

All notable changes to `hydra-toon` are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Each released
version will have a record in [docs/releases/](docs/releases/README.md).

## [Unreleased]

### Added

- The project, generated with `ost init --template renderer --name toon`
  (OpenStrata 0.23.6, template 0.5.1): the host-neutral core, the Vulkan
  backend, the headless runner, the standalone viewport and the `hdToon`
  Hydra adapter, all drawing the template's bootstrap triangle.
- A `hydra` build intent in `openstrata.toml` that builds the Hydra adapter.
- Documentation: the design policy, integration scope and material policy;
  the project layout; the capability matrix and measured configurations; the
  roadmap; the building guide; and the first `ost` dogfooding report.
- Renderer Phase 0's scene path. `RenderWorld` holds meshes (triangulated
  indices, points, transform, colour, visibility) and a `ToonView` camera,
  each with its own revision, and extraction turns a commit into a
  `DrawList`.
- `Toon::OffscreenRenderer`: a persistent Vulkan renderer that creates its
  pipeline once, reallocates render targets only on a resize, uploads mesh
  geometry only when its revision changes, and reads colour and depth back at
  any extent. It uses dynamic rendering, Synchronization2 and one timeline
  semaphore, with one frame in flight.
- `hdToon` draws Hydra meshes through the Hydra camera at the AOV's
  resolution. Mesh sync is routed by dirty bit. Skinned points are taken from
  `UsdSkel`'s ext computations, whose CPU kernels the delegate runs.
- The CTest `toon-render-world` for the core's dirty routing, and the first
  renderer report.
- `toon-hydra2-material-probe`, which records what of a Material's canonical
  semantics reaches a classic `HdMaterial`, and renderer report 02, which
  answers MAT-Q1 for MToon with it.
- `ToonMaterial` in the core: material policy §4's common part and MToon
  block, without textures. `RenderWorld` holds materials, and a value edit
  advances only a material's parameters revision while a model, alpha-mode
  or double-sidedness edit also advances its structure revision.
- `hdToon` creates a material Sprim for every Hydra material. In `Sync` it
  reads the prim's `vrm` container from the terminal scene index: `vrm/mtoon`
  selects MToon and its `vrm/material` and `vrm/mtoon` values are
  normalized into `ToonMaterial`; anything else is PreviewSurface. Meshes do
  not bind materials yet.
- The CTest `toon-renderer-hydra-material`, whose binary also reports what a
  real stage's materials select with `--stage`, and renderer report 03.
- `hdToon` observes the terminal scene index and takes value-only material
  changes from it: a material whose `vrm` locators alone were dirtied, which
  emulation never syncs, is re-read in `Update()`. `--stage` now also moves
  to the stage's end time code; renderer report 04.

### Changed

- The bootstrap triangle is a scene mesh drawn by `shaders/mesh.slang`
  through the same pipeline on every host; `triangle.slang` is gone.
- `renderer.frame.persistence` also requires one pipeline, one target
  allocation and one mesh upload across its 1,000 frames.
- The swapchain path renders with dynamic rendering and a depth attachment,
  and tracks its frame on a timeline semaphore instead of a fence.
- The device must be Vulkan 1.3 with `dynamicRendering`, `synchronization2`
  and `timelineSemaphore`; `shaderDrawParameters` is no longer required.
- `strata.lock` is no longer tracked. `ost` rewrites it on every build with
  the runtime that built last, so it recorded the workstation, not the
  project.
- The Hydra colour and depth AOVs are written bottom row first, as Hydra
  lays them out; the template's copy was upside down, and its triangle,
  itself flipped in Vulkan clip space, hid that.
- The Hydra renderer plugin reads `gpuEnabled` through the
  `HdRendererCreateArgs` container schema on OpenUSD 26.08 and later
  (`HD_API_VERSION` 98), which the template's code did not compile against.
- MSVC builds use `/utf-8`, so a CP932 host does not warn C4819 on the UTF-8
  sources.
- `openstrata.renderer.yaml` declares one frame context, matching the
  one-frame-in-flight policy and the generated swapchain path.
- The Hydra renderer plugin includes `hd/version.h` itself instead of relying
  on another header for `HD_API_VERSION`, as renderer template 0.5.2 does.
- `ost` 0.23.7 or newer is required. It keeps `ost validate` passing across a
  no-op build and after `ost renderer viewport`, so the documented
  workarounds for both are gone, and `ost validate --intent renderer-viewport`
  validates the viewport build.
- The install-tree CTest merges the installed runner's `renderer.install_tree`
  verdict into `renderer-report.json`, as renderer template 0.5.3 does, so
  `ost validate` on the default build passes the check after `ost test`.
  `ost` 0.23.8 or newer is required.
