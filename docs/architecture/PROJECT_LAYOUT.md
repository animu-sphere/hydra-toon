# Project layout

The binding structural contract: which targets exist, where new code goes, and
which way dependencies point. Rationale is in
[design/DESIGN_POLICY.md](../design/DESIGN_POLICY.md); what each target can do
today is [reference/CAPABILITY_MATRIX.md](../reference/CAPABILITY_MATRIX.md).
A structural change updates this page first, in its own pull request.

## 1. What the project is

One CMake project, adopted by OpenStrata as a **renderer project**
(`openstrata.renderer.yaml`), not a plugin workspace. Its internal libraries are
CMake targets, not independently versioned packages. The installed `Toon`
CMake package and the `hdToon` Hydra module are the distribution boundary.

It was generated with `ost init --template renderer --name toon` (template
`renderer` 0.5.1, `ost` 0.23.6; recorded in `openstrata.scaffold.yaml`). From
that moment every file is project-owned; the template is not re-applied.

| Name | Value |
| --- | --- |
| repository | `hydra-toon` |
| OpenStrata project / renderer name | `toon` |
| CMake project and package | `Toon` (`find_package(Toon)`, `Toon::` targets) |
| C++ namespace | `Toon` |
| Public header root | `include/toon/` |
| Hydra module and renderer plugin | `hdToon` (display name `Toon`) |
| CMake option prefix | `TOON_` |

## 2. Targets today

| Directory | Target | Alias | Role |
| --- | --- | --- | --- |
| `core/render-world/` | `toon-render-world` | `Toon::RenderWorld` | host-neutral scene state |
| `core/render-extraction/` | `toon-render-extraction` | `Toon::RenderExtraction` | scene state → draw work |
| `backend/vulkan/` | `toon-render-vulkan` | `Toon::Vulkan` | Vulkan backend: persistent offscreen renderer, swapchain presentation, the shared mesh pipeline, Slang shaders |
| `adapters/headless/` | `toon-headless` | — | headless runner; writes `renderer-report.json` |
| `adapters/viewport/` | `toon-viewport`, `toon-viewport-imgui` | — | standalone GLFW window with a Dear ImGui overlay, the renderer's main evaluation host ([design policy §31](../design/DESIGN_POLICY.md#31-evaluation-hosts)); optional (`TOON_ENABLE_VIEWPORT`); with the Hydra adapter in the same build, it links `toon-hydra2-runtime` and hosts a USD stage (`--usd`) |
| `adapters/hydra2/` | `hdToon`, `toon-hydra2-runtime` | — | the `HdRenderDelegate` adapter; optional (`TOON_ENABLE_HYDRA2`) |
| `adapters/fast/` | `toon-avatar-state` | `Toon::AvatarState` | optional revision-4 runtime state consumer (`TOON_ENABLE_AVATAR_STATE`); only the runtime C headers are required, with snapshot calls supplied by the host |
| `validation/` | CTest only | — | core boundary, core unit, evidence and install-tree checks |

`formations/vrm-host-session/` builds nothing: it is an OpenStrata Formation
that composes the published `toon` package with the runtime and `vrmImaging`,
run by `ost formation`. Every other directory under `formations/` is a local
Formation and ignored by Git.

`adapters/hydra2/` is OpenStrata's name for the Hydra scene-input slot; the code
is a classic `HdRenderDelegate` ([DESIGN_POLICY.md](../design/DESIGN_POLICY.md)
§30).

## 3. Where new code goes

The design policy's components map onto this layout as follows. A row is a
placement rule; it does not say the component exists.

