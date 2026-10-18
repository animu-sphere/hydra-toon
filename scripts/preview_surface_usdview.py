# SPDX-License-Identifier: Apache-2.0
"""testusdview worker for evaluate_preview_surface.py (Storm and Toon)."""
import json
import os
from pathlib import Path

from pxr import Gf, Plug, Sdf, Usd, UsdShade
from OpenGL import GL
from pxr.Usdviewq.common import ColorCorrectionModes, SelectionHighlightModes


def testUsdviewInputFunction(appController):
    root = Path(os.environ['TOON_PREVIEW_OUTPUT'])
    renderer = os.environ['TOON_PREVIEW_RENDERER']
    samples = int(os.environ['TOON_PREVIEW_SAMPLES'])
    stage = appController._dataModel.stage
    stage.SetEditTarget(stage.GetSessionLayer())
    shader = UsdShade.Shader(stage.GetPrimAtPath('/Material/Surface'))
    uv = UsdShade.Shader.Define(stage, '/Material/UV')
    uv.CreateIdAttr('UsdPrimvarReader_float2')
    uv.CreateInput('varname', Sdf.ValueTypeNames.Token).Set('st')
    uv.CreateOutput('result', Sdf.ValueTypeNames.Float2)
    transform = UsdShade.Shader.Define(stage, '/Material/Transform')
    transform.CreateIdAttr('UsdTransform2d')
    transform.CreateInput('in', Sdf.ValueTypeNames.Float2).ConnectToSource(uv.ConnectableAPI(), 'result')
    transform.CreateInput('rotation', Sdf.ValueTypeNames.Float).Set(0)
    transform.CreateOutput('result', Sdf.ValueTypeNames.Float2)
    texture = UsdShade.Shader.Define(stage, '/Material/Texture')
    texture.CreateIdAttr('UsdUVTexture')
    texture.CreateInput('st', Sdf.ValueTypeNames.Float2).ConnectToSource(transform.ConnectableAPI(), 'result')
    texture.CreateOutput('rgb', Sdf.ValueTypeNames.Float3)

    settings = appController._dataModel.viewSettings
    settings.showBBoxes = settings.showHUD = False
    settings.selHighlightMode = SelectionHighlightModes.NEVER
    settings.enableSceneLights = True
    settings.ambientLightOnly = False
    settings.domeLightEnabled = False
    settings.colorCorrectionMode = ColorCorrectionModes.SRGB
    settings.clearColorText = 'Black'
    appController._dataModel.selection.clearPrims()
    view = appController._stageView
    view.DrawAxis = lambda matrix: None
    view.SetPhysicalWindowSize(256, 256)
    if renderer == 'Toon':
        view.SetRendererSetting('toon:msaaSamples', samples)
    assert view.GetCurrentRendererId() == ('HdToonRendererPlugin' if renderer == 'Toon' else 'HdStormRendererPlugin')
    metadata = dict(openusd=list(Usd.GetVersion()), renderer=view.GetCurrentRendererId(),
                    gl_renderer=GL.glGetString(GL.GL_RENDERER).decode(),
                    gl_version=GL.glGetString(GL.GL_VERSION).decode(),
                    samples=samples, completed_cases=[])
    plugin = Plug.Registry().GetPluginWithName('hdToon' if renderer == 'Toon' else 'hdStorm')
    metadata['plugin_path'] = plugin.path
    cases = json.loads((root / 'cases.json').read_text())
    for case in cases:
        defaults = [('diffuseColor', Sdf.ValueTypeNames.Color3f, Gf.Vec3f(0.5,0.25,0.1)),
                    ('emissiveColor', Sdf.ValueTypeNames.Color3f, Gf.Vec3f(0)),
                    ('metallic', Sdf.ValueTypeNames.Float, 0.0),
                    ('roughness', Sdf.ValueTypeNames.Float, 0.6),
                    ('normal', Sdf.ValueTypeNames.Normal3f, Gf.Vec3f(0,0,1))]
        for name, kind, value in defaults:
            surface_input = shader.CreateInput(name, kind)
            surface_input.DisconnectSource()
            authored = case.get(name, value)
            surface_input.Set(Gf.Vec3f(*authored) if isinstance(authored, list) else authored)
        if 'texture' in case:
            normal = case['texture'] == 'normal'
            baked = renderer == 'Storm' and case.get('bake_reference_rotation', False)
            filename = 'normal-rotated.png' if baked else ('normal.png' if normal else 'color.png')
            texture.CreateInput('file', Sdf.ValueTypeNames.Asset).Set(
                Sdf.AssetPath(str(root / filename)))
            texture.CreateInput('sourceColorSpace', Sdf.ValueTypeNames.Token).Set('raw' if normal else 'sRGB')
            texture.CreateInput('wrapS', Sdf.ValueTypeNames.Token).Set('repeat')
            texture.CreateInput('wrapT', Sdf.ValueTypeNames.Token).Set('repeat')
            texture.CreateInput('scale', Sdf.ValueTypeNames.Float4).Set(Gf.Vec4f(2,2,2,1) if normal else Gf.Vec4f(1))
            texture.CreateInput('bias', Sdf.ValueTypeNames.Float4).Set(Gf.Vec4f(-1,-1,-1,0) if normal else Gf.Vec4f(0))
            transform.GetInput('rotation').Set(0 if baked else case.get('rotation', 0))
            shader.GetInput('normal' if normal else 'diffuseColor').ConnectToSource(texture.ConnectableAPI(), 'rgb')
        view.SetForceRefresh(True)
        view.updateView()
        appController._takeShot(str(root / f"{case['name']}-{renderer}-{samples}x.png"),
                                iterations=10, waitForConvergence=True)
        if renderer == 'Toon':
            frame = dict((key, int(value)) for key, value in
                         (field.split('=') for field in Path(os.environ['TOON_HYDRA_EVIDENCE']).read_text().splitlines()[-1].split()))
            assert frame['samples'] == samples and frame['width'] == frame['height'] == 256, frame
            assert frame['materials_preview'] >= 1 and frame['draws'] == 1, frame
            assert frame['draws_mtoon'] == frame['draws_mmd'] == 0, frame
        metadata['completed_cases'].append(case['name'])
    (root / f'{renderer}-{samples}x.json').write_text(json.dumps(metadata, indent=2) + '\n')
