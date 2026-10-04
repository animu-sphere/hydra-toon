# Evaluated expression overrides without Hydra sync

- Date: 2026-10-05
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 19.51;
  OpenUSD 26.08, CY2026 lookdev runtime
- Scope: the evaluated expression update boundary of
  [v0.3.0](../../roadmap/v0.3.0.md)

## Scene and transient values

`RenderWorld` keeps evaluated morph-weight and material-value overrides
separate from the values supplied by scene setters. A commit publishes the
override while scene values continue updating underneath it. Clearing
publishes the latest scene values. Setting or clearing equal effective
values preserves revisions, so it writes no extra GPU buffer.

The state regression checks snapshot immutability, signed weights, unchanged
rest geometry, targets, skin, pose and material structure, duplicate updates,
baseline-equal updates, restoration after underlying value edits, and
unknown or removed ids. Non-finite weights, empty or mismatched weight arrays
and structural material overrides are rejected. Target changes, weight-count
changes and structural material changes discard their affected override.
Removal leaves no override attached to a later object.

## Direct Hydra host route

The delegate forwards the same operations under its existing scene mutex.
The host uses ids from `CommitScene`, valid for that delegate's scene
lifetime. The Hydra material regression submits a normalized expression
value and commits it without `SyncAll`. A subsequent value-only scene-index
edit updates the source material while the expression remains visible;
clearing restores that new material value.

The UsdSkel regression submits the previously evaluated half-weight subshape
array through the delegate, with no USD authoring, time change, scene-index
read or Hydra sync between submission and commit. A later sync preserves
the override; clearing restores the full scene weights. Rest points,
normals, targets and pose retain their revisions and shared arrays.

The resulting direct-update and cleared snapshots replace the ordinary
half/full snapshots in the independent lit-surface and hull oracle from
[report 35](35-2026-10-05-morph-normals.md). Opaque, Mask and Blend with hulls
on/off give 24 additional comparisons, including the initial and pose-only
frames. Colour payloads match byte for byte and depth differences stay below
1e-6. Static upload and pipeline counters stay fixed after initialization.

## Persistent Vulkan expression sequence

`toon-expression-gpu` compares a persistent override renderer against a
second persistent renderer fed by ordinary scene setters. The fixture is
skinned, has position and normal morph offsets, and uses MToon with an
inverted hull. Each route cycles baseline, morph-only, material plus morph,
signed morph, and cleared baseline six times. The material expression edits
base colour, shading shift and outline width.

Thirty frames across Opaque, Mask and Blend, with hulls on/off, give 180
comparisons at 96x96 and 1x sampling. All colour payloads match byte for byte,
and depth differences stay below 1e-6. Every non-baseline phase must visibly
differ from the baseline; every cleared phase returns to it exactly.

Each route writes weights 19 times and its material slot 13 times, including
the first frame. The repeated morph-only and material-only phases assert
their individual write counts. Points, topology, skin and morph targets are
uploaded once, the pose is written once, and four scene pipelines are created
once. No texture upload or render-target reallocation occurs after the first
frame, and the override renderer reports zero Vulkan validation messages.

## Reproduction and limits

```sh
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
```

The build, all 40 CTests and OpenStrata validation pass, including the
usdview host, native viewport captures, morph-normal oracles and install-tree
checks. Artifact integrity is an explained skip: this run packages no release.

This is a synchronous host API before commit and extraction, not a latch
after draw extraction or a measured input-to-display path. Inputs must already
be evaluated subshape weights and normalized material values; VRM expression
semantics, inbetween evaluation and recording remain with their owners.
Dedicated viewport expression controls, representative-avatar input mapping,
look-at, late motion latching and latency telemetry remain outside this run.
