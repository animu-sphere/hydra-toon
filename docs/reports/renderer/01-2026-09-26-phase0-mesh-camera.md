# Renderer Phase 0 lands: scene meshes through the Hydra camera, on persistent GPU state

- Date: 2026-09-26
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000 (Vulkan 1.3.280 driver),
  Vulkan SDK 1.3.290 headers, `slangc` 2026.8
- Tooling: `ost 0.23.8`; runtimes `cy2026-windows-x86_64-py313-core` and
  `…-lookdev` (OpenUSD 26.08)
- Occasion: the change that completes Renderer Phase 0 — mesh rendering,
  the camera, rendering at the AOV's resolution, timeline-semaphore
  synchronization and a pipeline created once

## TL;DR

**Every Renderer Phase 0 item runs. `testusdview` draws the smoke scene's
mesh through its camera at the AOV's resolution. Its frame evidence shows one
pipeline for the renderer's life, render targets reallocated only on a
resize, and a points edit that re-uploads points and nothing else. No
Vulkan validation message was reported on any path.**

## 1. What was run

```sh
ost build --jobs auto && ost test && ost validate
ost renderer viewport -- --frames 8 --hidden
ost validate --intent renderer-viewport
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra
ost validate --profile lookdev --intent hydra
ost lock && ost lock --check
```

## 2. Results

| Build | Result |
| --- | --- |
| `core` | `ost test` 4/4, including the new `toon-render-world`; `ost validate` passed, `renderer.install_tree` PASS |
| `core`, headless evidence | 1,000 frames of one draw; `renderer.frame.persistence` now also requires one pipeline, one target allocation and one topology and points upload across them, and passed |
| standalone viewport | 8 frames presented through the mesh pipeline; `ost validate --intent renderer-viewport` passed |
| `hydra` intent | `ost test` 8/8; `ost validate` passed with 13 of 13 renderer assertions |

## 3. The Hydra frame evidence

`toon-renderer-usdview-host` appends one line per Hydra frame. From the run
above (fields abridged):

```text
frame=1 scene_revision=1 width=601 height=466 pipelines=1 target_allocations=1 topology_uploads=1 point_uploads=1
frame=2 scene_revision=2 width=597 height=540 pipelines=1 target_allocations=2 topology_uploads=1 point_uploads=1
frame=5 scene_revision=2 width=597 height=540 pipelines=1 target_allocations=2 topology_uploads=1 point_uploads=1
frame=6 scene_revision=3 width=597 height=540 pipelines=1 target_allocations=2 topology_uploads=1 point_uploads=2
```

- Frame 2: the viewer resized. The projection changed (a new scene
  revision), the targets were reallocated at the new extent, and no geometry
  was uploaded.
- Frame 6: the test script edits `points`. Points were uploaded once more and
  the triangulated topology was not. `usdview_smoke_test.py` now asserts
  exactly this.

The screenshots show the triangle at the viewer's resolution, in
perspective, and narrower after the edit. Before this change it was the
template's fixed 64×64 image, upscaled.

## 4. A skinned avatar

A VRM avatar converted to USDZ was opened in `testusdview` with
`--renderer Toon`. It is not redistributable and stays outside the
repository. Its 20 meshes are all bound to a `UsdSkel` skeleton, and at first
none of them drew: Hydra hands a skinned mesh's points to the delegate as an
ext-computation output, and `GetPoints` returns nothing. After the delegate
accepted `extComputation` Sprims and ran their CPU kernels during mesh sync,
as HdEmbree does, all 20 meshes drew in the rest pose, in flat grey, with
depth. Renderer Phase 1 replaces the CPU kernels with GPU skinning.

## 5. Not checked

`ost renderer view` has still not been run. No frame time was measured;
this report makes no performance claim.
