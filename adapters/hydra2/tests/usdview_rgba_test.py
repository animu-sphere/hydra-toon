# SPDX-License-Identifier: Apache-2.0
"""A bounded usdview consumer check, using its uncorrected linear framebuffer.

Qt labels this framebuffer premultiplied. Convert raw linear channels before
handing Qt straight sRGB pixels for PNG, or opaque sRGB pixels for display.
The stock sRGB screenshot is measured separately, never used as a foreground.
"""
import json
import math
import os
from pathlib import Path

from pxr import Gf, Sdf, UsdShade
from pxr.Usdviewq.common import ColorCorrectionModes, SelectionHighlightModes
from pxr.Usdviewq.qt import QtGui


def _encode(value):
    value = max(0.0, min(value, 1.0))
    return 12.92 * value if value <= 0.0031308 else 1.055 * value ** (1 / 2.4) - 0.055


def _decode(value):
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def _byte(value):
    return math.floor(value * 255 + 0.5)


def _raw(image, x, y):
    # pixel(), unlike pixelColor(), returns the stored premultiplied channels.
    value = image.pixel(x, y)
    return (value >> 16 & 255, value >> 8 & 255, value & 255, value >> 24 & 255)


def _capture(app, phase, mode, samples):
    view = app._stageView
    app._dataModel.viewSettings.colorCorrectionMode = mode
    view.SetForceRefresh(True)
    view.updateView()
    path = f'{os.environ["TOON_HYDRA_IMAGE"]}-{phase}.png'
    app._takeShot(path, iterations=3, waitForConvergence=True)
    evidence = Path(os.environ["TOON_HYDRA_EVIDENCE"]).read_text(encoding="utf-8").splitlines()
    frame = dict(field.split("=", 1) for field in evidence[-1].split())
    assert int(frame["samples"]) == samples and int(frame["buffers_written"]) >= 2, frame
    image = view.grabFrameBuffer()
    assert not image.isNull() and image.hasAlphaChannel()
    assert image.format() == QtGui.QImage.Format_ARGB32_Premultiplied, image.format()
    return image, path


