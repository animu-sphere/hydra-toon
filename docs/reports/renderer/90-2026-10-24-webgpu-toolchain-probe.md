# WebGPU toolchain probe: Dawn release and Slang-generated WGSL

- Date: 2026-10-24
- Machine: Windows 11 x86_64, NVIDIA RTX A5000, driver 597.16
- Tools: `slangc` 2026.8 from Vulkan SDK 1.4.350.0, the build's shader
  compiler; Dawn release
  [`v20261008.181332`](https://github.com/google/dawn/releases/tag/v20261008.181332)
  (Dawn commit `b73d662f73d57bb42209546910666a38e6db95a1`), Windows Release
  archive, and its `tint` executable
- Source: `main` at `fc3b43b`, shaders in `backend/vulkan/shaders/` unchanged
- Occasion: the [WebGPU RFC](../../design/WEBGPU_POLICY.md) for v0.7.0,
  before any backend code; no GPU work was submitted

## Dawn's published Windows build

Dawn publishes prebuilt archives for each release, among them
`Dawn-<commit>-windows-latest-Release.tar.gz`. The pinned candidate is
119,410,660 bytes with SHA-256
`b3574efe866329252d738fbb8346e5c1547bdbc23a9db42debfecb267a90cc1e`, and
holds:

| Content | Detail |
| --- | --- |
| Headers | `include/dawn/webgpu.h`, `webgpu_cpp.h`, the Dawn native backend headers (D3D11, D3D12, Vulkan, null) and the wire client |
| CMake package | `lib/cmake/Dawn/DawnConfig.cmake`; the target `dawn::webgpu_dawn` is a `STATIC IMPORTED` library, `lib/webgpu_dawn.lib`, 631,659,334 bytes, linking `user32`, `onecore_apiset` and `dxguid` |
| Tools | `bin/tint.exe`, `tint_info.exe` and two IR fuzz tools |
| Tint libraries | the `tint_*` static libraries, not needed by a consumer of `webgpu_dawn` |

There is no DLL: an executable that links the backend carries Dawn
statically. The latest release, `v20261022.205350`, publishes the same
asset names. Nothing was built or linked against the archive in this run.

## Slang to WGSL, unchanged shaders

Each of the eleven compiled modules was generated for both entry points
with the build's flags and the WGSL target:

```text
slangc <module>.slang -entry <stage>_main -stage <stage> -target wgsl
  -matrix-layout-column-major -o <module>.<stage>.wgsl
```

All 22 generations succeeded without diagnostics. Each output was then
checked with `tint --format wgsl -o NUL <file>`. Every module fails on
its first resource: the push-constant `DrawConstants` block becomes
`var<uniform> draw_0` with no `@group` or `@binding`, which Tint rejects
(`resource variables require '@group' and '@binding' attributes`).

To see what lies behind it, a copy of each output had that one declaration
given `@group(3) @binding(0)` and was checked again:

| Module | Vertex | Fragment |
| --- | --- | --- |
| `mesh` | valid | valid |
| `overlay` | valid | valid |
| `composite` | valid | `cannot index into expression of type 'sampler'`: the multisampled foreground load is generated as a `textureLoad` whose coordinates read from the sampler |
| `shadow` | valid | `texture_2d<f32> cannot be used as an element type of an array` |
| `preview_surface` | valid | texture array, as above |
| `mtoon`, `mtoon_transparent` | valid | texture array |
| `mtoon_outline` | texture array (the outline width texture is read in the vertex stage) | texture array |
| `mmd`, `mmd_transparent` | `'@location(0)' appears multiple times`: the vertex input and output structs carry semantic names without locations, and every member is emitted at location 0 | texture array |
| `mmd_outline` | valid | valid |

The texture-table form is `@binding(1) @group(0) var textures_1 :
array<texture_2d<f32>, 128>` beside `var samplers_0 : array<sampler, 9>`;
core WGSL has no arrays of texture or sampler bindings. The
storage buffers (materials, frame, shadow views, skinning and morph data) and
the `texture_2d_array<f32>` shadow map were emitted in valid forms; Tint stops
at the first error per module, so uniformity and later diagnostics are not
yet observed.

## What this establishes

- Dawn can be consumed as a pinned prebuilt release with a CMake package,
  without building Dawn, and its `tint` can validate generated WGSL at build
  time.
- With today's sources, `slangc` already produces WGSL for every stage. The
  remaining invalid forms are the four the RFC assigns to a per-backend
  bindings module and explicit declarations
  ([WebGPU policy §5](../../design/WEBGPU_POLICY.md#5-shaders)): push
  constants, the texture and sampler tables, the composite multisampled load
  and MMD's vertex locations.
- Not established: that Dawn creates a Direct3D 12 device on this machine,
  any render, uniformity diagnostics behind the first error, or performance.
  Those are stage 1's evidence.
