# Changelog

All notable changes to `hydra-toon` are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Each released
version will have a record in [docs/releases/](docs/releases/README.md).

## [Unreleased]

### Added

- The project, generated with `ost init --template renderer --name toon`
  (OpenStrata 0.23.6, template 0.5.1): the host-neutral core, the Vulkan
  backend, the headless runner, the standalone viewport and the `hdToon`
  Hydra adapter, all drawing the template's bootstrap triangle.
- A `hydra` build intent in `openstrata.toml` that builds the Hydra adapter.
- Documentation: the design policy, integration scope and material policy;
  the project layout; the capability matrix and measured configurations; the
  roadmap; the building guide; and the first `ost` dogfooding report.

### Changed

- The Hydra renderer plugin reads `gpuEnabled` through the
  `HdRendererCreateArgs` container schema on OpenUSD 26.08 and later
  (`HD_API_VERSION` 98), which the template's code did not compile against.
- MSVC builds use `/utf-8`, so a CP932 host does not warn C4819 on the UTF-8
  sources.
- `openstrata.renderer.yaml` declares one frame context, matching the
  one-frame-in-flight policy and the generated swapchain path.
