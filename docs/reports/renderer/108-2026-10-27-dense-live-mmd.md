# Dense live MMD preview: intake and validation cost

Date: 2026-10-27. Windows x64/MSVC Release, OpenUSD 26.08, Vulkan,
NVIDIA RTX A5000. Continues [report 107](107-2026-10-27-live-mmd-viewport.md).
Private asset names, source paths, screenshots and derived layers remain ignored.

## Intake and scoped preview

The second private avatar has 461 joints, 265 bodies and 135,135 source points.
Its USDZ and original PMX agree on joints/rest, points, primary UVs and morph
bindings. `--avatar-model` now supplies explicit PMX owner bytes when the
displayed stage is a composed USD/USDZ stage. Runtime retains its existing
joint, morph provenance and immutable primary-UV checks.

The user authorized a temporary damping preview. An ignored stronger layer
references the original USDZ, copies its stage metrics, and changes 124 damping
attributes on 99 bodies from coefficients at/above one to 0.99. Ten joints whose
endpoints are both kinematic are disabled in this preview. A local motion copy
omits one unsupported material-morph key; all 101,652 bone keys, 261 other morph
keys and all remaining VMD sections are unchanged. Original file hashes are
unchanged. These choices are visible limitations, not formal damping/material
support or native MMD equivalence.

The corrected preview completes 120 presented frames and 952 runtime ticks,
with 73 vertex morph bindings, one resident skin/UV mesh, zero mismatches and
zero refusals. The user confirmed that motion runs. A failed intake is now
distinguished from successful presentation: bounded native-physics runs return
failure if runtime evaluations were refused, even if the window drew frames.

## Measured cause

The initial window averaged 655 ms/frame (about 1.5 fps): 69.9 ms per fixed
runtime tick, 39.0 ms per resident conversion, and about 3 ms GPU scene time.
The runtime catches up at most eight ticks per display request and retains
backlog, so slow requests repeatedly pay for eight evaluations.

A local instrumented runtime, with no USD publication or rendering, measured
the same first 120 fixed ticks:

| Cost | Original | First validation change |
| --- | ---: | ---: |
| Total | 9,508.59 ms | 1,812.37 ms |
| State validation | 7,933.90 ms | 467.42 ms |
| State construction/copy | 531.80 ms | 441.08 ms |
| Owner pre/post evaluation | 878.34 ms | 710.38 ms |

Validation accounted for about 83% of the original runtime request. It rebuilt
a tree of copied `(mesh string, vertex index)` identities for every dense UV
snapshot. The renderer consumer repeated that pattern per result. Instruments
and raw output are local; wall-clock owner timings vary under concurrent work.

## Change and regression evidence

Both validators now check strict identity order while scanning all values.
Ordered layouts need no identity-container allocation. Unordered layouts sort
borrowed string-view keys in one contiguous array and still reject duplicates.
No finite-value, duplicate, identity, layout or binding validation was removed.
Runtime snapshots also share immutable owned identity strings across state
copies, while retaining independent value arrays and source-sample storage.

The final 120-frame preview measured 111.5 ms/frame (about 9 fps), 11.57 ms per
runtime tick and 3.87 ms per resident conversion, with the same 952 ticks and
zero refusals. This is about six times the original presentation rate; it still
falls behind wall time at 120 Hz and is not a real-time acceptance result.

Runtime regressions pass 22/22, including new ordered/reversed 65,536-record,
two-mesh UV layouts and nonadjacent duplicate rejection, plus retained output
after source changes, reset and runtime destruction. All 15 selected renderer
checks pass, including dense UV admission/refusal, consumer GPU response,
cached/uncached stream agreement, composed-stage source binding, scene
replacement, playback and skinning.

Further performance work is runtime owner evaluation and dense value
transport, then renderer conversion. Preserve fixed-tick/retained-state
semantics and measure CPU/GPU costs separately. Formal damping integration
remains separate; its owner-side
[source-policy slice](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/reports/2026-10-27-phase8-source-damping-response.md)
does not yet remove the preview approximation.
