# SPDX-License-Identifier: Apache-2.0
"""Decode PNG with the standard library; check the consumer, independently of stb."""
import argparse
from pathlib import Path
import struct
import subprocess
import zlib


def read_png(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    offset, compressed, srgb, ended = 8, bytearray(), False, False
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        name = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + size]
        crc = struct.unpack_from(">I", data, offset + 8 + size)[0]
        assert zlib.crc32(name + body) == crc, (path, name, "CRC")
        if name == b"IHDR":
            width, height, depth, kind, compression, filtering, interlace = struct.unpack(">IIBBBBB", body)
            assert (depth, kind, compression, filtering, interlace) == (8, 6, 0, 0, 0)
        elif name == b"sRGB":
            assert body == b"\0" and not compressed
            srgb = True
        elif name == b"IDAT":
            compressed.extend(body)
        elif name == b"IEND":
            ended = True
        offset += size + 12
    assert srgb and ended and offset == len(data), "missing encoding metadata or terminator"
    raw = zlib.decompress(compressed)
    stride = width * 4
    assert len(raw) == height * (stride + 1)
    pixels, previous = bytearray(), bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        mode = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        for x in range(stride):
            a = row[x - 4] if x >= 4 else 0
            b = previous[x]
            c = previous[x - 4] if x >= 4 else 0
            p = a + b - c
            distances = (abs(p - a), abs(p - b), abs(p - c))
            paeth = (a, b, c)[distances.index(min(distances))]
            assert 0 <= mode <= 4
            row[x] = (row[x] + (0, a, b, (a + b) // 2, paeth)[mode]) & 255
        pixels.extend(row)
        previous = row
    return width, height, pixels


def run(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True, encoding="utf-8")
    if result.returncode == 77:
        print("SKIP: no presentation device", result.stdout, result.stderr)
        raise SystemExit(77)
    assert result.returncode == 0, (command, result.stdout, result.stderr)
    return result.stdout


def encode(linear):
    return 12.92 * linear if linear <= 0.0031308 else 1.055 * linear ** (1 / 2.4) - 0.055


def decode(encoded):
    return encoded / 12.92 if encoded <= 0.04045 else ((encoded + 0.055) / 1.055) ** 2.4


def check_pixel(actual, source):
    alpha = source[3]
    assert actual[3] == alpha
    for channel in range(3):
        linear = min(source[channel] / alpha, 1) if alpha else 0
        assert abs(actual[channel] - encode(linear) * 255) <= 0.501, (actual, source)
        # Straight encoded PNG recomposes in linear space with preserved alpha.
        for background in (0, 1, 0.37):
            restored = decode(actual[channel] / 255) * alpha / 255 + background * (1 - alpha / 255)
            expected = (source[channel] / 255 if alpha else 0) + background * (1 - alpha / 255)
            assert abs(restored - expected) < 0.005


def check_writer(writer, output):
    path = output / "前景.png"
    run([writer, path])
    width, height, pixels = read_png(path)
    assert (width, height) == (256, 3)
    for y in range(height):
        for x in range(width):
            if y == 0:
                source = (x // 4, x // 2, x, x)
            elif y == 1:
                source = (1 if x else 255, 0, x if x else 255, x)
            else:
                source = (1, 64, 255, 255)
            at = (y * width + x) * 4
            check_pixel(pixels[at:at + 4], source)
    print("PNG decode: alpha 0..255, dark colours, linear unpremultiply, sRGB, rows and CRC passed")


def check_viewport(viewport, output):
    for samples in (1, 4):
        stable, screenshots = None, []
        for name, mode, rgb in (("black", "solid", (0, 0, 0)),
                                ("white", "solid", (1, 1, 1)),
                                ("colour", "solid", (0.17, 0.43, 0.81)),
                                ("checker", "checkerboard", (0.17, 0.43, 0.81))):
            path = output / f"foreground-{samples}-{name}.png"
            screenshot = path.with_suffix(".ppm")
            text = run([viewport, "--hidden", "--frames", 4, "--width", 96, "--height", 96,
                        "--vsync", "off", "--samples", samples, "--background", mode,
                        "--background-color", *rgb, "--foreground-output", path, "--screenshot", screenshot])
            assert "frames read back: 2" in text, text
            width, height, pixels = read_png(path)
            # Window decorations and DPI can enlarge a small requested extent.
            assert width >= 96 and height >= 96
            if stable is None:
                stable = pixels
            assert stable == pixels, "preview background contaminated PNG foreground"
            screenshots.append(screenshot.read_bytes())
            alphas = pixels[3::4]
            assert 0 in alphas and 255 in alphas
            if samples == 4:
                assert any(0 < alpha < 255 for alpha in alphas), "lost MSAA coverage"
            for at in range(0, len(pixels), 4):
                if pixels[at + 3] == 0:
                    assert pixels[at:at + 3] == b"\0\0\0"
        assert len(set(screenshots)) == 4, "presentation background did not change"
    print("Viewport PNG: 1x/4x coverage, background/UI isolation and independent dual readback passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--writer", type=Path)
    mode.add_argument("--viewport", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.writer:
        check_writer(args.writer, args.output)
    else:
        check_viewport(args.viewport, args.output)
