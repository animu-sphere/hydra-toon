# Changelog

All notable changes to `hydra-toon` are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html), each `v0.x.0` a
milestone ([versioning](docs/releases/README.md#versioning)). Each released
version has a record in [docs/releases/](docs/releases/README.md).

## [Unreleased]

### Added

- The WebGPU backend's third stage (renderer report 93): the shared shadow
  views, drawn by the caster pass into a depth32float array and read by
  MToon, PreviewSurface and MMD receivers, MMD's self-shadow among them;
  and `toon-viewport --backend webgpu`, presenting through the window's
  surface with the composite, captures, foreground PNG export, the overlay
  and its shadow-view previews, sample changes and GPU pass timing. Six
  shadow scenes of one to four views agree with Vulkan within WGPU-Q1's
  limits, exactly at 1x, and the presented image of the bootstrap view is
  byte-identical to the Vulkan session's at 1x and 4x. The `webgpu` intent
  builds the viewport.
- `PresentGpuTimes::scene`, the scene pass whole, and
  `PresentStatistics::gpu_parts_timed`: the WebGPU session times whole
  passes, its scene's parts unmeasured; the viewport's telemetry adds a
  `scene` row.
- The WebGPU backend's second stage (renderer report 92): MToon's and
  MMD's opaque, transparent and outline pipelines on per-material bind
  groups, skinning, morphs and the late latch, and 4x multisampling with
  MToon Mask's coverage through the sample mask and a depth resolve. On
  sixteen canonical scenes at 1x and 4x the backends agree within
  WGPU-Q1's limits, which gain measured multisampled classes for outline
  hull edges, Mask coverage thresholds and hull depth; the RGBA output,
  morph and late-latch GPU checks pass on WebGPU.
- The WebGPU backend's first stage
  ([WebGPU policy](docs/design/WEBGPU_POLICY.md), renderer report 91):
  `Toon::WebGPU` on a pinned Dawn release, its Direct3D 12 adapter by
  default and its Vulkan adapter for diagnosis, renders the unlit mesh
  pipeline and PreviewSurface offscreen with the Vulkan renderer's products
  and statistics. `ost build --intent webgpu` (`TOON_ENABLE_WEBGPU`) builds
  it, downloading the pinned Dawn and DXC releases; `toon-headless
  --backend webgpu` runs its evidence. On nine canonical scenes over both
  background modes the two backends' images agree exactly but for one
  textured pixel at one step, and the PreviewSurface GPU checks pass on
  both. WGPU-Q1 (parity limits) and WGPU-Q2 (bindings selection) are
  settled.

### Changed

- The shared shader stage interfaces carry their locations in their
  semantic indices, and explicit derivatives go through per-backend
  accessors, so WGSL generation numbers and differentiates them as SPIR-V
  does. The Vulkan backend's output is unchanged.
- The shader contract's C++ side — draw constants, frame rows, each
  model's parameter record, shadow view records, the draw and material
  flags — and the writers that fill them move from the Vulkan backend to
  `backend/shared/` (`Toon::BackendShared`), which both backends link, so
  the WebGPU backend no longer copies them. No output changes.
- The Slang shaders move from `backend/vulkan/shaders/` to the root
  `shaders/`, grouped by role, and declare their resources through one
  bindings module per backend, sampling textures by role. Compiled
  artifacts keep their names, and the Vulkan pipelines are unchanged.
- The backend-neutral public types — `OffscreenRenderer`, `PresentSession`,
  `LateFrameSource`, `FrameLatency`, the products, options and statistics —
  move from `vulkan_backend.hpp` and `vulkan_present.hpp` to
  `include/toon/backend.hpp`, which both include, so existing consumers
  compile unchanged; `SteadyNanoseconds` is inline there. The header
  boundary check also covers the backend headers and rejects WebGPU and
  Dawn includes
  ([WebGPU policy §3](docs/design/WEBGPU_POLICY.md#3-boundary-and-targets)).

- The committed VRM host Formation pins the published `toon` 0.6.0 package,
  replacing 0.2.0. Renderer report 89 verifies release assets, the GHCR pull,
  Formation smoke and headless GPU checks, and MMD and MToon avatar captures
  with shadows on and off matching the preparation build byte for byte.

## [0.6.0] - 2026-10-21

### Added

- Shared shadows, stage 1 of the shadow policy: the core's `ShadowFit`
  chooses one directional view (the first shadowing directional light, or
  the camera key) and fits it as MMD's self-shadow map was fitted, and one
  `shadow` caster pass draws MToon, PreviewSurface, unbound, unlit and MMD
  casters, Mask cut, at their skinned or morphed positions. MToon scales a
  light's ramped shading term by its visibility, leaving the rim's light
  unshadowed as three-vrm does; PreviewSurface scales the direct response.
  `toon:shadows`/`--shadows` (on by default) and UsdLux `shadow:enable`
  control it; statistics, Hydra evidence and viewport telemetry count views,
  caster draws and the fit's CPU time
  ([renderer report 85](docs/reports/renderer/85-2026-10-21-shared-shadow-stage-1.md)).
- Shared shadows, stage 2: up to four views, directional lights'
  orthographic and spot lights' perspective (cones up to 80 degrees), drawn
  into the layers of one D32 array image; further lights are counted.
  `toon:shadowResolution`, `toon:shadowDepthBias`, `toon:shadowNormalBias`
  and `toon:shadowFilterRadius` (viewport `--shadow-*` and a Shadow views
  panel) set the resolution and the MToon and PreviewSurface receivers'
  comparison, with defaults of 2,048, 1.5, 1.0 and 1.5 texels measured for
  acne and lost thin-caster shadows. A shadow material view
  (`--material-view shadow`) shows each receiver's visibility and the
  viewport previews each view's depth; statistics report the views'
  resolution, layers and memory
  ([renderer report 86](docs/reports/renderer/86-2026-10-21-shared-shadow-stage-2.md)).
- Shared shadows, stage 3 (acceptance): `renderer.shadow.cross_family`
  checks every receiving family (MToon, MMD, PreviewSurface, unbound) under
  every casting one, static, skinned and morphed, with shared shadows and
  MMD's self-shadow on and off, and each family's self-shadow; the avatar
  provider host requires the Hydra and fast paths to draw the same shadow
  views and casters; the original `shadow-cost.usda` and
  `shadow-cost-4.usda` fixtures (`toon-viewport-shadow-cost-1`, `-4`) record
  the GPU shadow pass, GPU work, CPU fit and submit with p95/p99 and the
  views' memory. MMD's frames are unchanged and MToon's shadows-on frames
  are its new reference
  ([renderer report 87](docs/reports/renderer/87-2026-10-21-shared-shadow-acceptance.md)).

### Changed

- MMD's self-shadow map is the shared view of the first direct light, drawn
  by the shared caster pass; an MMD-only scene draws byte-for-byte as before,
  while other families' casters now shadow MMD receivers. The `mmd_shadow`
  shader and `SceneShaders::mmd_shadow_*` are now `shadow` and
  `SceneShaders::shadow_*`.
- MToon and PreviewSurface receivers move along their normal by 1.0 texel
  rather than stage 1's provisional 1.5, which keeps more of thin casters'
  shadows; `ShadowFit::Update` takes the resolution from the draw list.

## [0.5.0] - 2026-10-19

### Added

- Bounded release acceptance with two original redistributable RGBA fixtures
  spanning MToon, PreviewSurface and MMD self-shadow at 1x/4x over four
  backgrounds. Independent PNG/source-over controls and one time-boxed
  existing tangent/reference run complete the adopted validation scope;
  native usdview transparent sRGB export and external authored/deformed
  tangent equivalence remain explicit limitations
  ([renderer report 83](docs/reports/renderer/83-2026-10-19-v050-acceptance.md)).

- Controlled usdview RGBA consumer checks for linear-to-straight sRGB PNG
  conversion and four-background composition at 1x/4x, with an independent
  opaque host display comparison and diagnostics for the native transparent
  sRGB screenshot limitation
  ([renderer report 82](docs/reports/renderer/82-2026-10-19-usdview-rgba-consumer.md)).

- Viewport foreground PNG export as straight sRGB RGBA, with alpha preserved,
  explicit colour metadata and independent decode/1x/4x CLI checks. Viewport
  sources and test drivers now live in separate directories; argument parsing
  and capture/export are split from the frame loop
  ([renderer report 81](docs/reports/renderer/81-2026-10-19-foreground-png.md)).

- Viewport consumption of an independent premultiplied linear RGBA target,
  GPU solid/checkerboard compositing, background UI/CLI controls and explicit
  foreground readback; controlled 1x/4x source-over, offscreen equality,
  hull, coverage, resize and isolation checks
  ([renderer report 80](docs/reports/renderer/80-2026-10-18-viewport-compositing.md)).

- Hydra opaque/transparent background settings and premultiplied linear
  RGBA colour AOV preservation, with 1x/4x render-pass/readback checks for
  Blend, silhouettes, settings rejection and source-over recomposition
  ([renderer report 79](docs/reports/renderer/79-2026-10-18-hydra-rgba.md)).

- Offscreen opaque/transparent background settings, explicit linear-transfer
  and premultiplied-alpha readback metadata, and MToon Mask sample-alpha
  correction. Controlled 1x/4x RGBA checks cover three material families,
  Blend layering, silhouettes, Mask and hulls
  ([renderer report 78](docs/reports/renderer/78-2026-10-18-offscreen-rgba.md)).

- Lightweight documentation link, anchor, owner and index validation through
  `scripts/check_docs.py` and a dedicated docs CI workflow.

- Lit display-colour fallback for meshes and subsets without an available
  material: an opaque, double-sided rough dielectric using the existing
  PreviewSurface pipeline, scene lights and camera fallback. Per-draw colours
  require no material slots; unreadable bound materials retain unlit magenta
  ([renderer report 77](docs/reports/renderer/77-2026-10-18-display-color-lighting.md)).

- Extended PreviewSurface Storm comparisons: curved and 70-degree grazing
  surfaces, packed scalar textures, Mask and Blend over a matching opaque
  clear, saved measurement masks and independent channel/compositing
  controls at 1x/4x MSAA
  ([renderer report 76](docs/reports/renderer/76-2026-10-18-preview-surface-extended-reference.md)).

- Controlled PreviewSurface comparisons through OpenUSD Storm and hdToon,
  with a redistributable mirrored-UV fixture, generated colour/normal maps,
  1x/4x captures, numerical summaries and sensitivity checks. Intermediate
  metalness and transformed-normal reference differences are recorded
  separately from matching cases
  ([renderer report 75](docs/reports/renderer/75-2026-10-18-preview-surface-reference.md)).

- MikkTSpace rest-tangent generation for USD meshes without authored
  tangents. Mirrored frame seams duplicate only conflicting vertices and
  preserve skin, morph and auxiliary data. UV/normal edits refresh frames;
  pose and morph weights retain them. MToon and PreviewSurface have
  independent CPU-normal image comparisons at 1x/4x MSAA
  ([renderer report 74](docs/reports/renderer/74-2026-10-18-generated-tangents.md)).

- PreviewSurface normal constants and RGB normal maps through the shared
  tangent frame, including explicit USD texture scale/bias, transformed UVs
  and missing-image fallback. Deformed authored frames, mirrored/rotated
  derivative frames and Opaque/Mask/Blend have CPU-normal comparisons at
  1x/4x MSAA; MToon keeps its own normal decoding and handedness
  ([renderer report 73](docs/reports/renderer/73-2026-10-18-preview-surface-normals.md)).

- Authored tangent frames from Hydra float4 `primvars:tangents`, preserving
  indexed seams and handedness into MToon's surface, transparent and outline
  pipelines. Independent tangent revisions retain geometry through motion;
  skin/model reflections and morph normal offsets have 1x/4x Vulkan checks
  against independently evaluated CPU normals. Generated MikkTSpace frames
  and external reference comparisons remain separate work
  ([renderer report 72](docs/reports/renderer/72-2026-10-18-authored-tangents.md)).

- Explicit unsupported-material fallback: unreadable bound Hydra materials
  draw opaque unlit magenta through the existing mesh pipeline, independently
  per subset. Unbound faces retain mesh display colour. Repairing a graph
  restores its surface and images without geometry uploads or pipeline
  creation; mixed-subset Vulkan checks cover 1x/4x MSAA
  ([renderer report 71](docs/reports/renderer/71-2026-10-18-material-fallback.md)).

- Generic USD mesh primvars: face-varying, uniform and constant `st` and
  normals, including indexed data resolved by Hydra. Corner vertices retain
  skin influences, morph ranges, MMD edge scales and additional UVs; smooth
  normals are computed before splitting UV seams. UV/normal value edits
  retain topology, and pose/weight edits retain geometry
  ([renderer report 70](docs/reports/renderer/70-2026-10-18-mesh-corner-primvars.md)).

- PreviewSurface image inputs for diffuse colour, emission, metallic,
  roughness and opacity: `UsdUVTexture` channel selection, scale/bias,
  fallback, colour space, wrapping, `st` and one `UsdTransform2d`. UV and
  value edits update the resident material slot; textured Mask/Blend and
  linear/sRGB sampling have 1x/4x Vulkan checks
  ([renderer report 69](docs/reports/renderer/69-2026-10-18-preview-surface-textures.md)).

- Basic constant-input metallic/roughness `UsdPreviewSurface` rendering:
  universal surface-terminal traversal, time-sampled diffuse colour,
  metallic, roughness, emission and opacity; GGX direct lighting, diffuse
  ambient, Opaque, hard-cutoff Mask and Blend. MToon and MMD keep selection
  priority. Texture
  connections and normal mapping remain outside this increment
  ([renderer report 68](docs/reports/renderer/68-2026-10-17-preview-surface-constants.md)).

### Changed

- Documentation ownership and the v0.5.0 release scope follow the 2026-10-18
  [maintenance rules](docs/contributing/documentation.md) and
  [bounded PreviewSurface comparison policy](docs/design/MATERIAL_POLICY.md#121-reference-comparison-and-stop-rule).

- A bound core material with `preview.enabled == false` draws its own
  `base_color` unlit and opaque; a default material is white. Mesh display
  colour applies only when the draw has no available bound material.

- Left-handed mesh winding uses `HdMeshUtil`'s conversion once, preserving
  the front face instead of reversing it again in the adapter.

- glTF PBR and mixed MToon/PBR avatar reproduction move to the later
  MaterialX work. v0.5.0 retains the ordinary USD PreviewSurface fallback;
  Seed-san's PBR cases are outside its release condition.

- Design and roadmap adopt the post-v0.4.0 direction: USD interoperability,
  tangent fidelity and reusable RGBA output in v0.5.0, semantic parity on
  WebGPU in v0.6.0, then host / compositor integration. A dedicated
  [render output policy](docs/design/RENDER_OUTPUT_POLICY.md) defines intended
  background, premultiplied alpha, coverage and GPU handoff contracts.

- Design and roadmap adopt the 2026-10-18 direction: v0.6.0 is shared
  shadow and lighting across MToon, MMD and PreviewSurface on Vulkan,
  WebGPU moves to v0.7.0 and host / compositor integration to v0.8.0 scale.
  [Design policy §35](docs/design/DESIGN_POLICY.md#35-shared-scene-services)
  defines the shared scene services and
  [material policy §14](docs/design/MATERIAL_POLICY.md#14-shadows-across-material-families)
  each model's shadow receive; the release gate states its evidence tiers.

### Fixed

- Hydra primvar GPU validation probes actual renderer creation before running
  its image oracles. A usable Vulkan loader without an instance/device now
  reports SKIP (77), matching the other GPU checks; shader initialization
  errors still fail. This fixes GPU-less release CI without changing renderer
  behavior ([renderer report 84](docs/reports/renderer/84-2026-10-19-ci-primvar-device-skip.md)).

- PreviewSurface metallic Fresnel keeps albedo at grazing incidence,
  interpolating F90 from white to albedo with metalness; an analytic
  1x/4x GPU regression rejects the previous white-metal endpoint
  ([renderer report 75](docs/reports/renderer/75-2026-10-18-preview-surface-reference.md)).

## [0.4.0] - 2026-10-17

### Changed

- The project's version lives in `VERSION`, one line `X.Y.Z`, which
  `CMakeLists.txt` reads as `project(Toon VERSION)` and reconfigures on
  when it changes; `scripts/release_version.py` requires
  `openstrata.toml`'s `[project].version` to equal it.

- MMD's self-shadow map follows the camera, as MMD's own does: across the
  light it spans what the camera sees of the receivers, their joints'
  posed boxes cut to the frustum (`OutlineBounds::ScenePieces`), in steps
  that hold the edges still as the camera moves. A receiver reads a disc
  six texels in radius, 32 bilinear taps on its own plane, after moving
  along its normal and allowing for its slope, so its edges no longer
  step or streak close up and are about as soft as MMD's.
  `MmdShadowParameters` is 96 bytes. The present session's GPU
  times and the viewport's telemetry have a `shadow` part
  (`PresentGpuTimes::shadow`) for the map's pass, which `uploads` no
  longer includes
  ([renderer report 63](docs/reports/renderer/63-2026-10-17-self-shadow-quality.md)).
- A point falls into MMD's self-shadow from 0.12 MMD units behind what the
  map saw first and wholly by 0.52, a sharpness of 2.5 per MMD unit where
  it was 15, fitted to MMD's frames of contact and grazing shadows at its
  default self-shadow distance; layers lying close over others are no
  longer shadowed where MMD leaves them lit. MAT-Q5 is resolved
  ([renderer report 66](docs/reports/renderer/66-2026-10-17-mat-q5-shadow-depth.md)).

- MMD material morphs' texture, sphere and toon tints no longer draw by
  default: MMD v9.32's frames of two models' tinted morphs show MMD
  draws none (MAT-Q6, resolved). `DrawList::mmd_morph_tints`, the render
  setting `toon:mmdMorphTints` or the viewport's `--mmd-morph-tints` and
  its "MMD morph tints" checkbox draw them in `full.fx`'s form, a frame
  value that rewrites no slot. `renderer.material.mmd_morph` covers both
  ([renderer report 62](docs/reports/renderer/62-2026-10-17-mat-q6-morph-tints.md)).
- An MMD material that receives the self-shadow is no longer toned by its
  ramp at `v = 0.5 − 0.5 N·L` while the self-shadow is on, which it is by
  default, but shaded as `full.fx`'s self-shadow pass shades it.
- The material descriptor set has two more bindings, MMD's self-shadow map
  and its parameters, and a device needs D32_SFLOAT to be drawable and
  sampled.
- Setting the UVs a mesh already has changes nothing, so an edit of another
  primvar, which Hydra reports through the same dirty bit, no longer gives
  them a new revision.
- MMD's textures are read undecoded, as MMD shades them.
- What every surface pipeline shares moved from `mtoon_common.slang` into
  `surface_common.slang`.
- Draw counts in `TOON_HYDRA_EVIDENCE` and the viewport's scene summary
  count parts: a mesh divided by its face subsets is one draw per part.

### Added

- MMD material morphs' texture, sphere and toon tints draw: the MMD
  block of `ToonMaterial` carries each as the evaluated multiply and add
  (`texture_tint`, `sphere_tint`, `toon_tint`, with `Toon::Float4`), and
  `mmd_opaque` and `mmd_transparent` tint the base, sphere and toon texels
  in MikuMikuEffect's `full.fx` form (MAT-Q6). An evaluated
  override sets them, a tint edit rewrites one slot, and `MmdParameters` is
  240 bytes. The viewport's Materials panel edits an MMD material's
  specular, ambient, edge alpha and tints. The headless check
  `renderer.material.mmd_morph` covers it
  ([renderer report 61](docs/reports/renderer/61-2026-10-12-mmd-material-morph.md)).
- MMD's self-shadow draws: `mmd_shadow`, the eighth scene pipeline, draws
  a depth map of every MMD draw whose material casts it from the first
  directional light, before the scene, fitted along the light to the MMD
  draws that cast or receive; a material that receives it is shaded as
  `full.fx`'s self-shadow pass shades it. On unless
  `DrawList::mmd_self_shadow`, the render setting `toon:mmdSelfShadow` or
  the viewport's `--mmd-self-shadow` turns it off; the comparison's depth
  is provisional (MAT-Q5, open). `OutlineBounds::SceneBounds`,
  `OffscreenStatistics::shadow_draws`, the headless check
  `renderer.material.mmd_self_shadow`, and `draws_mmd_shadow` in
  `TOON_HYDRA_EVIDENCE`
  ([renderer report 60](docs/reports/renderer/60-2026-10-12-mmd-self-shadow.md)).
- MMD sub-texture spheres draw: a material whose sphere mode is
  `subTexture` multiplies by its sphere texture sampled at the mesh's
  additional UV 1, as MikuMikuEffect's `full.fx` does. The hydra adapter
  reads `primvars:mmd:uv1`'s x and y; `RenderWorld::SetMeshSubTextureUVs`
  and `MeshSnapshot::sub_texture_uvs` carry them to `mmd_opaque` and
  `mmd_transparent` as a fourth vertex stream. The headless check
  `renderer.material.mmd` covers it
  ([renderer report 59](docs/reports/renderer/59-2026-10-12-mmd-sub-texture.md)).
- MMD edges draw: `mmd_outline`, the seventh scene pipeline, adds an
  inverted hull for every MMD draw whose material draws its edge, after
  every surface, `edgeSize` × the mesh's `primvars:mmd:edgeScale` wide, in
  the edge colour, unlit and decoded once. Its width is a share of the
  target's height that falls with the vertex's depth (MAT-Q4).
  `RenderWorld::SetMeshEdgeScales` and
  `MeshSnapshot::edge_scales` carry the edge scale; `MmdParameters` is 144
  bytes with the edge colour and size. The headless check
  `renderer.material.mmd_outline`, and `draws_mmd_outline` in
  `TOON_HYDRA_EVIDENCE` and the viewport
  ([renderer report 57](docs/reports/renderer/57-2026-10-11-mmd-outline.md)).
- MMD materials draw: `mmd_opaque` for a material that cannot blend and
  `mmd_transparent`, after the opaque draws in MMD's material order and
  writing depth, for the rest. MMD's shading is computed on stored values
  and decoded to linear once (MAT-Q3), under the scene's first direct
  light. MMD's slot is an `MmdParameters` in its own buffer; the scene
  pipelines are six. The headless check `renderer.material.mmd`, and
  `draws_mmd` in `TOON_HYDRA_EVIDENCE` and the viewport
  ([renderer report 56](docs/reports/renderer/56-2026-10-11-mmd-surface.md)).
- MAT-Q2 resolved: `Toon::SharedToonRamp` generates the renderer's own ten
  shared toon ramps from the colours MMD's `toon01`–`toon10` show, resident
  in texture-table entries 1–10; no MMD image is shipped.

- MMD materials are selected and read: a material prim with `mmdImaging`'s
  `mmd/material` and no `vrm/mtoon` is MMD, its fields and `mmd/drawOrder`
  normalized into `ToonMaterial`'s common part and a new MMD block, with the
  sphere and individual toon textures as two more texture roles. A dirtied
  `mmd` locator is a value-only change. `TOON_HYDRA_EVIDENCE` and the
  viewport count `materials_mmd`.
- `GeomSubset` material bindings: a mesh draws once per face subset that
  binds a material, with that material, and once for the faces none binds,
  with its own. `RenderWorld::SetMeshSubsets` and `MeshSnapshot::parts`
  carry the division in the core; the Vulkan backend issues one indexed
  draw per part over the mesh's buffers. CTests `toon-render-world`,
  `toon-subset-gpu` and `toon-renderer-hydra-geom-subsets`;
  `toon-hydra2-material-test --stage` prints each mesh's parts.
- Renderer report 55 resolves MAT-Q3 against captures from MMD itself: MMD
  shades its colours as stored, so the MMD path will compute on stored
  values and convert its result to linear once
  ([material policy §9](docs/design/MATERIAL_POLICY.md#9-open-questions)).
- Renderer report 58 resolves MAT-Q4 against five more captures from MMD:
  an edge of `edgeSize` × `edgeScale` is that size times (0.00108 + 0.0203 /
  the vertex's view depth in MMD units) of the target's height, so a field
  of view or a target size leaves its share alone, as MMD's does. The
  headless check `renderer.material.mmd_outline` checks it under a
  perspective camera
  ([renderer report 58](docs/reports/renderer/58-2026-10-12-mat-q4-edge-width.md)).
- Renderer report 65 checks MMD's vertex morphs against MMD's own frames:
  eight morphs on two models, weighed through the stage's blend shapes,
  change hdToon's frame as they change MMD's, the translucent blush
  excepted, which blends in the linear target where MMD blends stored
  values.

### Fixed

- An MMD material drawn on an earlier material's vertices, as a blush, an
  ambient-occlusion overlay or an expression layer is, now draws over it:
  `mmd_opaque` and `mmd_transparent` pass a fragment at the depth already
  written, as Direct3D 9's LESSEQUAL test, which MMD draws with, passes
  it. Before, such a layer lost the tie and never showed.
- An MMD material whose sphere mode adds or multiplies but which names no
  sphere texture no longer adds white: as in MMD, a sphere mode without a
  texture changes nothing. `renderer.material.mmd` covers both
  ([renderer report 64](docs/reports/renderer/64-2026-10-17-mmd-material-morph-evidence.md)).

## [0.3.0] - 2026-10-11

### Added

- The provider host check compares both paths' draw packets, in order, and
  their pipeline selection for every result, then replaces the scene: the
  old binding must refuse it until it is matched and bound in a new epoch.
- Viewport latency series for every input at every endpoint:
  `pose_latch`, `expression_latch`, `expression_buffer`,
  `expression_present_api`, `look_at_latch`, `look_at_buffer` and
  `look_at_present_api` join the existing ones, live and in
  `--telemetry-output`.
- The runtime's motion clip provider in both provider hosts:
  `toon-hydra2-avatar-provider-test --motion <stage>` and
  `toon-viewport-avatar --avatar-motion <stage>` read a stage's one-skeleton
  clip with the motion owner's reader and retarget it onto the avatar's VRM
  humanoid ahead of the VRM provider. `avatar-motion.usda` is a committed
  clip; CTests `toon-renderer-hydra-avatar-providers-motion{,-gpu}` and
  `toon-viewport-avatar-runtime-motion` compare its results with UsdImaging
  and apply them late in the viewport.
- `toon-viewport-avatar`, built only with the opt-in
  `TOON_AVATAR_RUNTIME_PROVIDERS` and never installed: the viewport as a host
  of `usd-avatar-runtime`'s real VRM expression/LookAt providers
  (`--avatar-runtime`, `--avatar-lookat`, `--avatar-rate`,
  `--avatar-reset-every`). Generated expression and gaze input, evaluated on
  a producer thread or once per frame after extraction, is matched to the
  scene's resident targets and applied over the extracted scene and again at
  the late read, with input times from the result's retained source samples.
  An `Avatar runtime` panel shows the applied result's identity, matching,
  owner diagnostics, source channels and bound outputs. CTests
  `toon-viewport-avatar-runtime-{late,thread}` cover late arrival, resets and
  scene replacement without static uploads.
- `emissiveFactor` in the avatar fast path: `Toon::AvatarMaterialBinding` has
  a finite `scale` that multiplies what it writes, each
  `HdToonResidentMaterial` describes its `emissive_strength`, and
  `HdToonMatchAvatarTargets` maps `inputs:vrm:material:emissiveFactor` to
  `Emissive` scaled by it, as the delegate scales the factor. The provider
  fixture binds an `emissionColor` at strength 2.
- `toon-hydra2-avatar-provider-test`, built only with the opt-in
  `TOON_AVATAR_RUNTIME_PROVIDERS`: a host over `usd-avatar-runtime`'s real VRM
  expression/LookAt providers that matches each published result through
  `HdToonMatchAvatarTargets`, applies it with `Toon::AvatarState`, and compares
  it with the same values composed by UsdImaging and usdSkelImaging: palettes,
  subshape weights, MToon materials and, with `--shaders`, colour and depth.
  `avatar-provider.usda` covers bone and expression gaze, an inbetween and
  material colour binds.
- `HdToonMatchAvatarTargets` (`toon-hydra2-avatar-binding`, built only with
  the optional runtime consumer): matches a runtime layout's skeleton/joint,
  mesh/shape, material/input and visibility identities to the delegate's
  resident description and returns `Toon::AvatarBindings` with per-identity
  mismatch diagnostics. The skinning check's bindings come from it; with
  `--stage`, a real avatar's UsdSkel-derived layout binds completely and
  composes usdSkelImaging's palettes.
- `Toon::AvatarState` inbetween subshapes: a morph binding may list a shape's
  inbetween slots and weight positions, and the adapter turns the runtime's
  one weight per shape into subshape weights as usdSkelImaging computes them.
  Bindings made from `DescribeResidentTargets` match the Hydra path's weights
  exactly on the skinning fixture; positions usdSkelImaging would drop and
  overlapping slots are refused. Existing primary-only bindings are unchanged.
- `HdToonRenderDelegate::DescribeResidentTargets`: the Hydra identities behind
  resident meshes and materials, GPU skin palettes (joint tokens, skeleton
  indices, inverse binds, placement) and subshape slots, for a host binding
  evaluated avatar targets. The skinning check covers a reordered joint list
  and a bound material; `--stage` checks a real avatar's palette composition.
- Optional `Toon::AvatarState` adapter for `usd-avatar-runtime` experimental
  C ABI revision 4: retained results, explicit pose/subshape/MToon/visibility
  mappings, layout and host binding checks, and release to the current scene.
  Controlled Vulkan late-input comparisons and real-runtime reset/destruction
  lifetime checks; independent core and installed component consumers.
- Viewport Materials panel: effective normalized values, bound mesh ids and
  override activity; MToon colour/scalar/texture-transform edits with per-material
  and release-all controls through the late value route. Equal-value overrides
  retain diagnostic activity without GPU writes. State/GPU checks cover late
  application, removal and replacement without additional static uploads.
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

- While `toon-viewport-avatar`'s runtime drives the scene, Morphs and
  Materials panel overrides are ignored with a note, since the runtime binds
  over the unoverridden scene.
- `Toon::AvatarState` follows the runtime's experimental C ABI revision 4 and
  is rebuilt against its headers. Base texture offset and scale bindings take
  the runtime's vec2 material values; the former vec3-with-zero-z convention
  is refused, as is a nonzero unused component.
- Architecture policy: Hydra and an optional evaluated-state fast adapter feed
  one renderer core. Avatar composition and shared output belong to
  `usd-avatar-runtime`; format evaluators remain upstream. The v0.3.0 gate
  requires runtime integration, Hydra/direct parity and producer-to-submit/
  present-return evidence, with actual display instrumentation tracked later.
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

- The provider host check syncs only the `geometry` and `proxy` render tags,
  as the viewport does, so usdSkelImaging's skeleton guide mesh no longer
  enters its image comparison.
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
