#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure outline sampling over motion in the Hydra-fed viewport.

Requires Pillow, NumPy and registered vrmImaging. Generated fixtures, models'
captures and logs stay local in --output. A supersampled image is a finite
comparison, not analytical truth or a reference MToon implementation.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from evaluate_antialiasing import LUT, WORLD_PIXEL, material, mesh, run

SIZE = 256
SAMPLES = (1, 4, 8)
WIDTHS = (0.5, 0.75, 1.5)


def fixture(output, motion):
    text = '''#usda 1.0
# SPDX-License-Identifier: Apache-2.0
(defaultPrim = "Test"
 startTimeCode = 0
 endTimeCode = 16
 metersPerUnit = 1
 upAxis = "Y")
def Xform "Test" {
'''
    text += material('Black', '(0,0,0)')
    # Fixed backdrop dominates scene bounds so all time codes frame alike.
    text += mesh('Backdrop', [(-2,-1,-0.1),(2,-1,-0.1),(2,1,-0.1),(-2,1,-0.1)],
                 [0,1,2,0,2,3], 'Black')
    for row, width in enumerate(WIDTHS):
        text += material(f'Hull{row}', '(0,0,0)', f'''
        token inputs:vrm:mtoon:outlineWidthMode = "screenCoordinates"
        float inputs:vrm:mtoon:outlineWidthFactor = {width / SIZE}
        color3f inputs:vrm:mtoon:outlineColorFactor = (1,1,1)
        float inputs:vrm:mtoon:outlineLightingMixFactor = 0''')
    translations, rotations = [], []
    for time in range(17):
        phase = min(time, 16-time) / 8
        shift = phase * WORLD_PIXEL if motion == 'translate' else 0
        angle = math.radians(12 * phase) if motion == 'rotate' else 0
        translations.append(f'{time}: [({shift},0,0)]')
        rotations.append(f'{time}: [({math.cos(angle/2)},0,0,{math.sin(angle/2)})]')
    text += '''    def SkelRoot "Moving" {
        def Skeleton "Skel" (prepend apiSchemas = ["SkelBindingAPI"]) {
            uniform token[] joints = ["root"]
            uniform matrix4d[] bindTransforms = [((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1))]
            uniform matrix4d[] restTransforms = [((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1))]
            rel skel:animationSource = </Test/Moving/Anim>
        }
        def SkelAnimation "Anim" {
            uniform token[] joints = ["root"]
'''
    text += '            float3[] translations.timeSamples = {' + ','.join(translations) + '}\n'
    text += '            quatf[] rotations.timeSamples = {' + ','.join(rotations) + '}\n'
    text += '            half3[] scales = [(1,1,1)]\n        }\n'
    for row, y in enumerate((0.6, 0, -0.6)):
        shape = mesh(f'Outline{row}', [(0.7,y,0),(-0.7,y,0),(0,y+0.1,0),
                     (0,y-0.1,0),(0,y,0.03),(0,y,-0.03)],
                     [0,2,4,1,4,2,0,4,3,1,3,4,0,5,2,1,2,5,0,3,5,1,5,3], f'Hull{row}',
                     normals=[(1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1)])
        shape = shape.replace('["MaterialBindingAPI"]', '["MaterialBindingAPI", "SkelBindingAPI"]')
        shape = shape.rsplit('    }', 1)[0] + '''        int[] primvars:skel:jointIndices = [0] (
            elementSize = 1
            interpolation = "constant"
        )
        float[] primvars:skel:jointWeights = [1] (
            elementSize = 1
            interpolation = "constant"
        )
        rel skel:skeleton = </Test/Moving/Skel>
    }
'''
        text += shape
    path = output / f'{motion}.usda'
    path.write_text(text + '    }\n}\n', encoding='utf-8')
    return path


