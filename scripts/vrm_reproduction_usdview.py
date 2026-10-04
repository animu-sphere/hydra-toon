# SPDX-License-Identifier: Apache-2.0
"""testusdview driver matching a --camera-output JSON capture.

TOON_COMPARISON_CAMERA is the exported camera, TOON_HYDRA_IMAGE the PNG,
and TOON_EXPECT_MTOON / TOON_EXPECT_DRAWS assert material selection.
Launch on a local VRM with the current hdToon and VRM plugins registered.
"""
import json
import os
from pathlib import Path

from pxr import Gf, UsdGeom
from pxr.Usdviewq.common import SelectionHighlightModes


def testUsdviewInputFunction(appController):
    stage = appController._dataModel.stage
    data = json.loads(Path(os.environ['TOON_COMPARISON_CAMERA']).read_text())
    view = data['view']
    projection = data['projection']
    matrix = Gf.Matrix4d(*view).GetInverse()
    near = projection[14] / (projection[10] - 1)
    far = projection[14] / (projection[10] + 1)
    camera = Gf.Camera(transform=matrix, projection=Gf.Camera.Perspective,
                       horizontalAperture=20 / projection[0],
                       verticalAperture=20 / projection[5], focalLength=10,
                       clippingRange=Gf.Range1f(near, far))
    stage.SetEditTarget(stage.GetSessionLayer())
    usd_camera = UsdGeom.Camera.Define(stage, '/ToonComparisonCamera')
    usd_camera.SetFromCamera(camera)
    settings = appController._dataModel.viewSettings
    settings.cameraPrim = usd_camera.GetPrim()
    settings.showBBoxes = False
    settings.showHUD = False
    settings.selHighlightMode = SelectionHighlightModes.NEVER
    stage_view = appController._stageView
    # usdview draws its axis after the renderer, outside the scene/AOV.
    stage_view.DrawAxis = lambda matrix: None
    appController._dataModel.selection.clearPrims()
    # A fixed physical framebuffer extent avoids resampling a host screenshot.
    stage_view.SetPhysicalWindowSize(data['width'], data['height'])
    stage_view.SetRendererSetting('toon:msaaSamples', 4)
    stage_view.SetRendererSetting('toon:metersPerUnit', float(UsdGeom.GetStageMetersPerUnit(stage)))
    stage_view.SetForceRefresh(True)
    stage_view.updateView()
    appController._takeShot(os.environ['TOON_HYDRA_IMAGE'], iterations=10, waitForConvergence=True)
    frame = dict((key, int(value)) for key, value in
                 (field.split('=') for field in
                  Path(os.environ['TOON_HYDRA_EVIDENCE']).read_text().splitlines()[-1].split()))
    assert frame['materials_mtoon'] == int(os.environ['TOON_EXPECT_MTOON']), frame
    assert frame['draws_mtoon'] == int(os.environ['TOON_EXPECT_DRAWS']), frame
    assert frame['samples'] == 4, frame
    assert frame['width'] == data['width'] and frame['height'] == data['height'], frame
    assert frame['draws_authored_normals'] == frame['draws_mtoon'], frame
    print('comparison frame:', frame)
