# Experimental avatar-state consumer

- Date: 2026-10-05
- Machine: Windows 11 x86_64, MSVC 14.51 / Visual Studio 18, NVIDIA RTX A5000
- Backend: Vulkan SDK 1.4.350.0, device API 1.4.329, 96x96 RGBA8/D32, 4x MSAA
- Runtime: local sibling working tree's experimental C ABI revision 3, built
  separately as a DLL; synthetic provider, no format/motion provider or OpenUSD

## Contract and mapping

`Toon::AvatarState` is optional. It includes only the runtime's public C
headers and accepts the host's function table for snapshot access/retention.
It consumes the runtime owner's [state](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/contracts/EVALUATED_STATE.md)
and [ABI](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/contracts/ABI.md)
contracts. It neither links the runtime library nor invokes evaluation. The external
host owns evaluation, runtime/library lifetime and serialization.
`RetainedAvatarSnapshot` retains before reading and replacing; failed reads
release their new reference and preserve the old one. Move and clear transfer
or release ownership. The table's supplying library must remain loaded.

The host binds opaque runtime identities to resident renderer ids and slots.
Joint TRS is composed in parent order, with metre translations converted to
the scene unit. Palette ordering, inverse binds, world-to-skeleton conversion
and skeleton-to-mesh conversion are explicit host inputs. No humanoid, gaze,
expression arbitration or inbetween interpolation is evaluated here. Already
resolved subshape values support signed weights and explicit zero.

Material bindings map owner input slots to `AvatarMaterialField`, not inferred
source names. Supported MToon fields are base RGBA, emission, shade, outline,
MatCap and rim colours; alpha, shading shift/toony, outline width and base
texture transforms. Texture offset/scale use vec3 with zero z because the
runtime contract has no vec2. Pipeline structure, texture ids and unsupported
fields require other integration work. Every deformation, material and
visibility source needs a binding; overlapping destination writes and
unsupported material models/types are rejected.

`Apply` reads one complete retained result over the latest unoverridden scene
baseline. A material input's `overridden=0` restores that field from this
baseline while other mapped inputs remain active. `Release` restores the
whole baseline. Both assign fresh revisions only to changed effective joint,
weight or material values; equal values reuse the prior immutable dynamic
arrays and revisions. They preserve geometry, skin, morph target and texture
arrays. Record copies, layout validation and changed arrays allocate on the
CPU; this prototype makes no allocation-free or measured consumer-cost claim.

Instance, frame, generation, layout ID/version and the active capability set
are checked. A reset can advance generation without rebinding an unchanged
layout; earlier generations/frames are rejected. Copied channel order,
identity, parent and type checks also detect layout changes. Host binding
epochs protect scene replacement even when numeric renderer ids are reused.
Static revision/array/count/material/texture changes require rebinding.
Invalid runtime candidates preserve output and adapter history. Errors identify the
failure category; full provider diagnostic/provenance mapping remains external.

`IdentityInfo` associates instance/frame/generation/input revision/layout and
host binding epoch with successful output. Input timestamps remain explicit
host-monotonic values from the supplied scene. The host must associate these
with the selected retained result, preserving original times when holding it.
Runtime evaluation seconds select renderer animation time and are never
inferred to be source-production timestamps.

Resolved visibility is supported in the output snapshot. A membership change
is rejected by `ApplyFastSnapshot` and must use ordinary extraction. It does
not silently become a late value write. The backend's existing late source
contract is unchanged; a host can retain the prior valid result or route a
structural candidate through preparation/extraction outside the callback.

## Evidence

`toon-avatar-state` checks palette order, metre/centimetre conversion, explicit
space transforms, signed subshape mapping, combined material inputs, equal
values, per-field and full release, visibility extraction and static resource
identity. It checks wrong instance, stale frame/generation/host epoch, changed
layout identity/channel/parent/type or capability set, changed resident
structure, malformed quaternions/values, failed rebinding and retained ownership.

`toon-avatar-state-runtime` loads the separately compiled revision-3 DLL,
registers one controlled provider and evaluates completed results through the
actual API. After releasing the producer handle, a retained result still maps.
Reset publishes generation 2 without changing layout; the earlier generation
is rejected. Both old and current results survive instance/runtime destruction
until their retained references are cleared. This exercises runtime lifecycle
and transport; the provider is synthetic and proves no VRM evaluation behavior.