def signal(image, factor=1):
    """Decode sRGB, box-average RGB in linear light, then take luminance."""
    data = np.asarray(LUT)[np.asarray(image)]
    height, width, _ = data.shape
    if factor != 1:
        data = data.reshape(height//factor, factor, width//factor, factor, 3).mean(axis=(1, 3))
    return data @ np.array([0.2126, 0.7152, 0.0722])


def metrics(images, references):
    actual, comparison = np.stack(images), np.stack(references)
    error = actual - comparison
    temporal = np.abs(np.diff(error, axis=0)).mean(axis=(1, 2))
    mass = np.abs(actual).sum(axis=(1, 2))
    ref_mass = np.abs(comparison).sum(axis=(1, 2))
    active = np.abs(comparison) >= 0.05
    return {
        'mean_absolute_linear_error': float(np.abs(error).mean()),
        'mean_temporal_residual': float(temporal.mean()),
        'max_temporal_residual': float(temporal.max()),
        'integrated_absolute_signal_range': float(np.ptp(mass)),
        'reference_signal_range': float(np.ptp(ref_mass)),
        'integrated_signal_error_range': float(np.ptp(mass-ref_mass)),
        'missing_signal_fraction': float(np.count_nonzero(active & (np.abs(actual) < 1e-6)) / max(1, active.sum())),
        'per_pose_signal': mass.tolist(),
        'per_pose_reference_signal': ref_mass.tolist(),
    }


def evaluate(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    summary = {'reference': {'controlled': '4x MSAA at 4x linear resolution, linear-light box average',
                             'avatar': '4x MSAA at 2x linear resolution, signed outlines-on/off difference in linear light'},
               'temporal_metric': 'mean abs((I[t]-I[t-1])-(R[t]-R[t-1])); no motion warping',
               'controlled': {}, 'avatar': {}, 'runs': []}
    viewport = args.viewport.resolve()
    summary['viewport_sha256'] = hashlib.sha256(viewport.read_bytes()).hexdigest()

    def capture(name, stage, samples, width, height, time, **kwargs):
        image, evidence = run(viewport, output, name, stage, samples, width, height, time, **kwargs)
        evidence['name'] = name
        evidence['sha256'] = hashlib.sha256((output / f'{name}.ppm').read_bytes()).hexdigest()
        if f'Presented {kwargs.get("frames", 2)} frames' not in evidence['completion'] or 'frames read back: 1)' not in evidence['completion']:
            raise RuntimeError(f'{name}: unexpected presentation/readback counts')
        if name.startswith(('translate-', 'rotate-')):
            poses = 51 if kwargs.get('frames') == 17 else 3
            if 'draws_skinned=3' not in evidence['scene'] or f'topology=4 points=4 materials=5 textures=0 skins=3 poses={poses} ' not in evidence['uploads']:
                raise RuntimeError(f'{name}: missing GPU skinning or unexpected uploads')
        summary['runs'].append(evidence)
        return image

    for motion in ('translate', 'rotate'):
        stage = fixture(output, motion)
        for dolly in (0, 4):
            name = f'{motion}-dolly{dolly}'
            references, images = [], {s: [] for s in SAMPLES}
            for phase in range(9):
                high = capture(f'{name}-reference-{phase}', stage, 4, SIZE*4, SIZE*4, phase,
                               draws=4, hulls=3, close=(0,0,dolly))
                references.append(signal(high, 4))
                for samples in SAMPLES:
                    shot = capture(f'{name}-{samples}x-{phase}', stage, samples, SIZE, SIZE, phase,
                                   draws=4, hulls=3, close=(0,0,dolly))
                    images[samples].append(signal(shot))
                print(f'{name}: phase {phase+1}/9', flush=True)
            result = {}
            # At 12 degrees each complete hull stays inside its disjoint band.
            # With dolly 4 the rows move farther apart, while each stays in frame.
            pixel_world = WORLD_PIXEL * (0.9**dolly)
            left = max(0, int(SIZE/2-1.2/pixel_world))
            right = min(SIZE, int(SIZE/2+1.2/pixel_world))
            for samples in SAMPLES:
                result[f'{samples}x'] = {}
                for width, y in zip(WIDTHS, (0.6, 0, -0.6)):
                    first = max(0, int(SIZE/2-(y+0.28)/pixel_world))
                    last = min(SIZE, int(SIZE/2-(y-0.28)/pixel_world))
                    result[f'{samples}x'][str(width)] = metrics(
                        [im[first:last, left:right] for im in images[samples]],
                        [im[first:last, left:right] for im in references])
                returned = capture(f'{name}-{samples}x-motion-return', stage, samples, SIZE, SIZE, 0,
                                   frames=17, step=1, draws=4, hulls=3, close=(0,0,dolly))
                repeated = capture(f'{name}-{samples}x-repeat', stage, samples, SIZE, SIZE, 0,
                                   draws=4, hulls=3, close=(0,0,dolly))
                initial = Image.open(output / f'{name}-{samples}x-0.ppm').convert('RGB').tobytes()
                if returned.tobytes() != initial or repeated.tobytes() != initial:
                    raise RuntimeError(f'{name}-{samples}x: repeated or returning pose changed')
            summary['controlled'][name] = result
            contact = Image.new('RGB', (SIZE*3, SIZE+24), '#202028')
            draw = ImageDraw.Draw(contact)
            for column, samples in enumerate(SAMPLES):
                shot = Image.open(output / f'{name}-{samples}x-4.ppm')
                contact.paste(shot, (column*SIZE,24))
                draw.text((column*SIZE+8,6), f'{samples}x, {motion}, dolly {dolly}', fill='white')
            contact.resize((SIZE*6,(SIZE+24)*2), Image.Resampling.NEAREST).save(output / f'{name}-comparison.png')

    if args.avatar:
        times = [args.avatar_time + i * args.avatar_step for i in range(args.avatar_phases)]
        for view, close in (('full', (0,0,0)), ('close', (*args.close_pan, args.close_dolly))):
            images = {s: [] for s in SAMPLES}
            references = []
            for phase, time in enumerate(times):
                name = f'avatar-{view}-{phase}'
                # Pixel pan stays physically constant when resolution doubles.
                high_close = (close[0]*2, close[1]*2, close[2])
                on = capture(f'{name}-reference-on', args.avatar.resolve(), 4, 2560,1440,time,
                             draws=args.avatar_draws, close=high_close, outlines='on')
                off = capture(f'{name}-reference-off', args.avatar.resolve(), 4, 2560,1440,time,
                              draws=args.avatar_draws, close=high_close, outlines='off', hulls=0)
                references.append(signal(on,2)-signal(off,2))
                for samples in SAMPLES:
                    on = capture(f'{name}-{samples}x-on', args.avatar.resolve(),samples,1280,720,time,
                                 draws=args.avatar_draws,close=close,outlines='on')
                    off = capture(f'{name}-{samples}x-off', args.avatar.resolve(),samples,1280,720,time,
                                  draws=args.avatar_draws,close=close,outlines='off',hulls=0)
                    images[samples].append(signal(on)-signal(off))
                print(f'avatar {view}: phase {phase+1}/{len(times)}', flush=True)
            summary['avatar'][view] = {'times': times, 'comparison_factor': 2,
                                      **{f'{s}x': metrics(images[s], references) for s in SAMPLES}}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    print(f'Evidence: {output / "summary.json"}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--viewport', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('build/outline-temporal'))
    parser.add_argument('--avatar', type=Path)
    parser.add_argument('--avatar-time', type=float, default=29)
    parser.add_argument('--avatar-step', type=float, default=0.25)
    parser.add_argument('--avatar-phases', type=int, default=5)
    parser.add_argument('--avatar-draws', type=int, default=20)
    parser.add_argument('--close-pan', type=float, nargs=2, default=(0,155))
    parser.add_argument('--close-dolly', type=float, default=12)
    args = parser.parse_args()
    if args.avatar_phases < 2 or not all(math.isfinite(v) for v in (args.avatar_time,args.avatar_step,*args.close_pan,args.close_dolly)) or args.avatar_step <= 0:
        parser.error('use at least two poses, a positive step and finite camera/time values')
    evaluate(args)


if __name__ == '__main__':
    main()
