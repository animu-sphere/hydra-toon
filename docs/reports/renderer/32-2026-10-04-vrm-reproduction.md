# VRM reproduction and the v0.2.0 practical baseline

- Date: 2026-10-04
- Environment: Windows 11 x86_64, MSVC 14.51, NVIDIA RTX A5000,
  Vulkan loader 1.4.321; OpenStrata 0.23.14, canonical CY2026 OpenUSD
  26.08 `lookdev`, Python 3.13 on the Hydra host
- VRM imaging: published `vrmImaging` 0.10.0,
  `sha256:894fd616f1414d5b393ff0d50abbf7ef18cb603562133c72e642493b48f71667`,
  with local matching `usdVrmFileFormat`, `usdVrmPackageResolver` and
  `vrmContainer` bundles
- Reference: three-vrm 3.5.5, three 0.180.0, browser WebGL2 through ANGLE
  on the RTX A5000, D3D11, four samples; dependencies installed locally
- Occasion: closing the remaining reproduction and temporal-quality work
  for [v0.2.0](../../releases/v0.2.0.md)

## Acceptance decision

The maintainer accepts the measured 4x MSAA default as v0.2.0's practical
baseline, with residual subpixel variation as a known limitation. This
changes the release acceptance in [design policy §10](../../design/DESIGN_POLICY.md#10-outline),
not the rasterizer. [Report 31](31-2026-10-04-outline-temporal.md) remains
the finite-supersampling evidence: 4x reduces the nominal 0.75 px rotating
hull's temporal residual by 68–69% against 1x, and the avatar close-up's by
68%. It does not establish flicker-free animation or analytical coverage.

## Matched reference captures

`scripts/evaluate_vrm_reproduction.py` captures raw VRMs in the Hydra-fed
viewport at 800×900, 4x MSAA, without the overlay. The viewport exports its
final OpenGL-convention camera through `--camera-output`. Each asset has
full-body and close-up views; the close-up uses pan (0,155), dolly 10.
`scripts/vrm_reference.html` loads those same bytes through three-vrm and
uses those matrices without independently framing the avatar.

The reference uses a camera-relative white directional light at
(0.25,0.5,1) and uniform ambient. Intensities π and 0.25π compensate
three's Lambert 1/π convention, matching Toon's unit key and 0.25 ambient.
Both outputs encode sRGB, have no tone mapping, and use the same linear
clear colour (0.05,0.10,0.15). VRM 0.x's root orientation is normalized with
`VRMUtils.rotateVRM0`, matching the importer. Captures evaluate the raw
rest pose: no spring-bone, constraint, humanoid normalization or expression
update is performed in the reference. No shader compilation errors were
recorded. The [guide](../../guides/BUILDING.md#local-vrm-reproduction-comparison)
gives the commands; the model files and browser modules are served only
from loopback, and nothing is uploaded.

`scripts/vrm_reproduction_usdview.py` uses `testusdview`, a session-layer
camera reconstructed from the same matrices, a fixed 800×900 physical
framebuffer, 4x MSAA and the stage's unit. Selection, bounds, HUD and the
post-render host axis are suppressed. Registered runtime/plugin paths
compose the same renderer and VRM plugins that OST supplies. This is a
deterministic host capture, not a claim about interactive session playback.

The asset hashes are:

| Asset | SHA-256 |
| --- | --- |
| AliciaSolid.vrm, VRM 0.x | `237bb02efadf8c13a114af91dd8e860173081457dee87017e51011c448d05dc2` |
| VRM1_Constraint_Twist_Sample.vrm, VRM 1.0 | `12c2b97e95e700783a6a550dc0eee2d7880aeedccef9ae67bc4c5a2f0f2631a2` |

Alicia has 12 MToon materials, 20 GPU-skinned/authored-normal draws, five
transparent draws, 15 hulls and seven textures. The VRM 1.0 asset has 13
materials and 13 GPU-skinned/authored-normal draws, seven hulls and 18
textures. The importer warns that some inverse bind matrices conflict
between skins and retains the first; these rest-pose captures do not
establish correct constrained animation. Mask coverage is established by
the controlled fixtures in [report 27](27-2026-10-01-antialiasing-quality.md),
not by a claim that these two avatars exercise every material feature.

## Differences

Foreground is the union of pixels differing from each image's clear colour
by more than three 8-bit channel levels. RGB MAE is measured in encoded
sRGB levels, not linear luminance or a perceptual score. P95 uses the largest
channel difference at each foreground pixel. IoU compares those foreground
masks. Background is excluded; mismatched silhouettes remain in the union.

| Asset / view | three-vrm MAE /255 | P95 channel difference | Foreground IoU | usdview MAE /255 |
| --- | ---: | ---: | ---: | ---: |
| Alicia / full | 4.277 | 31 | 0.996672 | 0.142 |
| Alicia / close | 1.349 | 12 | 0.999044 | 0.108 |
| VRM 1.0 / full | 2.587 | 23 | 0.998450 | 0.317 |
| VRM 1.0 / close | 1.011 | 1 | 0.999459 | 0.123 |

Three-vrm's fraction of foreground pixels with a maximum channel difference
above 16 is respectively 14.34%, 3.59%, 6.04% and 2.27%. The images retain
the same rest-pose proportions, material colours and major outline features;
fine edges and some surface/transparent pixels differ. These statistics do
not isolate mip selection, alpha sampling, draw sorting or VRM 0.x material
normalization as individual causes. No universal equivalence threshold is
claimed. usdview's P95 difference is one level, except the VRM 1.0 full view
at two; its linear RGBA8 AOV has a different quantization path from the
viewport's sRGB swapchain.

## usdview helper-light regression

The first host captures saturated the avatar white. Hdx advertises its
application `GlfSimpleLight` as a distant light to non-Storm delegates and
converts its intensity to 15,000. Reading that as an authored UsdLux light
both overwhelmed MToon and suppressed the camera-light fallback.

`HdToonLight::Sync` now recognizes the typed `HdLightTokens->params` payload
and excludes those helpers from the core world. It uses neither a path-name
test nor an intensity cutoff. Authored UsdLux lights continue to work;
application helper-light controls are unsupported. The adapter privately
links OpenUSD `glf` for that type, including its OpenGL import dependency;
the renderer backend remains Vulkan. `toon-renderer-hydra-lights` checks
helper distant/dome lights beside an authored rig and with that rig removed.
The four host captures above were taken after the fix.

## Continuous outline sequences

`scripts/evaluate_outline_sequence.py` captures every presented frame using
`--capture-sequence`, with outlines on, off, and an independent on repeat.
Two Alicia retargeted stages, VRMA 01 and 02, each cover 61 consecutive USD
time codes 29 through 89, at full-body and close-up camera distances. Extent
is 640×720, four samples; close-up pan (0,124), dolly 10. Every one of the
244 outlined frames matches its independent repeat byte for byte; all 61
frames per sequence are distinct. Twelve runs produce 732 captures.

Each run uploads 20 point/topology buffers and 20 skins only once, 13
material slots and seven textures, and writes 1,220 poses. Motion therefore
does not upload geometry. Mean successive signed outline-field changes,
in linear luminance, are 0.000608 / 0.003467 for VRMA 01 full/close and
0.000506 / 0.002801 for VRMA 02 full/close. They include intended motion
and changing visible coverage, not a flicker residual against ground truth.
The second close-up sequence moves across the frame boundary, so its
integrated signal range is not a stability score. No temporal filter was
added. Capturing every frame perturbs timing: use the uncaptured cost runs
in reports [24](24-2026-09-30-outline-stability-cost.md) and
[27](27-2026-10-01-antialiasing-quality.md) for performance evidence.

Logs, hashes, camera matrices, comparison sheets, per-frame PPMs and preview
GIFs remain under ignored `build/vrm-reproduction/` and
`build/outline-sequence/`. No model or capture is committed. Remaining
limits are finite asset/motion coverage, thin-feature/shading variation,
no temporal AA, no reference constraint/spring/expression playback, and the
existing host readback path. This report closes the milestone under the
explicit practical-baseline decision; it does not supersede the measured
limitations of report 31.

## Release-gate verification

Plain CMake build/CTest passes 6/6. OpenStrata `core` passes 6/6,
`hydra` passes 13/13 including the host and install checks, and
`viewport-usd` passes 34/34 including every-frame motion/camera capture
comparisons. `ost validate` passes for all three intents. GPU tests ran on
the RTX A5000; Vulkan validation remains clean. The viewport's final rerun
includes the helper-light fix and the sequence's manual-capture guard.

The local `hydra` package contains 38 files, `hdToon` and the scene shaders,
with a manifest, SPDX SBOM and checksums. Two packagings yield the same
archive digest:
`sha256:335900ac44f8cf4dd94a413ddd3f592f631090dffc1eff01e52cc0622775d3f7`.
This is a workstation artifact, not a published Formation pin.
`release_version.py --tag v0.2.0` and release-note rendering pass. The
viewport intent's pre-existing multiline inline TOML table was normalized
to TOML 1.0 syntax so the release preflight's standard Python parser accepts
it, without changing the build intent. The required dry run on `main`, tag,
registry push, draft publication and published-package Formation check
remain release-lane actions; they are not implied by these local gates.
