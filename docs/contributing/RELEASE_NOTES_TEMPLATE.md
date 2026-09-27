# hydra-toon {tag}

`hdToon` — a low-latency, avatar-first Hydra raster renderer on Vulkan, with
its host-neutral core, for VRM / MToon avatars in an OpenUSD `usdview`
session.

- **Release record:** [docs/releases/{tag}.md](https://github.com/animu-sphere/hydra-toon/blob/{tag}/docs/releases/{tag}.md)
- **Capability matrix:** [CAPABILITY_MATRIX.md](https://github.com/animu-sphere/hydra-toon/blob/{tag}/docs/reference/CAPABILITY_MATRIX.md)
- **Supported configurations:** [SUPPORTED_CONFIGURATIONS.md](https://github.com/animu-sphere/hydra-toon/blob/{tag}/docs/reference/SUPPORTED_CONFIGURATIONS.md)
- **Building:** [BUILDING.md](https://github.com/animu-sphere/hydra-toon/blob/{tag}/docs/guides/BUILDING.md)

{changelog}

## Artifacts

| Artifact | Contents |
| --- | --- |
| `toon-{version}-<target>--hydra.tar.zst` | The `hydra` intent's OpenStrata `renderer` package for `<target>`: the `hdToon` Hydra plugin with its SPIR-V shaders, the core and Vulkan backend libraries and headers, and `toon-headless` |
| `toon-{version}-<target>--hydra.manifest.json` | Its OpenStrata manifest: every file's digest, the component's environment and compatibility, and provenance |
| `toon-{version}-<target>--hydra.sbom.spdx.json` | Its SPDX SBOM |
| `toon-package-pins.json` | The digests a Formation pins, as below |
| `hydra-toon-{version}-src.tar.gz` | Source archive at this tag |
| `SHA256SUMS` | SHA-256 checksums of every file above |

> **Built without a GPU.** The release lane runs on a hosted runner with no
> Vulkan device, so its GPU assertions report an explained `SKIP`. What the
> renderer draws on a GPU is in the release record's reports, measured on a
> workstation.
>
> **Compose it; do not install it alone.** The package needs the canonical
> `lookdev` runtime it was built against, and a VRM avatar needs
> `usd-vrm-plugins`' `vrmImaging` beside it. An OpenStrata Formation composes
> the three.

## SHA-256 checksums

```text
{checksums}
```
