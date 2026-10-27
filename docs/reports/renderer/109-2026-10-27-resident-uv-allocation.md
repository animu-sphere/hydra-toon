# Dense resident UV conversion allocations

Date: 2026-10-27. Windows x64/MSVC Release, OpenUSD 26.08, Vulkan,
NVIDIA RTX A5000. Continues [report 108](108-2026-10-27-dense-live-mmd.md).

## Change

The resident adapter compared each incoming identity by constructing an owned
temporary `Identity`. Long mesh names therefore allocated a string for every
UV vertex, even after dense validation had become allocation-free for ordered
layouts. It also allocated and filled a complete UV vector before discovering
that the values matched the renderer baseline.

Identity checks now compare incoming bytes directly against the owned binding.
Mapped UV values are compared in renderer precision before allocating a changed
array. All finite/range, duplicate, identity, ordering and binding checks remain;
no producer pointer identity, frame-token shortcut or evaluation cache is used.
Changed UVs still regenerate tangents and seams through the existing route.
Runtime evaluation, fixed ticks, retained snapshots and output ABI are unchanged.

## Isolated conversion cost

A local synthetic harness used 135,135 source points and ordered primary UVs,
one mesh with a 65-byte owner identity, no joints, morphs or tangents, and explicit
one-to-one resident bindings. The baseline was commit `39420bf`; both versions
were compiled with the same MSVC Release settings. Each run warmed up ten
applications and timed 120 calls to `AvatarStateAdapter::Apply`, excluding
snapshot retention, binding, rendering and runtime evaluation. The changed case
alternated two non-baseline U values. A harness-local global `operator new`
counter counted allocations only inside Apply. Three runs of each version,
including reversed execution order, gave these medians of per-run means:

| Case | Before ms/Apply | After ms/Apply | Before allocations/Apply | After allocations/Apply |
| --- | ---: | ---: | ---: | ---: |
| Baseline UV values | 11.472 | 4.932 | 135,139 | 3 |
| Changed UV values | 13.048 | 4.978 | 135,140 | 5 |

The remaining allocations belong to frame storage and, for changed values,
the published UV array. This synthetic result is not an avatar FPS or
runtime-speed claim. Harness source, binaries and raw logs remain ignored
under `build/dense-consumer-bench/`.

## Regression evidence

The new dense adapter case uses 65,536 records over two long mesh identities,
reversed slot order and independent renderer mappings. It checks unchanged
array/revision reuse, equal identity bytes in different storage, changed values,
atomic rename/reorder refusal, reset restoration and immutable retained arrays.

All 14 selected renderer tests pass: avatar state and Vulkan GPU response,
Hydra skinning, native MMD cached/uncached stream agreement, viewport MMD
playback/replacement/teardown, ordinary playback admission and file replacement
with image comparisons. Existing tangent-seam tests continue to compare GPU
colour/depth against independently expanded streams.

The same private scoped preview from report 108 completes 120 presented frames
and 952 fixed ticks, with 73 vertex morph bindings, one resident skin/UV mesh,
zero mismatches/refusals and zero bodies after close. Its temporary source
limitations remain. Real-time performance and representative acceptance remain
open; runtime owner evaluation/value transport remains the next measured area.
