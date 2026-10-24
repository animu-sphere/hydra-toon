#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Capture a controlled PreviewSurface fixture with OpenUSD Storm and hdToon.

Requires the lookdev runtime's testusdview, matching Python, Pillow and NumPy.
Pass runtime bin/lib on PATH, lib/python on PYTHONPATH and register hdToon
with PXR_PLUGINPATH_NAME, as for the existing usdview host checks.
Only local generated images and logs go into --output.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw


CASES = [
    {'name': 'dielectric-rough', 'roughness': 1.0},
    {'name': 'dielectric-mid', 'roughness': 0.6},
    {'name': 'dielectric-smooth', 'roughness': 0.2},
    {'name': 'metal-rough', 'metallic': 1.0, 'roughness': 0.8},
    {'name': 'metal-mid', 'metallic': 1.0, 'roughness': 0.4},
    {'name': 'metal-mixed', 'metallic': 0.5, 'roughness': 0.6,
     'expected_difference': 'Storm tints intermediate-metal F0 before interpolation',
     'mae_range': [12,18], 'p95_range': [30,40]},
    {'name': 'emission', 'diffuseColor': [0,0,0], 'emissiveColor': [0.3,0.1,0.05]},
    {'name': 'color-texture', 'texture': 'color'},
    {'name': 'color-rotated', 'texture': 'color', 'rotation': 90},
    {'name': 'normal-map', 'texture': 'normal'},
    {'name': 'normal-rotated', 'texture': 'normal', 'rotation': 90,
     'expected_difference': 'Storm derives the normal frame from transformed coordinates',
     'mae_range': [30,40], 'p95_range': [75,90]},
    {'name': 'normal-rotation-baked', 'texture': 'normal', 'rotation': 90,
     'bake_reference_rotation': True},
    {'name': 'curved-dielectric', 'geometry': 'curved'},
    {'name': 'curved-metal', 'geometry': 'curved', 'metallic': 1.0, 'roughness': 0.8},
    {'name': 'grazing-dielectric', 'geometry': 'grazing'},
    {'name': 'grazing-metal', 'geometry': 'grazing', 'metallic': 1.0, 'roughness': 0.8},
    {'name': 'metallic-texture', 'texture': 'scalar', 'input': 'metallic', 'channel': 'r'},
    {'name': 'roughness-texture', 'texture': 'scalar', 'input': 'roughness', 'channel': 'g'},
    {'name': 'opacity-mask', 'texture': 'scalar', 'input': 'opacity', 'channel': 'b',
     'opacityThreshold': 0.5},
    {'name': 'opacity-blend', 'texture': 'scalar', 'input': 'opacity', 'channel': 'a'},
    {'name': 'opacity-constant', 'opacity': 0.45},
    {'name': 'metal-default', 'metallic': 1.0},
    {'name': 'roughness-low', 'roughness': 64/255},
    {'name': 'roughness-high', 'roughness': 204/255},
]


def srgb_decode(value):
    value = value / 255.0
    return np.where(value <= 0.04045, value/12.92, ((value+0.055)/1.055)**2.4)


def srgb_encode(value):
    return 255*np.where(value <= 0.0031308, value*12.92, 1.055*value**(1/2.4)-0.055)


def image_controls(output):
    """Independent channel/cutoff and linear-over-opaque checks, away from edges."""
    controls = []
    y, x = np.indices((256,256))
    # Mirrored U: each outer strip samples the image's left column.
    right = (x >= 88) & (x < 168)
    bottom = y >= 128
    quadrants = [~right & ~bottom, right & ~bottom, ~right & bottom, right & bottom]
    mask = measurement_mask({})
    clear = np.array([0.05, 0.10, 0.15])
    for renderer, samples in [('Storm',4), ('Toon',1), ('Toon',4)]:
        def read(name):
            return np.asarray(Image.open(output/f'{name}-{renderer}-{samples}x.png').convert('RGB'), dtype=float)

        opaque = read('dielectric-mid')
        background_error = float(np.abs(opaque[:16,:16]-srgb_encode(clear)).max())
        controls.append(dict(renderer=renderer, samples=samples, check='linear-clear',
                             max_channel=background_error, passed=background_error <= 2))
        for name, sources in [
            ('metallic-texture', ['dielectric-mid','metal-default','metal-default','dielectric-mid']),
            ('roughness-texture', ['roughness-low','roughness-high','roughness-low','roughness-high']),
            ('opacity-mask', [None,'dielectric-mid','dielectric-mid',None]),
        ]:
            actual = read(name)
            for quadrant, (region, source) in enumerate(zip(quadrants, sources)):
                expected = read(source) if source else np.broadcast_to(opaque[0,0], opaque.shape)
                error = np.abs(actual-expected)[region & mask]
                controls.append(dict(renderer=renderer, samples=samples, check=name,
                                     quadrant=quadrant, pixels=int((region & mask).sum()),
                                     max_channel=float(error.max()), passed=bool(error.max() <= 1)))
        if renderer == 'Toon':
            for name in ('opacity-blend','opacity-constant'):
                actual = read(name)
                for quadrant, region in enumerate(quadrants):
                    alpha = 0.45 if name == 'opacity-constant' else (0.2 if quadrant % 2 == 0 else 0.8)
                    expected = srgb_encode(srgb_decode(opaque)*alpha+clear*(1-alpha))
                    error = np.abs(actual-expected)[region & mask]
                    mae, p95 = float(error.mean()), float(np.percentile(error.max(axis=1),95))
                    controls.append(dict(renderer=renderer, samples=samples, check=name,
                                         quadrant=quadrant, alpha=alpha, mae_rgb=mae,
                                         p95_max_channel=p95, passed=mae <= 2 and p95 <= 3))
    return controls


