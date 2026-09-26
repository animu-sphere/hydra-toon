# Supported configurations

Configurations a build and test run has actually passed on. A configuration
that has only been reviewed is not listed. No CI runs yet, so every row is a
local run, and the report named in the row holds its detail.

## Measured

| OS | Compiler | Build | Runtime | GPU | Result | Report |
| --- | --- | --- | --- | --- | --- | --- |
| Windows 11 x86_64 | MSVC 14.51 (Visual Studio 18) | `core`, Release | OpenStrata `cy2026` `core` (no OpenUSD) | NVIDIA RTX A5000 | `ost build`, `ost test` 4/4, `ost validate` passed with `renderer.install_tree` PASS | [renderer 01](../reports/renderer/01-2026-09-26-phase0-mesh-camera.md) |
| Windows 11 x86_64 | MSVC 14.51 (Visual Studio 18) | `hydra` intent, Release | OpenStrata `cy2026` `lookdev`, OpenUSD 26.08, Python 3.13 | NVIDIA RTX A5000 | `ost build`, `ost test` 8/8 including `testusdview`, `ost validate` passed with 13 of 13 renderer assertions | [renderer 01](../reports/renderer/01-2026-09-26-phase0-mesh-camera.md) |
| Windows 11 x86_64 | MSVC 14.51 (Visual Studio 18) | standalone viewport | OpenStrata `cy2026` `core` | NVIDIA RTX A5000 | `ost renderer viewport -- --frames 8 --hidden` presented 8 frames; `ost validate --intent renderer-viewport` passed | [renderer 01](../reports/renderer/01-2026-09-26-phase0-mesh-camera.md) |

## Requirements

| Requirement | Version | Notes |
| --- | --- | --- |
| `ost` | 0.23.8 or newer | generated with 0.23.6; install smoke ported from renderer template 0.5.3, which lets `core` pass `renderer.install_tree` |
| CMake | 3.24 or newer | measured with 4.4 |
| C++ | C++20 | |
| Vulkan | 1.3 device, loader and headers | the device needs `dynamicRendering`, `synchronization2` and `timelineSemaphore`; without a suitable device the GPU checks report an explained `SKIP`, not a failure |
| `slangc` | bundled with the Vulkan SDK from 1.3.296 | found through `VULKAN_SDK`, then `PATH` |
| OpenUSD | 26.08 measured | Hydra adapter only; a real `lookdev` or `usd` runtime |
| GLFW | 3.4 | standalone viewport only; `find_package`, else a pinned FetchContent |

Linux and macOS are not measured. macOS is out of scope for the Vulkan-first
phases ([design policy §4](../design/DESIGN_POLICY.md#4-backends)).
