# The VRM host session

The `usdview` session that renders a VRM stage with `hdToon`, composed by
`ost formation` from released packages rather than a hand-set
`PXR_PLUGINPATH_NAME`. It was the last item
[before Renderer Phase 1](current.md#before-renderer-phase-1)'s MToon path,
and on Windows it is done:
[`formations/vrm-host-session/`](../../formations/vrm-host-session/) pins
published packages only. What is left is the other platforms.

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked · ⚠️ accepted workaround

## Members

| Member | Kind | Package name when published |
| --- | --- | --- |
| a CY2026 `lookdev` runtime with `usdview` | runtime | `ghcr.io/animu-sphere/openstrata-runtime-cy2026-lookdev:26.08-gl-<os>-<arch>` |
| `vrmImaging`, carrying `vrmSchema` | plugin | `ghcr.io/animu-sphere/usd-vrm-plugins:vrmImaging-<version>-cy2026-<os>-<arch>-py313-lookdev`, owned by `usd-vrm-plugins` |
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
  [ost report 05](../reports/ost/05-2026-09-27-v0.23.11-report-04-reverified.md),
  [06](../reports/ost/06-2026-09-27-v0.23.13-report-05-reverified.md)).
- ✅ **The Formation's own command runs.** `ost formation run` starts the
  declared `testusdview` through the host Python the runtime was validated
  with, and a deep manifest directory no longer breaks Qt
  ([ost report 06](../reports/ost/06-2026-09-27-v0.23.13-report-05-reverified.md);
  `ost` 0.23.13). The host Python 3.13 stays a prerequisite: the runtime
  ships none.
- ✅ **`vrmImaging` published.** `usd-vrm-plugins` v0.10.0 publishes it
  with the digests a Formation pins (its release's
  `lookdev-package-pins.json`); the Formations pin archive
  `sha256:894fd616…`, and every run gives its report's numbers
  ([renderer report 12](../reports/renderer/12-2026-09-28-published-vrmimaging.md)).
- ✅ **The renderer package published.** [v0.1.0](../releases/v0.1.0.md)
  publishes `ghcr.io/animu-sphere/hydra-toon:toon-0.1.0-cy2026-windows-x86_64-py313-lookdev`
  with the digests a Formation pins (its release's `toon-package-pins.json`,
  archive `sha256:265328f0…`).
- ✅ **A Formation in the repository** and its `testusdview` check.
  [`formations/vrm-host-session/`](../../formations/vrm-host-session/) pins
  the runtime, `vrmImaging` 0.10.0 and `toon` 0.1.0 by published digest; its
  declared command selects MToon on the committed probe stage and fails
  without `vrmImaging`, and the published package draws the avatar with
  report 12's numbers
  ([renderer report 13](../reports/renderer/13-2026-09-28-host-session-formation.md)).

## Linux x86_64

- ⬜ **The same session.** Not built or run. The planned commands, on a
  Linux host or WSL 2 with a Vulkan 1.3 device:

  ```sh
  # The canonical lookdev runtime, pinned by its OCI manifest digest.
  ost artifact pull oci://ghcr.io/animu-sphere/openstrata-runtime-cy2026-lookdev@sha256:d0dfa81e6bf7bbc19b0b0a5185d4eda789bbb38546505c236311c348b001980f       --expect-artifact sha256:7b41fe89f1c4b868ac6af73bc0593a24aa3ce4952fcf4fe4d96d0ae1724f3f34
  ost runtime pull cy2026 --profile lookdev       --from-artifact sha256:7b41fe89f1c4b868ac6af73bc0593a24aa3ce4952fcf4fe4d96d0ae1724f3f34
  ost runtime validate cy2026 --profile lookdev

  # vrmImaging 0.10.0 and the vrmSchema it carries, published by
  # usd-vrm-plugins (its v0.10.0 release's lookdev-package-pins.json).
  ost artifact pull oci://ghcr.io/animu-sphere/usd-vrm-plugins@sha256:85955418443f1fae6b9612059784812a3cffb2c8a849dc7340d2eb43cbf05360       --expect-artifact sha256:cd4b08df5ba0cba7eee1265995fa36fe687ee57a480aefc68d953cb7171a44de

  # Here: the renderer package, from the hydra intent.
  ost build --profile lookdev --intent hydra --jobs auto
  ost test --profile lookdev --intent hydra
  ost package --profile lookdev --intent hydra
  ost artifact import dist/toon/<version>/cy2026-linux-x86_64-py313-lookdev--hydra
  ```

  Then the Formation, as on Windows, with a host Python 3.13 installed.

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

  # In usd-vrm-plugins; it publishes no macOS lookdev package.
  ost plugin build plugins/vrmSchema --profile lookdev
  ost plugin build plugins/vrmImaging --profile lookdev
  ost plugin package plugins/vrmImaging --profile lookdev
  ost artifact import plugins/vrmImaging/dist/plugins/vrmImaging/0.10.0/cy2026-macos-arm64-py313-lookdev
  ```

  With those, `usdview` can show whether `vrmImaging` contributes its
  container on macOS, but with Storm; that is `usd-vrm-plugins`' evidence,
  not this renderer's.