| Design policy component | Directory | Target |
| --- | --- | --- |
| `ToonScene`, `ToonMesh`, `ToonSkeleton`, `ToonTexture`, `ToonView`, `ToonLight` | `core/render-world/` | `toon-render-world` |
| `ToonMaterial` and model normalization (§8) | `core/render-world/` until it needs its own target, then `core/material/` | — |
| `DrawPacket`, extraction, dirty routing (§14–§15) | `core/render-extraction/` | `toon-render-extraction` |
| `RenderGraph` (§16) | `core/render-graph/` when it exists | new core target |
| `ToonRenderer` (frame loop, late latch, telemetry) | `core/` | new core target |
| Vulkan backend | `backend/vulkan/` | `toon-render-vulkan` |
| WebGPU backend | `backend/webgpu/` | new backend target |
| Slang shaders | `backend/vulkan/shaders/` while Vulkan is the only consumer; `shaders/` at the root once a second backend compiles them | — |
| Hydra prims, render pass, scene indices | `adapters/hydra2/src/` | `toon-hydra2-runtime` |
| Public evaluated avatar state → renderer-private values | `adapters/fast/` | `toon-avatar-state` |
| Viewport camera, debug controls, timing and statistics display, image capture | `adapters/viewport/src/` | `toon-viewport` |
| The viewport's Hydra host: stage, scene indices, render index | `adapters/viewport/src/hydra_scene.cpp`, built only with the Hydra adapter | `toon-viewport` |
| Public headers — core (`render_world.hpp`, `extraction.hpp`), the backend-neutral contract (`backend.hpp`, `overlay.hpp`) and each backend's factories (`vulkan_backend.hpp`, `vulkan_present.hpp`) | `include/toon/` | — |

The viewport keeps private implementation and headers in `adapters/viewport/src/`;
fixtures, test executables and CMake/Python check drivers live in `tests/`,
with their registration in `tests/CMakeLists.txt`. The adapter's root CMake
file owns dependencies, executable composition and installation. Both viewport
executables share one source list; provider-only sources and dependencies
remain opt-in. Fixture paths stay under `adapters/viewport/tests/`.

Within `src/`, `main.cpp` owns the frame loop, `arguments.*` owns CLI parsing
and validation, and `capture.*` owns PPM, camera JSON and foreground PNG export.
`capture.cpp` consumes the explicit `ColorProduct` metadata, unpremultiplies
linear RGB, encodes sRGB and preserves linear alpha for PNG. The private wrapper
compiles the unmodified encoder vendored under `third_party/stb/`; its license
and attribution install with the viewport under `share/toon/licenses/stb`.
Neither the encoder nor its consumer is linked into core or the Vulkan backend.
The format/conversion checks also build in `validation/` without the viewport,
Vulkan or OpenUSD; independent PNG decode checks use an available Python 3
interpreter and its standard library.

The evaluated-state consumer belongs in the optional `adapters/fast/` target,
not in render-world, render-extraction or a backend. It may consume the minimal
public contract from `usd-avatar-runtime`; orchestration and format providers
stay in the external host. Adapter-specific public headers must be separate
from core headers. The option is `TOON_ENABLE_AVATAR_STATE` (default `OFF`);
`TOON_AVATAR_RUNTIME_INCLUDE_DIR` supplies the public C headers.
`include/toon/fast/avatar_state.hpp` is its separate public header. It maps
explicit host bindings into a renderer snapshot and retains runtime snapshots
through the supplied C function table. No evaluator or runtime library is
linked. The optional target is installed in a separate CMake export, loaded
only by `find_package(Toon COMPONENTS AvatarState)`; this component requires
the runtime headers at consumption time. Core-only consumers require none.
The runtime's evaluation Scene Index remains upstream, consumed through the
Hydra adapter's data-source route. Both adapters normalize into renderer-private
snapshot values and use the same extraction and backend paths.

