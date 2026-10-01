#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Evaluate MSAA through the actual viewport (requires Pillow and vrmImaging).

All generated fixtures and captures stay in --output. No external model is
copied. The optional --avatar is evaluated with the same deterministic motion
sequence at every sample count. Plugin registration is inherited from the host.
"""

import argparse
import json
import math
import re
import subprocess
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

SAMPLES = (1, 2, 4, 8)
SIZE = 256
PHASES = 9
# Frame() fits the radius sqrt(5) of the fixed [-2,2] x [-1,1] backdrop.
WORLD_PIXEL = 2 * 1.1 * math.sqrt(5) / math.cos(math.pi / 12) / SIZE


def mesh(name, points, indices, material, moving=False, uv=False, normals=None):
    text = f'''    def Mesh "{name}" (prepend apiSchemas = ["MaterialBindingAPI"])
    {{
        uniform token subdivisionScheme = "none"
        point3f[] points = {points}
        int[] faceVertexCounts = {[3] * (len(indices) // 3)}
        int[] faceVertexIndices = {indices}
        normal3f[] normals = {normals or [(0, 0, 1)] * len(points)} (interpolation = "vertex")
        rel material:binding = </Test/{material}>
'''
    if uv:
        text += '''        texCoord2f[] primvars:st = [(0,0),(1,0),(1,1),(0,1)] (interpolation = "vertex")
'''
    if moving:
        text += f'''        double3 xformOp:translate.timeSamples = {{0: (0,0,0), 8: ({WORLD_PIXEL},0,0)}}
        uniform token[] xformOpOrder = ["xformOp:translate"]
'''
    return text + '    }\n'


def material(name, color, extra='', schemas=''):
    return f'''    def Material "{name}" (prepend apiSchemas = ["VrmMaterialAPI", "VrmMToonAPI"{schemas}])
    {{
        color3f inputs:vrm:material:baseColorFactor = (0,0,0)
        color3f inputs:vrm:mtoon:shadeColorFactor = (0,0,0)
        color3f inputs:vrm:material:emissiveFactor = {color}
{extra}
    }}
'''


def fixture(directory):
    """Five isolated rows: geometry, Mask, Blend, RGB line art and a hull."""
    # Identical line profiles in alpha and RGB; linear texture filtering and
    # the renderer's mip chain remain enabled, as on an avatar.
    alpha = Image.new('RGBA', (512, 40), (255, 255, 255, 0))
    rgb = Image.new('RGBA', (512, 40), (0, 0, 0, 255))
    for image, color in ((alpha, (255,255,255,255)), (rgb, (255,255,255,255))):
        draw = ImageDraw.Draw(image)
        for index, width in enumerate((2, 3, 4, 8)):
            x = 45 + index * 120
            draw.line([(x, 4), (x + 50, 36)], fill=color, width=width)
    alpha.save(directory / 'alpha.png')
    rgb.save(directory / 'rgb.png')
    text = '''#usda 1.0
# SPDX-License-Identifier: Apache-2.0
(
    defaultPrim = "Test"
    startTimeCode = 0
    endTimeCode = 8
    metersPerUnit = 1
    upAxis = "Y"
)
def Xform "Test" {
'''
    text += material('Black', '(0,0,0)')
    text += material('White', '(1,1,1)')
    for mode in ('Mask', 'Blend'):
        text += material(mode, '(1,1,1)', f'''        token inputs:vrm:material:alphaMode = "{mode.upper()}"
        float inputs:vrm:material:alphaCutoff = 0.5
        asset inputs:vrm:textureInfo:baseColor:file = @alpha.png@''', ', "VrmTextureInfoAPI:baseColor"')
    text += material('RGB', '(1,1,1)', '        asset inputs:vrm:textureInfo:emissive:file = @rgb.png@', ', "VrmTextureInfoAPI:emissive"')
    text += material('Hull', '(0,0,0)', '''        token inputs:vrm:mtoon:outlineWidthMode = "screenCoordinates"
        float inputs:vrm:mtoon:outlineWidthFactor = 0.005859375
        color3f inputs:vrm:mtoon:outlineColorFactor = (1,1,1)
        float inputs:vrm:mtoon:outlineLightingMixFactor = 0''')
    text += mesh('Backdrop', [(-2,-1,-0.01),(2,-1,-0.01),(2,1,-0.01),(-2,1,-0.01)], [0,1,2,0,2,3], 'Black')
    points, indices = [], []
    for index, width in enumerate((0.5, 0.75, 1, 2)):
        x, y, w = -1.5 + index * 0.85, 0.7, width * WORLD_PIXEL / 2
        base = len(points)
        points += [(x-w,y-0.13,0),(x+w,y-0.13,0),(x+0.2+w,y+0.13,0),(x+0.2-w,y+0.13,0)]
        indices += [base,base+1,base+2,base,base+2,base+3]
    text += mesh('Strands', points, indices, 'White', moving=True)
    for name, y in (('Mask',0.35),('Blend',0),('RGB',-0.35)):
        text += mesh(name + 'Lines', [(-1.7,y-0.13,0),(1.7,y-0.13,0),(1.7,y+0.13,0),(-1.7,y+0.13,0)], [0,1,2,0,2,3], name, moving=True, uv=True)
    # Closed octahedron; inverted hull is visible around its black surface.
    text += mesh('Outline', [(0.9,-0.7,0),(-0.9,-0.7,0),(0,-0.56,0),(0,-0.84,0),(0,-0.7,0.001),(0,-0.7,-0.001)],
                 [0,2,4,1,4,2,0,4,3,1,3,4,0,5,2,1,2,5,0,3,5,1,5,3], 'Hull', moving=True,
                 normals=[(1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1)])
    path = directory / 'quality.usda'
    path.write_text(text + '}\n', encoding='utf-8')
    return path


def run(viewport, output, name, stage, samples, width, height, time, frames=2, step=None, capture=True, draws=None, hulls=None, close=None):
    path = output / f'{name}.ppm'
    command = [str(viewport), '--usd', str(stage), '--hidden', '--vsync', 'off', '--overlay', 'off',
               '--samples', str(samples), '--width', str(width), '--height', str(height),
               '--frames', str(frames), '--time', str(time)]
    if step is not None:
        command += ['--time-step', str(step)]
    if capture:
        command += ['--screenshot', str(path)]
    if draws is not None:
        command += ['--expect-draws', str(draws)]
    if hulls is not None:
        command += ['--expect-hulls', str(hulls)]
    if close is not None:
        command += ['--camera-pan', str(close[0]), str(close[1]), '--camera-dolly', str(close[2])]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    log = result.stdout + result.stderr
    (output / f'{name}.log').write_text(log, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'{name}: viewport exited {result.returncode}\n{log}')
    actual = re.search(r'Presentation: .*?, (\d+) sample', log)
    if actual is None or int(actual[1]) != samples:
        raise RuntimeError(f'{name}: requested {samples}x is unavailable; inspect log')
    mtoon = re.search(r'draws_mtoon=(\d+)', log)
    if mtoon is None or (draws is not None and int(mtoon[1]) != draws) or 'sRGB-encoded' not in log:
        raise RuntimeError(f'{name}: missing MToon or sRGB presentation; inspect log')
    if 'Validation' in log or 'validation message' in log.lower():
        raise RuntimeError(f'{name}: missing MToon or validation failure; inspect log')
    timing = {}
    for domain, part, count, mean, p95, maximum in re.findall(
            r'Timing: (cpu|gpu) (\w+) frames=(\d+) mean=([\d.]+).*? p95=([\d.]+).*? max=([\d.]+)', log):
        timing[f'{domain}_{part}'] = {'frames': int(count), 'mean_ms': float(mean), 'p95_ms': float(p95), 'max_ms': float(maximum)}
    evidence = {key: re.search(pattern, log)[0] for key, pattern in (
        ('scene', r'Scene summary: [^\n]+'), ('uploads', r'Uploads: [^\n]+'), ('draws', r'Draw calls: [^\n]+'),
        ('completion', r'Presented [^\n]+'))}
    evidence['timing'] = timing
    evidence['command'] = command
    return Image.open(path).convert('RGB') if capture else None, evidence


def linear(value):
    value /= 255
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


LUT = [linear(value) for value in range(256)]


def reference(image, factor=4):
    # Box-average in linear light; averaging encoded sRGB would bias edges.
    width, height = image.size
    pixels = image.convert('RGB').tobytes()
    rows = []
    for y in range(0, height, factor):
        for x in range(0, width, factor):
            rows.append(sum(LUT[pixels[((y+dy)*width+x+dx)*3]]
                            for dy in range(factor) for dx in range(factor)) / factor**2)
    return rows


def evaluate(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    stage = fixture(output)
    viewport = args.viewport.resolve()
    summary = {'reference': '4x MSAA at 4x linear resolution; linear-light box average',
               'phase_count': PHASES, 'motion_world_units': WORLD_PIXEL,
               'controlled': {}, 'avatar': {}, 'closeups': {}}
    # Rows are isolated by black gaps; project their world Y bounds to pixels.
    row_bounds = {name: (int(SIZE/2-(y+0.16)/WORLD_PIXEL), int(SIZE/2-(y-0.16)/WORLD_PIXEL))
                  for name, y in (('geometry',0.7),('mask',0.35),('blend',0),('rgb',-0.35),('outline',-0.7))}
    errors = {s: {name: [] for name in row_bounds} for s in SAMPLES}
    coverage = {s: {name: [] for name in row_bounds} for s in SAMPLES}
    shots = {}
    for phase in range(PHASES):
        high, _ = run(viewport, output, f'reference-{phase}', stage, 4, SIZE*4, SIZE*4, phase, draws=6, hulls=1)
        truth = reference(high)
        for samples in SAMPLES:
            image, evidence = run(viewport, output, f'fixture-{samples}x-{phase}', stage, samples, SIZE, SIZE, phase, draws=6, hulls=1)
            values = [LUT[value] for value in image.tobytes()[::3]]
            for name, (first, last) in row_bounds.items():
                start, end = first*SIZE, last*SIZE
                errors[samples][name].append(sum(abs(a-b) for a,b in zip(values[start:end], truth[start:end])) / (end-start))
                coverage[samples][name].append(sum(values[start:end]))
            if phase == 4:
                shots[samples] = image.copy()
                summary['controlled'][f'{samples}x'] = {'evidence': evidence}
        print(f'Controlled phase {phase+1}/{PHASES}', flush=True)
    for samples in SAMPLES:
        summary['controlled'][f'{samples}x']['rows'] = {
            name: {'mean_absolute_linear_error': sum(errors[samples][name])/PHASES,
                   'integrated_linear_coverage_range': max(coverage[samples][name])-min(coverage[samples][name])}
            for name in row_bounds}
    contact = Image.new('RGB', (SIZE*4, SIZE+24), '#202028')
    draw = ImageDraw.Draw(contact)
    for index, samples in enumerate(SAMPLES):
        contact.paste(shots[samples], (index*SIZE,24))
        draw.text((index*SIZE+8,6), f'{samples}x MSAA', fill='white')
    contact.resize((SIZE*8,(SIZE+24)*2), Image.Resampling.NEAREST).save(output / 'controlled-comparison.png')
    if args.avatar:
        avatar_shots = {}
        for samples in SAMPLES:
            image, evidence = run(viewport, output, f'avatar-{samples}x', args.avatar.resolve(), samples,
                                  1280, 720, 0, args.frames, 0.25, draws=args.avatar_draws)
            avatar_shots[samples] = image
            summary['avatar'][f'{samples}x'] = evidence
        # Counts establish an image difference only, never ground-truth error.
        for samples in SAMPLES[1:]:
            diff = ImageChops.difference(avatar_shots[1], avatar_shots[samples])
            data = diff.tobytes()
            summary['avatar'][f'{samples}x']['changed_pixels_from_1x'] = sum(any(data[i:i+3]) for i in range(0,len(data),3))
        for samples, image in avatar_shots.items():
            image.save(output / f'avatar-{samples}x.png')
        sheet = Image.new('RGB', (2560,1488), '#202028')
        draw = ImageDraw.Draw(sheet)
        for index, samples in enumerate(SAMPLES):
            x, y = (index%2)*1280, (index//2)*744
            sheet.paste(avatar_shots[samples], (x,y+24))
            draw.text((x+8,y+6), f'{samples}x MSAA', fill='white')
        sheet.save(output / 'avatar-comparison.png')
        for time in (0, 29.75, 299.75):
            close_sheet = Image.new('RGB', (2560,1488), '#202028')
            draw = ImageDraw.Draw(close_sheet)
            for index, samples in enumerate(SAMPLES):
                name = f'face-{time}-{samples}x'
                image, evidence = run(viewport, output, name, args.avatar.resolve(), samples,
                                      1280, 720, time, draws=args.avatar_draws,
                                      close=(*args.close_pan, args.close_dolly))
                summary['closeups'][name] = evidence
                x, y = (index%2)*1280, (index//2)*744
                close_sheet.paste(image, (x,y+24))
                draw.text((x+8,y+6), f'{samples}x MSAA, time {time}', fill='white')
            close_sheet.save(output / f'face-{time}-comparison.png')
            print(f'Close-up time {time}', flush=True)
        # Repeat with changing order and no captures, retaining the last 1024
        # frames after warm-up. Timings of two-frame quality shots are ignored.
        summary['benchmark'] = []
        for repeat, order in enumerate((SAMPLES, tuple(reversed(SAMPLES)), (1,4,8,2))):
            for samples in order:
                _, evidence = run(viewport, output, f'benchmark-{repeat+1}-{samples}x',
                                  args.avatar.resolve(), samples, 1280, 720, 0,
                                  args.frames, 0.25, capture=False, draws=args.avatar_draws)
                summary['benchmark'].append({'repeat': repeat+1, 'samples': samples, **evidence})
            print(f'Benchmark repeat {repeat+1}/3', flush=True)
    (output / 'summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    print(f'Evidence: {output / "summary.json"}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--viewport', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('build/aa-quality'))
    parser.add_argument('--avatar', type=Path)
    parser.add_argument('--avatar-draws', type=int, default=20)
    parser.add_argument('--frames', type=int, default=1200)
    parser.add_argument('--close-pan', type=float, nargs=2, default=(0, 155), metavar=('X', 'Y'))
    parser.add_argument('--close-dolly', type=float, default=12)
    args = parser.parse_args()
    if args.frames <= 1024:
        parser.error('--frames must exceed 1024 to exclude warm-up from benchmark telemetry')
    if args.avatar_draws <= 0 or not all(math.isfinite(v) for v in (*args.close_pan, args.close_dolly)):
        parser.error('draw count must be positive and camera values finite')
    evaluate(args)
