# Extended PreviewSurface reference comparisons

- Date: 2026-10-18
- Machine: Windows x86_64, NVIDIA RTX A5000, driver 597.16
- Runtime: canonical CY2026 OpenUSD 26.08 `lookdev`, combined `viewport-usd`
- Reference: Storm, OpenGL 4.6; hdToon uses Vulkan

## Scope and conditions

Extended `scripts/evaluate_preview_surface.py` and its `testusdview` worker
after [report 75](75-2026-10-18-preview-surface-reference.md). The committed
mirrored-UV fixture, perspective camera, normalized distant light, sRGB
correction, disabled helper/dome lights and 256x256 captures remain the
comparison conditions. Storm runs at host-requested 4x MSAA, hdToon at
actual 1x/4x, asserted from its frame evidence. Renderer identity, runtime,
plugin paths and completion of every case are recorded/checked.

The worker authors original curved and grazing geometry in the session
layer. The curved patch is a 32x32-cell triangulated dome,
`z = 0.45 * (1 - x*x - y*y)`, with analytic vertex normals proportional to
`(0.9*x, 0.9*y, 1)`. The grazing patch rotates the original plane 70 degrees
around Y; its central `N.V` is approximately 0.342. Both have dielectric
roughness 0.6 and pure-metal roughness 0.8 cases. No restricted asset is used.

The backend currently clears to opaque linear `(0.05, 0.10, 0.15, 1)`.
The worker sets the same clear in usdview's process-local colour palette.
An independent corner-pixel control requires the captured clear to agree
with sRGB encoding within two levels. This avoids attributing different
backgrounds to opacity shading, without changing a renderer API or deriving
an exposure from the reference. All cases now use this matched clear.

## Measurements

Each case saves its fixed, renderer-independent measurement mask and hash.
Planar cases retain report 75's 12,288 pixels, excluding silhouette, mirrored
seam and texture transitions. Curved cases use the inner 128x128 region
(16,384 pixels); grazing cases use a central 16x112 strip (1,792 pixels),
inside both sloping projected edges. These are interior shading comparisons,
not silhouette or multisample coverage measurements.

New comparisons, at both hdToon sample counts:

| Case | Mean absolute RGB error, /255 | P95 maximum-channel error, /255 |
| --- | ---: | ---: |
| Curved dielectric / metal | 0.744 / 1.670 | 3 / 5 |
| Grazing dielectric / metal | 0.667 / 2.053 | 1 / 4 |
| Metallic R texture / roughness G texture | 0.905 / 0.536 | 2 / 2 |
| Opacity B texture, Mask cutoff 0.5 | 0.665 | 2 |
| Opacity A texture, Blend | 1.733 | 5 |
| Constant opacity 0.45 | 2.033 | 4 |
| Pure-metal constant control | 1.204 | 3 |
| Roughness 64/255 / 204/255 constant controls | 0.496 / 0.544 | 2 / 1 |

The existing MAE <= 8 and P95 <= 20 gate is unchanged. All 44 matching
comparisons passed. Four comparisons still characterize report 75's two
known differences: intermediate metalness (MAE 14.720, P95 35) and rotated
normal coordinates (MAE 35.320, P95 83). Their bounds and causes are
unchanged. `reference_equivalent` remains false for the complete set;
`passed` includes their characterization, not a claim of equivalence.

## Independent controls

One generated raw RGBA image supplies distinct quadrant channels: metallic
R is 0 or 1; roughness G is 64/255 or 204/255; opacity B is 64/255 or
191/255; opacity A is 0.2 or 0.8. Each renderer's metallic and roughness
texture quadrants must match its corresponding constant-input capture
within one level. Mask quadrants below the cutoff must match the clear,
and retained quadrants the opaque capture, also within one level. This
checks channel selection and cutoff independently of external agreement.

For hdToon, each Blend quadrant also checks the encoded result of
`linear(opaque) * alpha + clear * (1 - alpha)`, using the same renderer's
opaque capture. The gate allows MAE <= 2 and P95 <= 3 for the encoding and
inverse-encoding approximations. Measured MAE was 0.370–0.867 and P95
0.631–1.758 levels at both sample counts. This is RGB over an opaque clear,
not an output-alpha test. Storm is checked by the external image gate;
the independent linear-compositing oracle applies only to hdToon.

All 55 controls and 36 sensitivity checks passed. A local negative-control
run replaced each of hdToon 1x's roughness-texture, Mask and Blend images
with its opaque capture in a temporary copy. Each replacement failed its
corresponding independent controls; restored captures passed.

## Verification and reproduction

With the runtime's `bin` and `lib` on `PATH`, `lib/python` on `PYTHONPATH`,
and current build-tree `hdToon` resources on `PXR_PLUGINPATH_NAME`, ran:

```sh
python scripts/evaluate_preview_surface.py --testusdview <lookdev>/bin/testusdview \
    --output build/preview-reference-extended
```

The final run produced 72 captures across 24 cases, 48 comparisons, 36
sensitivity checks and 55 controls. Inputs, geometry-generating worker,
shader, backend clear source, images and masks have SHA-256 evidence.
Commands, renderer/runtime identities, logs, masks, contact sheet and
`summary.json` remain local under the output directory. Stale PNGs, worker
metadata and evidence logs are removed/reset before capture.

Ran the five existing CTests selected by
`toon-preview-surface-gpu|toon-renderer-hydra-material|toon-renderer-hydra-primvars`
in the combined `viewport-usd` tree: 5/5 passed. Python compilation,
relative documentation paths and whitespace checks passed. No renderer
library or shader changed in this work.

## Limits

This is a finite direct-light comparison with smooth controlled geometry
and planar scalar/opacity textures. It does not establish general PBR
equivalence, authored-asset or glTF tangent parity, deformation against an
external renderer, transparent RGBA preservation, MSAA edge alpha or
background compositing. Those remain separate milestone work. No release
is published.
