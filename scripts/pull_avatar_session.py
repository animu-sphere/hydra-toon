#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pull a pinned avatar session and its owners, and print the prefixes to build against.

`adapters/viewport/avatar-session-pins.json` names each library by the digests of a
published `usd-avatar-runtime` release. `ost` verifies every digest when it pulls and
extracts. Pass the printed list as `CMAKE_PREFIX_PATH`, and the session's prefix, the
first entry, as `TOON_AVATAR_RUNTIME_INCLUDE_DIR` with `/include` appended.

Usage:

    python scripts/pull_avatar_session.py vrm --target cy2026-windows-x86_64-py313-usd \
        --out build/avatar-session
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
PINS = ROOT / "adapters" / "viewport" / "avatar-session-pins.json"


def run(*command: str) -> None:
    subprocess.run(command, check=True, stdout=sys.stderr)


def main() -> int:
    pins = json.loads(PINS.read_text(encoding="utf-8"))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("session", choices=sorted(pins))
    parser.add_argument("--target", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    prefixes = []
    for library in pins[args.session]["libraries"]:
        pin = library["targets"].get(args.target)
        if pin is None:
            parser.error(f"{library['id']} has no pin for {args.target}")
        prefix = args.out.resolve() / library["id"] / pin["digest"].removeprefix("sha256:")
        if not prefix.is_dir():
            run("ost", "artifact", "pull", pin["source"], "--expect-artifact", pin["digest"],
                "--require-target", args.target)
            run("ost", "artifact", "extract", pin["digest"], "--into", str(prefix))
        prefixes.append(prefix.as_posix())
    print(";".join(prefixes))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
