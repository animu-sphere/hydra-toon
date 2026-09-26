# hydra-toon

> A low-latency, avatar-first Hydra raster renderer, optimized for
> continuously changing animation rather than continuously changing scenes.

## Scope

`hydra-toon` renders VRM / MToon and MMD avatars through Hydra, on Vulkan first
and WebGPU later. Its measure is input-to-photon latency and stable frame time,
not peak FPS. It is built for VTuber and streaming applications and for
real-time motion from tracking, mocap and XR input.

It **owns** the renderer: the host-neutral core, the Vulkan (later WebGPU)
backend, the `hdToon` Hydra adapter, the MToon, MMD and PreviewSurface
realizations, and the low-latency animation path.

It **does not own** the source formats, the USD schemas it reads, motion
semantics or device input. Those belong to
[`usd-vrm-plugins`](https://github.com/animu-sphere/usd-vrm-plugins),
[`usd-mmd-plugins`](https://github.com/animu-sphere/usd-mmd-plugins),
[`usd-motion-plugins`](https://github.com/animu-sphere/usd-motion-plugins) and
[`motion-connectors`](https://github.com/animu-sphere/motion-connectors), and
the contract with each is the composed USD stage, never a link
([integration scope](docs/design/INTEGRATION_SCOPE_POLICY.md)). General USD
rendering is [`hydra-merlin`](https://github.com/animu-sphere/hydra-merlin)'s
role.

## Architecture

```text
             slow path
USD stage ─→ Hydra ─→ hdToon adapter ─→ ToonScene ─→ DrawPackets ─┐
                         (MToon / MMD / PreviewSurface             │
                          → ToonMaterial)                          ▼
             fast path                                   GPU resource update ─→ Vulkan | WebGPU
MotionPose ─────────────────────────── late latch ───────────────┘
```

Static scene state and per-frame motion state take separate paths. A pose
change updates a skeleton buffer and nothing else. See the
[design policy](docs/design/DESIGN_POLICY.md).

## Components

| Component | Directory | Role |
| --- | --- | --- |
| `toon-render-world` | `core/render-world/` | host-neutral scene state |
| `toon-render-extraction` | `core/render-extraction/` | scene state → draw work |
| `toon-render-vulkan` | `backend/vulkan/` | Vulkan backend and Slang shaders |
| `hdToon` | `adapters/hydra2/` | Hydra render delegate and renderer plugin |
| `toon-headless` | `adapters/headless/` | headless runner and renderer evidence |
| `toon-viewport` | `adapters/viewport/` | standalone window |

What each can do today is the
[capability matrix](docs/reference/CAPABILITY_MATRIX.md); where new code goes
is [PROJECT_LAYOUT.md](docs/architecture/PROJECT_LAYOUT.md).

## Documentation

[docs/](docs/README.md) — [design](docs/design/DESIGN_POLICY.md),
[layout](docs/architecture/PROJECT_LAYOUT.md),
[capabilities](docs/reference/CAPABILITY_MATRIX.md),
[roadmap](docs/roadmap/README.md), [building](docs/guides/BUILDING.md),
[changelog](CHANGELOG.md).

## Build

The project is built with [OpenStrata](https://github.com/animu-sphere/open-strata)
(`ost`) and generated from its `renderer` template:

```sh
ost build --jobs auto
ost test
ost validate
```

The default build needs Vulkan 1.3 and `slangc` and no OpenUSD. The Hydra
adapter, the viewport and plain CMake are in [BUILDING.md](docs/guides/BUILDING.md).

## License

[Apache-2.0](LICENSE).
