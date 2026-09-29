# OST dogfooding reports

This repository is built with [OpenStrata](https://github.com/animu-sphere/open-strata)
(`ost`), and these are the dated records of what that was like, measured in
this repository's own tree. They are upstream feedback first and our own
status trail second, and follow the series `usd-vrm-plugins` keeps
([its reports](https://github.com/animu-sphere/usd-vrm-plugins/tree/main/docs/reports/ost)):
the ecosystem's running ask list is there, and a report here records what only
this repository measured.

**They are append-only historical evidence.** A report is never rewritten to
match what later turned out to be true. When a newer `ost` resolves an item, a
new report re-verifies it and the superseded report gets a one-line
forward-note at the top.

## Reading order

The newest report carries the current asks.

| Report | `ost` | Subject |
| --- | --- | --- |
| [01](01-2026-09-26-v0.23.6-renderer-template-bootstrap.md) | 0.23.6 | The renderer template bootstraps; its Hydra adapter needs a fix for OpenUSD 26.08; a no-op build and the viewport launch record each fail `ost validate` |
| [02](02-2026-09-26-v0.23.7-report-01-reverified.md) | 0.23.7 | Report 01's asks are resolved; on `core`, `renderer.install_tree` stays SKIP after its CTest passes; a `lookdev` build rewrites `strata.lock` |
| [03](03-2026-09-26-v0.23.8-report-02-reverified.md) | 0.23.8 | Report 02's asks are resolved; `core` passes `renderer.install_tree` after `ost test`; `ost lock` restores the default pin; `ost build` rewrites `strata.lock` line endings |
| [04](04-2026-09-26-v0.23.10-a-renderer-formation.md) | 0.23.10 | A renderer Formation does not resolve: `formation resolve` refuses every packaged target; `ost package` has no intent; the renderer contract's plugin path; one lock per workspace, re-pinned by `ost build` but not `ost plugin build`; no canonical `usdview` runtime |
| [05](05-2026-09-27-v0.23.11-report-04-reverified.md) | 0.23.11 | Report 04 is answered and the VRM renderer Formation resolves, locks and runs; `formation run` gives `usdview` no Python; a deep manifest directory puts Qt's platform plugin past `MAX_PATH`; a plugin carried by a component and listed beside it is not a conflict |
| [06](06-2026-09-27-v0.23.13-report-05-reverified.md) | 0.23.13 | Report 05 is answered: the Formation's declared `testusdview` command runs through the selected host Python, from any manifest directory; run evidence still has no streams, and `toon`'s `lib` and `bin` are each on `PATH` twice |
| [07](07-2026-09-28-v0.23.13-a-hydra-fed-viewport.md) | 0.23.13 | A Hydra-fed viewport: the template turns an intent's Hydra adapter off under `ost renderer viewport`, the run rewrites the intent's build tree so `ost validate` passes without the adapter, and the viewport's launch cannot add a plugin |
| [08](08-2026-09-30-v0.23.14-report-07-reverified.md) | 0.23.14 | Report 07 is answered: `ost renderer viewport --intent hydra --with <vrmImaging>` builds both adapters in a tree of its own and draws the avatar 20 of 20 MToon; the workflow tree cannot be tested, and redaction labels the transient plugin root `<project-root>` |
