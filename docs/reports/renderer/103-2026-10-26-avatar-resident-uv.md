# Automatic resident primary UV matching

Date: 2026-10-26. Renderer base `70811e4` plus this change. Windows x64,
MSVC 19.51 Release, OpenUSD 26.08, current experimental runtime ABI-5 headers.
Generated resources only. Follows [report 102](102-2026-10-26-avatar-primary-uv.md).

## Boundary

The Hydra mesh retains source-point identities through both corner splitting
and generated tangent seams. `HdToonResidentMesh::primary_uv` exposes the source
point count and one source identity per resident vertex for complete vertex or
varying `st`. Other interpolation domains and incomplete arrays expose no map.
Point-count, interpolation and seam edits refresh or remove the description.

`HdToonMatchAvatarTargets` matches opaque mesh paths and dense source identities
to runtime slots independently of their order. Duplicates, sparse identities,
ambiguous/missing paths, mismatched counts, invalid resident source indices and
unrepresented source points produce per-mesh mismatches without partial UV
bindings. Valid outputs of other meshes still match. The existing state adapter
validates final scene compatibility and finite values before publication.

## Evidence

The rebuilt `toon-renderer-hydra-skinning` and `toon-renderer-hydra-primvars`
CPU checks pass. Generated mirrored MikkTSpace seams and indexed authored
tangent corners preserve their independently expected source maps; reversed
runtime slot orders map back to the expected sources. Removing seams, growing
the point array without matching UVs and switching vertex/face-varying/varying
interpolation refresh or remove the map. Multi-mesh matcher checks cover
refusal beside valid outputs and a missing runtime array.

Automatically matched no-tangent meshes bind and publish absolute UV values
by source identity, then Release restores the Hydra baseline. Resident tangent
bindings still refuse. The focused primvar GPU regression also passes, covering
the existing authored/generated tangent and seam rendering oracles; the new
UV metadata does not change those geometry streams.

## Reproduction and limits

From an initialized x64 MSVC developer shell, reuse the configured Hydra
build with avatar state enabled and current runtime headers:

```powershell
cmake --build build/avatar-state-hydra --config Release `
  --target toon-hydra2-skinning-test toon-hydra2-primvar-test toon-renderer-shaders --parallel 1
ctest --test-dir build/avatar-state-hydra -C Release `
  -R '^toon-renderer-hydra-(skinning|primvars|primvars-gpu)$' --output-on-failure
```

The shader target is required when reusing an older build directory. The
initial GPU run hit a Vulkan teardown failure with its stale/incomplete shader
set; rebuilding all shader outputs made the focused GPU check pass.

This establishes resident mapping and generated no-tangent CPU consumption.
It does not establish coherent runtime tangent regeneration, composed
MMD-to-GPU playback, runtime Hydra-publication parity, additional UV channels
or representative avatar acceptance. Tangent-bearing meshes can be matched
but remain inadmissible to the UV state adapter until regeneration is supplied.
No runtime ABI, evaluator algorithm or dependency pin changed.
