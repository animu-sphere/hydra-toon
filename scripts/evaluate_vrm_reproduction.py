#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare local VRMs with three-vrm using the viewport's exact camera.

Requires Pillow, a Hydra viewport with VRM plugins registered, and a local
npm installation of three@0.180.0 and @pixiv/three-vrm@3.5.5. Assets and
captures remain local. Open the printed loopback URL and run the reference
captures. This records differences, not a universal fidelity pass threshold.
"""

import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
from pathlib import Path
import subprocess
from urllib.parse import unquote, urlsplit

from PIL import Image


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(output):
    import numpy as np
    from PIL import ImageDraw
    manifest = json.loads((output / 'manifest.json').read_text())
    summary = []
    sheet = Image.new('RGB', (1200, 470 * len(manifest['cases'])), '#222222')
    draw = ImageDraw.Draw(sheet)
    for row, case in enumerate(manifest['cases']):
        name = case['name']
        viewport = Image.open(output / f'{name}-viewport.png').convert('RGB')
        actual = np.asarray(viewport).astype(float)
        background = actual[0, 0]
        entries = [('viewport', viewport)]
        item = dict(name=name, label=case['label'], comparisons={})
        for kind in ('reference', 'usdview'):
            path = output / f'{name}-{kind}.png'
            image = Image.open(path).convert('RGB')
            if image.size != viewport.size:
                raise ValueError(f'{path}: capture extent differs')
            other = np.asarray(image).astype(float)
            # Ignore clear colour. Union retains mismatched silhouettes.
            a_mask = np.max(np.abs(actual - background), axis=2) > 3
            b_mask = np.max(np.abs(other - other[0, 0]), axis=2) > 3
            union = a_mask | b_mask
            error = np.abs(actual - other)
            item['comparisons'][kind] = dict(
                foreground_pixels=int(union.sum()),
                foreground_mae_rgb_levels=float(error[union].mean()),
                foreground_p95_max_channel_levels=float(np.percentile(error.max(axis=2)[union], 95)),
                foreground_fraction_max_channel_gt16=float((error.max(axis=2)[union] > 16).mean()),
                foreground_mask_iou=float((a_mask & b_mask).sum() / union.sum()),
                sha256=sha256(path))
            entries.append((kind, image))
        for column, (kind, image) in enumerate(entries):
            draw.text((column*400+5, row*470+4), f'{case["label"]} / {kind}', fill='white')
            sheet.paste(image.resize((400,450)), (column*400,row*470+20))
        summary.append(item)
    sheet.save(output / 'comparison.png')
    (output / 'summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))


def prepare(args):
    cases = []
    for index, asset in enumerate(args.avatar):
        for view in ('full', 'close'):
            name = f'avatar-{index}-{view}'
            image = args.output / f'{name}.ppm'
            camera = args.output / f'{name}-camera.json'
            command = [str(args.viewport), '--usd', str(asset), '--hidden',
                       '--vsync', 'off', '--overlay', 'off', '--frames', '2',
                       '--width', '800', '--height', '900', '--samples', '4',
                       '--screenshot', str(image), '--camera-output', str(camera)]
            if view == 'close':
                command += ['--camera-pan', '0', '155', '--camera-dolly', '10']
            result = subprocess.run(command, capture_output=True, text=True)
            log = args.output / f'{name}.log'
            log.write_text(result.stdout + result.stderr, encoding='utf-8')
            if result.returncode or '4 sample(s) per pixel, sRGB' not in result.stdout:
                raise RuntimeError(f'viewport failed: {log}')
            if 'draws_mtoon=0' in result.stdout or 'materials_mtoon=0' in result.stdout:
                raise RuntimeError(f'MToon imaging missing: {log}')
            Image.open(image).save(args.output / f'{name}-viewport.png')
            cases.append(dict(name=name, asset=index, label=f'{asset.name} / {view}',
                              camera=json.loads(camera.read_text()),
                              asset_sha256=sha256(asset),
                              viewport_sha256=sha256(image), command=command))
    manifest = dict(cases=cases, three='0.180.0', three_vrm='3.5.5')
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    return manifest


def serve(args, manifest):
    html = Path(__file__).with_name('vrm_reference.html').read_bytes()
    names = {case['name'] for case in manifest['cases']}
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            route = unquote(urlsplit(self.path).path)
            content_type = 'application/octet-stream'
            if route == '/':
                data, content_type = html, 'text/html'
            elif route == '/config':
                data, content_type = json.dumps(manifest).encode(), 'application/json'
            elif route.startswith('/asset/'):
                try:
                    index = int(route.removeprefix('/asset/'))
                    if index < 0:
                        raise IndexError(index)
                    data = args.avatar[index].read_bytes()
                except (ValueError, IndexError):
                    self.send_error(404)
                    return
            else:
                root = args.reference_root if route.startswith('/modules/') else args.output
                relative = route.removeprefix('/modules/') if route.startswith('/modules/') else route[1:]
                path = (root / relative).resolve()
                if not path.is_relative_to(root) or not path.is_file():
                    self.send_error(404)
                    return
                data = path.read_bytes()
                if path.suffix == '.js':
                    content_type = 'text/javascript'
                elif path.suffix == '.png':
                    content_type = 'image/png'
            self.send_response(200)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_POST(self):
            name = self.path.removeprefix('/capture/')
            if name not in names or not self.path.startswith('/capture/'):
                self.send_error(404)
                return
            # Only named generated captures can be written; never model files.
            length = int(self.headers.get('Content-Length', '0'))
            if length <= 0 or length > 20_000_000:
                self.send_error(413)
                return
            payload = json.loads(self.rfile.read(length))
            import base64
            image = Image.open(io.BytesIO(base64.b64decode(payload.pop('image')))).convert('RGB')
            if image.size != (800, 900):
                self.send_error(400)
                return
            image.save(args.output / f'{name}-reference.png')
            (args.output / f'{name}-reference.json').write_text(json.dumps(payload, indent=2))
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b'OK')

    print(f'Open http://127.0.0.1:{args.port}/ and capture the reference set.', flush=True)
    ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--viewport', type=Path)
    parser.add_argument('--avatar', type=Path, action='append')
    parser.add_argument('--reference-root', type=Path,
                        help='npm node_modules directory')
    parser.add_argument('--output', type=Path, default=Path('build/vrm-reproduction'))
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--reuse', action='store_true', help='serve existing viewport captures')
    parser.add_argument('--summarize', action='store_true', help='compare saved reference and usdview images')
    args = parser.parse_args()
    args.output = args.output.resolve()
    if args.summarize:
        summarize(args.output)
        return
    if not (args.viewport and args.avatar and args.reference_root):
        parser.error('--viewport, --avatar and --reference-root are required to capture')
    args.reference_root = args.reference_root.resolve()
    args.avatar = [asset.resolve() for asset in args.avatar]
    args.output.mkdir(parents=True, exist_ok=True)
    for package, version in [('three', '0.180.0'), ('@pixiv/three-vrm', '3.5.5')]:
        actual = json.loads((args.reference_root / package / 'package.json').read_text())['version']
        if actual != version:
            raise ValueError(f'{package}: expected {version}, got {actual}')
    manifest = json.loads((args.output / 'manifest.json').read_text()) if args.reuse else prepare(args)
    for case in manifest['cases']:
        if sha256(args.avatar[case['asset']]) != case['asset_sha256']:
            raise ValueError('reused captures belong to different avatar bytes/order')
    serve(args, manifest)


if __name__ == '__main__':
    main()
