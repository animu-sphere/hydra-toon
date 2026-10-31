# WebGPU stage 4: acceptance

- Date: 2026-10-25
- Machine: Windows 11 x86_64, NVIDIA RTX A5000, driver 597.16 (Direct3D 12
  driver 32.0.15.9716); MSVC 14.51 / Visual Studio 18, CMake 4.4.3
- Tools: `slangc` 2026.8 from Vulkan SDK 1.4.350.0; Dawn release
  `v20261008.181332` and its `tint`, DXC release `v1.9.2609`, both as
  pinned in [report 91](91-2026-10-24-webgpu-stage-1.md); `ost` 0.23.14
- Source: branch `claude/v070-webgpu-stage-4` from `main` at `e7b686e`
- Occasion: stage 4 of the [WebGPU policy](../../design/WEBGPU_POLICY.md#10-stages),
  the acceptance item of the [v0.7.0 roadmap](../../releases/v0.7.0.md)

## TL;DR

**Stage 4 is in, and the milestone's done-when condition holds on this
machine. A `webgpu-usd` intent builds the Hydra adapter beside the WebGPU
backend, so `toon-viewport --backend webgpu --usd <stage>` draws USD
stages through Dawn next to the Vulkan session from one tree. Every
committed USD stage, twenty of them, agrees with Vulkan within one linear
step at 1x and 4x over both backgrounds, with the same scene, upload,
draw, hull and shadow-view statistics; six of them are now CTests. With
`vrmImaging` and `mmdImaging`, local AliciaSolid, YYB Miku and a kitchen
holding both agree within one step at 1x but for a few pixels on mesh and
hull edges, and at 4x differ on at most 4e-4 of their pixels, on edges;
through Dawn's Vulkan adapter every one of those frames is byte-identical
to the Vulkan backend's, so the differences are Direct3D 12's
rasterization of the same draws. On the `shadow-cost` fixtures the
backends' GPU passes are within 0.05 ms of each other; on AliciaSolid
WebGPU's GPU frame is 0.22 ms where Vulkan's is 0.61 ms, because the
Vulkan backend keeps vertex, skin and morph buffers in host-visible
memory that a discrete GPU reads over PCIe.**

## 1. The `webgpu-usd` intent

No intent built the Hydra adapter with the WebGPU backend, so the WebGPU
viewport drew only the bootstrap scene ([report 93](93-2026-10-25-webgpu-stage-3.md#7-what-remains)).
The project chose a new intent over widening `webgpu`:

```toml
[build.intents.webgpu-usd]
cache = { TOON_ENABLE_HYDRA2 = ..., TOON_ENABLE_VIEWPORT = ..., TOON_ENABLE_WEBGPU = ... }
```

`ost build --profile lookdev --intent webgpu-usd` builds it. The
`webgpu` intent stays OpenUSD-free at the `core` profile, and the `hydra`
and `viewport-usd` intents and the published package do not change. No
code changed: the viewport already took `--usd` and `--backend` apart, and
the WebGPU session drew its first stage, `shadow-cost.usda`, with the
Vulkan session's statistics on the first run.

## 2. USD parity

`adapters/viewport/tests/check_webgpu_usd_parity.py` draws one stage on
both backends at 1x and 4x, over the solid and checkerboard backgrounds,
captures the presented image and the straight foreground PNG, and
compares them per channel. It requires the lines the viewport prints for
the scene summary, uploads, draw calls, hull draws, late latch, shadow
views and animation to be equal, except `pipeline_binds`: the Vulkan
backend binds the unlit pipeline whether or not an unlit draw follows
(`vulkan_scene.cpp`), WebGPU binds on demand, so a frame without unlit
draws counts one more bind on Vulkan. That is how each backend records,
not what it draws; both counts are kept in the record.

**Linear steps.** The scene target is linear RGBA8 on both backends and
the composite encodes it to sRGB. In the dark one linear step spans up to
two encoded ones: `preview-reference`'s one differing pixel is 96 against
98 encoded, linear 30 against 31. The comparison therefore measures each
colour channel in linear steps, as far apart as the nearest linear values
that encode to the two codes, and keeps the encoded figures beside them.
WGPU-Q1's limits apply to the linear steps, as they did offscreen, where
the target is read before encoding.

**Committed stages.** Every `.usda` beside the viewport's and the Hydra
adapter's tests, 640 × 360, four frames, without plugins: largest linear
difference / share of pixels more than one step apart / largest encoded
difference, over both backgrounds and both products.

| Stage | 1x | 4x |
| --- | --- | --- |
| `preview-reference` | 1 / 0 / 2 | 1 / 0 / 2 |
| `avatar-provider` | 1 / 0 / 1 | 1 / 0 / 1 |
| `shadow-cost` | 0 / 0 / 0 | 1 / 0 / 2 |
| `shadow-cost-4` | 0 / 0 / 0 | 1 / 0 / 1 |
| the other sixteen | 0 / 0 / 0 | 0 / 0 / 0 |

`avatar-motion` and `material-probe` draw nothing in the viewport and
compare as empty frames. Without `vrmImaging` the MToon fixtures draw
their fallback PreviewSurface, so the same harness ran again under the
plugins (below), where `mtoon-uv-animation`, `mtoon-uv-reference`,
`outline-depth-motion`, `outline-occlusion-motion` and `scene-lights` draw
MToon with hulls and two shadow views: all identical, 0 / 0 / 0 at 1x and
4x.

`toon-viewport-webgpu-usd-parity-*` runs six of them in the `webgpu-usd`
tree with `--max 1 --over-one 0`: `preview-reference` (lit
PreviewSurface), `skinning` (skin, morph and unlit), `geom-subsets`,
`avatar-provider` (two skinned meshes with a morph), `scene-lights` and
`shadow-cost-4` (sixteen skinned casters, a morph, four views).

## 3. Avatars

Local only, as the avatars are not redistributable: `vrmImaging` and
`mmdImaging` as in [report 87](87-2026-10-21-shared-shadow-acceptance.md#4-mtoons-looks),
800 × 900 (the kitchen 1280 × 720), the same harness and measures.

| Scene | Draws | 1x | 4x |
| --- | --- | --- | --- |
| AliciaSolid `alicia_VRMA_01.usda`, full body | 20 MToon (5 transparent), 15 hulls, 1 view | 1 / 0 / 1 | 15 / 4.7e-5 / 9 |
| the same, face (`--camera-dolly 12 --camera-pan 0 170`) | 20 MToon, 13 hulls | 221 / 1.1e-5 / 154 | 59 / 2.6e-4 / 31 |
| YYB Miku | 52 MMD, 30 edges, 79 transparent draws | 0 / 0 / 0 | 52 / 3.9e-4 / 97 |
| Kitchen_set with both avatars | PreviewSurface, MToon and MMD, 1 view | 1 / 0 / 2 | 63 / 1.4e-4 / 51 |

Every difference above one step is a pixel on an edge. At 1x the face's
eight are pixel centres on a one-pixel hull line or where one mesh meets
another, drawn on one backend and not the other; at 4x they are samples
along silhouettes, thin hair strands and hulls. With `--outlines off`
AliciaSolid's full body is within one step at 4x, the face keeps two
pixels at 1x and one at 4x where meshes meet, and Miku's 4x keeps 24 steps
on 1e-4 of its pixels along strand edges.

**Direct3D 12, not the backend.** The same frames through Dawn's Vulkan
adapter (`--webgpu-adapter vulkan`) — the face at 1x, the full body at
4x, Miku at 4x without edges and the kitchen at 4x — are byte-identical
to the Vulkan backend's. The WebGPU backend records the same image; the
Direct3D 12 driver path, with vertex stages compiled by DXC rather than
from SPIR-V, places some edges a last bit apart, and on a dense avatar a
pixel centre or sample on such an edge falls on the other side. Stage
2's hull-edge class (two steps on 2e-4 at 4x) was measured on canonical
scenes whose hulls are few and wide; these figures are recorded here and
not made a CTest limit, as the avatars cannot be committed.

**One run.** One of seven Vulkan runs of the kitchen at 1x drew a 44 × 3
strip on a Kitchen_set cabinet's lower edge with the other of two
coincident surfaces, 118 pixels; six further runs of each backend are
byte-identical to one another and within one step of each other. Its
statistics matched, and the strip is two coplanar surfaces, so it is the
stage's depth tie resolved differently in that run, not a backend
difference. The cause is not established.

## 4. Performance

**Fixtures in `ost test`.** `toon-viewport-shadow-cost-1` and `-4` take a
backend (`check_shadow_cost.cmake`'s `BACKEND`, recorded as `backend`); in
the `webgpu-usd` tree `toon-viewport-webgpu-shadow-cost-1` and `-4` run
them on WebGPU, 64 frames at 256 × 256. Mean / p95 / p99 ms:

| Fixture | Backend | GPU shadow pass | GPU work | CPU fit | CPU submit |
| --- | --- | --- | --- | --- | --- |
| `shadow-cost` | Vulkan | 0.037 / 0.039 / 0.053 | 0.072 / 0.074 / 0.092 | 0.020 / 0.022 / 0.052 | 0.652 / 0.846 / 2.572 |
| `shadow-cost` | WebGPU | 0.030 / 0.031 / 0.034 | 0.069 / 0.071 / 0.075 | 0.021 / 0.027 / 0.041 | 0.520 / 0.798 / 1.398 |
| `shadow-cost-4` | Vulkan | 0.138 / 0.141 / 0.147 | 0.205 / 0.213 / 0.224 | 0.030 / 0.026 / 0.432 | 1.138 / 1.445 / 2.291 |
| `shadow-cost-4` | WebGPU | 0.134 / 0.135 / 0.140 | 0.208 / 0.210 / 0.219 | 0.026 / 0.033 / 0.076 | 0.696 / 1.207 / 2.128 |

Both backends draw the views at 2,048 texels in 16 and 64 MiB and upload
no static resource after the first frame.

**At report 87's size.** 800 × 900, 4x, vsync off, time step 0.25, 240
frames, the backends alternating per run, two rounds; mean / p95 / p99
ms from the first round (the second is within 0.02 ms on every GPU row):

| Scene | Backend | GPU shadow | GPU scene | GPU frame | CPU fit | CPU submit | Frame interval |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `shadow-cost`, shadows off | Vulkan | 0 | 0.051 / 0.052 / 0.053 | 0.063 / 0.064 / 0.066 | 0.001 / 0.001 / 0.002 | 0.555 / 0.621 / 1.116 | 0.956 / 1.039 / 1.553 |
| | WebGPU | 0 | 0.050 / 0.051 / 0.051 | 0.073 / 0.074 / 0.075 | 0.001 / 0.002 / 0.007 | 0.363 / 0.411 / 0.747 | 0.747 / 0.832 / 1.260 |
| `shadow-cost` | Vulkan | 0.037 / 0.038 / 0.040 | 0.088 / 0.089 / 0.092 | 0.135 / 0.136 / 0.139 | 0.020 / 0.021 / 0.024 | 0.778 / 0.821 / 0.964 | 1.194 / 1.292 / 1.391 |
| | WebGPU | 0.030 / 0.031 / 0.031 | 0.087 / 0.088 / 0.088 | 0.149 / 0.150 / 0.151 | 0.021 / 0.023 / 0.029 | 0.434 / 0.533 / 0.981 | 0.817 / 0.971 / 1.506 |
| `shadow-cost-4` | Vulkan | 0.138 / 0.139 / 0.141 | 0.206 / 0.208 / 0.211 | 0.353 / 0.356 / 0.358 | 0.024 / 0.024 / 0.028 | 1.268 / 1.390 / 1.504 | 1.664 / 1.796 / 1.953 |
| | WebGPU | 0.134 / 0.135 / 0.135 | 0.231 / 0.233 / 0.233 | 0.399 / 0.402 / 0.403 | 0.025 / 0.028 / 0.032 | 0.561 / 0.734 / 0.842 | 0.966 / 1.154 / 1.269 |
| AliciaSolid, shadows off | Vulkan | 0 | 0.341 / 0.347 / 0.349 | 0.361 / 0.359 / 0.363 | 0.007 / 0.002 / 0.003 | 1.107 / 1.200 / 1.537 | 1.598 / 1.759 / 2.130 |
| | WebGPU | 0 | 0.094 / 0.101 / 0.103 | 0.118 / 0.125 / 0.126 | 0.007 / 0.002 / 0.004 | 0.532 / 0.649 / 1.077 | 1.016 / 1.212 / 1.717 |
| AliciaSolid, camera key | Vulkan | 0.247 / 0.248 / 0.370 | 0.347 / 0.360 / 0.473 | 0.612 / 0.616 / 0.860 | 0.159 / 0.158 / 0.232 | 1.665 / 1.838 / 2.249 | 2.173 / 2.407 / 3.022 |
| | WebGPU | 0.039 / 0.040 / 0.040 | 0.152 / 0.169 / 0.171 | 0.225 / 0.240 / 0.242 | 0.160 / 0.164 / 0.176 | 0.724 / 0.859 / 1.113 | 1.218 / 1.392 / 1.909 |

Vulkan's figures agree with report 87's. The WebGPU scene row is the pass
whole (report 93); Vulkan's GPU frame also counts the barriers between
its timestamps. On the fixtures the backends' GPU passes are within
0.05 ms; WebGPU's composite is 0.001 to 0.002 ms longer and its CPU
submit shorter.

**The avatar gap.** AliciaSolid's 61,896 skinned triangles cost Vulkan
0.25 ms in the shadow pass and WebGPU 0.04 ms, and the scene 0.35 against
0.15 ms. Through Dawn's Vulkan adapter WebGPU draws the same frame in 0.042
ms of shadow pass and 0.170 ms of scene, so the driver API is not the
cause. The Vulkan backend allocates every buffer — vertices, indices,
skins, morph offsets and the frame's records — through `CreateHostBuffer`,
which asks for host-visible memory and does not prefer device-local; on
this discrete GPU that is system memory, read over PCIe by every vertex
in every pass. Dawn copies `wgpuQueueWriteBuffer` data into device memory.
A local build that asked first for host-visible device-local memory (the
resizable BAR heap) drew AliciaSolid in 0.028 ms of shadow pass, 0.135 ms
of scene and 0.178 ms of GPU frame, and without shadows 0.083 and 0.100;
it was not kept. The fixtures, a few hundred vertices, do not show it.
Against design policy §23 both backends keep the CPU submit under 2 ms at
p95 and the avatar's GPU work far below its 2 to 6 ms.

[Report 117](117-2026-10-31-vulkan-device-local-meshes.md) keeps the device-local change.

## 5. The done-when condition

| Condition | Evidence |
| --- | --- |
| Transparent output over both backgrounds | Offscreen: `toon-webgpu-parity`'s canonical scenes over both clears (reports 91–93). Viewport: every stage above, presented image and straight foreground, solid and checkerboard; MToon transparent and MMD's 79 transparent draws on the avatars |
| Outline | MToon hulls and their frustum and occlusion omission (`outline-*`, AliciaSolid's 15 hulls), MMD edges (Miku's 30); hull counts equal in every run |
| Blend | PreviewSurface and MToon Blend canonical scenes (report 92); AliciaSolid's five transparent MToon draws |
| Normals | Authored, generated and normal-mapped normals in the canonical scenes; `draws_authored_normals` equal on every stage |
| Skinning, morphs, late values | `skinning`, `avatar-provider`, `shadow-cost*`, `morph-motion` and the avatars; the late latch's applied count equal on every run |
| Cross-material shadows | Canonical shadow scenes across families (report 93); the kitchen's PreviewSurface, MToon and MMD under one view |
| Performance | §4, p95/p99 beside Vulkan |

## 6. Tests and gates

| Build | `ost build`, `ost test`, `ost validate` |
| --- | --- |
| `lookdev` `webgpu-usd` | pass, 99 of 99, among them `toon-viewport-webgpu-shadow-cost-1`, `-4` and the six `toon-viewport-webgpu-usd-parity-*` |
| `core` | pass, 23 of 23 |
| `lookdev` `hydra` | pass, 37 of 37 |
| `lookdev` `viewport-usd` | pass, 73 of 73 |
| `webgpu` | pass, 52 of 52 |

`ost package --profile lookdev --intent hydra` succeeds
(`sha256:f81152b2…`); its manifest lists no Dawn, DXC or WGSL file.
`scripts/check_docs.py` passes. The avatar, kitchen, Dawn
Vulkan adapter and cost runs are manual, not CTests.

## 7. What remains

Cutting v0.7.0 through the release gate. The Vulkan backend's buffers in
host-visible system memory (§4) are a Vulkan finding outside this
milestone's WebGPU scope; moving them to device-local memory, with a
fallback where the BAR heap is small, is a change of its own.
