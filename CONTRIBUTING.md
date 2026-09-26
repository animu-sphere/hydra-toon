# Contributing to Hydra Toon

Thank you for helping improve Hydra Toon. Please read the
[Code of Conduct](CODE_OF_CONDUCT.md) before participating. Report suspected
vulnerabilities through the [security policy](SECURITY.md), not a public issue.

## Before you start

- Search existing issues and the [roadmap](docs/roadmap/README.md). For larger
  changes, open an issue first to agree on scope.
- Check the [integration scope](docs/design/INTEGRATION_SCOPE_POLICY.md) and
  [project layout](docs/architecture/PROJECT_LAYOUT.md). Source formats, USD
  schemas, motion semantics and device input belong to sibling projects.
- Check the [capability matrix](docs/reference/CAPABILITY_MATRIX.md) before
  describing a feature as implemented.

## Make a change

1. Fork the repository and make a focused branch.
2. Follow the [building and testing guide](docs/guides/BUILDING.md) for
  prerequisites and platform-specific setup. Build and test with plain CMake:

  ```sh
  cmake -S . -B build/plain-cmake -G Ninja
  cmake --build build/plain-cmake
  ctest --test-dir build/plain-cmake --output-on-failure
  ```

  To validate the OpenStrata renderer workflow as well, run:

   ```sh
   ost build --check
   ost build --jobs auto
   ost test
   ost validate
   ```

3. Add or update focused tests when behavior changes. For Hydra or viewport
   changes, run the corresponding commands in the building guide when the
   required runtime or hardware is available.
4. Update the owning documentation for changes to public behavior, structure
   or capability. Follow the [documentation guidelines](docs/contributing/documentation.md);
   do not add non-redistributable models, textures or captures.
5. Open a pull request describing the change, linking any related issue,
   listing the checks you ran and explaining any checks you could not run.

Contributions are submitted under the project's [Apache-2.0 license](LICENSE).