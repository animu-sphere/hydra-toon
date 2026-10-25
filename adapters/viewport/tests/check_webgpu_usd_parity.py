# SPDX-License-Identifier: Apache-2.0
"""Vulkan/WebGPU parity of one USD stage in the viewport (WebGPU policy §9).

Draws the stage on both backends at each sample count, over the solid and
checkerboard backgrounds, captures the presented image and the straight
foreground, and compares them per channel: the maximum and mean absolute
difference and the fraction of pixels more than one step apart, both as
encoded and in the linear 8-bit steps of the scene target the composite
encodes from, where the limits apply (an encoded step is narrower than a
linear one in the dark). The scene,
upload, draw, hull and shadow-view statistics the viewport prints must match
exactly. Writes OUTPUT.json and fails when a difference passes the limits.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_png import read_png  # noqa: E402

# The linear 8-bit values of the scene target that encode to each sRGB
# code, as the composite encodes them; a code none encodes to stands for its
# nearest decoded value. Two codes are as far apart as their nearest
# linear values.
def _encode(linear):
    return 12.92 * linear if linear <= 0.0031308 else 1.055 * linear ** (1 / 2.4) - 0.055


def _ranges():
    found = {}
    for v in range(256):
        code = round(255 * _encode(v / 255))
        low, high = found.get(code, (v, v))
        found[code] = (min(low, v), max(high, v))
    for code in range(256):
        if code not in found:
            decoded = code / 255 / 12.92 if code <= 10 else ((code / 255 + 0.055) / 1.055) ** 2.4
            found[code] = (round(255 * decoded),) * 2
    return [found[code] for code in range(256)]


LINEAR = _ranges()
LINEAR_DISTANCE = [[max(0, LINEAR[b][0] - LINEAR[a][1], LINEAR[a][0] - LINEAR[b][1])
                    for b in range(256)] for a in range(256)]

# Lines that are a fact of the scene and its extraction, not of the backend.
EXACT = ("Scene summary:", "Uploads:", "Draw calls:", "Hull draws:", "Late latch:",
         "Shadow views:", "Animation:")


def read_ppm(path):
    data = path.read_bytes()
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    assert match and match.group(3) == b"255", path
    width, height = int(match.group(1)), int(match.group(2))
    pixels = data[match.end():]
    assert len(pixels) == width * height * 3, path
    return width, height, pixels


def compare(first, second, channels, alpha=None):
    """Channel differences as encoded, or, given the alpha channel's index (or
    one past the last channel when there is none), in linear steps."""
    w1, h1, a = first
    w2, h2, b = second
    assert (w1, h1) == (w2, h2), "the captures differ in size"
    distance = None
    if alpha is not None:
        distance = [None if c == alpha else LINEAR_DISTANCE for c in range(channels)]
    pixels = w1 * h1
    channel_max = [0] * channels
    channel_sum = [0] * channels
    channel_over = [0] * channels
    over = 0
    for start in range(0, pixels * channels, channels):
        apart = False
        for c in range(channels):
            if distance and distance[c]:
                d = distance[c][a[start + c]][b[start + c]]
            else:
                d = abs(a[start + c] - b[start + c])
            if d:
                channel_sum[c] += d
                if d > channel_max[c]:
                    channel_max[c] = d
                if d > 1:
                    channel_over[c] += 1
                    apart = True
        over += apart
    return {
        "width": w1, "height": h1,
        "max": max(channel_max),
        "channels": [{"max": channel_max[c], "mean": channel_sum[c] / pixels,
                      "over_one": channel_over[c] / pixels} for c in range(channels)],
        "over_one": over / pixels,
    }


def run(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    if result.returncode == 77:
        print("SKIP: no presentation device", result.stdout, result.stderr)
        raise SystemExit(0)
    if result.returncode != 0:
        raise SystemExit(f"{command} failed ({result.returncode}):\n{result.stdout}\n{result.stderr}")
    return result.stdout


def facts(output):
    lines = [line.strip() for line in output.splitlines() if line.startswith(EXACT)]
    # The overlay's own draws are a presentation detail; the overlay is off.
    # Pipeline binds are how a backend records, not what it draws: Vulkan
    # binds the unlit pipeline even when no unlit draw follows.
    return [re.sub(r" (overlay(_textures)?|pipeline_binds)=\d+", "", line) for line in lines]


def binds(output):
    found = re.search(r"pipeline_binds=(\d+)", output)
    return int(found.group(1)) if found else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--viewport", required=True, type=Path)
    parser.add_argument("--scene", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--samples", default="1,4")
    parser.add_argument("--frames", default=4, type=int)
    parser.add_argument("--width", default=640, type=int)
    parser.add_argument("--height", default=360, type=int)
    parser.add_argument("--max", type=int, help="largest channel difference allowed")
    parser.add_argument("--over-one", type=float, help="largest fraction more than one step apart")
    parser.add_argument("extra", nargs=argparse.REMAINDER, help="viewport arguments after --")
    arguments = parser.parse_args()
    extra = [a for a in arguments.extra if a != "--"]
    arguments.output.parent.mkdir(parents=True, exist_ok=True)

    record = {"scene": arguments.scene.name, "arguments": extra, "runs": []}
    failures = []
    for samples in arguments.samples.split(","):
        for background in ("solid", "checkerboard"):
            images, outputs, bound = {}, {}, {}
            for backend in ("vulkan", "webgpu"):
                stem = arguments.output.parent / (
                    f"{arguments.output.name}-{samples}x-{background}-{backend}")
                output = run([arguments.viewport, "--backend", backend, "--hidden",
                              "--width", arguments.width, "--height", arguments.height,
                              "--frames", arguments.frames, "--vsync", "off", "--overlay", "off",
                              "--samples", samples, "--background", background,
                              "--usd", arguments.scene, *extra,
                              "--screenshot", stem.with_suffix(".ppm"),
                              "--foreground-output", stem.with_suffix(".png")])
                device = re.search(r"Device: ([^\r\n]+)", output)
                record.setdefault("device", device.group(1) if device else "")
                images[backend] = (read_ppm(stem.with_suffix(".ppm")),
                                   read_png(stem.with_suffix(".png")))
                outputs[backend] = facts(output)
                bound[backend] = binds(output)
            run_record = {"samples": int(samples), "background": background,
                          "presented": compare(images["vulkan"][0], images["webgpu"][0], 3, 3),
                          "foreground": compare(images["vulkan"][1], images["webgpu"][1], 4, 3),
                          "presented_encoded": compare(images["vulkan"][0], images["webgpu"][0], 3),
                          "foreground_encoded": compare(images["vulkan"][1], images["webgpu"][1], 4),
                          "facts": outputs["vulkan"], "pipeline_binds": bound}
            name = f"{samples}x {background}"
            if outputs["vulkan"] != outputs["webgpu"]:
                failures.append(f"{name}: statistics differ:\n  vulkan {outputs['vulkan']}\n"
                                f"  webgpu {outputs['webgpu']}")
            for product in ("presented", "foreground"):
                result = run_record[product]
                if arguments.max is not None and result["max"] > arguments.max:
                    failures.append(f"{name} {product}: max {result['max']} > {arguments.max}")
                if arguments.over_one is not None and result["over_one"] > arguments.over_one:
                    failures.append(f"{name} {product}: {result['over_one']:.2e} of pixels more "
                                    f"than one step apart > {arguments.over_one:.2e}")
                encoded = run_record[product + "_encoded"]
                print(f"{arguments.scene.name} {name} {product}: linear max {result['max']} "
                      f"over-one {result['over_one']:.2e} mean "
                      f"{max(c['mean'] for c in result['channels']):.2e}; encoded max "
                      f"{encoded['max']} over-one {encoded['over_one']:.2e}")
            record["runs"].append(run_record)
    arguments.output.with_suffix(".json").write_text(json.dumps(record, indent=2) + "\n")
    if failures:
        raise SystemExit("\n".join(failures))


if __name__ == "__main__":
    main()
