# testusdview script: draws the stage with hdToon and asserts how many
# materials selected MToon, from hdToon's host evidence.
#
# TOON_EXPECT_MTOON  the MToon count to assert (default 1: material-probe.usda)
# TOON_HYDRA_EVIDENCE  where hdToon appends per-frame evidence (default: a
#                      temporary file)
# TOON_HYDRA_IMAGE  where the frame is written (default: a temporary file)
import os
import tempfile

_scratch = None


def _path(variable, name):
    global _scratch
    path = os.environ.get(variable)
    if not path:
        if _scratch is None:
            _scratch = tempfile.mkdtemp(prefix="toon-vrm-check-")
        path = os.path.join(_scratch, name)
        # hdToon reads the variable on every frame, so setting it here, in
        # usdview's process, before the shot is enough.
        os.environ[variable] = path
    return path


def _last_frame(evidence):
    with open(evidence, encoding="utf-8") as stream:
        line = stream.read().splitlines()[-1]
    return dict((key, int(value)) for key, value in
                (field.split("=", 1) for field in line.split()))


def testUsdviewInputFunction(appController):
    evidence = _path("TOON_HYDRA_EVIDENCE", "evidence.txt")
    image = _path("TOON_HYDRA_IMAGE", "frame.png")
    appController._dataModel.viewSettings.showHUD = False
    appController._stageView.SetForceRefresh(True)
    appController._stageView.updateView()
    appController._takeShot(image, iterations=10, waitForConvergence=True)
    renderer = appController._stageView.GetCurrentRendererId()
    frame = _last_frame(evidence)
    print("renderer:", renderer)
    print("frame:", frame)
    print("evidence:", evidence)
    print("image:", image)
    assert renderer == "HdToonRendererPlugin", renderer
    expected = int(os.environ.get("TOON_EXPECT_MTOON", "1"))
    assert frame["materials_mtoon"] == expected, frame
