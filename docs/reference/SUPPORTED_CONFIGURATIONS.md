# Supported configurations

Configurations a build and test run has actually passed on. A configuration
that has only been reviewed is not listed. A local run names the report that
holds its detail; the one CI row is the
[release workflow](../releases/README.md#how-a-release-is-cut)'s build, which
runs on a tag or a manual dry run, never on a pull request.

## Measured

| OS | Compiler | Build | Runtime | GPU | Result | Report |
| --- | --- | --- | --- | --- | --- | --- |
| Windows 11 x86_64 | MSVC 14.51 (Visual Studio 18) | v0.2.0 preparation: plain CMake and OpenStrata `core`, `hydra`, `viewport-usd`, Release | canonical CY2026 OpenUSD 26.08 `lookdev`; `core` has no OpenUSD; `ost` 0.23.14 | NVIDIA RTX A5000 | plain CTest 6/6; OST core 6/6, hydra 13/13, viewport 34/34; all three `ost validate` runs passed; reproducible local package; matched VRM 0.x/1.0 rest poses and every-frame motion repeats | [renderer 32](../reports/renderer/32-2026-10-04-vrm-reproduction.md) |
| Windows 11 x86_64 | MSVC 14.51 (Visual Studio 18) | `core`, Release | OpenStrata `cy2026` `core` (no OpenUSD) | NVIDIA RTX A5000 | `ost build`, `ost test` 6/6, `ost validate` passed with `renderer.install_tree` PASS and animated outline-occlusion GPU evidence | [renderer 30](../reports/renderer/30-2026-10-04-outline-occlusion.md) |
| Windows 11 x86_64 | MSVC 14.51 (Visual Studio 18) | `hydra` intent, Release | OpenStrata `cy2026` `lookdev`, the canonical `26.08-gl-windows-x86_64` leaf: OpenUSD 26.08, oneTBB 2022.1.0, Python 3.13 | NVIDIA RTX A5000 | `ost build`, `ost test` 9/9 including `testusdview`, `ost validate` passed with 13 of 13 renderer assertions; packaged and run in a Formation with `vrmImaging` | [renderer 06](../reports/renderer/06-2026-09-27-vrm-formation.md) |
| Windows Server 2022 x86_64, GitHub-hosted `windows-2022` | MSVC 14.44 (Visual Studio 2022) | `hydra` intent, Release | the same canonical `lookdev` leaf; Vulkan SDK 1.4.350.0 | none: `vkCreateInstance` fails, and OpenGL is below the 4.5 `usdview` needs | `ost build`; `ost test` 9 passed and `toon-renderer-usdview-host` `SKIP`; `ost validate` passed with every GPU and `usdview` host assertion `SKIP`; packaged twice to one archive digest | release workflow dry run [36332782445](https://github.com/animu-sphere/hydra-toon/actions/runs/36332782445) |
| Windows 11 x86_64 | MSVC 14.44 (the release workflow's build) | the published `toon` 0.1.0 package, in `formations/vrm-host-session/` with `vrmImaging` 0.10.0 | the same canonical `lookdev` leaf | NVIDIA RTX A5000 | `ost formation doctor` passed; `ost formation run` passed on the probe stage; the avatar drew 20 of 20 draws MToon and skinned, 15 outlined | [renderer 13](../reports/renderer/13-2026-09-28-host-session-formation.md) |
| Windows 11 x86_64 | MSVC 14.51 (Visual Studio 18) | standalone viewport | OpenStrata `cy2026` `core` | NVIDIA RTX A5000 | `ost renderer viewport -- --frames 8 --hidden` presented 8 frames; `ost validate --intent renderer-viewport` passed | [renderer 01](../reports/renderer/01-2026-09-26-phase0-mesh-camera.md) |
| Windows 11 x86_64 | MSVC 14.51 (Visual Studio 18) | the Hydra adapter and the viewport: `ost renderer viewport --intent hydra` with `vrmImaging` 0.10.0 by `--with`, and the `viewport-usd` intent | the same canonical `lookdev` leaf; `ost` 0.23.14 | NVIDIA RTX A5000 | the avatar drew 20 of 20 draws MToon; `ost validate --intent hydra--renderer-viewport` passed; `viewport-usd`: `ost test` 32/32 and `ost validate` passed; controlled occlusion and representative avatar culling captures identical | [renderer 30](../reports/renderer/30-2026-10-04-outline-occlusion.md); workflow composition in [`ost` 08](../reports/ost/08-2026-09-30-v0.23.14-report-07-reverified.md) |

## Requirements

| Requirement | Version | Notes |
| --- | --- | --- |
| `ost` | 0.23.8 or newer; 0.23.11 or newer to package the `hydra` intent and compose it in a Formation; 0.23.14 or newer for `ost renderer viewport --intent hydra --with <plugin>`; the release workflow pins 0.23.14 | generated with 0.23.6; install smoke ported from renderer template 0.5.3, which lets `core` pass `renderer.install_tree` |
| CMake | 3.24 or newer | measured with 4.4 |
| C++ | C++20 | |
| Vulkan | 1.3 device, loader and headers | the device needs `dynamicRendering`, `synchronization2` and `timelineSemaphore`; without a suitable device the GPU checks report an explained `SKIP`, not a failure |
| `slangc` | bundled with the Vulkan SDK from 1.3.296 | found through `VULKAN_SDK`, then `PATH` |
| OpenUSD | 26.08 measured | Hydra adapter only; a real `lookdev` or `usd` runtime |
| GLFW | 3.4 | standalone viewport only; `find_package`, else a pinned FetchContent |
| Dear ImGui | 1.92.8 at `8936b58fe26e8c3da834b8f60b06511d537b4c63` | standalone viewport only, compiled into it by a pinned FetchContent, as `hydra-merlin`'s viewport pins it; MIT, its licence installed under `share/toon/licenses/imgui` |

Linux and macOS are not measured. macOS has no backend: there is no Metal
backend ([design policy §4](../design/DESIGN_POLICY.md#4-backends)).
