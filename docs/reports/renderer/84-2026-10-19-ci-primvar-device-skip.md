# Primvar validation on GPU-less release CI

- Date: 2026-10-19
- Local machine: Windows x86_64, RTX A5000; MSVC 14.51, CY2026 OpenUSD
  26.08 `lookdev`, `hydra` intent
- Scope: the v0.5.0 release dry run after PR 106; test capability gating only

## Failure and correction

The [main dry run](https://github.com/animu-sphere/hydra-toon/actions/runs/39189520731)
at `19c388eae641b710ee7b93abd5aa008e536ed49d` built successfully, but
`toon-renderer-hydra-primvars-gpu` failed at `vkCreateInstance` with
`VkResult -9`. The hosted Windows runner has a Vulkan loader but no usable
GPU driver. The other GPU tests reported their expected explained skips.
The failed dry run did not produce a tag or release.

The primvar test's old `ProbeVulkanBackend()` gate checked the loader API,
which is insufficient evidence of an instance or device. Its `main` now
probes `CreateOffscreenRenderer` like the existing Hydra skinning test:
`FrameStatus::Skip` prints the reason and returns 77; initialization failures
return 1. The probe is scoped to the GPU invocation and released before the
existing checks. No renderer, shader, image oracle, tolerance or CTest skip
property changes. The CPU invocation still runs without a GPU probe.

## Targeted verification

Using the existing local `hydra` tree and runtime `bin`/`lib` on `PATH`:

- With `VK_DRIVER_FILES` pointing at an absent build-local ICD manifest,
  the old binary reproduced the CI failure, exit 1 and `VkResult -9`.
- After rebuilding only `toon-hydra2-primvar-test`, the same missing-driver
  invocation prints the reason and returns 77.
- With the real driver restored but an absent shader directory, the test
  still fails with exit 1 and `cannot open SPIR-V shader`; capability gating
  does not hide missing shaders or other initialization failures.
- `ctest --test-dir build/cy2026-windows-x86_64-py313-lookdev--hydra
  -R toon-renderer-hydra-primvars --output-on-failure`: CPU and actual GPU
  image checks passed 2/2, including the existing 1x/4x oracles, in 7.83 s.

The renderer's bounded real-GPU release acceptance remains
[report 83](83-2026-10-19-v050-acceptance.md). An explained no-device skip
does not replace that evidence. This local report does not claim a passing
new main dry run, tag or published package; those follow the correction.
