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