The Hydra side of binding preparation stays in `adapters/hydra2/src/`:
`HdToonRenderDelegate::DescribeResidentTargets` (`adapter.hpp`) returns the
prim paths, material paths, palette joint tokens, inverse binds, placement and
subshape identities behind the ids `CommitScene` publishes. Its plain structs
use OpenUSD path and token types only; it is not a core or backend header, and
the fast adapter does not depend on it. Each mesh reads these from the terminal
scene index during its own sync, sharing the subshape numbering that packs its
morph targets. The host's matching lives beside it in
`src/avatar_binding.*` as `toon-hydra2-avatar-binding`, built only when
`Toon::AvatarState` exists and never installed or linked into `hdToon`:
`HdToonMatchAvatarTargets` pairs runtime skeleton/joint, mesh/shape,
material/input and visibility identities with the description, takes the
canonical material input table from the host, and reports each runtime or
resident identity it cannot bind. Each shape's morph binding lists its
described subshape slots; the fast
adapter turns the shape's runtime weight into those slots' weights with
usdSkelImaging's inbetween interpolation, so neither the host nor core
evaluates inbetweens.

A host over the runtime's real providers lives beside the Hydra checks as
`adapters/hydra2/tests/avatar_provider_test.cpp`
(`toon-hydra2-avatar-provider-test`). It is built only with
`TOON_AVATAR_RUNTIME_PROVIDERS` (default `OFF`, requires
`TOON_ENABLE_AVATAR_STATE`), which finds the runtime package's
`vrm_expression_usd`, `vrm_lookat_usd`, `vrm_usd`, `motion` and `motion_usd`
components. It links them, and
through them the VRM and motion owners. It is never installed, and no
library target links it. Its CTests also need `TOON_VRM_IMAGING_RESOURCES`,
so the fixture's materials are MToon; `TOON_VRM_SCHEMA_RESOURCES` defaults to
the schema package the providers link. `run_avatar_providers.cmake` puts the
linked DLLs' directories on `PATH` and registers both plugins. The host
states the stage's `metersPerUnit` on each commit and syncs the drawn
render tags, as the viewport does. Its
oracle authors each result into the session layer for UsdImaging to compose;
that authoring belongs to the check, not to a renderer path.

`toon-viewport-avatar` is the viewport built from the same sources with
`adapters/viewport/src/avatar_runtime.*`, an external host of the same
providers. It is built only with `TOON_AVATAR_RUNTIME_PROVIDERS` and the
Hydra adapter, and is never installed; `toon-viewport` links no provider.
`avatar_runtime.hpp` carries no runtime or OpenUSD type, and the overlay
reads only its status. The host reaches the stage and delegate through
`hydra_scene_parts.hpp`, which only OpenUSD translation units include. Its
CTests use `run_avatar_providers.cmake` with `EXPECT` patterns.

The sparse morph representation lives in `include/toon/render_world.hpp`:
`ToonMorphOffset` and `ToonMorphRange` are immutable structural arrays,
while weights have an independent revision. `SetMeshMorphWeights` does not
change geometry, skin or target revisions. The Hydra adapter normalizes
usdSkelImaging's aggregator arrays once per input revision and forwards its
evaluated weights separately; it does not define expression semantics.
OpenUSD 26.08 packs position offsets only. The adapter reads normal offsets
from the terminal scene index's `skelBinding` and `skelBlendShape` containers,
matching the binding order and sorted inbetween subshape slots. Sparse,
dense and normal-only offsets share the resident target buffer. Missing
normal offsets retain authored or derived rest normals; no per-frame normal
recalculation or geometry upload occurs. An aggregator edit re-sends rest
points only if their values changed. Normal-offset edits refresh targets
independently of rest geometry and weights. The Hydra skinning CTest checks
these edits; `toon-renderer-hydra-morph-normals` compares lit surfaces and
hulls against independently CPU-deformed rest geometry.

The Vulkan mesh cache owns per-mesh target, range and weight storage buffers
alongside influences and joints in descriptor set 1 (five vertex bindings).
All scene shaders evaluate morph offsets before skinning in `skinning.slang`.
The device capability probe requires ten storage-buffer bindings including
the three models' parameter buffers, frame data and MMD shadow parameters.

`HdToonMesh` in `adapters/hydra2/src/adapter.cpp` normalizes mesh primvars
before publishing them to the shared render world. It retains source-point
geometry in a private `RenderWorld`; when `st` or normals use face-varying
or uniform interpolation, it triangulates a matching corner topology with
`HdMeshUtil`, retaining holes, orientation and subset face ordering. Only
emitted polygon corners become vertices. The same map expands points,
vertex normals, skin influences, morph ranges, MMD edge scales and
additional UVs. Constant primvars expand to the resulting vertex count;
Hydra resolves indexed primvars before the adapter reads them.

