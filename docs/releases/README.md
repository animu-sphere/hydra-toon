# Release records

Each released version gets an immutable record here: its objective, the
capabilities it shipped, compatibility notes, and known limitations. Release
records are history — once written for a released version they are not
rewritten; new work goes to a new record. Active, incomplete work lives in the
[roadmap](../roadmap/), not here.

| Version | Record | Theme |
| --- | --- | --- |
| — | — | Nothing has been released. v0.1.0 is planned to carry Renderer Phase 0 ([status table](../roadmap/README.md#status-at-a-glance)). |

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
   the [status table](../roadmap/README.md#status-at-a-glance) names what it
   carries.
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
  tagged `toon-X.Y.Z-<target>--hydra`, and writes `toon-package-pins.json`:
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
