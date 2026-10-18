---
status: accepted
owner: hydra-toon
---

# Integration scope policy

> This document says how far `hydra-toon` goes, what it consumes from which
> sibling repository, and what it does not own. On structure, [PROJECT_LAYOUT.md](../architecture/PROJECT_LAYOUT.md)
> wins; on materials, [MATERIAL_POLICY.md](MATERIAL_POLICY.md) does; on RGBA
> output and compositing, [RENDER_OUTPUT_POLICY.md](RENDER_OUTPUT_POLICY.md) does.

## 1. The rule

> **Authored assets and canonical format semantics cross through the composed
> USD stage and its schemas. Evaluated avatar state crosses through
> `usd-avatar-runtime`'s public output contract. Both routes feed one
> renderer-private scene model.**

`hydra-toon` never parses `.vrm`, `.vrma`, `.pmx` or `.vmd`, never links a
format repository's file-format plugin or model library, and never reads a
realization graph (`/preview`, `/mtlx`) as the source of a toon look. It reads
the canonical semantics those repositories author, through the names their
schemas define.

The evaluated-state route supplements that authored-stage boundary; it does
not add a link to format evaluators. `usd-avatar-runtime` owns composition,
evaluation order, lifecycle and publication. Format repositories retain their
expression, look-at, morph and IK semantics. `hydra-toon` consumes resolved
effects and translates them into its own joint palettes, morph slots and
material parameters. The shared result type belongs to the runtime, not to
the renderer ([evaluated-state contract](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/contracts/EVALUATED_STATE.md),
[output paths](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/architecture/OUTPUT_PATHS.md)).

