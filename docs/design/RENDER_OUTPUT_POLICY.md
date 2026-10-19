---
status: accepted
owner: hydra-toon
---

# Render output policy

This document owns the intended reusable RGBA output, background, alpha and
GPU handoff contracts. It expands
[DESIGN_POLICY.md](DESIGN_POLICY.md) and wins on output semantics. Materials
and their draw order belong to [MATERIAL_POLICY.md](MATERIAL_POLICY.md).
Implementation status belongs to the
[capability matrix](../reference/CAPABILITY_MATRIX.md); unfinished work and
acceptance criteria belong to the [roadmap](../roadmap/README.md), and dated
acceptance evidence belongs to [renderer reports](../reports/renderer/).

## 1. Product and consumers

The renderer produces a reusable image without assuming a display background:

```text
RenderWorld → DrawList → backend renderer → RGBA render product
                                                ├→ viewport compositor → swapchain
                                                ├→ Hydra output boundary
                                                └→ external host / compositor
```

The product is independent of the swapchain. The viewport is one consumer;
its presentation and debug backgrounds do not define the scene's output.
Hydra and direct evaluated input share this output contract. The renderer
does not acquire responsibility for avatar evaluation or host orchestration.

## 2. Background and environment

Support `opaque` and `transparent` modes through
`toon:backgroundMode = opaque | transparent` (string or token) and
`toon:backgroundColor = float4`. Colour components must be finite and in
[0,1], in linear sRGB; the fourth component is ignored when clearing.
Defaults are `opaque` and `(0.05, 0.10, 0.15, 1)`. Invalid settings must
retain the last accepted pair. Background settings own the scene clear,
independently of host AOV clear hints.

| Mode | Scene clear RGBA |
| --- | --- |
| `opaque` | `(background.rgb, 1)` |
| `transparent` | `(0, 0, 0, 0)` |

Material transparency and background transparency are independent. Blend
materials must work over either clear. Background clear, environment lighting,
background geometry and sky/environment imagery are separate controls.
Environment lighting may eventually illuminate a scene whose background
remains alpha zero; implementing that lighting is not required for the RGBA
contract. Visible background geometry still contributes its own coverage.

## 3. Alpha and colour boundaries

Use **premultiplied RGBA** for the internal render product and GPU handoff:
`rgb = source_rgb * alpha`, `a = alpha`. Keep the logical convention the same
on Vulkan and WebGPU. Blend and resolve operate in the renderer's linear
working colour space; a boundary must state its encoding and colour space.
Premultiplication avoids edge halos and gives compositors a defined source.

For premultiplied source-over, `Cout = Cs + Cd * (1 - As)` and
`Aout = As + Ad * (1 - As)`. Surface shader outputs and pipeline blend factors
must agree so that alpha is applied once. The product convention does not
require material parameter colours to be stored premultiplied.

Convert at a consumer boundary when its contract requires another convention:
Hydra/host output follows that host's contract, PNG export uses straight
RGBA, and a GPU compositor normally receives premultiplied RGBA. Straight
conversion divides RGB by alpha only where alpha is nonzero; zero-alpha
pixels use zero RGB. Record colour encoding and conversion alongside alpha
convention so compositing never relies on an implicit interpretation.

The v0.5.0 contract is the boundary between the scene-linear foreground and
the encoded output — colour space, transfer and alpha convention. Exposure,
tone mapping and HDR output are later work, taken up when a host's display
differences or an HDR requirement ask for them
([later](../roadmap/later.md#after-v070)).

## 4. Foreground and multisample coverage

Character surfaces, toon outlines and transparent surfaces all contribute
foreground alpha. An outline pixel is foreground even outside its surface's
depth footprint. Never reconstruct alpha as `depth exists ? 1 : 0`.
Preserve material alpha, hull coverage and the existing model's draw order.
MMD self-shadow shades the character surface and is included in its RGBA,
as shadows between material families will be
([MATERIAL_POLICY.md §14](MATERIAL_POLICY.md#14-shadows-across-material-families)):
a shadow received on a surface is that surface's colour, not separate
coverage.

Resolve multisample premultiplied colour and alpha consistently, including
silhouettes, outlines, Mask edges, alpha-to-coverage and Blend surfaces.
One opaque foreground sample in 4x MSAA over transparent clear should resolve
to alpha 0.25 and the corresponding premultiplied colour. Mask coverage must
not multiply the same opacity twice; transparent surfaces must retain their
foreground-over-transparent result. Define and test these semantics on
Vulkan before reproducing them in WebGPU.

## 5. Hydra and viewport boundaries

Hydra's colour `HdRenderBuffer` must preserve RGBA through AOV representation
and readback: transparent background alpha zero, foreground alpha from
coverage and material opacity. If its representation differs from the
internal product, convert at the adapter boundary. The hdToon colour AOV
uses linear sRGB, linear transfer and premultiplied RGBA8 UNORM, with the
bottom row first. It preserves the internal product's channels without
unpremultiplication or display encoding. A host requiring straight alpha
or encoded colour must explicitly convert at its consuming boundary.

The viewport composites the product over a selectable solid colour or a
checkerboard transparent preview before presenting. Debug grids and future
image/video backgrounds belong to that compositor. A transparent native
window is not required. Presentation UI must not contaminate an externally
consumed foreground product.

## 6. Evidence boundary and additional outputs

Acceptance must test background independence and foreground alpha together.
Use a bounded redistributable fixture set spanning MToon, MMD and
PreviewSurface: silhouettes, outlines, Mask, Blend, transparent hair,
emission, normal maps and MMD self-shadow on receiving surfaces. Check the
same foreground over black, white, checkerboard and an arbitrary image,
visually and against independent linear source-over controls, at supported
sample counts. Background selection must not change the product. Record
measurements, accepted differences and unverified host conventions in reports.

Ground/contact/drop shadows and shadow catchers are separate future work,
after host / compositor integration, reusing the shared shadow pass.
Possible additional outputs include a shadow matte, depth, normal and
object/prim identity; they are not prerequisites for reusable foreground
RGBA. The RGBA milestone covers scene geometry, toon outlines and transparent
materials, including MMD self-shadow on those surfaces.

## 7. GPU texture handoff

A later zero-copy integration must describe more than an image handle:

| Field | Contract to establish |
| --- | --- |
| Resource | Backend image/texture handle, extent and format |
| Interpretation | Colour space/encoding and premultiplied alpha convention |
| Ownership and lifetime | Producer/consumer ownership, retention, release and reuse |
| Synchronization | Readiness semaphore/fence or equivalent, completion and safe reuse |
| Identity and timing | Frame identity, producer timestamp and clock mapping |
| Resource state | Image layout/resource state and required transitions |

Vulkan, WebGPU and Hgi can expose different native APIs while sharing these
logical meanings. Viewport, Hydra host, Tessera, Mimikuri and video compositors
are prospective consumers. Concrete API and synchronization choices belong
to their integration work; this policy does not assert those integrations
exist or promise a universal cross-API handle. The integration also
states what happens on resize, device loss, several consumers and capture,
and keeps a measurable readback fallback.

## 8. Sequencing

Version assignments and work order belong to the [roadmap](../roadmap/README.md).
Actual scanout/photon timing is host instrumentation, separate
from renderer-core completion and submit/present-return measurements
([design policy §23](DESIGN_POLICY.md#23-performance-kpis)).
