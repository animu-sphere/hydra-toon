# Accepted avatar state export (2026-11-08)

Category: adapter integration over generated values, Windows x86_64, MSVC
19.51 Release, Python 3.13.14. The consumer was built with Vulkan, Hydra,
viewport and format providers disabled, against experimental runtime ABI 5.
No GPU was used.

The diagnostic `AvatarStateAdapter::Apply` overload captures the input the
consumer accepted after binding/conversion checks, before representation
mapping in the report: parent-local TRS, morphs, material overrides, UVs,
visibility, capabilities and source-sample fields. Ordinary Apply calls do no
serialization. Capture does not evaluate, advance simulation, author USD or
reverse-engineer values from palettes.

`toon-avatar-state-test --state-output <file>` emits six accepted states with
two instances whose handles exceed 2^53, a reset with repeated frame IDs,
recreation with the same layout, invisible/visible membership, primary UVs,
animated materials/morphs and baseline restoration. UTF-8, quote and backslash
identities survive JSON serialization. Refused foreign instances and a stale
generation leave the prior capture unchanged; duplicate deliveries give the
same capture. Instance handles follow the runtime owner's canonical integer
representation, including values larger than 2^53; Python's JSON reader
preserves them exactly. Input revision is exported and compared as well.

The runtime-owned `scripts/check_consumer_state.py` compares this output with
an independent Python oracle: six states, 0 differences, using the owner's
default `2e-6` tolerance and exact float32 material comparison. All seven
channel omissions, stale reset and recreated-instance substitutions, and a
visibility bit change are detected (10 negative controls). These checks use
the consumer's exported input, not a producer-side export as their actual.

The real-runtime regression also captures retained snapshots from a freshly
built ABI-5 runtime DLL, preserves the accepted report on a stale-generation
refusal, and reproduces the report after runtime destruction. Its evaluator
is generated; it is not a VRM or MMD session.

All four headless `toon-avatar-state*` CTests passed: ordinary adapter tests,
CLI export, the installed consumer, and the real-runtime regression. The
runtime's seven core/consumer CTests and 37 Python comparison tests also
passed after integration with its PR #95, preserving that change's integer
instance representation, input revision and sequence validation.
The renderer documentation checker passed over 167 Markdown files. Local
compiler probes needed Release configuration and embedded debug information
to avoid a PDB manager mismatch; the successful builds use MSVC Release.

This is generated Direct-consumer receipt and serialization evidence.
The runtime owns its comparison tool/oracle and native session writers;
`hydra-toon` owns capture and mapping/rejection tests. No resident VRM/MMD
viewport capture, runtime Hydra publication, Direct/Hydra comparison, pixel
comparison, consumer-cost measurement or Linux run is established here.
