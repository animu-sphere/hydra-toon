# hydra-toon documentation

Documentation is organized by responsibility, and each category answers one
class of question — the layout `usd-vrm-plugins`, `usd-mmd-plugins`,
`usd-motion-plugins`, `open-strata` and `hydra-merlin` share. Every subject has
one owning document; this page says which.

When a document disagrees with the implementation, the implementation wins
and the document is a bug. When a summary disagrees with
[architecture/PROJECT_LAYOUT.md](architecture/PROJECT_LAYOUT.md) about
*structure*, the layout wins.

| Category | Answers | Start here |
| --- | --- | --- |
| [design/](design/) | What the renderer is meant to be, and why. | [DESIGN_POLICY.md](design/DESIGN_POLICY.md) |
| [architecture/](architecture/) | Which targets exist, where code goes, and how they depend on each other. | [PROJECT_LAYOUT.md](architecture/PROJECT_LAYOUT.md) |
| [reference/](reference/) | What is implemented now, and on what it was measured. | [CAPABILITY_MATRIX.md](reference/CAPABILITY_MATRIX.md) · [SUPPORTED_CONFIGURATIONS.md](reference/SUPPORTED_CONFIGURATIONS.md) |
| [roadmap/](roadmap/) | What is built next, milestone by milestone. | [README.md](roadmap/README.md) · [later.md](roadmap/later.md) |
| [guides/](guides/) | How to perform a task. | [BUILDING.md](guides/BUILDING.md) |
| [releases/](releases/) | What a released version established, and how a release is cut. | [README.md](releases/README.md) |
| [reports/](reports/) | What was verified, under which conditions, and how. | [README.md](reports/README.md) |
| [archive/](archive/) | What used to be planned or authoritative and is now superseded. | [README.md](archive/README.md) |
| [contributing/](contributing/) | How these documents are maintained, and how release notes are rendered. | [documentation.md](contributing/documentation.md) · [RELEASE_NOTES_TEMPLATE.md](contributing/RELEASE_NOTES_TEMPLATE.md) |

## Source of truth

| Question | Owner |
| --- | --- |
| Purpose, principles, renderer architecture and shared scene services | [design/DESIGN_POLICY.md](design/DESIGN_POLICY.md) |
| What this repository owns, what it consumes from which sibling, what it does not own | [design/INTEGRATION_SCOPE_POLICY.md](design/INTEGRATION_SCOPE_POLICY.md) |
| Reading MToon, MMD and PreviewSurface materials; `ToonMaterial`; outline; draw order; pipelines; how each model receives shared shadows | [design/MATERIAL_POLICY.md](design/MATERIAL_POLICY.md) |
| Reusable RGBA products, background modes, alpha/coverage, viewport compositing and GPU texture handoff | [design/RENDER_OUTPUT_POLICY.md](design/RENDER_OUTPUT_POLICY.md) |
| Shared shadows: shadowing lights, settings, casters, views, resources and MMD self-shadow migration | [design/SHADOW_POLICY.md](design/SHADOW_POLICY.md) |
| The WebGPU backend: Dawn, its boundary, binding model, WGSL, frames, output, hosts and parity evidence | [design/WEBGPU_POLICY.md](design/WEBGPU_POLICY.md) |
| The Vulkan GPU handoff to a host: borrowed device, product ring, synchronization, consumers and readback fallback | [design/GPU_HANDOFF_POLICY.md](design/GPU_HANDOFF_POLICY.md) |
| Targets, names, directories, dependency directions, build intents, install tree | [architecture/PROJECT_LAYOUT.md](architecture/PROJECT_LAYOUT.md) |
| Implemented capabilities — the only statement of current status | [reference/CAPABILITY_MATRIX.md](reference/CAPABILITY_MATRIX.md) |
| Measured platforms and runtimes | [reference/SUPPORTED_CONFIGURATIONS.md](reference/SUPPORTED_CONFIGURATIONS.md) |
| Unfinished milestone actions and acceptance criteria; later work | [roadmap/v0.8.0.md](roadmap/v0.8.0.md) and [roadmap/later.md](roadmap/later.md); sequence/navigation in [roadmap/README.md](roadmap/README.md) |
| Dated experiments, performance and reference evidence | [reports/renderer/](reports/renderer/) |
| Versioning, release gate and released achievements | [releases/](releases/) |
| Short dated change entries | [CHANGELOG.md](../CHANGELOG.md) |
| How documents here are maintained, and how they cite sibling repositories | [contributing/documentation.md](contributing/documentation.md) |

Where the design documents overlap, the narrower one wins:
INTEGRATION_SCOPE_POLICY.md, MATERIAL_POLICY.md, RENDER_OUTPUT_POLICY.md,
SHADOW_POLICY.md, WEBGPU_POLICY.md and GPU_HANDOFF_POLICY.md over
DESIGN_POLICY.md on their subjects, GPU_HANDOFF_POLICY.md over
RENDER_OUTPUT_POLICY.md on its integration,
and none of them over PROJECT_LAYOUT.md on structure.

## Owned elsewhere

This repository consumes these subjects and does not define them. Each is
linked, never restated:

| Subject | Owner |
| --- | --- |
| VRM / MToon material semantics (`VrmMaterialAPI`, `VrmMToonAPI`, `VrmTextureInfoAPI`) and the VRM stage | [`usd-vrm-plugins`](https://github.com/animu-sphere/usd-vrm-plugins/tree/main/docs) |
| MMD material semantics (`MmdMaterialAPI`) and the MMD stage | [`usd-mmd-plugins`](https://github.com/animu-sphere/usd-mmd-plugins/tree/main/docs) |
| Generic motion: `MotionPose`, `MotionStream`, sampling, retargeting, recording | [`usd-motion-plugins`](https://github.com/animu-sphere/usd-motion-plugins/tree/main/docs) |
| Avatar evaluation composition, lifecycle and shared evaluated-state publication | [`usd-avatar-runtime`](https://github.com/animu-sphere/usd-avatar-runtime/tree/main/docs) |
| Device and protocol input | [`motion-connectors`](https://github.com/animu-sphere/motion-connectors/tree/main/docs) |
| Build, runtimes, renderer evidence and validation | [`open-strata`](https://github.com/animu-sphere/open-strata/tree/main/docs) |
