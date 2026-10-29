# Live MMD material delivery

Date: 2026-10-29. Windows x64/MSVC Release, OpenUSD 26.08, Vulkan,
NVIDIA RTX A5000. The host consumes an installed runtime `mmd_session` package.
Inputs are generated PMX/VMD and a synthetic two-by-two texture; no private
asset or capture is committed.

## Boundary

Runtime binds the direct-PMX stage-contract v2 materials and calls the installed
owner material query on raw final channels. It supplies six canonical inputs
and separate vec4 multiply/add values for each sampled texture/sphere/toon
contribution. Renderer target matching uses material paths and explicit input
identities. The renderer maps resolved values into `ToonMaterial`; it evaluates
no group, morph, control or physics operation.

The canonical fields are `inputs:mmd:material:{diffuseColor,specularColor,
specularPower,ambientColor,edgeColor,edgeSize}`. Sampled coefficients use opaque
runtime identities `mmd:{texture,sphere,toon}Tint:{multiply,add}`. They are not
new USD inputs. The existing `--mmd-morph-tints on` requests full.fx realization;
the default preserves MMD's untinted reading. Diffuse alpha below one promotes
an opaque MMD material into Blend with a structural revision. Releasing the
override restores the authored draw membership.

## Evidence

Six focused CTests pass:

- `toon-avatar-state` checks all 40 material components, MMD/MToon admission,
  alpha draw membership and baseline release.
- `toon-avatar-state-gpu` also compares the complete mapped material against a
  manually populated reference at 96x96 / 4x MSAA. Images agree exactly;
  toggling sampled tints changes pixels, and Vulkan validation reports zero
  messages. Existing VRM state/GPU regressions in that executable pass.
- `toon-mmd-runtime-cache` and `toon-viewport-mmd-runtime` preserve the earlier
  generated joint/morph/UV, lifecycle and bounded-window evidence. The old
  empty material declaration is now admitted.
- `toon-mmd-material-resident` drives two textured materials with all-material
  multiply, material-specific add and group/direct VMD weights. All 24 inputs
  bind without mismatch. Cached/uncached material values and revisions agree,
  reset replay is exact over two 240-tick generations, pause holds state, zero
  restores the baseline, and stop/restart succeeds.
- `toon-viewport-mmd-material` presents 120 frames at 320x320 with the generated
  material motion and tint realization enabled; all 24 inputs bind, with zero
  runtime refusals or target mismatches.

Generate the shared inputs with the runtime's
`tools/generate-mmd-material-fixture.py --owner <mmd-repository> --out <directory>`.
Configure `TOON_MMD_MATERIAL_TEST_PMX` and `TOON_MMD_MATERIAL_TEST_VMD` alongside
the existing MMD viewport acceptance inputs/plugins. The runtime's capability
matrix owns its direct-owner and canonical USD publication evidence.

This establishes generated live material transport and GPU realization. It
does not establish representative private-avatar material correctness, full
Hydra/direct image parity, additional UV support or MMD L3 acceptance.
