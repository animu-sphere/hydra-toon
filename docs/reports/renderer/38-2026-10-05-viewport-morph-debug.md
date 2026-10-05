# Viewport morph debugging through evaluated overrides

Later: [report 39](39-2026-10-05-late-frame-input.md) adds late commit, so debug
edits can reach the current submit after extraction.

- Date: 2026-10-05
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Runtime: canonical CY2026 OpenUSD 26.08 lookdev
- Scope: the morph diagnostics of [v0.3.0](../../roadmap/v0.3.0.md)

## Panel and host boundary

The Morphs panel lists resident evaluated weight slots by mesh id and slot
index. Dragging a field, or entering a signed value with Ctrl+click, submits
the mesh's whole effective array through `HydraScene` to the existing
delegate override API. Per-mesh release and release-all controls resume
scene evaluation. Edits are published by the next frame's commit, without
USD authoring or a Hydra sync request. Animation can continue evaluating
underneath an override.

The panel reads override activity from
`MeshSnapshot::morph_weights_overridden`. An override equal to the scene
weights still appears active, but changes no rendering revision. Clearing
it similarly needs no GPU write. Binding/weight-count edits invalidate
the override and its flag in the render world; scene replacement owns a
new delegate and retains no old override. UI state holds no independent
copy of those values.

## State and GPU checks

`toon-viewport-morph-debug-state` and `toon-viewport-morph-debug-gpu` use
the same `HydraScene` entry points as the panel with the committed sparse,
GPU-skinned `morph-motion.usda` fixture. Snapshots may include uninitialized
guide meshes; the test selects the resident morph mesh rather than relying
on its position in the snapshot.

State checks reject missing ids, wrong counts and non-finite values without
changing revisions or requesting sync. They check equal-value override and
clear diagnostics, signed weights, latest-value restoration and replacement
with an active override. The core expression state test additionally checks
the diagnostic flag after target and weight-count invalidation.

The persistent 96x96, 4x MSAA GPU run has seven frames:

| Frame | Action | Effective weights | Override active | Weight writes in frame |
| --- | --- | --- | --- | --- |
| 0 | Scene at time code 1 | 0 | no | 1, initial upload |
| 1 | Set override | 1 | yes | 1 |
| 2 | Repeat override | 1 | yes | 0 |
| 3 | Evaluate scene at time code 2 | 1 | yes | 0 |
| 4 | Release override | 1 | no | 0 |
| 5 | Set signed override | -0.5 | yes | 1 |
| 6 | Release override | 1 | no | 1 |

There are two Hydra syncs, startup and the explicit time move; debug edits
request none. The run writes four weight buffers total. Topology, rest
points, normals, skin, pose and targets retain their state. After the first
frame there are no topology/point/skin/target/texture uploads, material or
pose writes, pipeline rebuilds or render-target allocations.

The positive override changes the image from baseline; the signed override
changes it from both baseline and positive. Duplicate edits, evaluation
beneath the override and releases give the positive frame's colour and
depth back exactly. Vulkan validation reports no messages. This host-route
regression complements the independent CPU-oracle morph/expression tests;
it does not add a new reference renderer.

## Reproduction and limits

```sh
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
```

Build, all 45 CTests and OpenStrata validation pass. Validation binds the
renderer evidence to the completed test run; artifact integrity is skipped
because no release package is produced. The first direct CTest invocation
lacked the runtime DLL paths for bootstrap viewport checks; the canonical
OpenStrata test environment resolves that launch failure.

The existing OpenUSD-free `core--renderer-viewport` tree also builds.
Its seven selected expression-state, core-boundary, playback-state,
presentation and overlay-free capture checks pass. No OpenUSD dependency
was introduced into this build route. `git diff --check` passes.

The controls are compiled and their host operations are automated. Individual
mouse interactions and a representative avatar's expression semantics were
not exercised in this run. Slots are evaluated renderer subshapes, including
any source-mapped inbetweens, without source expression names. Expression
mapping, material-value controls, look-at, skeleton diagnostics, late latching
and input-to-display latency remain separate work.