def measurement_mask(case):
    """Fixed interior regions, independent of renderer pixels/clear colour."""
    mask = np.zeros((256, 256), dtype=bool)
    if case.get('geometry') == 'curved':
        mask[64:192, 64:192] = True
    elif case.get('geometry') == 'grazing':
        # 70-degree Y rotation narrows the projected patch to ~58 pixels.
        mask[72:184, 120:136] = True
    else:
        mask[56:200, 56:200] = True
        for boundary in (88, 128, 168):
            mask[:, boundary-8:boundary+8] = False
        mask[120:136, :] = False
    return mask


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--testusdview', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('build/preview-reference'))
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / 'summary.json').unlink(missing_ok=True)
    scripts = Path(__file__).resolve().parent
    fixture = scripts.parent / 'adapters/hydra2/tests/preview-reference.usda'
    (output / 'cases.json').write_text(json.dumps(CASES, indent=2) + '\n')
    # Large constant quadrants expose UV origin, colour space, decoding and
    # mirrored handedness without making texture filtering the reference test.
    for name, colors in [('normal', [(191,230,204), (50,190,220), (80,60,230), (200,80,180)]),
                         ('color', [(180,70,30), (30,150,70), (40,70,190), (180,140,30)])]:
        image = Image.new('RGB', (64,64))
        draw = ImageDraw.Draw(image)
        for index, color in enumerate(colors):
            x, y = index % 2 * 32, index // 2 * 32
            draw.rectangle((x,y,x+31,y+31), fill=color)
        image.save(output / f'{name}.png')
        if name == 'normal':
            # R(90) on bottom-left UVs is a clockwise rotation of the image.
            image.transpose(Image.Transpose.ROTATE_270).save(output / 'normal-rotated.png')
    # Independent RGBA channels: metal endpoints, roughness, Mask and Blend.
    image = Image.new('RGBA', (64,64))
    draw = ImageDraw.Draw(image)
    for index, color in enumerate([(0,64,64,51), (255,204,191,204),
                                   (255,64,191,51), (0,204,64,204)]):
        x, y = index % 2 * 32, index // 2 * 32
        draw.rectangle((x,y,x+31,y+31), fill=color)
    image.save(output / 'scalar.png')
    commands = []
    for renderer, samples in [('Storm', 4), ('Toon', 1), ('Toon', 4)]:
        evidence = output / f'{renderer}-{samples}x-evidence.log'
        # Do not let stale host evidence satisfy the worker's assertions.
        evidence.write_text('')
        (output / f'{renderer}-{samples}x.json').unlink(missing_ok=True)
        env = dict(os.environ, TOON_PREVIEW_OUTPUT=str(output), TOON_PREVIEW_RENDERER=renderer,
                   TOON_PREVIEW_SAMPLES=str(samples), TOON_HYDRA_EVIDENCE=str(evidence),
                   HDX_MSAA_SAMPLE_COUNT=str(samples))
        for case in CASES:
            # An unsuccessful capture must never reuse an earlier PNG.
            (output / f"{case['name']}-{renderer}-{samples}x.png").unlink(missing_ok=True)
        command = [sys.executable, str(args.testusdview.resolve()), str(fixture),
                   '--renderer', renderer, '--camera', '/Camera', '--testScript',
                   str(scripts / 'preview_surface_usdview.py')]
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=120)
        (output / f'{renderer}-{samples}x.log').write_text(result.stdout, encoding='utf-8')
        commands.append(dict(command=command, renderer=renderer, samples=samples,
                             metadata=json.loads((output / f'{renderer}-{samples}x.json').read_text())
                             if result.returncode == 0 else None))
        if result.returncode:
            raise RuntimeError(f'{renderer} capture failed ({result.returncode}); see {output}')
        metadata = commands[-1]['metadata']
        if metadata['completed_cases'] != [case['name'] for case in CASES]:
            raise RuntimeError(f'{renderer} did not complete the requested case list')
        if metadata['clear_linear'] != [0.05,0.10,0.15,1.0]:
            raise RuntimeError(f'{renderer} used an unexpected clear')
    results = []
    sheet = Image.new('RGB', (256*3, 280*len(CASES)), '#222222')
    labels = ImageDraw.Draw(sheet)
    for row, case in enumerate(CASES):
        mask = measurement_mask(case)
        Image.fromarray(mask.astype(np.uint8)*255).save(output / f"{case['name']}-mask.png")
        images = []
        for column, (renderer, samples) in enumerate([('Storm',4), ('Toon',1), ('Toon',4)]):
            path = output / f"{case['name']}-{renderer}-{samples}x.png"
            image = Image.open(path).convert('RGB')
            if image.size != (256,256):
                raise RuntimeError(f'unexpected capture extent: {path}')
            images.append(np.asarray(image, dtype=np.float32))
            sheet.paste(image, (column*256,row*280+24))
            labels.text((column*256+4,row*280+4), f"{case['name']} / {renderer} {samples}x", fill='white')
        for index, samples in enumerate((1,4), 1):
            error = np.abs(images[index] - images[0])[mask]
            item = dict(case=case['name'], samples=samples, pixels=int(mask.sum()),
                        geometry=case.get('geometry', 'planar'),
                        mask_sha256=digest(output / f"{case['name']}-mask.png"),
                        mae_rgb=float(error.mean()), p95_max_channel=float(np.percentile(error.max(axis=1),95)),
                        max_channel=float(error.max()),
                        image_sha256=digest(output / f"{case['name']}-Toon-{samples}x.png"),
                        reference_sha256=digest(output / f"{case['name']}-Storm-4x.png"))
            item['reference_match'] = item['mae_rgb'] <= 8 and item['p95_max_channel'] <= 20
            if 'expected_difference' in case:
                item['expected_difference'] = case['expected_difference']
                item['pass'] = (case['mae_range'][0] <= item['mae_rgb'] <= case['mae_range'][1]
                                and case['p95_range'][0] <= item['p95_max_channel'] <= case['p95_range'][1])
            else:
                item['pass'] = item['reference_match']
            results.append(item)
    sheet.save(output / 'comparison.png')
    # Cross-case sensitivity checks catch a disconnected image, ignored UV
    # transform, unchanged roughness/metalness or stale material slot.
    signals = []
    by_name = {case['name']: case for case in CASES}
    for renderer, samples in [('Storm',4), ('Toon',1), ('Toon',4)]:
        for a, b in [('normal-map','dielectric-mid'), ('normal-map','normal-rotated'),
                     ('color-texture','color-rotated'), ('dielectric-mid','metal-mid'),
                     ('dielectric-mid','dielectric-smooth'),
                     ('metallic-texture','dielectric-mid'), ('roughness-texture','dielectric-mid'),
                     ('opacity-mask','dielectric-mid'), ('opacity-blend','dielectric-mid'),
                     ('opacity-constant','dielectric-mid'),
                     ('curved-dielectric','curved-metal'), ('grazing-dielectric','grazing-metal')]:
            first = np.asarray(Image.open(output / f'{a}-{renderer}-{samples}x.png').convert('RGB'), dtype=float)
            second = np.asarray(Image.open(output / f'{b}-{renderer}-{samples}x.png').convert('RGB'), dtype=float)
            mask = measurement_mask(by_name[a]) & measurement_mask(by_name[b])
            changed = float(np.abs(first-second)[mask].mean())
            signals.append(dict(renderer=renderer, samples=samples, cases=[a,b], mae_rgb=changed, passed=changed > 1))
    inputs = [fixture, Path(__file__).resolve(), scripts / 'preview_surface_usdview.py',
              scripts.parent / 'shaders/preview/preview_surface.slang',
              scripts.parent / 'backend/vulkan/vulkan_scene.cpp']
    inputs.extend(output / name for name in ('color.png','normal.png','normal-rotated.png','scalar.png','cases.json'))
    controls = image_controls(output)
    summary = dict(input_sha256={path.name: digest(path) for path in inputs},
                   commands=commands, cases=results,
                   signals=signals, controls=controls,
                   reference_equivalent=all(item['reference_match'] for item in results),
                   passed=(all(item['pass'] for item in results) and all(item['passed'] for item in signals)
                           and all(item['passed'] for item in controls)))
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    for item in results:
        kind = 'difference characterization' if 'expected_difference' in item else 'reference match'
        print(f"{item['case']} {item['samples']}x: MAE={item['mae_rgb']:.3f}, p95={item['p95_max_channel']:.1f}, {kind}: {item['pass']}")
    for item in controls:
        if not item['passed']:
            print(f'failed control: {item}')
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
