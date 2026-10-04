#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Capture continuous VRM outline sequences and verify every repeated frame.

Requires Pillow, NumPy and a Hydra viewport with VRM plugins registered.
Readbacks perturb timing: use uncaptured runs for performance evidence.
The signed ON/OFF differences measure image variation, not perceptual flicker
or reference-renderer fidelity. No assets or captures are redistributed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

import numpy as np
from PIL import Image

from evaluate_outline_temporal import signal


def evaluate(args):
    evidence = []
    for index, asset in enumerate(args.avatar):
        for view in ('full', 'close'):
            captures = {}
            records = []
            for mode in ('on', 'off', 'repeat'):
                name = f'avatar-{index}-{view}-{mode}'
                directory = args.output / name
                command = [str(args.viewport), '--usd', str(asset), '--hidden',
                           '--vsync', 'off', '--overlay', 'off', '--samples', '4',
                           '--width', '640', '--height', '720', '--time', str(args.time),
                           '--time-step', str(args.step), '--frames', str(args.frames),
                           '--outlines', 'off' if mode == 'off' else 'on',
                           '--capture-sequence', str(directory),
                           '--expect-draws', str(args.draws)]
                if view == 'close':
                    command += ['--camera-pan', '0', '124', '--camera-dolly', '10']
                result = subprocess.run(command, capture_output=True, text=True)
                log = args.output / f'{name}.log'
                log.write_text(result.stdout + result.stderr, encoding='utf-8')
                if result.returncode:
                    raise RuntimeError(f'capture failed: {log}')
                images = [directory / f'frame-{frame:06}.ppm' for frame in range(args.frames)]
                if not all(path.is_file() for path in images):
                    raise RuntimeError(f'missing capture: {directory}')
                if f'frames read back: {args.frames}' not in result.stdout:
                    raise RuntimeError(f'readback count differs: {log}')
                if f'draws_mtoon={args.draws} ' not in result.stdout or '4 sample(s) per pixel' not in result.stdout:
                    raise RuntimeError(f'MToon/sample selection differs: {log}')
                match = re.search(r'Uploads: topology=(\d+) points=(\d+) materials=(\d+) textures=(\d+) skins=(\d+) poses=(\d+)', result.stdout)
                if not match:
                    raise RuntimeError(f'missing upload evidence: {log}')
                counters = dict(zip(('topology','points','materials','textures','skins','poses'), map(int, match.groups())))
                if any(counters[key] != args.draws for key in ('topology','points','skins')):
                    raise RuntimeError(f'animation uploaded geometry: {log}')
                hashes = [hashlib.sha256(path.read_bytes()).hexdigest() for path in images]
                captures[mode] = (images, hashes)
                records.append(dict(mode=mode, command=command, hashes=hashes, uploads=counters))
            if captures['on'][1] != captures['repeat'][1]:
                raise RuntimeError(f'every-frame repeat mismatch: {asset.name}/{view}')
            signed = []
            thumbnails = []
            for on, off in zip(captures['on'][0], captures['off'][0]):
                signed.append(signal(Image.open(on)) - signal(Image.open(off)))
                thumbnails.append(Image.open(on).convert('RGB').resize((320,360)))
            field = np.stack(signed)
            magnitude = np.abs(field).sum(axis=(1,2))
            variation = np.abs(np.diff(field, axis=0)).mean(axis=(1,2))
            thumbnails[0].save(args.output / f'avatar-{index}-{view}.gif',
                               save_all=True, append_images=thumbnails[1:], duration=round(1000*args.step/30), loop=0)
            evidence.append(dict(asset_sha256=hashlib.sha256(asset.read_bytes()).hexdigest(),
                                 name=asset.name, view=view, frames=args.frames,
                                 every_frame_repeat_identical=True,
                                 unique_on_frames=len(set(captures['on'][1])),
                                 outline_absolute_signal_min=float(magnitude.min()),
                                 outline_absolute_signal_max=float(magnitude.max()),
                                 outline_temporal_change_mean=float(variation.mean()),
                                 outline_temporal_change_max=float(variation.max()), records=records))
            (args.output / 'summary.json').write_text(json.dumps(evidence, indent=2))
            print(f'{asset.name}/{view}: {args.frames} repeated frames match; pose-only uploads', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--viewport', type=Path, required=True)
    parser.add_argument('--avatar', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, default=Path('build/outline-sequence'))
    parser.add_argument('--frames', type=int, default=61)
    parser.add_argument('--time', type=float, default=29)
    parser.add_argument('--step', type=float, default=1)
    parser.add_argument('--draws', type=int, default=20)
    args = parser.parse_args()
    if args.frames < 2:
        parser.error('--frames must be at least 2')
    args.output.mkdir(parents=True, exist_ok=True)
    evaluate(args)


if __name__ == '__main__':
    main()