The authored-stage boundary is also stated by the format repositories:
[`usd-vrm-plugins` material policy §5.3.1](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/MATERIAL_ARCHITECTURE_POLICY.md#531-the-boundary-with-hydra-toon)
and [`usd-mmd-plugins` material policy §12](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/MATERIAL_POLICY.md#12-rendering-and-integration-belong-elsewhere).

## 2. What this repository owns

- The renderer: the host-neutral core, the Vulkan backend, and later the
  WebGPU backend ([DESIGN_POLICY.md](DESIGN_POLICY.md) §4, §7).
- The Hydra adapter (`hdToon`) and any scene index that normalizes input for
  it ([DESIGN_POLICY.md](DESIGN_POLICY.md) §5).
- An optional fast adapter from public evaluated avatar state into the same
  renderer-private representation. Neither adapter evaluates avatar behavior
  ([DESIGN_POLICY.md §34](DESIGN_POLICY.md#34-evaluated-avatar-input-and-transport-parity)).
- The full MToon realization: toon lighting, shade, shading shift and toony,
  GI equalization, rim, MatCap, outline, UV animation, MToon transparency and
  render ordering.
- An MMD toon realization: diffuse / alpha, toon ramp, sphere multiply / add /
  sub-texture, edge, shadow flags, MMD draw order.
- A `UsdPreviewSurface` realization for materials with no toon semantics.
- Reusable RGBA render products, transparent output, shared alpha semantics
  and consumer-boundary conversion. Viewport presentation and future Hydra,
  host or compositor handoff consume that product under
  [RENDER_OUTPUT_POLICY.md](RENDER_OUTPUT_POLICY.md).
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
| `MotionPose`, `MotionStream`, the joint vocabulary, coordinates and time — behind the evaluated pose this repository receives from `usd-avatar-runtime`; no motion type is linked | `usd-motion-plugins` | [motion contract](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/MOTION_CONTRACT.md) |
| Device and protocol input (MediaPipe, mocap, OSC, XR) | `motion-connectors` | [docs](https://github.com/animu-sphere/motion-connectors/tree/main/docs) — bridged to `usd-motion-plugins` by external composition, preferably `usd-avatar-runtime` ([motion design policy §44](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/DESIGN_POLICY.md#44-motion-and-connector-boundary)); never reached directly |
| Avatar evaluation composition, order, lifecycle and public evaluated state | `usd-avatar-runtime` | [dependency boundary](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/architecture/DEPENDENCIES.md) · [evaluated state](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/contracts/EVALUATED_STATE.md) · [publication](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/architecture/OUTPUT_PATHS.md) |
| Build, runtime adoption, renderer evidence and validation | `open-strata` (`ost`) | [adopting a renderer project](https://github.com/animu-sphere/open-strata/blob/main/docs/guides/adopt-a-renderer-project.md) |

`hydra-merlin` is a reference for technique and is not a dependency; what it
is a reference for, and what is designed separately here, is
[DESIGN_POLICY.md](DESIGN_POLICY.md) §3.

## 4. Dependency rules

1. **No link-time edge to a format repository.** Schemas are read by their
   registered names and tokens through the USD schema registry. A consumer that
   wants generated accessors must first show that the edge does not make
   `hydra-toon` unbuildable without a VRM or MMD install.
2. **The raw blobs are not an API.** `customData.vrm:mtoon:raw` and any other
   source-preserving blob are never read; a value the typed schema lacks is a
   missing schema field, raised with its owner
   ([`usd-vrm-plugins` material policy §6.5](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/MATERIAL_ARCHITECTURE_POLICY.md#65-source-of-truth)).
3. **Motion and evaluated state keep their upstream owners.** Generic motion
   types and vocabulary belong to `usd-motion-plugins`; final avatar state
   belongs to `usd-avatar-runtime`. The renderer defines no competing motion
   vocabulary or shared avatar result type. Converting resolved rig values to
   renderer-private joint palettes, morph buffers and material slots is its
   adapter's responsibility. A semantic humanoid pose is not assumed to be a
   complete final rig.
4. **No USD-level `ToonMaterialAPI`.** The common representation is
   renderer-private. Both format repositories rule a shared USD schema out
   until two working concrete paths show common semantics
   ([`usd-mmd-plugins` material policy §12](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/MATERIAL_POLICY.md#12-rendering-and-integration-belong-elsewhere)).
5. **The core stays host-neutral.** OpenUSD and upstream runtime types stay
   in their adapters, under the placement rules in
   [PROJECT_LAYOUT.md §4](../architecture/PROJECT_LAYOUT.md#4-dependency-directions).
   The core and backends have no dependency on avatar runtime orchestration,
   registration or format evaluators.
6. **Runtime integration is optional.** The fast adapter may consume the
   runtime's minimal public state/ABI contract. Core, backend and Hydra-only
   builds must remain usable without that package. Adapter code does not
   invoke evaluator callbacks; it consumes published results with explicit
   binding identity and lifetime. Runtime ABI details remain with their
   [owner](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/contracts/ABI.md).

## 5. What this repository does not own

- The source formats, their parsing, or the meaning of any canonical attribute.
- The USD schemas it reads, or their versions.
- The portable realizations (`/preview`, `/mtlx`) the format repositories
  author.
- Motion semantics: retargeting, sampling, filtering, recording.
- VRM expression arbitration and look-at evaluation; MMD morph and IK
  evaluation. Their reusable algorithms stay with the format owners.
- Device input.
- Avatar runtime orchestration, evaluator scheduling, physics and the shared
  evaluated-state contract. The runtime's evaluation Scene Index belongs
  upstream; this repository owns consuming and normalizing its data sources.
- Host application orchestration and compositor internals. Prospective
  Mimikuri and Tessera integrations consume renderer output; they do not move
  application ownership or avatar evaluation into the renderer core.
- General-purpose USD rendering; that is `hydra-merlin`'s role.

## 6. Cross-repository observations

Track cross-repository questions at their owner; this page does not mirror
resolution status. Renderer consumption rules belong to the sections above
and current renderer support belongs to the capability matrix.

| Question | Owning discussion / contract |
| --- | --- |
| Hydra canonical-value observation under emulation | [usd-vrm-plugins#252](https://github.com/animu-sphere/usd-vrm-plugins/issues/252) |
| Evaluated-state source provenance | [usd-avatar-runtime#22](https://github.com/animu-sphere/usd-avatar-runtime/issues/22) · [source provenance contract](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/contracts/EVALUATED_STATE.md#source-sample-provenance) |
| Texture offset/scale value types | [usd-avatar-runtime#23](https://github.com/animu-sphere/usd-avatar-runtime/issues/23) · [state groups](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/contracts/EVALUATED_STATE.md#1-state-groups) |
| Resident target matching responsibility | [usd-avatar-runtime#24](https://github.com/animu-sphere/usd-avatar-runtime/issues/24) · [output path boundary](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/architecture/OUTPUT_PATHS.md#resident-target-matching) |
