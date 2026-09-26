# Reports

Dated evidence from real runs: builds, hardware sessions, benchmarks, and
OpenUSD or OpenStrata behaviour this project had to measure.

| Document | Contents |
| --- | --- |
| [ost/](ost/) | The `ost` dogfooding series — one report per version exercised. Append-only; the newest report carries the live upstream ask list. |

Renderer benchmarks — the latency and frame-time KPIs of
[design policy §23](../design/DESIGN_POLICY.md#23-performance-kpis) — will be
reports here too, one per measured session, once there is something to
measure.

## What belongs where

A report captures *how* something was validated, on a specific machine, at a
specific time. It is working history, not a current-state contract:

- Current structure belongs in [architecture/](../architecture/), and current
  capability in [reference/](../reference/).
- Design rationale belongs in [design/](../design/).
- Incomplete work belongs in the [roadmap](../roadmap/).
- Shipped scope belongs in the [changelog](../../CHANGELOG.md), with
  per-version detail in [releases/](../releases/).

When a report disagrees with a current-state document, the current-state
document wins and the report is history.