Authored tangent frames use `MeshSnapshot::tangents` (`Float4`, independent
revision) and `RenderWorld::SetMeshTangents` in the existing public core
header. The adapter reads float4 `primvars:tangents`, including Hydra-resolved
indices and vertex, varying, face-varying, uniform or constant interpolation,
through the same corner map. This adapter input uses xyz along +U and w
equal to +/-1, with `w * cross(N,T)` along +V of bottom-left `st`; an adapter
carrying a top-left source basis must convert its sign. Short or invalid
authored frames select the existing derivative fallback. No format schema
is introduced. Tangent changes invalidate both late-snapshot and optional
avatar-state structural bindings.
CPU point-computation fallbacks, including dual quaternion skinning, retain
the derivative path because they supply no matching deformed tangent frame.

Without authored tangents, the Hydra adapter's private
`src/generated_tangents.*` runs the unmodified MikkTSpace implementation
vendored under the repository's `third_party/mikktspace/`, compiled inside
`toon-hydra2-runtime`. Third-party source and its license/attribution belong
under `third_party/<dependency>/`; renderer-owned wrappers stay with their
consumer. It uses the
normalized rest triangles, unit normals and bottom-left `st`. The C source
compiles as C++; no new target, runtime dependency or public header is added.
Its pinned origin and license are recorded in that directory's README and
source notices; the license and attribution install under
`share/toon/licenses/mikktspace`. Missing, short, non-finite or entirely collapsed UV data
retains the derivative fallback.

Generated corner frames keep the existing indexed layout when possible.
A shared vertex receiving different frames is duplicated, and the same map
expands normals, UVs, skin influences, morph ranges, edge scales and extra
UVs; triangle order and material-subset ranges remain unchanged. A frame
rebuild follows rest-point, normal, UV or topology edits, never pose or
morph-weight edits. Only a changed seam map or triangle assignment rebuilds
the other streams. Authored tangents take precedence over generation.

The Vulkan mesh cache uploads tangents by their own revision. MToon's three
pipelines and PreviewSurface consume a fourth float4 vertex stream, bound
at stride zero to a zero frame when absent; MMD keeps its existing fourth
stream. Shared skinning deforms tangent directions and
preserves the sign through joint reflections; `surface_common.slang` applies
the forward model-view transform and its reflection sign. Fragment shading
orthogonalizes against the morphed/skinned normal and converts bottom-left
st handedness to each model's normal convention: top-left for MToon,
bottom-left for PreviewSurface. The shared `TangentMappedNormal` consumes
already decoded vectors; only MToon decodes its texture and normal scale.
Pose and morph weight updates retain the rest frames. Morph tangent deltas
are not represented.
`validation/tangent_test.cpp` supplies state and CPU-normal-reference GPU
checks within the existing validation layout; no installed target is added.

Smooth rest normals are derived on the source mesh before UV seams split
its vertices. `RenderWorld::SetMeshNormals` accepts an optional `authored`
flag, defaulting to true; false retains adapter-derived values while
keeping `MeshSnapshot::authored_normals` false. Empty or insufficient arrays
still use core derivation. Source revisions retain the corner map and
static arrays through pose and weight edits; UV and normal value edits
retain topology unless they change a generated tangent seam.
`toon-hydra2-primvar-test` checks this normalization
through UsdImaging and compares textured and deformed Vulkan draws to
independent explicit-vertex fixtures. No backend vertex contract changes.

