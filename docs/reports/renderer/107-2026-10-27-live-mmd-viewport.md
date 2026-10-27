# Live PMX/VMD physics in the dedicated viewport

Date: 2026-10-27. Windows x64, MSVC Release, OpenUSD 26.08, Vulkan,
NVIDIA RTX A5000. Local installed MMD, runtime, Stage Runner and Jolt packages.
This is a scoped application host and renderer-consumer integration.

## Data flow

The optional `toon-viewport-mmd` links the installed runtime's
`AvatarRuntime::avatarMmdSession`. That reusable native session was extracted
from the runtime-owned usdview host; the Python wrapper delegates to it.
Original PMX/VMD are read directly. MMD owns binding and pre/post controls,
runtime owns fixed ticks and the shared Step barrier, and Jolt owns simulation.
No recording or derived USD motion is read.

The complete final joint, vertex/group morph and primary-UV layout is matched
to resident resources. Physics advances before extraction. Late GPU reads
consume retained output and never evaluate or step. Changed UVs therefore
pass through extraction and coherent tangent regeneration. USD authoring is OFF.

## Evidence

- The native session test checks 720 ticks over three generations, exact replay,
  pause, tick/frame identity, immutable output through reset/close and unchanged
  root/session USD. The runtime's configured suite passes 22/22, including the
  existing eight usdview regressions.
- A generated morph/UV PMX and generated mixed VMD were drawn for 120 frames:
  one Hydra sync, 238 ticks, one resident skin, one morph and one UV mesh,
  zero mismatches/refusals, and zero bodies on teardown. The viewport tests
  exercise reset/scene replacement and cached/uncached resident stream agreement.
  All 15 selected renderer regressions pass, including existing skinning,
  consumer GPU, playback and file-replacement checks.
- A private original PMX/VMD was displayed in the actual window: 177 bodies,
  one resident skin, 94 vertex morph bindings, one UV mesh, 20 MMD draws and
  10 textures. The initial bounded run completed 949 ticks in 120 presented
  frames with zero mismatches/refusals and one Hydra sync. Assets, names,
  screenshots and raw diagnostics remain local and ignored.
- In the actual window Pause held tick 2291 and its pose across observations;
  Reset returned to paused tick 0; Stop showed zero bodies and the authored
  pose; Play rebuilt the world and resumed. Window close reported zero bodies.
  The user's final visual acceptance decision for this new route remains open.

## Performance and limits

The private model initially presented about 7–8 fps. A 1,024-frame rolling
window measured 123.4 ms average CPU time in the combined runtime/scene section,
0.95 ms GPU scene time and 1.45 ms total GPU work. These are separate pipeline
measurements. With 120 Hz ticks, at most eight catch-up ticks per request and
retained backlog, simulation time falls behind wall time in this slow session.

The host now measures runtime milliseconds per tick separately from resident
conversion milliseconds per result. An unchanged retained result over an
unchanged renderer baseline reuses its conversion, avoiding repeat validation,
UV mapping and palette construction at late reads and while paused. New result
identities, scene revisions, reset and rebind invalidate reuse. Further work
must preserve final-state agreement and measure each cost; real-time performance
on the private model is not established.

A subsequent 120-frame private run measured 13.84 ms per runtime tick and
7.08 ms per resident conversion, with 119 conversions and 119 reuses. It
completed 947 ticks, with 2.73 ms mean GPU scene time and 4.93 ms total GPU
work. This samples different motion times than the earlier rolling window;
it is cost separation, not a controlled before/after FPS comparison. Runtime
evaluation dominates the eight-tick catch-up requests. The next performance
work should profile dense-state copying/validation and owner evaluation there,
then resident conversion, while retaining immutable snapshots and output parity.

Motion holds its last key; live physics has no automatic loop, seek or speed
control. Reset displays the authored pose until the first resumed tick; bodies
still initialize at the evaluated VMD frame-zero pose. Spring opt-ins remain OFF.
Existing unsupported-morph admission and SDEF approximation remain. Material
morphs, additional UV output, private Hydra/direct numerical or pixel parity,
multiple avatars and representative L3 acceptance remain open.

See the [viewport guide](../../guides/BUILDING.md#live-mmd-physics-in-the-dedicated-viewport)
for build/run and generated test inputs. The
[runtime session contract](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/architecture/MMD_SESSION.md)
owns ticks, resource lifetime and snapshot borrowing.
