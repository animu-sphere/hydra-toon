# Renderer reports

Dated runs of the renderer itself: what a change made it do, on which
machine, with which evidence. Append-only, like every report here; a later
finding gets a new report and a one-line forward note on the old one.

| Report | Subject |
| --- | --- |
| [01](01-2026-09-26-phase0-mesh-camera.md) | Renderer Phase 0 lands: scene meshes through the Hydra camera at the AOV's resolution, one pipeline, uploads only on change; a skinned avatar draws once ext computations run |
| [02](02-2026-09-26-mat-q1-material-inputs.md) | MAT-Q1 measured: canonical material values reach a classic delegate only through `vrmImaging`'s `vrm` container; a value-only change produces no Sync, and the delegate's terminal-scene-index hooks see it |
| [03](03-2026-09-26-material-sprim.md) | `hdToon`'s material Sprim: MToon selected from `vrmImaging`'s container and normalized into `ToonMaterial`; every material is PreviewSurface without the plugins |
| [04](04-2026-09-26-material-value-route.md) | Value-only material changes taken from the terminal scene index: a time move across a canonical value reaches `ToonMaterial` in `Update()`, without a Sync |
| [05](05-2026-09-26-vrm-usdview-session.md) | The VRM `usdview` session composed from packages: every VRM material is MToon in `testusdview` with `vrmImaging` and PreviewSurface without it; the Formation does not resolve |