PreviewSurface's bounded USD shader-network reader stays in
`adapters/hydra2/src/material.cpp`. It normalizes six `UsdUVTexture` inputs
(diffuse, emissive, metallic, roughness, opacity, normal), their channel and
scale/bias/fallback, and a primvar reader with at most one `UsdTransform2d`
into the PreviewSurface block of `ToonMaterial`. Base and emissive reuse
the common texture references; metallic, roughness and opacity add three
references to the adapter's role table. USD transforms are converted into
the existing top-left image-space texture-reference convention. The Vulkan
`PreviewParameters` slot is 640 bytes: three common float4 values, a
signed tangent-space normal float4, then six 96-byte texture-input records.
The normal image reuses the common normal texture reference; USD scale/bias
applies before mapping, without an implicit [0,1] decode. Primvar fallback
coordinates occupy the UV rows' padding. The texture cache and fixed
pipelines are shared with the existing models; no new target or material
compiler is introduced.

`scripts/evaluate_preview_surface.py`, its `testusdview` worker and
`adapters/hydra2/tests/preview-reference.usda` belong to validation, with no
dependency from renderer libraries. Generated images, logs and summaries
stay in the selected output directory. Comparison scope and stop rules
belong to [material policy §12.1](../design/MATERIAL_POLICY.md#121-reference-comparison-and-stop-rule);
dated evidence belongs to [renderer reports](../reports/renderer/).

Unreadable Hydra materials normalize to unlit magenta in that same reader.
For `ToonShadingModel::PreviewSurface`, `preview.enabled == false` means a
bound material draws its common `base_color` unlit and opaque, without
sampling images. The Vulkan material cache retains that colour and the
unlit pass pushes it per mesh part. A part without an available material uses
a temporary rough-dielectric PreviewSurface entry with `MeshSnapshot::color`,
without allocating a per-mesh material or slot. A default bound core material
is unlit white.
No new shader, pipeline, parameter layout or dependency is needed.
`toon-renderer-hydra-material-fallback-gpu` exercises mixed subset bindings
and graph repair through UsdImaging at 1x and 4x MSAA.

Offscreen and presentation statistics expose
`morph_uploads` and `morph_weight_writes`; the viewport shows both in its
upload panel. Rest/joint outline envelopes cannot certify a morphed hull,
and morphed triangles cannot certify occlusion, so both conservatively keep
the affected hulls. `validation/morph_test.cpp` compares all scene pipelines
against CPU-deformed rest geometry; `adapters/viewport/tests/check_morph.cmake`
checks a Hydra-fed captured morph sequence and its upload counters.

Evaluated expression overrides also live in `RenderWorld`, in the existing
render-world target and public header. `SetMeshMorphWeightsOverride` keeps a
transient weight array apart from the scene weights;
`SetMaterialParametersOverride` keeps a transient material value apart from
the scene material. Commit publishes the overrides with independent fast
revisions and the same static arrays. Clear restores the latest scene
values. Target edits, weight-count edits, structural material edits and
removal invalidate the affected override. Material overrides reject
`IsStructuralChange`; weight overrides require finite values and the
resident scene weight count. Identical effective values write no GPU buffer.
The Hydra delegate forwards these operations under its scene mutex using
ids from `CommitScene`; no USD authoring, scene-index read or Hydra sync is
involved in a host override. Scene evaluation can continue underneath it.
The host supplies evaluated subshape weights and normalized material values;
source expression semantics and inbetween evaluation remain outside core.
`validation/expression_test.cpp` verifies
state isolation and persistent Vulkan draws; the Hydra material and skinning
checks verify the direct host route and restoration after scene sync.

The viewport's USD playback transport lives in `adapters/viewport/src/playback.*`
within `toon-viewport`. It converts elapsed seconds using the stage's time
codes per second, and owns play/pause, range seeking, speed and loop state.
It evaluates no motion or expression semantics. `hydra_scene.cpp` observes
the terminal scene index and syncs only after time or prim changes; unchanged
frames still apply pending updates and commit the delegate's state. Camera
and lighting diagnostics remain draw-list values. The state test compiles
the transport under `validation/` without OpenUSD, Vulkan or windowing;
`check_playback.cmake` verifies the actual Hydra-fed presentation path.

The viewport's Morphs panel reads effective weights and
`MeshSnapshot::morph_weights_overridden` from the current commit. This
diagnostic flag does not advance rendering revisions. Overlay commands go
through `HydraScene::SetMorphWeightsOverride` / `ClearMorphWeightsOverride`
in `hydra_scene.cpp` and are published by this frame's late commit without
USD authoring or a sync request. No UI copy of the binding or override state
is retained, so target/count invalidation and scene replacement follow the
render world's lifetime. The panel identifies evaluated subshape slots by
mesh id and weight index; source expression names and mappings are not
defined here. `toon-viewport-morph-debug-test`, built only with the viewport
and Hydra adapter, exercises this host route with state and GPU checks.

The Materials panel reads the same effective `FrameSnapshot`, listing renderer
material ids, bound mesh ids, normalized values and
`MaterialSnapshot::parameters_overridden`. Like the morph flag, this flag is
diagnostic only: equal-value activation and release advance no rendering
revision. The panel edits MToon colours, scalars and texture transforms through
`HydraScene::SetMaterialParametersOverride` and releases through
`ClearMaterialParametersOverride`; late commit publishes these operations after
extraction. Model, alpha mode, sidedness and texture identities are displayed,
not edited. PreviewSurface values remain read-only in the panel; readable networks
draw through the basic PBR realization. UI values come from each commit, so removal,
structural invalidation and scene replacement retain no independent override
state. The host-route state/GPU tests live in `material_debug_test.cpp` within
the existing viewport target layout. Runtime frame identities and source-owner
expression/material mappings remain the optional runtime adapter's concern.

Late evaluated values reuse the existing targets. `ApplyFastSnapshot` lives
in `core/render-extraction/fast_snapshot.cpp`: it validates the entire
candidate before replacing fast draw values in existing storage. Draw
membership, material bindings, immutable structural arrays, slow revisions,
palette/weight counts, material structure and textures must match. Camera,
pose, signed weights, material parameters and lights must be finite.
Structural changes remain ordinary extraction work.

Both Vulkan sessions expose `LateFrameSource` in `backend.hpp`.
After frame/acquire waits, structural resource preparation and overlay
uploads, they call the host once, validate its complete `FrameSnapshot`,
then write fast buffers and record commands using the selected values. A
rejected structural candidate retains the extracted frame and exposes a
reason/count; a callback error fails the frame. There is no format-library
link or source expression evaluator here. The host owns evaluation,
serialization, source clock conversion and revisions. Immutable arrays
must be shared with ordinary extraction, and the callback/context must
remain valid until unregistered. An empty source disables the late read.

The viewport uses `HydraScene::ReadFast`, a delegate commit without USD time
changes, pending-update processing or Hydra sync, and supplies its current
orbit camera afterward. Debug edits made after extraction can therefore
reach the current submit. USD playback still evaluates before extraction;
this path does not create a new live MotionPose or gaze evaluator.

`FrameSnapshot::InputTimes` carries host-monotonic input timestamps without
making the render world own a producer clock. Vulkan statistics record late
read, buffer-write, submit and present-API-return endpoints, plus joint,
morph and material write CPU costs. The existing viewport telemetry keeps
1024-frame windows, variance and standard deviation, with a live Latency
panel and `--telemetry-output` JSON. Each response series records only the
first completed frame for an input update; missing timestamps are absent
samples. Per-frame JSON retains timestamps for age analysis. Present return
is not scanout; offscreen frames have no present endpoint. Source-produced
sample and display measurements require external host instrumentation.
`toon-late-frame-test` verifies atomic rejection and GPU image equivalence;
`check_latency.cmake` checks exported actual presentation endpoints.

Hydra background settings are read in `adapters/hydra2/src/adapter.cpp` at
render-pass execution and applied to the existing offscreen renderer. The
adapter retains the last accepted pair across sample-count recreation,
without editing scene or lighting state. `HdToonRenderBuffer::WriteColor`
flips/resamples linear premultiplied RGBA8 without changing its channels.
The existing render-buffer test executable also drives a real UsdImaging
scene through the Hydra render pass for 1x/4x RGBA checks; no installed
target or new backend boundary is added.

## 4. Dependency directions

```text
adapters/{headless,viewport,hydra2,fast}
        │
        ▼
backend/vulkan ──→ core/render-extraction ──→ core/render-world
```

1. `core/` depends on nothing but the C++ standard library.
2. Public core headers contain no OpenUSD, Hydra, Vulkan, WebGPU, windowing or
   DCC type. Backend headers share `include/toon/` but expose no Vulkan type
   either: the backend hands out plain C++ results, and Vulkan stays in its
   `.cpp` files. `backend.hpp` declares what every backend implements —
   `OffscreenRenderer`, `PresentSession`, `LateFrameSource`, the products,
   options and statistics — and a backend's own header includes it and adds
   only its factories and the inputs only it consumes: SPIR-V paths
   (`SceneShaders`) and `PresentSurfaceProvider` for Vulkan.
   `validation/check-core-boundary.cmake.in` enforces the boundary, WebGPU
   and Dawn includes among the forbidden ones, for the core and backend
   headers it lists; **a new public header is added to that list in the
   same change.**
3. A backend depends on `core/`, never on an adapter or on another backend.
4. OpenUSD appears only under `adapters/hydra2/` and in the viewport's optional
   `src/hydra_scene.cpp` and provider-only `src/avatar_runtime.cpp`, with their
   private `src/hydra_scene_parts.hpp` bridge. They compile only with the Hydra
   adapter; the latter also requires runtime providers. Other viewport sources
   include no OpenUSD header. GLFW, Dear ImGui and Native File Dialog Extended appear only under
   `adapters/viewport/`; the file dialog is linked only in Hydra builds, with
   its zlib licence installed under `share/toon/licenses/nativefiledialog-extended`.
   The window returns a UTF-8 path to the frame loop, which prepares a new
   Hydra scene and snapshot before replacing the active scene. Source-format
   parsing stays with OpenUSD's registered plugins. `PresentSession::ResetScene`
   clears the old mesh, material and texture caches after the frame in flight,
   keeping the swapchain, overlay and cumulative statistics. The
   backend draws the viewport's overlay from `include/toon/overlay.hpp`'s
   plain data.
   Deterministic animation evaluation sets USD time in `hydra_scene.cpp`;
   the viewport's frame loop chooses the time code per presented frame.
   Initial pan/dolly capture controls stay in the viewport host; the AA
   evaluation driver in `scripts/evaluate_antialiasing.py` generates local
   fixtures and consumes viewport captures without a renderer dependency.
   The outline comparison switch is `DrawList::outlines`, consumed during
   backend command recording without changing scene materials.
   `scripts/evaluate_outline_temporal.py` consumes viewport captures for
   spatial/temporal outline comparisons; its Pillow/NumPy dependencies
   belong to evaluation tooling, not a renderer target. Texture
   zero-width metadata stays in the backend's revision-keyed texture cache.
   `OutlineBounds` in `core/render-extraction/outline_bounds.cpp` builds
   rest-point and per-joint envelopes on points, topology or skin revisions.
   The backend's mesh cache retains them; command recording evaluates the
   current pose, camera, unit and full outline width against all six
   clip planes for opaque, Mask and Blend hulls. It transforms joint boxes,
   not the vertex array. Depth planes use `ToonView`'s -w..w range, preserved
   by the Vulkan clip conversion; depth clamp stays disabled and raster
   depth bias applies after clipping. `DrawList::outline_frustum_culling`
   supplies a frustum-only evaluation reference; surfaces retain their ordinary
   draw route. `scripts/evaluate_outline_depth.py` consumes captures from
   the viewport's committed skinned depth-plane fixture; VRM imaging stays
   an external plugin rather than a renderer dependency.
   `OutlineOcclusion` in `core/render-extraction/outline_occlusion.cpp`
   projects a bounded set of small rigid opaque triangles from the current
   draw list. The entire expanded hull must project strictly inside one
   triangle and behind its farthest depth, with arithmetic and raster
   margins. Mask, Blend, skinned and single-sided MToon blockers cannot
   certify coverage. The shared backend mesh recorder supplies the target
   extent and applies `DrawList::outline_occlusion_culling` to both opaque
   and transparent hulls. The viewport's `--outline-culling off` disables
   both decisions. `scripts/evaluate_outline_occlusion.py` evaluates the
   committed skinned reveal/return fixture through the actual viewport.
   The Hydra adapter privately links OpenUSD `glf` to recognize typed
   application helper lights. Its imported OpenGL dependency stays in the
   host adapter; the renderer backend remains Vulkan. Bounded sequence
   captures and camera JSON export stay in the viewport host.
   `scripts/evaluate_outline_sequence.py` and
   `scripts/evaluate_vrm_reproduction.py` consume those local captures;
   Pillow, NumPy and three-vrm belong only to evaluation tooling.
   Skeleton diagnostics stay in the viewport adapter: `skeleton_debug.hpp`
   carries names, parent indices and world origins, and `skeleton_debug.cpp`
   projects/clips the overlay's joints and bones using `ToonView`. Only
   `hydra_scene.cpp` links `usdSkel` and evaluates the selected USD time with
   `UsdSkelSkeletonQuery` and `UsdGeomXformCache`. USD scene-index notices
   invalidate the scene-owned diagnostic query cache. The overlay requests
   diagnostics only while its panel or screen display is active; neither the
   core nor the backend receives skeleton hierarchy or debug geometry.
5. No target links a format repository
   ([integration scope §4](../design/INTEGRATION_SCOPE_POLICY.md#4-dependency-rules)),
   except the opt-in, uninstalled real-provider hosts above. They reach
   the VRM owners only through the runtime's providers, and every build
   without `TOON_AVATAR_RUNTIME_PROVIDERS` needs no VRM install.
6. The optional evaluated-state consumer under `adapters/fast/` may depend on
   the runtime's minimal public state/ABI contract and renderer targets. That
   dependency never flows into core/backend headers or required package
   dependencies for core, backend or Hydra-only consumers. The adapter does
   not link runtime orchestration or evaluator providers and includes no
   OpenUSD types. Static resource preparation, identity mapping and generation
   checks precede late value updates; incompatible bindings return to the
   ordinary structural route. `RenderWorld` and `FrameSnapshot` remain
   renderer-private contracts, not the shared runtime output type.

## 5. Build intents and runtime profiles

| Build | How | Runtime profile | OpenUSD |
| --- | --- | --- | --- |
| default | `ost build` | `core` (from `openstrata.toml`) | none |
| Hydra adapter | `ost build --profile lookdev --intent hydra` | `lookdev` or `usd` (a real runtime) | yes |
| standalone viewport | `ost renderer viewport` | `core` | none |
| viewport hosting Hydra | `ost renderer viewport --intent viewport-usd --profile lookdev -- --usd <stage>` | `lookdev` or `usd` | yes |

The `hydra` intent (`TOON_ENABLE_HYDRA2=ON`) and the `viewport-usd` intent
(`TOON_ENABLE_HYDRA2=ON`, `TOON_ENABLE_VIEWPORT=ON`) are declared in
`openstrata.toml`. `ost renderer view` and `ost renderer viewport` request
their adapter through `OST_RENDERER_ADAPTERS` (`hydra2`, `viewport`); the
project reads that list as adapters to add to what the intent's options
enable, never as the whole set.

## 6. Install tree

```text
<prefix>/
├── bin/toon-headless, bin/shaders/*.spv
├── include/toon/
├── lib/toon-render-*.lib|.a
├── lib/cmake/Toon/            ToonConfig.cmake, ToonTargets.cmake
├── lib/usd/hdToon/            hdToon module, resources/plugInfo.json, shaders/   (Hydra build only)
└── share/toon/                openstrata.renderer.yaml, openstrata.scaffold.yaml, tests/usdview-smoke.usda
```