`toon-avatar-state-gpu` uses one persistent renderer and an independent
ordinary-scene oracle for each MToon Opaque, Mask and Blend route. Twelve
pose/morph/material samples per route arrive through the actual backend late
callback after extracting the unchanged baseline. An analytic palette oracle
uses the known root rotation/translation and child translation; weights and
material values use ordinary scene setters. Each of 36 sample colour products
matches exactly and depths agree within 1e-6. Three releases match the baseline
colour/depth exactly. Geometry remains visible; Blend's authored lack of depth
writes is checked through colour rather than requiring opaque depth behavior.

After initial preparation each changed sample writes one palette, one weight
buffer and one material slot. A held duplicate writes none; release writes
each once and repeated release writes none. Point/topology, skin, target and
texture uploads, pipelines and targets remain fixed. There are 45 actual
late-callback frames including duplicates/releases and zero Vulkan validation
messages. These are offscreen tests, not representative viewport/parity or
source-to-display latency measurements.

`toon-avatar-state-installed` installs to its own prefix, separately builds and
runs a default `find_package(Toon)` consumer and a
`find_package(Toon REQUIRED COMPONENTS AvatarState)` consumer. The default
package imports no runtime-header/adapter target. The optional component finds
runtime headers independently and links without a runtime library. The adapter
header and export install only when the build option is enabled.

## Commands and limits

The variables below stand for the locally resolved sibling source/header
directories, built runtime DLL and Vulkan SDK root; machine-local paths are
not recorded in this report.

```powershell
cmake -S $runtimeSource -B build/avatar-runtime-fixture -G "Visual Studio 18 2026" -DAVATAR_BUILD_TESTS=OFF
cmake --build build/avatar-runtime-fixture --config Release --parallel 8
cmake -S . -B build/avatar-state-no-vulkan -G "Visual Studio 18 2026" -DTOON_ENABLE_VULKAN=OFF -DTOON_ENABLE_AVATAR_STATE=ON "-DTOON_AVATAR_RUNTIME_INCLUDE_DIR=$runtimeHeaders"
cmake --build build/avatar-state-no-vulkan --config Release --parallel 8
ctest --test-dir build/avatar-state-no-vulkan -C Release --output-on-failure
cmake -S . -B build/avatar-state-vulkan -G "Visual Studio 18 2026" -DTOON_ENABLE_AVATAR_STATE=ON "-DTOON_AVATAR_RUNTIME_INCLUDE_DIR=$runtimeHeaders" "-DTOON_AVATAR_RUNTIME_TEST_LIBRARY=$runtimeDll" "-DVulkan_INCLUDE_DIR=$sdkRoot/Include" "-DVulkan_LIBRARY=$sdkRoot/Lib/vulkan-1.lib" "-DTOON_SLANGC_EXECUTABLE=$sdkRoot/Bin/slangc.exe"
cmake --build build/avatar-state-vulkan --config Release --parallel 8
ctest --test-dir build/avatar-state-vulkan -C Release --output-on-failure
cmake --build build/morph-no-vulkan --config Release --parallel 8
ctest --test-dir build/morph-no-vulkan -C Release --output-on-failure
ost build --profile lookdev --intent viewport-usd --jobs auto
ost test --profile lookdev --intent viewport-usd
ost validate --profile lookdev --intent viewport-usd
```

All 18 Vulkan, 13 adapter/no-Vulkan and 11 adapter-disabled/no-Vulkan CTests
pass, including installed consumers. The existing Hydra viewport builds and
all 53 of its CTests and completion-bound `ost validate` pass with the optional
consumer disabled. Artifact integrity is an explained skip because no package
was created. MSVC SDK access required execution outside
the sandbox; the first sandboxed configure could not read the user's SDK path.
No sibling sources were edited by this renderer change.

Real motion/VRM provider composition, source-owner material mappings and
diagnostics, dedicated viewport integration, Hydra/direct parity and consumer
copy/allocation/latency measurements remain in the v0.3.0 roadmap. The runtime
ABI is experimental and this run does not freeze it or complete that milestone.
