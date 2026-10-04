#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare current-pose outline occlusion through the Hydra-fed viewport.

Register vrmImaging and its DLL dependencies in the process environment.
All evidence is local; the fixture needs no avatar or third-party Python library.
"""

import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def evaluate(viewport, output):
    fixture = Path(__file__).resolve().parents[1] / "adapters/viewport/tests/outline-occlusion-motion.usda"
    output.mkdir(parents=True, exist_ok=True)
    evidence = []

    def capture(name, time, frames, samples, culling, hulls, outlines="on"):
        image = output / f"{name}.ppm"
        command = [
            str(viewport), "--usd", str(fixture), "--hidden", "--vsync", "off",
            "--overlay", "off", "--width", "256", "--height", "256",
            "--samples", str(samples), "--frames", str(frames), "--time", str(time),
            "--expect-draws", "2", "--expect-hulls", str(hulls),
            "--outline-culling", culling, "--outlines", outlines,
            "--screenshot", str(image),
        ]
        if frames == 33:
            command.extend(("--time-step", "0.25"))
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60)
        (output / f"{name}.log").write_text(result.stdout, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{name}: viewport exited {result.returncode}; see its log")
        if "draws_mtoon=2" not in result.stdout or "draws_skinned=1" not in result.stdout:
            raise RuntimeError(f"{name}: MToon/GPU skinning missing; register vrmImaging")
        if f", {samples} sample(s) per pixel," not in result.stdout:
            raise RuntimeError(f"{name}: requested sample count was not selected")
        uploads = next(line for line in result.stdout.splitlines() if line.startswith("Uploads:"))
        poses = 29 if frames == 33 else 1
        if f"topology=2 points=2 materials=3 textures=0 skins=1 poses={poses} " not in uploads:
            raise RuntimeError(f"{name}: unexpected updates: {uploads}")
        digest = hashlib.sha256(image.read_bytes()).hexdigest()
        evidence.append({"name": name, "command": command, "sha256": digest, "uploads": uploads})
        return digest

    cases = (
        ("covered", 1, 2, 0),
        ("inside", 2, 2, 0),
        ("edge", 3, 2, 1),
        ("blocker-away", 4, 2, 1),
        ("covered-again", 5, 2, 0),
        ("blocker-behind", 6, 2, 1),
        ("edge-return", 7, 2, 1),
        ("inside-return", 8, 2, 0),
        ("returned", 9, 2, 0),
        ("motion-return", 1, 33, 0),
    )
    for samples in (1, 4):
        hashes = {}
        for label, time, frames, hulls in cases:
            name = f"{label}-{samples}x"
            actual = capture(f"{name}-on", time, frames, samples, "on", hulls)
            reference = capture(f"{name}-off", time, frames, samples, "off", 1)
            if actual != reference:
                raise RuntimeError(f"{name}: culling changed the image")
            if hulls:
                surface = capture(f"{name}-surface", time, frames, samples, "on", 0, "off")
                if actual == surface:
                    raise RuntimeError(f"{name}: expected a visible outline")
            hashes[label] = actual
            print(f"{name}: identical, hulls {hulls}/1", flush=True)
        if hashes["covered"] != hashes["returned"] or hashes["covered"] != hashes["motion-return"]:
            raise RuntimeError(f"{samples}x: motion return changed the initial image")
    (output / "summary.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--viewport", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("build/outline-occlusion"))
    args = parser.parse_args()
    evaluate(args.viewport.resolve(), args.output.resolve())


if __name__ == "__main__":
    main()
