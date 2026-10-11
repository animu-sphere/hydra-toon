# The runtime's motion clip provider in the host composition

Later: [report 51](51-2026-10-11-v030-evidence.md) compares draw packets and
rebinding with the oracle and ages input at every endpoint.

- Date: 2026-10-11
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Runtime: canonical CY2026 OpenUSD 26.08 lookdev; `usd-avatar-runtime`
  `b8c05c1` (experimental C ABI revision 4) with its VRM USD bindings, motion
  adapter and motion USD binding; motion owners 0.5.4; `vrmSchema` 0.10.0;
  `vrmImaging` 0.10.0 lookdev package; `usdVrmFileFormat` and
  `usdVrmaFileFormat` 0.10.0 for the `.vrm` avatar and the `.vrma` clips
- Scope: real motion providers in the composition, in
  [v0.3.0](../../releases/v0.3.0.md)

## The gap

Reports 47 to 49 composed only the runtime's VRM expression/LookAt provider,
so every pose came from gaze. An avatar's body never moved, and nothing
showed a clip's pose matching UsdImaging's or reaching the viewport.

## Change

Both hosts, `toon-hydra2-avatar-provider-test` (`--motion <stage>`) and
`toon-viewport-avatar` (`--avatar-motion <stage>`), can now register the
runtime's motion clip provider ahead of the VRM provider:

- **Clip.** The motion owner's strict reader (`ReadStageClip`) reads the
  clip and source rest of the one skeleton in the motion stage. The
  runtime's VRM humanoid binding gives the avatar's target skeleton, its
  humanoid map, joint identities and root placement. The host supplies
  VRM's required bones, so a partial humanoid is the owner's diagnostic,
  not a refusal. Clip seconds are runtime seconds.
- **Order.** The VRM provider runs after the motion provider, so gaze reads
  the clip-posed head. The host evaluates nothing itself.
- **Frames.** The check walks its frames through the clip, from its first
  sample to its last. The viewport samples the clip at wall time on the
  producer thread, or at the result count at a zero rate. Either way it
  plays once, then holds the last sample.
- **Pose time.** Every result must retain the clip's pose sample. A clip pose
  carries a clip time, not a production time, so the viewport leaves pose
  latency unmeasured. Its panel shows the sample's clip time and whether it
  was held.

`avatar-motion.usda` is a committed motion stage: a three-joint humanoid
source skeleton named by VRM human bone, as an imported VRMA is, in metres.
Over one second, the spine turns each way, the head nods and the hips sway.

The check now syncs only the `geometry` and `proxy` render tags, as the
viewport does. Before, it also synced usdSkelImaging's skeleton guide mesh,
whose points Hydra poses on the CPU. Gaze barely moved it, but a clip moves
it on the Hydra side only, and the images then differed in 489 pixels.

## Evidence

The fixture:

| CTest | Result |
| --- | --- |
| `toon-renderer-hydra-avatar-providers-motion` | 18 results, every one with a pose sample; palettes within 1.1e-6 relative of UsdImaging's, weights within 0; 16 posed, 15 weighted, 3 recoloured |
| `toon-renderer-hydra-avatar-providers-motion-gpu` | 36 images, colour step 0, depth 0, no flipped pixels; after the first result, 34 joint, 16 weight and 5 material writes and no static upload |
| `toon-viewport-avatar-runtime-motion` | 48 frames at rate 0, every late read applying a result published after extraction; 49 pose samples, none held; no static upload after the first frame, 1 Hydra sync |

The owner reported `MOTION_RETARGET_MISSING_REQUIRED_BONE` for the bones the
fixture's humanoid lacks.

The seven VRMA clips of `VRMA_MotionPack`, on two local avatars
(neither committed), through the check with `--shaders`:

| Avatar | Results per clip | Palettes | Images |
| --- | --- | --- | --- |
| `AliciaSolid.usdz` | 38; 37 posed | within 0 on all seven | colour step 0, depth 0, no flipped pixels on all seven; 740 joint and 84 weight writes after the first, no static upload |
| `Seed-san.vrm` | 40; 39 posed | within 4.8e-7 relative | 5 to 31 flipped pixels per clip, at most 7 in a view, where an edge's coverage flips; 819 joint and 99 weight writes after the first, no static upload |

Both are within report 47's declared tolerances. `MOTION_RETARGET_UNBOUND_DRIVEN_BONE`
was reported for clip bones the avatars do not bind.

In the viewport, with the producer at 120 results per second and vsync on
for 600 frames, `VRMA_01` posed both avatars:

| Avatar | Late | Pose samples | Writes after the first frame |
| --- | --- | --- | --- |
| `AliciaSolid.usdz` | 595 of 600 frames | 660 taken, none held | 11,920 joint, 2,089 weight, no static upload |
| `Seed-san.vrm` | 596 of 600 frames | 1,014 taken, none held | 12,495 joint, 1,785 weight, no static upload |

## Commands and limits

The configuration is report 49's; the runtime package must provide the
`motion` and `motion_usd` components. With the `.vrma` format plugin also
registered:

```powershell
toon-hydra2-avatar-provider-test <avatar> --motion <clip.vrma> --expect-motion `
    --shaders build/avatar-viewport/backend/vulkan/shaders
toon-viewport-avatar --usd <avatar> --avatar-runtime --avatar-motion <clip.vrma>
```

All 64 CTests of the tree pass.

The clip is a bound source, not a live observation: the runtime has no live
pose intake yet, so no producer time exists for a pose. The clip plays once.
Draw packet and pipeline comparisons with the oracle, structural rebinding
in the parity host, and the present-API endpoint for expression and gaze
input remain open.
