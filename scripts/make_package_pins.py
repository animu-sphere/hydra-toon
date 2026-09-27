#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Write the pin table for the renderer packages a release publishes.

The release lane's ``publish`` job pushes each ``toon`` package (today the
``hydra`` intent's, for a Formation on the canonical CY2026 ``lookdev``
runtime) to GHCR. A Formation needs two digests per package: the archive
digest, which it names as the component's ``artifact``, and the OCI manifest
digest to pull it from, which changes on every republish. This script writes
both, from the rows the job actually pushed, as ``toon-package-pins.json``
and a Markdown section for the release notes.

Rows are tab-separated: ``name version target tag archive_digest
[oci_digest]``, where ``tag`` is the OCI tag, the archive's name without
``.tar.zst``. A dry run passes rows without an OCI digest and no
``--published``; the table then names no source and says so.

Usage (what .github/workflows/release.yml runs):

    python scripts/make_package_pins.py --rows .ost-ci/pushed.tsv \
        --version 0.1.0 --repository ghcr.io/animu-sphere/hydra-toon \
        --out pins --published
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys


def read_rows(path: pathlib.Path, repository: str, published: bool) -> list[dict]:
    packages: list[dict] = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        fields = line.split("\t")
        if len(fields) not in (5, 6):
            raise SystemExit(f"{path}:{number}: expected 5 or 6 fields, got {len(fields)}")
        name, version, target, tag, artifact = fields[:5]
        oci = fields[5] if len(fields) == 6 else ""
        if published and not oci:
            raise SystemExit(f"{path}:{number}: {tag} was published but has no OCI digest")
        if any(p["tag"] == tag for p in packages):
            raise SystemExit(f"{path}:{number}: {tag} appears twice")
        entry = {"name": name, "version": version, "target": target,
                 "tag": tag, "artifact": artifact}
        if oci:
            entry["source"] = f"oci://{repository}@{oci}"
        packages.append(entry)
    if not packages:
        raise SystemExit(f"{path}: no package rows")
    return sorted(packages, key=lambda p: p["tag"])


def render_markdown(packages: list[dict], repository: str, published: bool) -> str:
    md = ["## Renderer packages", ""]
    if not published:
        md += ["> Dry run: nothing was pushed, so no source is named. "
               "The digests are this build's.", ""]
    md += ["For a Formation on the canonical CY2026 `lookdev` runtime. Pull "
           "by the OCI digest, then name the archive digest as the `toon` "
           "component's `artifact`.", "",
           "| Package | Target | Archive digest (`artifact`) | Pull from |",
           "| --- | --- | --- | --- |"]
    for p in packages:
        source = f"`{p['source']}`" if "source" in p else "not published"
        md.append(f"| `{p['name']}` {p['version']} | `{p['target']}` "
                  f"| `{p['artifact']}` | {source} |")
    if published:
        md += ["", "```sh"]
        md += [f"ost artifact pull {p['source']} --expect-artifact {p['artifact']}"
               for p in packages]
        md.append("```")
    return "\n".join(md) + "\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rows", type=pathlib.Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--published", action="store_true")
    args = parser.parse_args(argv)

    packages = read_rows(args.rows, args.repository, args.published)
    args.out.mkdir(parents=True, exist_ok=True)
    document = {"version": args.version, "published": args.published,
                "repository": args.repository, "packages": packages}
    (args.out / "toon-package-pins.json").write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (args.out / "toon-package-pins.md").write_text(
        render_markdown(packages, args.repository, args.published), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
