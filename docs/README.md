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
| [roadmap/](roadmap/) | What is built next, milestone by milestone. | [README.md](roadmap/README.md) · [v0.2.0.md](roadmap/v0.2.0.md) · [v0.3.0.md](roadmap/v0.3.0.md) · [later.md](roadmap/later.md) |
| [guides/](guides/) | How to perform a task. | [BUILDING.md](guides/BUILDING.md) |
| [releases/](releases/) | What a released version established, and how a release is cut. | [README.md](releases/README.md) |
| [reports/](reports/) | What was verified, under which conditions, and how. | [README.md](reports/README.md) |
| [archive/](archive/) | What used to be planned or authoritative and is now superseded. | [README.md](archive/README.md) |
| [contributing/](contributing/) | How these documents are maintained, and how release notes are rendered. | [documentation.md](contributing/documentation.md) · [RELEASE_NOTES_TEMPLATE.md](contributing/RELEASE_NOTES_TEMPLATE.md) |

## Source of truth

| Question | Owner |
| --- | --- |
| Purpose, principles, renderer architecture, how milestones are ordered | [design/DESIGN_POLICY.md](design/DESIGN_POLICY.md) |
| What this repository owns, what it consumes from which sibling, what it does not own | [design/INTEGRATION_SCOPE_POLICY.md](design/INTEGRATION_SCOPE_POLICY.md) |
| Reading MToon, MMD and PreviewSurface materials; `ToonMaterial`; outline; draw order; pipelines | [design/MATERIAL_POLICY.md](design/MATERIAL_POLICY.md) |
| Targets, names, directories, dependency directions, build intents, install tree | [architecture/PROJECT_LAYOUT.md](architecture/PROJECT_LAYOUT.md) |
| Implemented capabilities — the only statement of current status | [reference/CAPABILITY_MATRIX.md](reference/CAPABILITY_MATRIX.md) |
| Measured platforms and runtimes | [reference/SUPPORTED_CONFIGURATIONS.md](reference/SUPPORTED_CONFIGURATIONS.md) |
| What each milestone contains, and the order of work | [roadmap/](roadmap/) |
| Versioning, the release gate, how a release is cut, and what each release established | [releases/](releases/); change by change, the [CHANGELOG](../CHANGELOG.md) |
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
