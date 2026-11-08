# Avatar state provenance export (2026-11-08)

Category: adapter integration over generated values, Windows x86_64, MSVC
19.51 Release, Python 3.13.14, experimental runtime ABI 5. Vulkan, Hydra,
viewport and format providers were disabled; no GPU was used.

The diagnostic writer preserves capability IDs and versions, and every
retained sample's source, actor, channel, evaluator and affine-clock numbers,
including runtime sample seconds. The canonical format and validation rules
belong to the runtime owner's
[parity contract](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/architecture/OUTPUT_PATHS.md#4-parity-evidence).
The public C ABI and ordinary Apply path are unchanged.

The six-state CLI probe carries a version-7 capability and three samples
sharing a channel across two sources and two actors. It includes a selected
input with an empty evaluator, escaped UTF-8 identities and nonidentity clocks.
The independent runtime-owned oracle found 0 differences at default tolerances,
and all 17 delivery/identity/visibility/provenance controls failed comparison.
The runtime's 48 Python tests cover matching, missing fields, duplicate identity,
invalid mappings and capability version changes independently of the writers.

All four headless avatar-state CTests passed: adapter, CLI export, installed
consumer and real-runtime retained capture. The latter used the freshly built
runtime DLL, with retained use after reset/destruction and unchanged capture
after rejected delivery. The runtime's seven configured core/consumer CTests
passed as well. Both documentation checkers and `git diff --check` passed.

This measures generated Direct-consumer receipt and serialization. The runtime
owns format/oracle/session-writer checks; the renderer owns capture and conversion
rejection. It establishes no Linux, hosted CI, resident VRM/MMD viewport export,
Hydra publication/parity, pixel comparison or performance measurement.
