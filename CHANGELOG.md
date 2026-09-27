# Changelog

All notable changes to `hydra-toon` are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html), each `v0.x.0` a
milestone ([versioning](docs/releases/README.md#versioning)). Each released
version has a record in [docs/releases/](docs/releases/README.md).

## [Unreleased]

### Added

- `formations/vrm-host-session/`: the VRM `usdview` session as a committed
  OpenStrata Formation of the canonical `lookdev` runtime, `vrmImaging`
  0.10.0 and `toon` 0.1.0, each pinned by its published digest. Its declared
  command runs `vrm_material_check.py` in `testusdview` on the probe stage
  and needs no environment.
- Renderer report 13: the published `toon` 0.1.0 draws the avatar in that
  Formation with the numbers of the workstation's packages.
- MToon's rim: MatCap, the parametric rim
  and the rim multiply texture, mixed with the light by
  `rimLightingMixFactor`, in `mtoon_opaque` and `mtoon_outline`. `hdToon`
  reads the `matcap` and `rimMultiply` texture roles; a material's slot grew
  to 352 bytes. The headless check `renderer.material.mtoon_rim`, and
  renderer report 14.
- MToon transparency: `mtoon_transparent` (`shaders/mtoon_transparent.slang`)
  blends MToon's Blend alpha mode over what is behind it, after every
  opaque and Mask draw, in MToon's render queue order from
  `renderQueueOffsetNumber` and `transparentWithZWrite`, writing depth only
  with the latter. A double-sided transparent surface draws its back faces
  first, and its outline hull draws after it with its alpha. The core's
  `IsTransparent`, `RenderQueue` and `WritesDepth` state the rules; the host
  frame evidence adds `draws_transparent`. The headless check
  `renderer.material.mtoon_transparent`, and renderer report 15.
- Anti-aliasing: MSAA, 4 samples per pixel by default, resolved as the
  scene pass ends, colour by averaging and depth by sample 0, in the
  offscreen renderer and the viewport. A Mask material's cut is spread over
  its samples by alpha to coverage in `mtoon_opaque`. `RenderOptions` sets
  the count, `toon-viewport --samples N` and the `hdToon` render setting
  `toon:msaaSamples` expose it, and the statistics and host frame evidence
  record it as `samples`. The headless check `renderer.antialiasing.msaa`,
  and renderer report 16.
- Documentation: the release milestones. `v0.x.0` versions replace
  Renderer Phase 0–7 as the delivery sequence; the roadmap has a page per
  coming milestone and no status marks, and the capability matrix is the
  only statement of status. The versioning and patch policy and the release
  gate are in `docs/releases/README.md`.

### Changed

- Four scene pipelines where there were three. Depth writes are dynamic
  state in every MToon pipeline, and `mtoon_outline` blends, returning 1
  for an opaque material, so one hull pipeline serves both.
- `openstrata.renderer.yaml` lists `renderer.material.mtoon_rim` and
  `renderer.material.mtoon_transparent` among the assertions `ost validate`
  requires.
- The MToon pipelines' push constants carry view-from-object's top three
  rows in place of the normal matrix, which the shaders now derive, so a
  fragment knows its view-space position.

## [0.1.0] - 2026-09-28

The first release: Renderer Phase 0 and Renderer Phase 1's renderer work,
published as a package a Formation pins
([release record](docs/releases/v0.1.0.md)).

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
- The host frame evidence (`TOON_HYDRA_EVIDENCE`) counts the materials that
  selected PreviewSurface and MToon, so a `usdview` session shows whether VRM
  materials reached MToon; renderer report 05 and `ost` report 04, on
  composing that session from packages.
- The roadmap page for the VRM host session on Windows, Linux and macOS.
- The VRM `usdview` session runs as an OpenStrata Formation of the canonical
  `lookdev` runtime, `vrmImaging` and the `toon` package; renderer report 06
  and `ost` report 05, which re-verifies report 04 against `ost` 0.23.11.
- The first Renderer Phase 1 slice: meshes bind their material, and a mesh
  whose material selected MToon draws through `mtoon_opaque`
  (`shaders/mtoon.slang`): lit and shade colours with shading shift and
  toony, GI equalization and emission, untextured, under a stand-in camera
  key light. Each material is a slot in one parameter buffer, rewritten only
  when its values change. `RenderWorld` binds a material per mesh and
  derives smooth vertex normals from the points and topology. `hdToon` syncs
  a mesh's material binding and reverses a left-handed mesh's winding.
- The headless assertion `renderer.material.mtoon_opaque`, and
  `material_writes`, `draws` and `draws_mtoon` in the host frame evidence;
  renderer report 07.
- Basic textures: `mtoon_opaque` samples MToon's base colour texture and
  shade multiply texture through the mesh's UVs, each with glTF's wrap modes
  and `KHR_texture_transform`. `RenderWorld` holds decoded textures and mesh
  UVs, each with its own revision, and `ToonMaterial` references textures by
  id. The backend keeps a 128-entry texture table and uploads a texture,
  with mipmaps, only when it changes. `hdToon` reads
  `vrm/textureInfo/baseColor` and `shadeMultiply`, decodes each image once
  with `HioImage` and reads a mesh's `st`.
- The headless assertion `renderer.material.mtoon_textured`, and
  `texture_uploads` and `textures` in the host frame evidence; renderer
  report 08.
- GPU skinning: a mesh `UsdSkel` skins linearly is skinned in the vertex
  stage of both scene pipelines (`shaders/skinning.slang`). `RenderWorld`
  holds a mesh's `ToonSkin` and `ToonSkinPose`, each with its own revision;
  the backend uploads influences when the skin changes and writes the joint
  buffer alone when the pose does. `hdToon` reads usdSkelImaging's
  aggregator and skinning computations itself, re-reading rest data only
  when the aggregator's inputs changed, and applies blend shapes to the rest
  points; dual quaternion skinning still runs the CPU kernel.
- The headless assertion `renderer.skinning.gpu`, the CTest
  `toon-renderer-hydra-skinning`, and `skin_uploads`, `pose_writes` and
  `draws_skinned` in the host frame evidence; renderer report 09.
- The inverted-hull outline: `mtoon_outline` (`shaders/mtoon_outline.slang`)
  draws, before the surface, the hull of every MToon draw whose material asks
  for an outline, each skinned vertex moved out along its normal by MToon's
  width, in world or screen units, times the outline width texture's G, with
  front faces culled, in the outline colour mixed with the surface's shading.
  `ToonMaterial`'s MToon block carries the outline width texture, and
  `HasOutline` says whether a material draws a hull. `hdToon` reads
  `vrm/textureInfo/outlineWidthMultiply` as data.
- The headless assertion `renderer.material.mtoon_outline`, and
  `draws_outline` in the host frame evidence; renderer report 10.
- Renderer report 11: an expression bake from `usd-vrm-plugins`, played in
  `testusdview`, rewrites its MToon material's slot once per time move and
  uploads nothing — `usd-vrm-plugins`' imaging Step I4, in the host session.
- Renderer report 12: the VRM Formations pin the `vrmImaging` 0.10.0 that
  `usd-vrm-plugins` published, and each of reports 06–11's runs gives its
  report's numbers.
- The release workflow, `.github/workflows/release.yml`: a `vX.Y.Z` tag
  builds, tests, validates and reproducibly packages the `hydra` intent on
  Windows with the canonical `lookdev` runtime, pushes the package to
  `ghcr.io/animu-sphere/hydra-toon`, and drafts a GitHub release with the
  package, the digests a Formation pins, a source archive and checksums.
  `scripts/release_version.py`, `make_release_notes.py` and
  `make_package_pins.py` serve it; how a release is cut is in
  `docs/releases/README.md`.

### Changed

- The bootstrap triangle is a scene mesh drawn by `shaders/mesh.slang`
  through the same pipeline on every host; `triangle.slang` is gone.
- `renderer.frame.persistence` also requires one pipeline, one target
  allocation and one mesh upload across its 1,000 frames. With
  `mtoon_opaque` it requires the two scene pipelines, each created once, and
  with `mtoon_outline` the three.
- `mtoon.slang`'s shared parts moved to `mtoon_common.slang`, which
  `mtoon_outline.slang` includes too. The material set is visible to the
  vertex stage, and a material's parameter slot grew from 144 to 224 bytes
  to hold the outline.
- `CreateOffscreenRenderer`, `RenderOffscreen` and `CreatePresentSession`
  take a `SceneShaders` set, which `SceneShadersIn` fills from a shader
  directory, instead of one vertex and fragment shader path.
- The swapchain path renders with dynamic rendering and a depth attachment,
  and tracks its frame on a timeline semaphore instead of a fence.
- The device must be Vulkan 1.3 with `dynamicRendering`, `synchronization2`
  and `timelineSemaphore`; `shaderDrawParameters` is no longer required.
  With textures it must also offer `shaderSampledImageArrayDynamicIndexing`
  and 128 sampled images per stage.
- `strata.lock` is no longer tracked. `ost` rewrites it on every build with
  the runtime that built last, so it recorded the workstation, not the
  project.
- The `lookdev` build is measured on OpenStrata's canonical
  `26.08-gl-windows-x86_64` runtime instead of an adopted local build, and
  the renderer package comes from the `hydra` intent (`ost package --intent`,
  `ost` 0.23.11). The per-profile `strata.<runtime-id>.lock` files are not
  tracked either.
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
- `toon-renderer-usdview-host` is a `SKIP` on a host whose OpenGL is below
  4.5, which `usdview` needs whatever the renderer, as on a hosted CI runner;
  any other failure still fails.
