# Changelog

All notable changes to `hydra-toon` are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html), each `v0.x.0` a
milestone ([versioning](docs/releases/README.md#versioning)). Each released
version has a record in [docs/releases/](docs/releases/README.md).

## [Unreleased]

### Added

- Viewport Skeleton panel: USD joint names, parents and evaluated world
  positions, with optional bones, joint markers, labels and selection
  highlighting. Diagnostics use UsdSkel's animation mapping and rest fallback,
  with homogeneous six-plane clipping, and never request Hydra sync or
  change renderer state. The display follows selected USD time; external
  late pose overrides remain outside this diagnostic view.
- Skeleton state/GPU and projection regressions: nonidentity world/bind
  transforms, reordered and partial animation, layer edits/removal,
  unchanged colour/depth and scene writes, and near-plane crossings.
- Evaluated late-frame input in both Vulkan sessions: after GPU/acquire waits
  and structural preparation, accept latest poses, morph weights, material
  parameters and camera values without another extraction or static upload.
  Structural or non-finite samples are rejected atomically and retain the
  ordinary extracted frame. Source evaluation remains external.
- Monotonic input/write/submit/present-API timestamps, fast-buffer CPU costs,
  frame-time variance, a viewport Latency panel and `--telemetry-output` JSON.
  Repeated unchanged inputs do not inflate response-latency samples; actual
  display timing remains unmeasured.
- Late-input state/GPU and viewport presentation regressions, including
  post-extraction updates and structural fallback with exact image checks.
- Viewport Morphs panel: inspect evaluated subshape slots, edit signed weights,
  and release one or all transient overrides to resume current USD animation.
  Edits use the delegate fast path without USD authoring or Hydra sync.
- Override activity in mesh snapshots, including equal-value overrides without
  GPU writes; Hydra-host state and GPU regressions for the viewport debug route.
- Viewport wall-clock USD animation with play/pause, seeking, single-time-code
  steps, playback speed and looping; Space toggles playback. Deterministic
  `--time-step` captures retain their existing sampling behavior.
- Playback state and GPU regressions: paused/unchanged time syncs Hydra once,
  matches deterministic capture pixels, and playback keeps static uploads fixed.
- Transient evaluated morph-weight and material-parameter overrides in the
  render world and Hydra delegate, accepting host updates without USD edits
  or Hydra sync. Scene values remain separate; clearing restores their latest
  values, and binding or material structure changes invalidate stale overrides.
- Expression state checks and 180 persistent Vulkan colour/depth comparisons
  across MToon Opaque, Mask and Blend, with and without hulls, requiring only
  weight-buffer and material-slot writes after initial upload.
- Hydra ingestion of sparse, dense and normal-only UsdSkel blend-shape
  normal offsets, including inbetweens, into resident GPU targets. Missing
  offsets retain authored or derived rest normals. Lit MToon surface and
  hull images match independent CPU references across Opaque, Mask and Blend.
- Sparse GPU morphs before skinning in every Vulkan scene pipeline, with
  independent target and weight revisions and resident rest geometry.
- Morph upload/write statistics in both renderer paths, Hydra evidence and
  the viewport upload panel; CPU-reference image checks and a continuous
  Hydra-fed morph capture test.

### Changed

- The viewport syncs Hydra only after time or scene-index changes; paused
  camera and diagnostic frames reuse scene state. The overlay and final log
  expose the sync count.
- Linear UsdSkel blend-shape weights, including inbetweens, update the GPU
  weight buffer without baking or uploading points. Morphed hulls bypass
  rest-envelope culling conservatively; morphed meshes cannot act as rigid
  occluders. Dual quaternion GPU evaluation remains outside this change.
- The committed VRM host Formation pins the published `toon` 0.2.0 package.
  Renderer report 33 verifies release assets, anonymous GHCR access,
  Formation smoke and headless GPU checks, and four avatar captures matching
  the preparation build pixel for pixel.

### Fixed

- A UsdSkel aggregator edit that changes only morph targets no longer
  re-sends unchanged rest points. Normal-offset edits preserve geometry,
  skin, pose and weight revisions.

## [0.2.0] - 2026-10-04

### Changed

- Accept measured 4x MSAA as the v0.2.0 outline-quality baseline, recording
  residual subpixel sampling variation as a known limitation.

### Fixed

- Ignore typed Hdx application helper lights advertised as distant/dome
  lights. Their converted intensity no longer saturates avatars in usdview
  or disables the camera-light fallback; authored UsdLux rigs are retained.
- Keep the viewport intent's inline TOML table compatible with Python's
  standard parser, so release-version preflight accepts the manifest.

### Added

- Local VRM reproduction comparison against pinned three-vrm and usdview
  using the viewport's exported camera; full and close-up VRM 0.x/1.0
  captures, foreground differences and continuous outline sequence evidence.
- Bounded viewport `--capture-sequence DIR` for every presented frame and
  `--camera-output FILE` for its final OpenGL-convention view/projection.
  Captures omit the overlay; readbacks require separate performance runs.

- Reproducible outline temporal-quality evaluation in the Hydra viewport:
  thin GPU-skinned hulls under translation and rotation at two distances,
  linear-light spatial and temporal residuals against finite supersampling,
  and signed outlines-on/off comparisons over animated VRM poses. Renderer
  report 31 records 248 capture runs; default 4x sampling variation remains.

- Current-frame opaque-triangle occlusion for MToon hulls, including
  animated Opaque, Mask and Blend targets. A rigid unlit or double-sided
  Opaque blocker must cover the entire expanded hull and be wholly nearer
  in depth; uncertain coverage keeps the draw. Work and storage are bounded,
  with no CPU skinning, temporal history, GPU query, readback or extra
  upload. `--outline-culling off` disables both frustum and occlusion
  omission. Renderer report 30 records 1,792 identical GPU colour/depth
  comparisons, viewport reveal/return captures and representative VRM
  regressions. Skinned occluders and combined triangle coverage are excluded.

- Conservative near/far-plane omission for MToon outline hulls. The
  animated full-width envelope now covers all six clip planes, preserving
  hulls that cross a depth boundary and the existing slope depth bias.
  Renderer report 29 records identical colour/depth over 384 controlled
  poses at 1x/4x MSAA, and viewport captures with depth-clipped hulls omitted
  and a returning skinned silhouette preserved. No extra uploads or waits.

- Conservative side-frustum omission for MToon outline hulls, including
  animated Opaque, Mask and Blend meshes. Cached rest-point/joint envelopes
  account for the current pose, world/screen width and stage unit without
  CPU vertex skinning or extra uploads. The viewport's
  `--outline-culling on|off` compares against ordinary hull submission.
  Renderer report 28 records identical colour/depth over 240 controlled
  poses and identical representative-avatar captures with fewer hull draws.

- Reproducible viewport AA evaluation: generated thin-feature fixtures,
  linear-light supersampled comparisons, avatar close-ups and repeated
  motion timings at 1x/2x/4x/8x. Renderer report 27 retains the 4x default
  and records the remaining texture/shading and temporal limitations.
  `--camera-pan X Y` and `--camera-dolly N` reproduce initial evaluation
  views, with capture tests proving a sample change preserves the camera.

- Scene lights: a host-neutral `ToonLight` and revisioned world state,
  Hydra distant, sphere and dome Sprims, and per-frame Vulkan light data.
  Directional, inverse-square point, shaped spot and uniform ambient feed
  MToon surfaces, rim and lit outlines. Colour, intensity, exposure,
  diffuse, colour temperature, transform and visibility come from USD;
  time and parameter edits upload no geometry or material. A scene without
  supported lights retains the camera key and ambient. The fixed buffer
  holds 32 direct sources; uniform ambient lights sum independently.
- Viewport lighting and material controls: scene/camera lighting, direct
  and ambient strength, fallback key direction, and surface, base colour,
  normals, direct-light and ambient views. CLI equivalents provide
  repeatable captures. Renderer report 26 records the USD time/update
  route, GPU checks and animated avatar comparisons.

- MToon's remaining texture inputs: sRGB emissive, linear normal with its
  scale, shading shift's linear R and scale, and UV animation mask's linear
  B. All nine MToon texture roles retain their own wrap and transform;
  normal mapping derives a tangent frame from deformed positions and mesh
  UVs, retaining vertex normals when the image or usable UVs are absent.
- Timed MToon UV scroll and rotation before each role's texture transform,
  including outline width in the vertex stage; the mask stays unanimated
  and MatCap stays view-mapped. `RenderWorld::SetTimeSeconds` and Hydra's
  `toon:timeSeconds` feed one frame buffer, without material writes or
  geometry uploads. The viewport converts USD time codes by the stage's
  `timeCodesPerSecond`. Renderer report 25 records GPU channel, transform,
  Mask and culling checks and the viewport's static-reference comparison.

- Outline evaluation in the viewport: `--time` and `--time-step` select
  reproducible USD poses, and `--outlines on|off` and the overlay's Outlines
  checkbox compare hulls without material edits or uploads. Actual hull
  draw counts include transparent hulls; `--expect-hulls` checks them.
  GPU checks cover perspective and scaled outline width, width-texture
  sampling, subpixel motion at 1x/4x/8x, and nearly coincident skinned hulls.
  Renderer report 24 records the animated VRM's draw and GPU cost and the
  remaining subpixel coverage variation at 4x.
- All-zero G width textures omit their hull draw calls, for Opaque and
  Blend, with the decision refreshed only on texture revision changes.
  Missing textures and meshes without UVs retain their white fallback.
  Zero, negative and non-finite outline widths emit no hull.

- The viewport's `Open File...` button and Ctrl+O use Native File Dialog
  Extended 1.3.0, following hydra-merlin. USD, VRM and PMX filters pass the
  selected UTF-8 path to OpenUSD's registered format plugins. A successful
  open replaces and frames the scene; a failure retains it and displays an
  error. `PresentSession::ResetScene` releases the previous scene's cached
  resources after its GPU frame completes. Seven regression tests cover
  replacement from bootstrap and USD, overlapping mesh ids, Japanese paths
  and failed opens, with byte-for-byte capture comparisons.
- The viewport's telemetry (design policy §24): a present session writes
  GPU timestamps between the parts of each frame (uploads, the swapchain
  image wait, unlit, outline, opaque, transparent, resolve, capture,
  overlay) and reads them a frame later, and `PresentStatistics` carries
  them with the draw calls of each part, the triangles, the pipeline binds
  and `RenderFrame`'s CPU wait and submit time. `toon-viewport` shows them
  with its Hydra sync, extraction and frame interval, as mean, p95 and max
  over the last 1,024 frames, on a Dear ImGui 1.92.8 overlay. The overlay
  also shows the uploads and has buttons for the MSAA sample count. `O`
  hides it, `--overlay off` turns it off, and a run ends with `Timing:`
  lines. ImGui stays in the viewport: it hands the backend an
  `OverlayDrawList`, which a pass of its own draws after any capture is
  copied, so a screenshot never shows it. The CTests
  `toon-viewport-capture-no-overlay` and
  `toon-viewport-capture-without-overlay`, and renderer report 22.
- The viewport's MSAA sample count changes while it runs: the keys 1, 2, 4
  and 8, and `PresentSession::SetSamples`, which waits for the frame in
  flight and rebuilds the four scene pipelines and the multisampled
  targets, keeping the set layouts, descriptor sets and everything
  uploaded. `PresentStatistics` counts `sample_changes` and the uploads
  `OffscreenStatistics` counts. `toon-viewport --switch-samples N`, the
  CTest `toon-viewport-samples`, and renderer report 21.
- The Hydra-fed viewport: the `viewport-usd` build intent builds the Hydra
  adapter and the viewport together, and `toon-viewport --usd <stage>`
  populates a render index through UsdImaging's scene indices with
  `hdToon`'s delegate, syncs it each frame for the `geometry` and `proxy`
  render tags and draws the delegate's scene into the swapchain, with no
  render pass and no readback. `HdToonRenderDelegate::CommitScene` takes a
  snapshot to reuse. In the VRM Formation, 20 of 20 avatar draws select
  MToon, answering design policy DP-Q1. Renderer report 19.
- The viewport's orbit camera: left drag orbits, middle or Shift+left drag
  pans, right drag or the wheel dollies, F frames the visible meshes for the
  stage's `upAxis`, R returns to the last framing.
- Frame capture: `PresentSession::RequestCapture` and `TakeCapture` read
  one presented frame back, counted in `PresentStatistics::readbacks`;
  `toon-viewport --screenshot <file.ppm>` and the P key write it. The
  viewport fails if a frame it did not capture was read back.
- The viewport's swapchain takes an 8-bit sRGB format where the surface
  offers one, so the scene pipelines' linear colour is encoded as it is
  written; it had been shown unencoded, too dark and too saturated, where
  `usdview`'s sRGB colour correction encodes the same colour.
  `PresentStatistics::srgb_encoded` and the `Presentation:` line state it.
- `toon-viewport --expect-draws N`, and the CTests `toon-viewport-capture`
  and, with the Hydra adapter, `toon-viewport-present-usd` on the `usdview`
  smoke stage.

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
- The scene's unit: `RenderWorld::SetMetersPerUnit` and the `hdToon` render
  setting `toon:metersPerUnit`, 1 by default, since Hydra does not carry a
  stage's `metersPerUnit`. A world-coordinates outline's width is metres,
  divided into the scene's unit through a draw constant, so a new unit
  rewrites no parameter slot. Renderer report 17.
- Documentation: the release milestones. `v0.x.0` versions replace
  Renderer Phase 0–7 as the delivery sequence; the roadmap has a page per
  coming milestone and no status marks, and the capability matrix is the
  only statement of status. The versioning and patch policy and the release
  gate are in `docs/releases/README.md`.

### Changed

- Texture upload barriers make transferred pixels visible to both the
  vertex and fragment stages; the outline vertex shader samples width
  textures, which previously had only fragment-stage synchronization.

- Right-button camera dolly uses horizontal motion: dragging right zooms
  in and dragging left zooms out, at the existing sensitivity.
- `OST_RENDERER_ADAPTERS` adds the adapters it names to those the build
  intent enables, where it used to replace them, so `ost renderer viewport`
  no longer turns an intent's Hydra adapter off (`ost` report 07, Q1).
- `ost` 0.23.14. The release workflow bootstraps it. A VRM avatar is looked
  at in the viewport with
  `ost renderer viewport --intent hydra --with <vrmImaging>`, which builds
  the two adapters in a tree of its own and adds the plugin by digest, where
  the built viewport used to be run as the VRM Formation's command; the
  `viewport-usd` intent stays for the viewport's Hydra CTest. `ost`
  report 08 re-verifies report 07 against it: 20 of 20 draws MToon.
- `toon-viewport` prints `Selected backend:`, `Device:` and `Presentation:`,
  which `ost renderer viewport` records in its launch record, and a summary
  of its last frame's draws and materials.

- Documentation: the 2026-09-28 direction. The dedicated viewport becomes
  the renderer's main evaluation host (design policy §31) and leads
  v0.2.0, now "MToon quality and the viewport foundation"; Linux and
  multi-vendor Vulkan leave v0.2.0 and the release gate for a later
  platform-coverage piece of work; the work order has thirteen steps, ending
  with platform coverage. The design policy adds scene lighting (§32) and
  the checks a new feature passes (§33), keeps MSAA as the anti-aliasing
  baseline with FXAA or TAA only after an evaluation asks for them, keeps the
  fixed texture table until it runs out, and treats Hydra AOV readback as
  acceptable for integration but not as a premise.
- Four scene pipelines where there were three. Depth writes are dynamic
  state in every MToon pipeline, and `mtoon_outline` blends, returning 1
  for an opaque material, so one hull pipeline serves both.
- `openstrata.renderer.yaml` lists `renderer.material.mtoon_rim` and
  `renderer.material.mtoon_transparent` among the assertions `ost validate`
  requires.
- The MToon pipelines' push constants carry view-from-object's top three
  rows in place of the normal matrix, which the shaders now derive, so a
  fragment knows its view-space position.
- A world-coordinates outline was drawn in stage units whatever the stage's
  unit; it is now drawn in metres.
- `mtoon_outline` rasterizes with a depth bias of one resolution step plus
  the triangle's depth slope, away from the camera, so a hull thinner than
  the depth buffer resolves loses the depth test to the surface it outlines
  instead of showing the outline colour across it. The headless check
  `renderer.material.mtoon_outline` draws such a hull. Renderer report 18.

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
