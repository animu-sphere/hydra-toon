#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bounded two-fixture release acceptance; independent PNG/linear source-over oracle."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--shaders', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    summary = output / 'summary.json'
    summary.unlink(missing_ok=True)
    result = subprocess.run([str(args.capture.resolve()), str(args.shaders.resolve()), str(output)],
                            capture_output=True, text=True, timeout=60)
    (output / 'capture.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    print(result.stdout, end='')
    if result.returncode:
        print(result.stderr)
        return result.returncode
    module_path = Path(__file__).resolve().parents[1] / 'adapters/viewport/tests/check_png.py'
    spec = importlib.util.spec_from_file_location('png_oracle', module_path)
    png = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(png)
    results = []
    for name in ('avatar', 'mmd-shadow'):
        for samples in (1, 4):
            prefix = f'{name}-{samples}'
            raw = (output / f'{prefix}.rgba').read_bytes()
            width, height, encoded = png.read_png(output / f'{prefix}.png')
            assert (width, height) == (256, 256) and len(raw) == width * height * 4
            assert 0 in raw[3::4] and 255 in raw[3::4]
            if samples == 4:
                assert any(0 < a < 255 for a in raw[3::4]), 'missing MSAA edge coverage'
            native = {background: (output / f'{prefix}-{background}.rgba').read_bytes()
                      for background in ('black', 'white')}
            for pixels in native.values():
                assert len(pixels) == len(raw)
            max_export, max_native = 0, 0
            for background in ('black', 'white', 'checker', 'image'):
                pixels = bytearray()
                for y in range(height):
                    for x in range(width):
                        at = (y * width + x) * 4
                        alpha = raw[at + 3] / 255
                        assert encoded[at + 3] == raw[at + 3], 'PNG changed alpha'
                        if alpha == 0:
                            assert raw[at:at+3] == encoded[at:at+3] == b'\0\0\0'
                        # Original arbitrary RGB8 image: asymmetric channels, rows and stripes.
                        back = ([0]*3 if background == 'black' else [1]*3 if background == 'white'
                                else [0.15 if (x//13+y//13) % 2 else 0.75]*3
                                if background == 'checker' else
                                [png.decode(v/255) for v in ((3*x+y)%256, (x+2*y)%256,
                                                            50 if (x//19)%2 else 210)])
                        for channel in range(3):
                            expected = raw[at+channel]/255 + back[channel]*(1-alpha)
                            restored = png.decode(encoded[at+channel]/255)*alpha + back[channel]*(1-alpha)
                            max_export = max(max_export, abs(restored-expected))
                            if background in native:
                                actual = native[background]
                                assert actual[at+3] == 255
                                max_native = max(max_native, abs(actual[at+channel]-expected*255))
                            pixels.append(round(max(0, min(1, png.encode(expected)))*255))
                (output / f'{prefix}-{background}.ppm').write_bytes(
                    f'P6\n{width} {height}\n255\n'.encode() + pixels)
            assert max_export < 0.005, (prefix, 'export/composite error', max_export)
            assert max_native <= 2.1, (prefix, 'native background/composite error', max_native)
            results.append(dict(fixture=name, samples=samples, width=width, height=height,
                                max_linear_export_error=max_export, max_native_byte_error=max_native,
                                foreground_sha256=hashlib.sha256(raw).hexdigest()))
    summary.write_text(json.dumps(dict(passed=True, cases=results), indent=2)+'\n')
    print('Two fixtures, 1x/4x, four backgrounds: PNG/source-over and native controls passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
