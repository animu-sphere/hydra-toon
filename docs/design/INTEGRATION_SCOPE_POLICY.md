---
status: accepted
owner: hydra-toon
---

# Integration scope policy

> Status: **accepted**, 2026-09-26. This document says how far `hydra-toon`
> goes, what it consumes from which sibling repository, and what it does not
> own. On structure, [PROJECT_LAYOUT.md](../architecture/PROJECT_LAYOUT.md)
> wins; on materials, [MATERIAL_POLICY.md](MATERIAL_POLICY.md) does.

## 1. The rule

> **The contract between `hydra-toon` and a format repository is the composed
> USD stage and its schemas, and nothing else.**

`hydra-toon` never parses `.vrm`, `.vrma`, `.pmx` or `.vmd`, never links a
format repository's file-format plugin or model library, and never reads a
realization graph (`/preview`, `/mtlx`) as the source of a toon look. It reads
the canonical semantics those repositories author, through the names their
schemas define.

This is the same boundary the format repositories state from their side:
[`usd-vrm-plugins` material policy §5.3.1](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/MATERIAL_ARCHITECTURE_POLICY.md#531-the-boundary-with-hydra-toon)
and [`usd-mmd-plugins` material policy §12](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/MATERIAL_POLICY.md#12-rendering-and-integration-belong-elsewhere).

## 2. What this repository owns

- The renderer: the host-neutral core, the Vulkan backend, and later the
  WebGPU backend ([DESIGN_POLICY.md](DESIGN_POLICY.md) §4, §7).
- The Hydra adapter (`hdToon`) and any scene index that normalizes input for
  it ([DESIGN_POLICY.md](DESIGN_POLICY.md) §5).
- The full MToon realization: toon lighting, shade, shading shift and toony,
  GI equalization, rim, MatCap, outline, UV animation, MToon transparency and
  render ordering.
- An MMD toon realization: diffuse / alpha, toon ramp, sphere multiply / add /
  sub-texture, edge, shadow flags, MMD draw order.
- A `UsdPreviewSurface` realization for materials with no toon semantics.
- The renderer-private `ToonMaterial` and everything below it: pipelines,
  parameter buffers, Slang shaders ([MATERIAL_POLICY.md](MATERIAL_POLICY.md)).
- The low-latency path: late motion latching, the skeleton / morph fast path,
  and renderer telemetry ([DESIGN_POLICY.md](DESIGN_POLICY.md) §11–§12, §24).

## 3. What it consumes, and from whom

Each subject is linked to its owner and never restated here
([contributing/documentation.md](../contributing/documentation.md)).

| Subject | Owner | Canonical document |
| --- | --- | --- |
| `VrmMaterialAPI`, `VrmMToonAPI`, `VrmTextureInfoAPI` — VRM / MToon material semantics as `inputs:vrm:*` Material interface inputs | `usd-vrm-plugins` | [material policy §6](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/MATERIAL_ARCHITECTURE_POLICY.md#6-canonical-vrm-material-semantics) · [schema contract](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) |
| `vrmImaging` — the Hydra view of those semantics: the `vrm/<group>/<field>` data sources on the Hydra material prim, and what dirties them | `usd-vrm-plugins` | [imaging policy §28](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/VRM_IMAGING_POLICY.md#28-frozen-in-step-i1) |
| The VRM stage: humanoid, expressions, look-at, the `/Asset` layout | `usd-vrm-plugins` | [design policy](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/DESIGN_POLICY.md) · [VRM motion policy](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/VRM_MOTION_POLICY.md) |
| `MmdMaterialAPI` — MMD material semantics as `inputs:mmd:material:*`; draw order (`mmd:sourceIndex`); edge scale and sub-texture UV primvars | `usd-mmd-plugins` | [material policy §4](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/MATERIAL_POLICY.md#4-canonical-material-semantics) · [stage contract](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/STAGE_CONTRACT.md) |
| `mmdImaging` — the Hydra view of `MmdMaterialAPI` (planned, does not exist yet) | `usd-mmd-plugins` | [package contract](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/architecture/PACKAGE_CONTRACT.md) |
| `MotionPose`, `MotionStream`, the joint vocabulary, coordinates and time | `usd-motion-plugins` | [motion contract](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/MOTION_CONTRACT.md) |
| Device and protocol input (MediaPipe, mocap, OSC, XR) | `motion-connectors` | [docs](https://github.com/animu-sphere/motion-connectors/tree/main/docs) — reached only through `usd-motion-plugins`' types, never directly |
| Per-frame composition of the avatar stack, and when rendering happens in it | `usd-avatar-runtime` | its own documentation |
| Build, runtime adoption, renderer evidence and validation | `open-strata` (`ost`) | [adopting a renderer project](https://github.com/animu-sphere/open-strata/blob/main/docs/guides/adopt-a-renderer-project.md) |

`hydra-merlin` is a reference for technique — Vulkan setup, the OpenStrata
adoption path, Hydra adapter handling across OpenUSD versions — and is not a
dependency ([DESIGN_POLICY.md](DESIGN_POLICY.md) §3).

## 4. Dependency rules

1. **No link-time edge to a format repository.** Schemas are read by their
   registered names and tokens through the USD schema registry. A consumer that
   wants generated accessors must first show that the edge does not make
   `hydra-toon` unbuildable without a VRM or MMD install.
2. **The raw blobs are not an API.** `customData.vrm:mtoon:raw` and any other
   source-preserving blob are never read; a value the typed schema lacks is a
   missing schema field, raised with its owner
   ([`usd-vrm-plugins` material policy §6.5](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/MATERIAL_ARCHITECTURE_POLICY.md#65-source-of-truth)).
3. **Motion arrives as `usd-motion-plugins` types.** `hydra-toon` defines no
   pose or stream type of its own at its boundary; what it does with a pose
   inside the renderer (joint palettes, morph buffers) is its own.
4. **No USD-level `ToonMaterialAPI`.** The common representation is
   renderer-private. Both format repositories rule a shared USD schema out
   until two working concrete paths show common semantics
   ([`usd-mmd-plugins` material policy §12](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/MATERIAL_POLICY.md#12-rendering-and-integration-belong-elsewhere)).
5. **The core stays host-neutral.** OpenUSD appears only under
   `adapters/hydra2/` ([PROJECT_LAYOUT.md](../architecture/PROJECT_LAYOUT.md) §4).

## 5. What this repository does not own

- The source formats, their parsing, or the meaning of any canonical attribute.
- The USD schemas it reads, or their versions.
- The portable realizations (`/preview`, `/mtlx`) the format repositories
  author.
- Motion semantics: retargeting, sampling, filtering, recording.
- Device input.
- Avatar runtime orchestration and physics.
- General-purpose USD rendering; that is `hydra-merlin`'s role.

## 6. Cross-repository observations

Discrepancies seen while writing this document. They are raised with the
owner, not resolved here.

| Observed | Where | Owner |
| --- | --- | --- |
| The imaging policy records the Hydra path `hydra-toon` reads as not measured, and the Step I3 handshake as the place to measure it. [Renderer report 02](../reports/renderer/02-2026-09-26-mat-q1-material-inputs.md) measured it: the `vrm` container reaches a classic `HdMaterial` through the terminal scene index, and a value-only change (§28.3's path for high-frequency values) produces no `Sync` under emulation | `usd-vrm-plugins` imaging policy §27, §28.3 | `usd-vrm-plugins` |
