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
| [11](11-2026-09-27-expression-bake.md) | An expression bake played in `testusdview`: each time move rewrites the MToon slot once and uploads nothing; the bake's emission is on screen with `vrmImaging` and absent without it |
| [12](12-2026-09-28-published-vrmimaging.md) | The VRM Formations pin the published `vrmImaging` 0.10.0 from GHCR; each of reports 06–11's runs gives its report's numbers |
| [13](13-2026-09-28-host-session-formation.md) | The VRM host session is a Formation in the repository, pinning published packages only; its command checks MToon on the probe stage, and the published `toon` 0.1.0 draws the avatar with report 12's numbers |
| [14](14-2026-09-28-mtoon-rim.md) | MToon's rim: MatCap, the parametric rim and the rim multiply texture in both MToon pipelines; 7 avatar materials share one MatCap image, uploaded once; a rim edit rewrites one slot per material |
| [15](15-2026-09-28-mtoon-transparent.md) | MToon transparency: Blend draws through `mtoon_transparent` in render-queue order, with depth only under `transparentWithZWrite`; the avatar's 4 Blend materials are 5 of 20 draws, and OPAQUE gives report 14's image back exactly |
| [16](16-2026-09-28-msaa.md) | Anti-aliasing: 4x MSAA resolved as the scene pass ends, Mask by alpha to coverage; on the avatar 4x changes 8,331 edge pixels, and 1 sample gives report 15's image back exactly |
| [17](17-2026-09-28-outline-meters-per-unit.md) | The scene's unit: a world-coordinates outline is metres, divided by `toon:metersPerUnit`; the avatar in a stage of centimetres draws as in metres at 0.01, 7 pixels apart, and loses its outlines at 1 |
| [18](18-2026-09-28-outline-depth-bias.md) | The hull's depth bias: `mtoon_outline` is pushed back by one depth step and the slope's worth, so a hull at its surface's depth loses to it; of report 17's 2,399 skirt pixels 2,026 return to the surface, and in metres 93 specks of outline colour leave the hair, chest, hands and frills |
