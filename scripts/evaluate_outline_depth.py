#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare animated depth-plane hull omission in the actual viewport.

Requires a Hydra viewport and registered vrmImaging. Plugin registration
and DLL paths are inherited; no external model or Python library is needed.
"""

import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def evaluate(viewport, output):
    fixture = Path(__file__).resolve().parents[1] / "adapters/viewport/tests/outline-depth-motion.usda"
    output.mkdir(parents=True, exist_ok=True)
    evidence = []
    cases = (
        ("visible", 1, 2, 1),
        ("near-boundary", 2, 2, 1),
        ("near-outside", 3, 2, 0),
        ("far-boundary", 6, 2, 1),
        ("far-outside", 7, 2, 0),
        ("motion-return", 1, 33, 1),
    )

    def capture(name, time, frames, hulls, samples, culling, outlines="on"):
        image = output / f"{name}.ppm"
        command = [
            str(viewport), "--usd", str(fixture), "--hidden", "--vsync", "off",
            "--overlay", "off", "--width", "256", "--height", "256",
            "--samples", str(samples), "--frames", str(frames), "--time", str(time),
            "--expect-draws", "1", "--expect-hulls", str(hulls),
            "--outline-culling", culling, "--outlines", outlines,
            "--screenshot", str(image),
        ]
        if frames == 33:
            command.extend(("--time-step", "0.25"))
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60)
        (output / f"{name}.log").write_text(result.stdout, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{name}: viewport exited {result.returncode}; see its log")
        if "draws_mtoon=1" not in result.stdout or "draws_skinned=1" not in result.stdout:
            raise RuntimeError(f"{name}: MToon GPU skinning is missing; register vrmImaging")
        if f", {samples} sample(s) per pixel," not in result.stdout:
            raise RuntimeError(f"{name}: requested MSAA count was not selected")
        uploads = next(line for line in result.stdout.splitlines() if line.startswith("Uploads:"))
        expected_poses = 33 if frames == 33 else 1
        if f"topology=1 points=1 materials=2 textures=0 skins=1 poses={expected_poses} " not in uploads:
            raise RuntimeError(f"{name}: unexpected uploads: {uploads}")
        digest = hashlib.sha256(image.read_bytes()).hexdigest()
        evidence.append({"name": name, "command": command, "sha256": digest, "uploads": uploads})
        return digest

    for samples in (1, 4):
        hashes = {}
        for label, time, frames, hulls in cases:
            name = f"{label}-{samples}x"
            actual = capture(f"{name}-on", time, frames, hulls, samples, "on")
            reference = capture(f"{name}-off", time, frames, 1, samples, "off")
            if actual != reference:
                raise RuntimeError(f"{name}: culling changed the image")
            hashes[label] = actual
            if label.endswith("boundary"):
                surface = capture(f"{name}-surface", time, frames, 0, samples, "on", "off")
                if actual == surface:
                    raise RuntimeError(f"{name}: boundary does not exercise visible outline pixels")
            print(f"{name}: identical, hulls {hulls}/1", flush=True)
        if hashes["visible"] != hashes["motion-return"]:
            raise RuntimeError(f"{samples}x: returning pose differs from the initial pose")
    (output / "summary.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--viewport", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("build/outline-depth"))
    args = parser.parse_args()
    evaluate(args.viewport.resolve(), args.output.resolve())


if __name__ == "__main__":
    main()
