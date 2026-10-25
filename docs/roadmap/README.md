# Roadmap

Unfinished work only. The [capability matrix](../reference/CAPABILITY_MATRIX.md)
owns implementation status; [release records](../releases/) own released
achievements. Maintenance rules belong to the
[documentation guidelines](../contributing/documentation.md).
This page is the navigation entry point; no `current.md` is needed.

## Sequence

| Milestone | Focus | Work |
| --- | --- | --- |
| v0.8.0 | Host integration: the RGBA product handed to a host on the GPU, Vulkan, Tessera first | [v0.8.0.md](v0.8.0.md) |
| Later, on demand | Platforms, materials and measurement-driven candidates | [later.md](later.md#platform-and-gpu-coverage) · [candidates](later.md#after-v070) · [unassigned work](later.md#not-yet-in-a-milestone) |

Finish the current scope before adding a feature family. Settle shared scene
semantics on Vulkan before reproducing them on WebGPU, and preserve toon
fidelity throughout. Ordering principles belong to
[design policy §25](../design/DESIGN_POLICY.md#25-implementation-phases);
PreviewSurface comparison limits belong to
[material policy §12.1](../design/MATERIAL_POLICY.md#121-reference-comparison-and-stop-rule).
