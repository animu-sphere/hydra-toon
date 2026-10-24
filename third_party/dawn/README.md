# Dawn and what its library carries

The WebGPU backend links Dawn's `webgpu_dawn` statically from the published
[Dawn release `v20261008.181332`](https://github.com/google/dawn/releases/tag/v20261008.181332)
(Dawn commit `b73d662f73d57bb42209546910666a38e6db95a1`), Windows Release
archive, SHA-256
`b3574efe866329252d738fbb8346e5c1547bdbc23a9db42debfecb267a90cc1e`, a
pinned download (`cmake/ToonDependencies.cmake`). Nothing of Dawn is
vendored here but these notices, which install with every executable that
links it under `share/toon/licenses/dawn`
([WebGPU policy §2](../../docs/design/WEBGPU_POLICY.md#2-implementation-and-acquisition)).

| Component | Licence | Revision (Dawn's `DEPS`) | File |
| --- | --- | --- | --- |
| Dawn and Tint | BSD-3-Clause | `b73d662f73d57bb42209546910666a38e6db95a1` | [LICENSE](LICENSE) |
| Abseil | Apache-2.0 | `af5aede14a0b4013be6890148d34e0fd7b1fac69` | [licenses/abseil-cpp.txt](licenses/abseil-cpp.txt) |
| SPIRV-Tools | Apache-2.0 | `ce818d1f2db53916d89fa1fe0012237779480a9b` | [licenses/SPIRV-Tools.txt](licenses/SPIRV-Tools.txt) |
| SPIRV-Headers | MIT-style Khronos | `86f980c731e62ae4eaf383d320449d71687936bf` | [licenses/SPIRV-Headers.txt](licenses/SPIRV-Headers.txt) |
| Vulkan-Headers | Apache-2.0 or MIT | `c46850864f4661461b0f6cb9922c058ffea4915e` | [licenses/Vulkan-Headers.txt](licenses/Vulkan-Headers.txt), text in [licenses/Apache-2.0.txt](licenses/Apache-2.0.txt) |
| Vulkan-Utility-Libraries | Apache-2.0 | `90738f6edfa19561a978fc5f97543ba37075ba9d` | [licenses/Vulkan-Utility-Libraries.txt](licenses/Vulkan-Utility-Libraries.txt), text in [licenses/Apache-2.0.txt](licenses/Apache-2.0.txt) |
| PartitionAlloc | BSD-3-Clause (Chromium) | `4dd6a9f723c6ffdb44147b073ea23b6f9a0d5751` | [licenses/partition_alloc.txt](licenses/partition_alloc.txt) |

The components are those whose symbols the release's `webgpu_dawn.lib`
contains ([renderer report 91](../../docs/reports/renderer/91-2026-10-24-webgpu-stage-1.md));
each licence is copied unmodified from its repository at the revision
Dawn's `DEPS` pins. A Dawn pin change rechecks this list.

Dawn's Direct3D 12 device loads DXC at run time. The pinned
[DXC release `v1.9.2609`](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609)
provides `dxcompiler.dll`, installed beside the executables with the
release's `LICENSE-LLVM.txt` and `LICENCE-MIT.txt` under
`share/toon/licenses/dxc`; `dxil.dll`, under Microsoft's own licence, is not
redistributed and is not needed.
