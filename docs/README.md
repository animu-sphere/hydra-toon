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
| [reference/](reference/) | What is implemented now, and on what. | [CAPABILITY_MATRIX.md](reference/CAPABILITY_MATRIX.md) · [SUPPORTED_CONFIGURATIONS.md](reference/SUPPORTED_CONFIGURATIONS.md) |
| [roadmap/](roadmap/) | What incomplete work remains. | [README.md](roadmap/README.md) · [current.md](roadmap/current.md) · [vrm-host-session.md](roadmap/vrm-host-session.md) |
| [guides/](guides/) | How to perform a task. | [BUILDING.md](guides/BUILDING.md) |
| [releases/](releases/) | What shipped in a released version. | [README.md](releases/README.md) |
| [reports/](reports/) | What was measured or observed. | [README.md](reports/README.md) |
| [archive/](archive/) | What used to be planned or authoritative and is now superseded. | [README.md](archive/README.md) |
| [contributing/](contributing/) | How these documents are maintained, and how release notes are rendered. | [documentation.md](contributing/documentation.md) · [RELEASE_NOTES_TEMPLATE.md](contributing/RELEASE_NOTES_TEMPLATE.md) |

## Source of truth

| Question | Owner |
| --- | --- |
| Purpose, principles, renderer architecture, Renderer Phase 0–7 | [design/DESIGN_POLICY.md](design/DESIGN_POLICY.md) |
| What this repository owns, what it consumes from which sibling, what it does not own | [design/INTEGRATION_SCOPE_POLICY.md](design/INTEGRATION_SCOPE_POLICY.md) |
| Reading MToon, MMD and PreviewSurface materials; `ToonMaterial`; outline; draw order; pipelines | [design/MATERIAL_POLICY.md](design/MATERIAL_POLICY.md) |
| Targets, names, directories, dependency directions, build intents, install tree | [architecture/PROJECT_LAYOUT.md](architecture/PROJECT_LAYOUT.md) |
| Implemented capabilities; measured platforms and runtimes | [reference/](reference/) |
| Incomplete work, and which release carries it | [roadmap/](roadmap/) |
| Released history, and how a release is cut | [releases/](releases/) and the [CHANGELOG](../CHANGELOG.md) |
| How documents here are maintained, and how they cite sibling repositories | [contributing/documentation.md](contributing/documentation.md) |

Where the design documents overlap, the narrower one wins:
INTEGRATION_SCOPE_POLICY.md and MATERIAL_POLICY.md over DESIGN_POLICY.md on
their subjects, and none of them over PROJECT_LAYOUT.md on structure.

## Owned elsewhere

This repository consumes these subjects and does not define them. Each is
linked, never restated:

| Subject | Owner |
| --- | --- |
| VRM / MToon material semantics (`VrmMaterialAPI`, `VrmMToonAPI`, `VrmTextureInfoAPI`) and the VRM stage | [`usd-vrm-plugins`](https://github.com/animu-sphere/usd-vrm-plugins/tree/main/docs) |
| MMD material semantics (`MmdMaterialAPI`) and the MMD stage | [`usd-mmd-plugins`](https://github.com/animu-sphere/usd-mmd-plugins/tree/main/docs) |
| Generic motion: `MotionPose`, `MotionStream`, sampling, retargeting, recording | [`usd-motion-plugins`](https://github.com/animu-sphere/usd-motion-plugins/tree/main/docs) |
| Device and protocol input | [`motion-connectors`](https://github.com/animu-sphere/motion-connectors/tree/main/docs) |
| Build, runtimes, renderer evidence and validation | [`open-strata`](https://github.com/animu-sphere/open-strata/tree/main/docs) |
