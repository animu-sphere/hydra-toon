# Hydra morph normals and rest-normal fallback

- Date: 2026-10-05
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 19.51;
  OpenUSD 26.08, CY2026 lookdev runtime
- Scope: morph normal treatment in the avatar animation path of
  [v0.3.0](../../roadmap/v0.3.0.md)

## Adapter routing

OpenUSD 26.08's position aggregator exposes packed position offsets and
ranges, but does not expose normal offsets. `hdToon` reads those from the
terminal scene index's `skelBinding` and `skelBlendShape` containers when
the aggregator changes. Binding order and sorted nonzero inbetween weights
identify the same subshape slots usdSkelImaging's evaluated weights use.
This introduces no stage access or link to a format or imaging library.

Sparse and dense normal offsets join the resident GPU targets. A normal-only
target can affect a point beyond the position ranges; it gets a zero
position offset with the appropriate weight slot. Missing normal offsets
retain the rest normal, whether authored or derived. Normals are not
recalculated from morphed positions. A malformed sparse normal-array length
contributes no normal offsets without discarding valid position offsets.

The Hydra scene-index regression drives zero, half and full weights, a
separate pose edit, and signed fractional weights. The authored inbetween
moves point 0 by 0.75 in z at weight 0.5 and changes its rest normal from
(0.6, 0, 0.8) to (0.4, 0, 1), before a second target's normal-only
contribution. A position-only target retains its original normal.

Editing only `normalOffsets` refreshes the morph targets while preserving
point, normal, skin, pose and weight revisions. The adapter now compares
rest-point values before re-sending them after an aggregator edit; unchanged
positions no longer cause a geometry upload. Removal of normal offsets,
malformed sparse arrays, and removal of authored normals are also checked.

## Lit-surface and hull oracle

`toon-renderer-hydra-morph-normals` evaluates four Hydra snapshots in each
of three normal treatments: authored offsets, position-only morphs with
authored rest normals, and position-only morphs with derived rest normals.
Each runs through MToon Opaque, Mask and Blend, with hulls enabled and
disabled. Two persistent Vulkan renderers compare the adapter's GPU targets
against independently authored CPU-deformed rest points and normals, then
apply the same skin binding and pose.

All 72 comparisons at 128×128 and 1x sampling have byte-identical colour
payloads; every depth difference is below 1e-6. Front and back triangles
exercise the inverted hull, and directional lighting uses a non-saturated
MToon ramp. A deliberately stale-normal reference produces a different
image in every authored-offset route, proving that the fixture detects
omitted normal offsets even with identical deformed positions.

Every four-frame route uploads points, topology, skin and morph targets
once, and creates four pipelines once. The pose-only frame updates joints;
the two weight frames update weights. Static upload/material/pipeline
counters remain unchanged after the first frame. Both renderers report
zero Vulkan validation messages.

## Reproduction and limits

```sh
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
```

All 38 CTests pass, including the usdview host, the nine-frame native morph
sequence, the existing 288 sparse-morph GPU comparisons and install-tree
checks. OpenStrata validation passes; artifact integrity is an explained
skip because this run does not package a release.

The image oracle is a controlled deformation fixture, not a claim of
representative-avatar expression fidelity. Position-only targets retain
rest shading directions even when the surface bends. Face-varying normals,
GPU dual quaternion skinning, direct expression updates that bypass Hydra,
late motion latching and input-to-display latency remain outside this run.
