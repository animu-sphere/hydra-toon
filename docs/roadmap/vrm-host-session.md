# The VRM host session

The `usdview` session that renders a VRM stage with `hdToon`, composed by
`ost formation` from released packages rather than a hand-set
`PXR_PLUGINPATH_NAME`. It is the last item
[before Renderer Phase 1](current.md#before-renderer-phase-1)'s MToon path.

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked · ⚠️ accepted workaround

## Members

| Member | Kind | Package name when published |
| --- | --- | --- |
| a CY2026 `lookdev` runtime with `usdview` | runtime | `ghcr.io/animu-sphere/openstrata-runtime-cy2026-lookdev:26.08-gl-<os>-<arch>` |
| `vrmImaging`, carrying `vrmSchema` | plugin | owned by `usd-vrm-plugins` |
| `toon` (`hdToon`) | renderer | `ghcr.io/animu-sphere/hydra-toon:toon-<version>-cy2026-<os>-<arch>-py313-lookdev` |

The names follow the organization's existing packages:
`openstrata-runtime-cy2026-usd`'s leaf tags, and the
`<package>-<version>-cy2026-<os>-<arch>-py313-<profile>` tags
`usd-motion-plugins` publishes. Only `vrmImaging` is named: its package
carries the `vrmSchema` it was built against
([renderer report 05](../reports/renderer/05-2026-09-26-vrm-usdview-session.md)).
The runtime's oneTBB only has to be oneTBB, not Intel TBB; the version
CY2026 names (2022.x) is what a managed build pins.

## Windows x86_64

- ✅ **The session as a Formation** of the canonical runtime, `vrmImaging`
  and `toon`: resolved, locked and run by `ost formation`, every VRM material
  selecting MToon in `testusdview`
  ([renderer report 06](../reports/renderer/06-2026-09-27-vrm-formation.md);
  [ost report 05](../reports/ost/05-2026-09-27-v0.23.11-report-04-reverified.md)).
- ⚠️ **The run supplies the host's Python.** `ost formation run` puts no
  interpreter on the command's `PATH` and the runtime ships none, so the
  Formation's `testusdview` command is overridden with the host's
  `python.exe` (ost report 05 Q1,
  [open-strata#259](https://github.com/animu-sphere/open-strata/issues/259)). Replace when `ost` composes the platform
  interpreter.
- ⚠️ **The Formation must live in a short directory.** Under a deep one, Qt
  cannot load its platform plugin from the materialized runtime (ost report
  05 Q2, [open-strata#260](https://github.com/animu-sphere/open-strata/issues/260)).
- ⬜ **A Formation in the repository** and its `testusdview` check. It pins
  published digests only, so it waits on the next item.
- ⬜ **Publish the renderer package** as
  `ghcr.io/animu-sphere/hydra-toon:toon-<version>-cy2026-windows-x86_64-py313-lookdev`,
  and `vrmImaging` from `usd-vrm-plugins`. The runtime is published
  (`sha256:b982656c…`).

## Linux x86_64

- ⬜ **The same session.** Not built or run. The planned commands, on a
  Linux host or WSL 2 with a Vulkan 1.3 device:

  ```sh
  # The canonical lookdev runtime, pinned by its OCI manifest digest.
  ost artifact pull oci://ghcr.io/animu-sphere/openstrata-runtime-cy2026-lookdev@sha256:d0dfa81e6bf7bbc19b0b0a5185d4eda789bbb38546505c236311c348b001980f       --expect-artifact sha256:7b41fe89f1c4b868ac6af73bc0593a24aa3ce4952fcf4fe4d96d0ae1724f3f34
  ost runtime pull cy2026 --profile lookdev       --from-artifact sha256:7b41fe89f1c4b868ac6af73bc0593a24aa3ce4952fcf4fe4d96d0ae1724f3f34
  ost runtime validate cy2026 --profile lookdev

  # In usd-vrm-plugins: vrmImaging and the vrmSchema it carries.
  ost plugin build plugins/vrmSchema --profile lookdev
  ost plugin build plugins/vrmImaging --profile lookdev
  ost plugin package plugins/vrmImaging --profile lookdev
  ost artifact import plugins/vrmImaging/dist/plugins/vrmImaging/0.9.0/cy2026-linux-x86_64-py313-lookdev

  # Here: the renderer package, from the hydra intent.
  ost build --profile lookdev --intent hydra --jobs auto
  ost test --profile lookdev --intent hydra
  ost package --profile lookdev --intent hydra
  ost artifact import dist/toon/<version>/cy2026-linux-x86_64-py313-lookdev--hydra
  ```

  Then the Formation, as on Windows. Ost report 05 Q1 applies to Linux too:
  `testusdview`'s `#!/usr/bin/env python3` meets the same `PATH`.

## macOS arm64

- ⬜ **The runtime and the VRM package only.** `hdToon` is Vulkan, and
  macOS is out of scope for the Vulkan-first phases
  ([design policy §4](../design/DESIGN_POLICY.md#4-backends);
  [supported configurations](../reference/SUPPORTED_CONFIGURATIONS.md)), so
  no renderer package is planned for macOS before Renderer Phase 6. Not
  built or run. The planned commands, on macOS 15 with Xcode's 15.5 SDK:

  ```sh
  # A lookdev runtime with usdview, at CY2026's SDK and deployment target;
  # there is no canonical macOS lookdev leaf. The build interpreter needs
  # Jinja2, PySide6 and PyOpenGL.
  ost runtime pull cy2026 --profile lookdev --build <OpenUSD v26.08 checkout> \
      --openusd-variant metal --sdk 15.5 --deployment-target 13.0 --jobs <n>
  ost runtime validate cy2026 --profile lookdev
  ost runtime export cy2026 --profile lookdev --slim

  # In usd-vrm-plugins, as on Linux.
  ost plugin build plugins/vrmSchema --profile lookdev
  ost plugin build plugins/vrmImaging --profile lookdev
  ost plugin package plugins/vrmImaging --profile lookdev
  ost artifact import plugins/vrmImaging/dist/plugins/vrmImaging/0.9.0/cy2026-macos-arm64-py313-lookdev
  ```

  With those, `usdview` can show whether `vrmImaging` contributes its
  container on macOS, but with Storm; that is `usd-vrm-plugins`' evidence,
  not this renderer's.
