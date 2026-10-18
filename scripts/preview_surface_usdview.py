# SPDX-License-Identifier: Apache-2.0
"""testusdview worker for evaluate_preview_surface.py (Storm and Toon)."""
import json
import math
import os
from pathlib import Path

from pxr import Gf, Plug, Sdf, Usd, UsdGeom, UsdShade
from OpenGL import GL
from pxr.Usdviewq.common import ColorCorrectionModes, SelectionHighlightModes
from pxr.Usdviewq import viewSettingsDataModel


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
    for channel in 'rgba':
        texture.CreateOutput(channel, Sdf.ValueTypeNames.Float)

    mesh = UsdGeom.Mesh(stage.GetPrimAtPath('/World/Patch'))
    st = UsdGeom.PrimvarsAPI(mesh).GetPrimvar('st')
    rest = (mesh.GetPointsAttr().Get(), mesh.GetFaceVertexCountsAttr().Get(),
            mesh.GetFaceVertexIndicesAttr().Get(), mesh.GetNormalsAttr().Get(), st.Get())
    # Original, redistributable tessellated dome with analytic smooth normals.
    points, normals, coords, indices = [], [], [], []
    divisions = 32
    for j in range(divisions+1):
        for i in range(divisions+1):
            x, y = -1+2*i/divisions, -1+2*j/divisions
            points.append(Gf.Vec3f(x, y, 0.45*(1-x*x-y*y)))
            normals.append(Gf.Vec3f(0.9*x, 0.9*y, 1).GetNormalized())
            coords.append(Gf.Vec2f(i/divisions, j/divisions))
    for j in range(divisions):
        for i in range(divisions):
            a = j*(divisions+1)+i
            indices.extend((a,a+1,a+divisions+2,a,a+divisions+2,a+divisions+1))
    curved = (points, [3]*(len(indices)//3), indices, normals, coords)
    angle = math.radians(70)
    grazing = ([Gf.Vec3f(p[0]*math.cos(angle), p[1], -p[0]*math.sin(angle)) for p in rest[0]],
               rest[1], rest[2], [Gf.Vec3f(math.sin(angle),0,math.cos(angle))], rest[4])

    settings = appController._dataModel.viewSettings
    settings.showBBoxes = settings.showHUD = False
    settings.selHighlightMode = SelectionHighlightModes.NEVER
    settings.enableSceneLights = True
    settings.ambientLightOnly = False
    settings.domeLightEnabled = False
    settings.colorCorrectionMode = ColorCorrectionModes.SRGB
    # Match vulkan_scene.cpp's current fixed opaque linear clear. This is a
    # worker-local usdview palette entry, not image-derived exposure matching
    # or a renderer background API. RGBA/background modes are separate work.
    clear = (0.05, 0.10, 0.15, 1.0)
    viewSettingsDataModel._CLEAR_COLORS_DICT['Black'] = clear
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
                    samples=samples, clear_linear=clear, completed_cases=[])
    plugin = Plug.Registry().GetPluginWithName('hdToon' if renderer == 'Toon' else 'hdStorm')
    metadata['plugin_path'] = plugin.path
    cases = json.loads((root / 'cases.json').read_text())
    for case in cases:
        geometry = {'planar': rest, 'curved': curved, 'grazing': grazing}[case.get('geometry','planar')]
        mesh.GetPointsAttr().Set(geometry[0])
        mesh.GetFaceVertexCountsAttr().Set(geometry[1])
        mesh.GetFaceVertexIndicesAttr().Set(geometry[2])
        mesh.GetNormalsAttr().Set(geometry[3])
        mesh.SetNormalsInterpolation('vertex' if case.get('geometry') == 'curved' else 'constant')
        st.Set(geometry[4])
        defaults = [('diffuseColor', Sdf.ValueTypeNames.Color3f, Gf.Vec3f(0.5,0.25,0.1)),
                    ('emissiveColor', Sdf.ValueTypeNames.Color3f, Gf.Vec3f(0)),
                    ('metallic', Sdf.ValueTypeNames.Float, 0.0),
                    ('roughness', Sdf.ValueTypeNames.Float, 0.6),
                    ('opacity', Sdf.ValueTypeNames.Float, 1.0),
                    ('opacityThreshold', Sdf.ValueTypeNames.Float, 0.0),
                    ('normal', Sdf.ValueTypeNames.Normal3f, Gf.Vec3f(0,0,1))]
        for name, kind, value in defaults:
            surface_input = shader.CreateInput(name, kind)
            surface_input.DisconnectSource()
            authored = case.get(name, value)
            surface_input.Set(Gf.Vec3f(*authored) if isinstance(authored, list) else authored)
        if 'texture' in case:
            normal = case['texture'] == 'normal'
            baked = renderer == 'Storm' and case.get('bake_reference_rotation', False)
            filename = 'normal-rotated.png' if baked else f"{case['texture']}.png"
            texture.CreateInput('file', Sdf.ValueTypeNames.Asset).Set(
                Sdf.AssetPath(str(root / filename)))
            texture.CreateInput('sourceColorSpace', Sdf.ValueTypeNames.Token).Set('sRGB' if case['texture'] == 'color' else 'raw')
            texture.CreateInput('wrapS', Sdf.ValueTypeNames.Token).Set('repeat')
            texture.CreateInput('wrapT', Sdf.ValueTypeNames.Token).Set('repeat')
            texture.CreateInput('scale', Sdf.ValueTypeNames.Float4).Set(Gf.Vec4f(2,2,2,1) if normal else Gf.Vec4f(1))
            texture.CreateInput('bias', Sdf.ValueTypeNames.Float4).Set(Gf.Vec4f(-1,-1,-1,0) if normal else Gf.Vec4f(0))
            transform.GetInput('rotation').Set(0 if baked else case.get('rotation', 0))
            target = case.get('input', 'normal' if normal else 'diffuseColor')
            shader.GetInput(target).ConnectToSource(texture.ConnectableAPI(), case.get('channel','rgb'))
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