def _background(name, x, y):
    # The image fixture is original, top-left sRGB RGB8, with distinct rows and
    # channels. Decode it before composition, like an opaque image consumer.
    if name == "image":
        encoded = ((17 * x + 29 * y) % 256, (53 * x + 7 * y) % 256,
                   (11 * x + 31 * y) % 256)
        return tuple(_decode(value / 255) for value in encoded)
    if name == "checker":
        return (0.18 if (x // 13 + y // 13) % 2 else 0.72,) * 3
    return (1.0 if name == "white" else 0.0,) * 3


def _image(width, height, pixels):
    # Keep the backing bytes alive until the image owns its copy.
    result = QtGui.QImage(bytes(pixels), width, height, width * 4,
                         QtGui.QImage.Format_RGBA8888).copy()
    result.setColorSpace(QtGui.QColorSpace(QtGui.QColorSpace.SRgb))
    return result


def _save(image, path):
    assert image.save(str(path), "PNG"), path
    restored = QtGui.QImage(str(path))
    assert not restored.isNull() and restored.size() == image.size()
    assert restored.colorSpace() == image.colorSpace(), "PNG lost sRGB metadata"
    return restored


def _export(linear, path):
    width, height = linear.width(), linear.height()
    source, straight = [], bytearray()
    for y in range(height):
        for x in range(width):
            pixel = _raw(linear, x, y)
            source.append(pixel)
            alpha = pixel[3]
            straight.extend([_byte(_encode(c / alpha)) if alpha else 0
                             for c in pixel[:3]] + [alpha])
    restored = _save(_image(width, height, straight), path)
    max_error = 0.0
    for i, pixel in enumerate(source):
        actual = restored.pixelColor(i % width, i // width).getRgb()
        assert actual == tuple(straight[i * 4:i * 4 + 4]), "PNG changed straight RGBA"
        for channel in range(3):
            premultiplied = _decode(actual[channel] / 255) * actual[3] / 255
            max_error = max(max_error, abs(premultiplied - pixel[channel] / 255))
    assert max_error < 0.005, ("PNG linear reconstruction", max_error)
    return source, max_error


def _composite(source, width, height, name, path):
    pixels = bytearray()
    for i, pixel in enumerate(source):
        bg = _background(name, i % width, i // width)
        pixels.extend([_byte(_encode(pixel[c] / 255 + bg[c] * (1 - pixel[3] / 255)))
                       for c in range(3)] + [255])
    restored = _save(_image(width, height, pixels), path)
    # Independent PNG decode through Qt, then source-over in the reverse
    # direction. This checks alpha, transfer, image orientation and channels.
    error = 0.0
    for i, pixel in enumerate(source):
        bg = _background(name, i % width, i // width)
        actual = restored.pixelColor(i % width, i // width).getRgb()
        assert actual[3] == 255
        for c in range(3):
            expected = pixel[c] / 255 + bg[c] * (1 - pixel[3] / 255)
            error = max(error, abs(_decode(actual[c] / 255) - expected))
    assert error < 0.005, (name, error)
    return error


def testUsdviewInputFunction(app):
    view = app._stageView
    settings = app._dataModel.viewSettings
    settings.showBBoxes = settings.showHUD = False
    settings.selHighlightMode = SelectionHighlightModes.NEVER
    # StageView draws its origin axes unconditionally, after the renderer.
    # Suppress that host decoration in this controlled foreground consumer.
    view.DrawAxis = lambda _: None
    stage = app._dataModel.stage
    material = UsdShade.Material.Define(stage, "/Material")
    shader = UsdShade.Shader.Define(stage, "/Material/Surface")
    shader.CreateIdAttr("UsdPreviewSurface")
    shader.CreateInput("diffuseColor", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(0))
    shader.CreateInput("emissiveColor", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(0.6, 0.3, 0.15))
    opacity = shader.CreateInput("opacity", Sdf.ValueTypeNames.Float)
    material.CreateSurfaceOutput().ConnectToSource(shader.ConnectableAPI(), "surface")
    UsdShade.MaterialBindingAPI.Apply(stage.GetPrimAtPath("/World/Triangle")).Bind(material)
    view.SetRendererSetting("toon:backgroundMode", "transparent")
    root = os.environ["TOON_HYDRA_IMAGE"]
    results = []
    for samples in (1, 4):
        view.SetRendererSetting("toon:msaaSamples", samples)
        opaque = None
        for alpha in (1.0, 0.25, 0.0):
            opacity.Set(alpha)
            phase = f"rgba-{samples}-{alpha}"
            linear, _ = _capture(app, phase + "-linear", ColorCorrectionModes.DISABLED, samples)
            width, height = linear.width(), linear.height()
            source, export_error = _export(linear, f"{root}-{phase}-straight.png")
            if alpha == 1:
                opaque = source
            assert len(opaque) == len(source)
            center = (height // 2) * width + width // 2
            assert source[center][3] == _byte(alpha), (phase, source[center])
            assert source[0] == (0, 0, 0, 0), (phase, source[0])
            if alpha == 0:
                assert all(pixel == (0, 0, 0, 0) for pixel in source)
            else:
                for i, pixel in enumerate(source):
                    if pixel[3] == 0:
                        assert pixel[:3] == (0, 0, 0)
                    if opaque[i][3] == 255:
                        assert all(abs(pixel[c] - opaque[i][c] * pixel[3] / 255) < 1.6
                                   for c in range(3)), (phase, i, pixel, opaque[i])
            if samples == 4 and alpha == 1:
                assert any(0 < pixel[3] < 255 for pixel in source), "lost MSAA silhouette"
            composites = {}
            display_errors = {}
            if alpha == 0.25:
                for name in ("black", "white", "checker", "image"):
                    composites[name] = _composite(source, width, height, name,
                                                  f"{root}-{phase}-{name}.png")
                # Consumer work must leave the captured foreground unchanged.
                assert source == [_raw(linear, x, y) for y in range(height) for x in range(width)]
                # A separate, actual host display path is the oracle for solid
                # backgrounds: let Vulkan compose into an opaque AOV, then let
                # usdview apply sRGB. It must agree with our foreground consumer.
                view.SetRendererSetting("toon:backgroundMode", "opaque")
                for name, value in (("black", 0.0), ("white", 1.0)):
                    view.SetRendererSetting("toon:backgroundColor", Gf.Vec4f(value, value, value, 1))
                    displayed, _ = _capture(app, phase + "-display-" + name,
                                            ColorCorrectionModes.SRGB, samples)
                    composed = QtGui.QImage(f"{root}-{phase}-{name}.png")
                    assert displayed.size() == composed.size()
                    display_error = 0
                    for y in range(height):
                        for x in range(width):
                            actual = _raw(displayed, x, y)
                            expected_pixel = composed.pixelColor(x, y).getRgb()
                            assert actual[3] == 255
                            difference = max(abs(actual[c] - expected_pixel[c]) for c in range(3))
                            display_error = max(display_error, difference)
                            assert difference <= 3, (
                                "opaque host display differs from foreground composite", samples, name, x, y,
                                actual, expected_pixel)
                    display_errors[name] = display_error
                view.SetRendererSetting("toon:backgroundMode", "transparent")
                returned, _ = _capture(app, phase + "-returned-linear", ColorCorrectionModes.DISABLED, samples)
                assert returned.size() == linear.size()
                assert source == [_raw(returned, x, y) for y in range(height) for x in range(width)], (
                    "host background changes altered foreground")
            native, native_path = _capture(app, phase + "-native-srgb", ColorCorrectionModes.SRGB, samples)
            assert native.size() == linear.size()
            assert _raw(native, width // 2, height // 2)[3] == source[center][3]
            exported = QtGui.QImage(native_path).pixelColor(width // 2, height // 2).getRgb()
            expected = [_byte(_encode(c / source[center][3])) if source[center][3] else 0
                        for c in source[center][:3]]
            native_error = max(abs(exported[c] - expected[c]) for c in range(3))
            # Opaque is a control. A transparent native screenshot is diagnostic:
            # stock usdview's correction/Qt boundary is outside hdToon's control.
            if alpha == 1:
                assert native_error <= 2, ("opaque sRGB control", exported, expected)
            result = dict(samples=samples, opacity=alpha, width=width, height=height,
                          png_linear_error=export_error, composite_linear_error=composites,
                          opaque_host_display_rgb_error=display_errors,
                          linear_center=list(source[center]),
                          native_srgb_raw_center=list(_raw(native, width // 2, height // 2)),
                          native_srgb_center=list(exported), expected_straight_center=expected,
                          native_srgb_rgb_error=native_error)
            results.append(result)
            print("RGBA consumer:", json.dumps(result), flush=True)
    Path(f"{root}-rgba-consumer.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
