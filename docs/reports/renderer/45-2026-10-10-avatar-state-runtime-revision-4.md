# Avatar-state consumer on runtime revision 4

- Date: 2026-10-10
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Runtime: local sibling working tree's experimental runtime C ABI revision 4
  headers and library
  ([usd-avatar-runtime#25](https://github.com/animu-sphere/usd-avatar-runtime/pull/25));
  canonical CY2026 OpenUSD 26.08 lookdev for the Hydra build
- Scope: following the runtime's answers to the observations in
  [integration scope §6](../../design/INTEGRATION_SCOPE_POLICY.md#6-cross-repository-observations)

## The change

`usd-avatar-runtime` resolved three observations this repository had recorded
([usd-avatar-runtime#22](https://github.com/animu-sphere/usd-avatar-runtime/issues/22),
[#23](https://github.com/animu-sphere/usd-avatar-runtime/issues/23),
[#24](https://github.com/animu-sphere/usd-avatar-runtime/issues/24)). Its
revision 4 adds a vec2 material value and retained source-sample records to
the snapshot, and grows the state view and writer table. Revision-3 consumers
are refused.

`Toon::AvatarState` pinned revision 3 at compile time, so it now requires
revision 4. Base texture offset and scale bindings expect vec2. The report 42
convention, vec3 with zero z, is refused at `Bind` as a type mismatch rather
than accepted as an alias. As with every runtime material value, a nonzero
unused component is refused. Releasing one of the two inputs leaves the
other's override active. No other mapping changes, and the adapter does not
read the new source-sample records yet. The runtime assigns runtime-to-resident
matching to a host on the renderer's side, which matches this repository's
v0.3.0 plan, so no code moves.

## Evidence

`toon-avatar-state` adds a texture-transform case: vec2 offset `(0.25, 0.5)`
and scale `(2, 3)` reach the material exactly, and releasing the offset
restores its scene value while the scale stays overridden. A vec3 offset with
zero z is refused, and so is a vec2 with a nonzero third component.

| Configuration | Result |
| --- | --- |
| avatar state, no Vulkan, Release | 13/13 CTests, including the installed component consumer |
| avatar state, Vulkan, Release | 18/18 CTests, including real runtime lifetime against the revision-4 library and the late GPU comparisons |
| avatar state with Hydra 2.0, Release | 25/25 CTests, including `toon-renderer-hydra-skinning`'s exact inbetween weight parity |

The runtime's opt-in transport host separately passes its real-avatar probe
transport with this adapter rebuilt; that evidence lives with the runtime.

## Commands and limits

```powershell
cmake --build build/avatar-runtime-fixture --config Release --target avatarRuntime
cmake --build build/avatar-state-no-vulkan --config Release --parallel 4
ctest --test-dir build/avatar-state-no-vulkan -C Release --output-on-failure
cmake --build build/avatar-state-vulkan --config Release --parallel 4
ctest --test-dir build/avatar-state-vulkan -C Release --output-on-failure
cmake --build build/avatar-state-hydra
ctest --test-dir build/avatar-state-hydra --output-on-failure
```

The runtime fixture and headers come from the runtime pull request's branch.
This change must be merged after or together with it. No provider emits texture
transforms yet, so the vec2 path has adapter evidence only. Source-latency
reporting from the retained sample records, real-avatar matching and rendering
remain v0.3.0 work.
