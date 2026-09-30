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
| Viewport camera, debug controls, timing and statistics display, image capture | `adapters/viewport/` | `toon-viewport` |
| The viewport's Hydra host: stage, scene indices, render index | `adapters/viewport/hydra_scene.cpp`, built only with the Hydra adapter | `toon-viewport` |
| Public headers — core (`render_world.hpp`, `extraction.hpp`) and backend (`vulkan_backend.hpp`, `vulkan_present.hpp`) | `include/toon/` | — |

## 4. Dependency directions

```text
adapters/{headless,viewport,hydra2}
        │
        ▼
backend/vulkan ──→ core/render-extraction ──→ core/render-world
```

1. `core/` depends on nothing but the C++ standard library.
2. Public core headers contain no OpenUSD, Hydra, Vulkan, WebGPU, windowing or
   DCC type. Backend headers share `include/toon/` but expose no Vulkan type
   either: the backend hands out plain C++ results, and Vulkan stays in its
   `.cpp` files. `validation/check-core-boundary.cmake.in` enforces it for the
   headers it lists; **a new public core header is added to that list in the
   same change.**
3. A backend depends on `core/`, never on an adapter or on another backend.
4. OpenUSD appears only under `adapters/hydra2/` and in
   `adapters/viewport/hydra_scene.cpp`, which is compiled only when the
   Hydra adapter is built too; no other viewport source includes an OpenUSD
   header. GLFW, Dear ImGui and Native File Dialog Extended appear only under
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
   The outline comparison switch is `DrawList::outlines`, consumed during
   backend command recording without changing scene materials. Texture
   zero-width metadata stays in the backend's revision-keyed texture cache.
5. No target links a format repository
   ([integration scope §4](../design/INTEGRATION_SCOPE_POLICY.md#4-dependency-rules)).

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
