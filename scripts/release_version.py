#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Print the project version, and fail when its two declarations disagree.

The version is declared twice: ``VERSION``, whose one line ``CMakeLists.txt``
reads into ``project(Toon VERSION)`` and the installed
``ToonConfigVersion.cmake`` carries, and ``[project].version`` in
``openstrata.toml``, which names the package ``ost package`` writes. A
release needs both to be the tag's version.

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


def file_version() -> str:
    lines = (REPO_ROOT / "VERSION").read_text(encoding="utf-8").splitlines()
    if not lines:
        raise SystemExit("ERROR: VERSION is empty")
    return lines[0].strip()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tag", help="the release tag, vX.Y.Z, that both must equal")
    args = parser.parse_args(argv)

    manifest, version = manifest_version(), file_version()
    if manifest != version:
        raise SystemExit(
            f"ERROR: openstrata.toml declares {manifest}, VERSION {version}")
    if not SEMVER.match(manifest):
        raise SystemExit(f"ERROR: version {manifest!r} is not X.Y.Z")
    if args.tag is not None and args.tag != f"v{manifest}":
        raise SystemExit(
            f"ERROR: tag {args.tag} does not match the project version {manifest}")
    print(manifest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
