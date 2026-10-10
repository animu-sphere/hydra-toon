# Avatar-state inbetween subshapes

- Date: 2026-10-10
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Runtime: canonical CY2026 OpenUSD 26.08 lookdev for the Hydra comparison;
  local sibling working tree's experimental runtime C ABI revision 3 headers
- Scope: mapping the runtime's resolved blend-shape weights onto resident
  subshape slots in
  [v0.3.0](../../roadmap/v0.3.0.md#evaluated-state-adapter-and-representative-avatar-evidence)

## The gap

The runtime publishes one resolved weight per `(mesh_id, target_id)`; its USD
binders use the mesh path and the `skel:blendShapes` token as that identity
([evaluated state](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/contracts/EVALUATED_STATE.md),
[expression binding](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/architecture/VRM_EXPRESSION_USD_BINDING.md)).
A resident mesh's morph weights are per subshape instead: usdSkelImaging
turns each shape weight into weights for the shape and its inbetweens by
piecewise-linear interpolation. Report 42's adapter copied one value into one
slot, so a shape with inbetweens could not reach the Hydra path's weights,
and no static host binding could make it: the result depends on the value.

## Change

`AvatarMorphBinding` keeps its primary slot and gains an optional list of
`AvatarInbetweenSlot` entries, each a resident slot and the shape weight at
which it applies fully, as `DescribeResidentTargets` (report 43) reports them.
At `Bind` the adapter sorts the rest at 0, the inbetweens and the primary at 1
into knots and refuses what usdSkelImaging would drop: a non-finite position,
or one within 1e-6 of the rest, the primary or another inbetween. Every slot,
primary or inbetween, joins the existing overlap check. At `Apply` it zeroes
the shape's slots and repeats `UsdSkelImagingComputeBlendShapeWeights` in its
float arithmetic: the pair of knots around the weight, or the outermost pair
beyond them, interpolates, and a weight within 1e-6 of a knot writes only that
knot's slot. A shape without inbetweens still receives its weight unchanged,
so existing bindings, including the runtime's probe host, keep their meaning.

This is UsdSkel deformation, which the Hydra path receives from
usdSkelImaging; no expression, format or humanoid semantics are added, and
core still receives only evaluated subshape weights.

## Evidence

`toon-avatar-state` binds a shape with inbetweens at -0.5 and 0.5 beside a
plain shape over nonzero baseline weights. Eight weights from -1 to 1.5,
including both knots and extrapolation on either side, give hand-worked slot
values exactly, and the plain shape keeps its own slot. Positions at 0, at 1,
within 5e-7 of 1, coincident, NaN, past the weight count, on the shape's own
primary slot or on another shape's slot are refused without replacing the
existing binding.

With `TOON_ENABLE_AVATAR_STATE` in a Hydra build, `toon-renderer-hydra-skinning`
now builds bindings from the delegate's description of `skinning.usda`'s
linear quad: `lift`'s primary and its inbetween at 0.5, `tilt` and `plain`. For
twelve lift weights from -1 to 2, with `tilt` at 0.3 and `plain` at -0.7, the
weights are authored on the SkelAnimation and synced through Hydra, then the
same values reach the adapter as runtime shape weights. Its subshape weights
equal usdSkelImaging's exactly at every step. With the interpolation disabled
the comparison fails at the first step.

The fixture's dual quaternion quad is skinned by usdSkelImaging's CPU kernel,
which rewrites its points and normals on every animation edit. The adapter
treats that as a scene change and requires rebinding, so the comparison
rebinds the same bindings over each synced frame. A scene whose Hydra
evaluation keeps animating a CPU-skinned mesh therefore cannot hold one
binding; this is existing behavior, not changed here.

## Commands and limits

```powershell
cmake --build build/avatar-state-no-vulkan --config Release --parallel 8
ctest --test-dir build/avatar-state-no-vulkan -C Release --output-on-failure
cmake --build build/avatar-state-vulkan --config Release --parallel 8
ctest --test-dir build/avatar-state-vulkan -C Release --output-on-failure
# In a Visual Studio x64 environment, with the lookdev toolchain:
cmake -S . -B build/avatar-state-hydra -G Ninja -DCMAKE_TOOLCHAIN_FILE=<lookdev toolchain> -DCMAKE_BUILD_TYPE=Release -DTOON_ENABLE_HYDRA2=ON -DTOON_ENABLE_VIEWPORT=OFF -DTOON_ENABLE_AVATAR_STATE=ON -DTOON_AVATAR_RUNTIME_INCLUDE_DIR=<runtime headers>
cmake --build build/avatar-state-hydra
ctest --test-dir build/avatar-state-hydra --output-on-failure
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
```

All 13 adapter/no-Vulkan and 18 Vulkan CTests pass, including the
real-runtime lifetime, GPU and installed-consumer checks. All 25 CTests of
the Hydra build with the consumer pass, as do all 53 of the ordinary
viewport-usd build with it disabled, and completion-bound `ost validate`;
artifact integrity is an explained skip because no package was created.

The Hydra build is the first single-configuration (Ninja) tree to run
`toon-avatar-state-installed`. Its installed consumer was configured without
a build type and so compiled for Debug against the Release install; the check
now passes the tested configuration as `CMAKE_BUILD_TYPE`, which
multi-configuration generators ignore.

This compares subshape weights with usdSkelImaging on one controlled fixture.
It does not bind a real avatar's runtime results, render them, measure cost
or latency, or establish full Hydra/direct parity; those remain in the
v0.3.0 roadmap.
