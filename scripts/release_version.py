#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Print the project version, and fail when its two declarations disagree.

The version is declared twice: ``[project].version`` in ``openstrata.toml``,
which names the package ``ost package`` writes, and ``project(Toon VERSION)``
in ``CMakeLists.txt``, which the installed ``ToonConfigVersion.cmake``
carries. A release needs both to be the tag's version.

Usage (what .github/workflows/release.yml runs):

    python scripts/release_version.py              # prints 0.1.0
    python scripts/release_version.py --tag v0.1.0 # also requires the tag
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys
import tomllib

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SEMVER = re.compile(r"^\d+\.\d+\.\d+$")


def manifest_version() -> str:
    with (REPO_ROOT / "openstrata.toml").open("rb") as f:
        return tomllib.load(f)["project"]["version"]


def cmake_version() -> str:
    text = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    m = re.search(r"project\(\s*Toon\b[^)]*?\bVERSION\s+([0-9.]+)", text, re.S)
    if not m:
        raise SystemExit("ERROR: CMakeLists.txt declares no project(Toon VERSION ...)")
    return m.group(1)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tag", help="the release tag, vX.Y.Z, that both must equal")
    args = parser.parse_args(argv)

    manifest, cmake = manifest_version(), cmake_version()
    if manifest != cmake:
        raise SystemExit(
            f"ERROR: openstrata.toml declares {manifest}, CMakeLists.txt {cmake}")
    if not SEMVER.match(manifest):
        raise SystemExit(f"ERROR: version {manifest!r} is not X.Y.Z")
    if args.tag is not None and args.tag != f"v{manifest}":
        raise SystemExit(
            f"ERROR: tag {args.tag} does not match the project version {manifest}")
    print(manifest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
