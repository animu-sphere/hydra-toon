#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Render GitHub release notes for one version from the changelog + template.

Fills ``docs/contributing/RELEASE_NOTES_TEMPLATE.md`` with:

* ``{version}`` / ``{tag}`` — the release version (default: the project's,
  from ``scripts/release_version.py``).
* ``{changelog}``           — that version's ``## [X.Y.Z] - YYYY-MM-DD``
                              section of ``CHANGELOG.md``, heading dropped.
* ``{checksums}``           — contents of a ``SHA256SUMS`` file, when the
                              release workflow passes one.

The section must be *finalized*: headed by the version and a release date.
Tagging with an unfinished changelog is the mistake this guard exists to
catch. ``--allow-unreleased`` is for a dry run: it accepts a heading without a
date and, when the version has no section yet, renders ``## [Unreleased]``.

Usage (what .github/workflows/release.yml runs):

    python scripts/make_release_notes.py --version 0.1.0 \
        --checksums dist-release/SHA256SUMS --out release-notes.md
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import release_version  # noqa: E402


def changelog_section(changelog: str, name: str) -> tuple[str, str] | None:
    """Return (heading, body) of the ``## [name]`` section, or None."""

    lines = changelog.splitlines()
    start = next((i for i, line in enumerate(lines)
                  if re.match(rf"^## \[{re.escape(name)}\]", line)), None)
    if start is None:
        return None
    end = next((j for j in range(start + 1, len(lines))
                if lines[j].startswith("## ")), len(lines))
    return lines[start], "\n".join(lines[start + 1:end]).strip()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", help="release version X.Y.Z (default: the project's)")
    parser.add_argument("--changelog", default=str(REPO_ROOT / "CHANGELOG.md"))
    parser.add_argument("--template",
                        default=str(REPO_ROOT / "docs" / "contributing" /
                                    "RELEASE_NOTES_TEMPLATE.md"))
    parser.add_argument("--checksums", help="SHA256SUMS file to inline into the notes")
    parser.add_argument("--allow-unreleased", action="store_true",
                        help="dry run: accept an undated heading, or [Unreleased]")
    parser.add_argument("--out", help="write the notes here (default: stdout)")
    args = parser.parse_args(argv)

    version = args.version or release_version.manifest_version()
    changelog = pathlib.Path(args.changelog).read_text(encoding="utf-8")
    section = changelog_section(changelog, version)
    if section is None and args.allow_unreleased:
        section = changelog_section(changelog, "Unreleased")
    if section is None:
        raise SystemExit(f"ERROR: CHANGELOG.md has no '## [{version}]' section")
    heading, body = section
    finalized = re.match(rf"^## \[{re.escape(version)}\] - \d{{4}}-\d{{2}}-\d{{2}}$", heading)
    if not finalized and not args.allow_unreleased:
        raise SystemExit(
            f"ERROR: changelog heading is not finalized: {heading!r}\n"
            f"Write it as '## [{version}] - YYYY-MM-DD' before tagging "
            f"(or pass --allow-unreleased for a dry run).")
    if not body:
        raise SystemExit(f"ERROR: the changelog section {heading!r} is empty")

    checksums = "(appended by the release workflow)"
    if args.checksums:
        checksums = pathlib.Path(args.checksums).read_text(encoding="utf-8").strip()

    notes = pathlib.Path(args.template).read_text(encoding="utf-8")
    for key, value in {
        "{version}": version,
        "{tag}": f"v{version}",
        "{changelog}": body,
        "{checksums}": checksums,
    }.items():
        notes = notes.replace(key, value)

    if args.out:
        pathlib.Path(args.out).write_text(notes, encoding="utf-8", newline="\n")
        print(f"wrote release notes for v{version} -> {args.out}")
    else:
        sys.stdout.write(notes)
    return 0


if __name__ == "__main__":
    sys.exit(main())
