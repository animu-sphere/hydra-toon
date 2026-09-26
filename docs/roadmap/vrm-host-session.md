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

- ✅ **The session, composed by hand from the three packages**, selects
  MToon for every VRM material in `testusdview`
  ([renderer report 05](../reports/renderer/05-2026-09-26-vrm-usdview-session.md)).
- ⛔ **`ost formation resolve`** refuses every packaged target
  ([ost report 04](../reports/ost/04-2026-09-26-v0.23.10-a-renderer-formation.md) P1).
- ⚠️ **The renderer package comes from an intentless build** with
  `TOON_ENABLE_HYDRA2=ON` set in its cache, and its contract names a plugin
  directory the install does not have (ost report 04 P2, P3). Replace when
  `ost package` takes an intent and the contract matches the install.
- ⬜ **A Formation in the repository** and a `testusdview` check run through
  `ost formation run`, once P1–P3 are answered. It pins published digests
  only, so it waits on the next item.
- ⬜ **Publish the runtime and the renderer package** under the names above.
  The runtime here is an adopted local build with no source identity; a
  published one should come from `ost`'s canonical producer (ost report 04
  P5) or a managed `--build`.

## Linux x86_64

- ⬜ **The same session.** Not built or run. The planned commands, on a
  Linux host or WSL 2 with a Vulkan 1.3 device:

  ```sh
  # A lookdev runtime: OpenUSD 26.08 with usdview, built by ost for CY2026,
  # which pins the CY cell's oneTBB. The build interpreter needs Jinja2,
  # PySide6 and PyOpenGL.
  ost runtime pull cy2026 --profile lookdev --build <OpenUSD v26.08 checkout> \
      --openusd-variant gl --jobs <n>
  ost runtime validate cy2026 --profile lookdev
  ost runtime export cy2026 --profile lookdev --slim

  # In usd-vrm-plugins: vrmImaging and the vrmSchema it carries.
  ost lock --profile lookdev
  ost plugin build plugins/vrmSchema --profile lookdev
  ost plugin build plugins/vrmImaging --profile lookdev
  ost plugin package plugins/vrmImaging --profile lookdev
  ost lock
  ost artifact import plugins/vrmImaging/dist/plugins/vrmImaging/0.9.0/cy2026-linux-x86_64-py313-lookdev

  # Here: the renderer package (see the Windows workaround above).
  ost build --profile lookdev --jobs auto
  cmake -DTOON_ENABLE_HYDRA2=ON build/cy2026-linux-x86_64-py313-lookdev
  ost build --profile lookdev --jobs auto
  ost test --profile lookdev
  ost package --profile lookdev
  ost artifact import dist/toon/<version>/cy2026-linux-x86_64-py313-lookdev
  ```

  Then the Formation, as on Windows. ost report 04 P1 applies to Linux
  targets too.

## macOS arm64

- ⬜ **The runtime and the VRM package only.** `hdToon` is Vulkan, and
  macOS is out of scope for the Vulkan-first phases
  ([design policy §4](../design/DESIGN_POLICY.md#4-backends);
  [supported configurations](../reference/SUPPORTED_CONFIGURATIONS.md)), so
  no renderer package is planned for macOS before Renderer Phase 6. Not
  built or run. The planned commands, on macOS 15 with Xcode's 15.5 SDK:

  ```sh
  # A lookdev runtime with usdview, at CY2026's SDK and deployment target.
  # The build interpreter needs Jinja2, PySide6 and PyOpenGL.
  ost runtime pull cy2026 --profile lookdev --build <OpenUSD v26.08 checkout> \
      --openusd-variant metal --sdk 15.5 --deployment-target 13.0 --jobs <n>
  ost runtime validate cy2026 --profile lookdev
  ost runtime export cy2026 --profile lookdev --slim

  # In usd-vrm-plugins, as on Linux.
  ost lock --profile lookdev
  ost plugin build plugins/vrmSchema --profile lookdev
  ost plugin build plugins/vrmImaging --profile lookdev
  ost plugin package plugins/vrmImaging --profile lookdev
  ost lock
  ost artifact import plugins/vrmImaging/dist/plugins/vrmImaging/0.9.0/cy2026-macos-arm64-py313-lookdev
  ```

  With those, `usdview` can show whether `vrmImaging` contributes its
  container on macOS, but with Storm; that is `usd-vrm-plugins`' evidence,
  not this renderer's.
