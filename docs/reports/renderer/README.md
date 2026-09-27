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
| [06](06-2026-09-27-vrm-formation.md) | The VRM `usdview` session as a Formation: `ost formation run` draws with `hdToon`; 12 MToon slots on the avatar with `vrmImaging`, none without; the Formation's own command waits on a Python in the session |
| [07](07-2026-09-27-mtoon-opaque.md) | `mtoon_opaque` draws: meshes bind their material, all 20 avatar draws are shaded as MToon with `vrmImaging` and none without; a value-only edit rewrites one parameter slot |
| [08](08-2026-09-27-basic-textures.md) | Basic textures: `mtoon_opaque` samples the base and shade colour textures through UVs, wrap and texture transform; the avatar's 12 MToon materials share 6 images, each uploaded once |
| [09](09-2026-09-27-gpu-skinning.md) | GPU skinning: all 20 avatar draws skinned in the vertex stage; a joint turn writes 20 joint buffers and uploads nothing else; dual quaternion skinning stays on the CPU kernel |
| [10](10-2026-09-27-mtoon-outline.md) | Inverted-hull outline: `mtoon_outline` adds the hull of 15 of the 20 avatar draws in world or screen units; widening or disabling an outline rewrites one parameter slot |
