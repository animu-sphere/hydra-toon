# Release records

Each released version gets an immutable record here: what it established,
compatibility notes, and known limitations. Release records are history —
once written for a released version they are not rewritten, except to keep a
link resolving. What comes next is the [roadmap](../roadmap/).

| Version | Record | Milestone |
| --- | --- | --- |
| v0.2.0 | [v0.2.0.md](v0.2.0.md) | MToon quality, the Hydra-fed viewport, measured 4x AA and VRM reproduction; published package GPU-verified in [report 33](../reports/renderer/33-2026-10-04-published-v020.md) |
| v0.1.0 | [v0.1.0.md](v0.1.0.md) | Foundation: the render world, the persistent Vulkan renderer, `hdToon`, opaque MToon with textures, GPU skinning and the inverted-hull outline, and the release lane |

## Versioning

Each `v0.x.0` is a milestone
([design policy §25](../design/DESIGN_POLICY.md#25-implementation-phases)):
the version says how far the renderer has come, not how much changed, and it
is released when the milestone's work is done. Ordinary fixes, improvements
and compatibility work go into the next `v0.x.0`.

A patch release (`v0.x.1`) is cut only when the next milestone cannot wait:
a published package that is unusable in practice, a serious crash, a missing
release artifact, a serious packaging, ABI or dependency mismatch, a security
fix, or a serious regression.

## Release gate

Before a `v0.x.0` is tagged, the validation its milestone needs has passed:

- **Build** on Windows, with plain CMake and with OpenStrata. Linux joins
  the gate with
  [platform and GPU coverage](../roadmap/later.md#platform-and-gpu-coverage),
  not before
  ([design policy §25](../design/DESIGN_POLICY.md#25-implementation-phases)).
- **Tests** of what the tree implements: the core, render extraction, the
  backend, materials, skinning, morphs, the Hydra adapter, the install tree,
  a host smoke test and the viewport's presentation test.
- **GPU evidence.** CI without a GPU never completes a renderer release. On
  at least one real GPU, Vulkan validation, a representative scene and a
  representative avatar, with frame evidence and performance evidence, each
  a [report](../reports/).
- **Packaging**: the GitHub release, a source archive, the binary package,
  its manifest and SPDX SBOM, checksums, the OCI package on GHCR, and the
  digests a Formation pins.

## How a release is cut

A tag `vX.Y.Z` starts
[`.github/workflows/release.yml`](../../.github/workflows/release.yml). Before
tagging:

1. `[project].version` in `openstrata.toml` and `project(Toon VERSION)` in
   `CMakeLists.txt` are both `X.Y.Z`; `python scripts/release_version.py
   --tag vX.Y.Z` checks it.
2. `CHANGELOG.md`'s `[Unreleased]` section is renamed
   `## [X.Y.Z] - YYYY-MM-DD`, and a new, empty `[Unreleased]` section sits
   above it.
3. This version's record is written here and listed in the table above, and
   the version's page in the [roadmap](../roadmap/) is deleted.
4. A dry run of the workflow (`workflow_dispatch` on `main`) is green.

The workflow then:

- **preflight** — refuses a tag that differs from either version, or a
  changelog section without a date.
- **build** — on Windows x86_64 with the canonical CY2026 `lookdev` runtime,
  the Vulkan SDK and `ost`, each pinned by digest: builds, tests and
  validates the `hydra` intent, packages it twice and requires the same
  archive digest, and requires the package to carry `hdToon` and every scene
  shader. The runner has no Vulkan device, so the GPU assertions are an
  explained `SKIP`; the evidence is kept as a workflow artifact.
- **publish** — pushes the package to `ghcr.io/animu-sphere/hydra-toon`,
  tagged `toon-X.Y.Z-<target>`, and writes `toon-package-pins.json`:
  the archive digest a Formation names as `artifact`, and the OCI digest to
  pull it from.
- **release** — creates a **draft** GitHub release with the package, its
  manifest and SBOM, the pin table, a source archive and `SHA256SUMS`, and
  notes rendered from the changelog through
  [RELEASE_NOTES_TEMPLATE.md](../contributing/RELEASE_NOTES_TEMPLATE.md)
  (`scripts/make_release_notes.py`).

A human publishes the draft. The first push of a package creates it private
on GHCR; making it public is a one-time step in its package settings. Then a
Formation is re-pinned to the published package and run on a GPU, and that
run is a [report](../reports/).

A dry run builds, tests and packages the same way and uploads everything as
workflow artifacts; it pushes nothing and creates no release.
