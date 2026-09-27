# Roadmap

The roadmap holds only **incomplete** work owned by this repository. Shipped
work is in the [CHANGELOG](../../CHANGELOG.md) and the
[release records](../releases/); rationale lives in [design/](../design/).
Sibling repositories' work is planned in their own roadmaps and not mirrored
here.

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked · ⚠️ accepted workaround

| Document | Contents |
| --- | --- |
| [current.md](current.md) | Renderer Phase 0's accepted stand-ins, what must precede Renderer Phase 1's MToon path, Renderer Phase 1's items and stand-ins, Renderer Phase 2's items and stand-ins, and project infrastructure. |
| [vrm-host-session.md](vrm-host-session.md) | The VRM `usdview` session as a Formation: its members, package names, and the state and plan on Windows, Linux and macOS. |

## Sequence

One sequence is live: **Renderer Phase 0–7**, defined in
[design policy §25](../design/DESIGN_POLICY.md#25-implementation-phases).
A reference to it is always qualified — "Renderer Phase 1", never a bare
"Phase 1" — because every sibling repository has phases of its own.

## Status at a glance

**This table is the single source of truth for which release a phase lands
in.** Other documents name a phase and defer the version here.

| Phase | Status | Target |
| --- | --- | --- |
| Renderer Phase 0 — Skeleton | ✅ | [v0.1.0](../releases/v0.1.0.md) |
| Renderer Phase 1 — Avatar MVP | ✅ renderer work in v0.1.0; the host session's Formation on `main` ([report 13](../reports/renderer/13-2026-09-28-host-session-formation.md)) | the release after v0.1.0, unscheduled |
| Renderer Phase 2 — MToon completion | 🚧 rim and MatCap on `main` ([report 14](../reports/renderer/14-2026-09-28-mtoon-rim.md)) | unscheduled |
| Renderer Phase 3 — Animation fast path | ⬜ | unscheduled |
| Renderer Phase 4 — MMD | ⬜ | unscheduled |
| Renderer Phase 5 — UsdPreviewSurface | ⬜ | unscheduled |
| Renderer Phase 6 — WebGPU | ⬜ | unscheduled |
| Renderer Phase 7 — Optimization | ⬜ | unscheduled |

## Quality bar (applies to every phase)

- A pose, expression or camera change never rebuilds topology, draw packets or
  pipelines ([design policy §11, §14](../design/DESIGN_POLICY.md#14-dirty-propagation)).
- No `vkDeviceWaitIdle` or `vkQueueWaitIdle` in an ordinary frame
  ([§19](../design/DESIGN_POLICY.md#19-cpu--gpu-synchronization)).
- Public core headers stay free of OpenUSD, Hydra, Vulkan and windowing types,
  and CI enforces it ([PROJECT_LAYOUT.md §4](../architecture/PROJECT_LAYOUT.md#4-dependency-directions)).
- Nothing links a format repository; a sibling's contract is linked, never
  restated ([integration scope](../design/INTEGRATION_SCOPE_POLICY.md)).
- A performance claim cites a measurement, and a measurement is a
  [report](../reports/).
- Every documented command is one that has actually been run.
