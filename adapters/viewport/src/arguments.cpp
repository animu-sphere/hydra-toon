// SPDX-License-Identifier: Apache-2.0
#include "arguments.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace Toon::viewport {
namespace {

std::uint64_t ReadUnsigned(std::string_view value, std::string_view name) {
  // std::stoull would accept and wrap a leading minus sign.
  if (value.empty() || value.front() < '0' || value.front() > '9') {
    throw std::invalid_argument(std::string(name) +
                                " must be a non-negative integer");
  }
  std::size_t consumed = 0;
  std::uint64_t result = 0;
  try {
    result = std::stoull(std::string(value), &consumed);
  } catch (const std::exception&) {
    throw std::invalid_argument(std::string(name) +
                                " must be a non-negative integer");
  }
  if (consumed != value.size()) {
    throw std::invalid_argument(std::string(name) +
                                " must be a non-negative integer");
  }
  return result;
}

float ReadFiniteFloat(std::string_view value, std::string_view name) {
  std::size_t consumed = 0;
  const float result = std::stof(std::string(value), &consumed);
  if (consumed != value.size() || !std::isfinite(result)) {
    throw std::invalid_argument(std::string(name) + " must be finite");
  }
  return result;
}

} // namespace

Arguments ParseArguments(int argc, char** argv) {
  Arguments result;
  for (int index = 1; index < argc; ++index) {
    const std::string_view option(argv[index]);
    const auto next = [&]() -> std::string_view {
      if (++index >= argc) {
        throw std::invalid_argument(std::string(option) + " requires a value");
      }
      return argv[index];
    };
    if (option == "--width") {
      result.width = static_cast<std::uint32_t>(ReadUnsigned(next(), option));
    } else if (option == "--height") {
      result.height = static_cast<std::uint32_t>(ReadUnsigned(next(), option));
    } else if (option == "--frames") {
      result.frame_limit = ReadUnsigned(next(), option);
    } else if (option == "--telemetry-output") {
      result.telemetry_output = next();
    } else if (option == "--background") {
      const auto value = next();
      if (value != "solid" && value != "checkerboard")
        throw std::invalid_argument("--background must be solid or checkerboard");
      result.background.checkerboard = value == "checkerboard";
    } else if (option == "--background-color") {
      result.background.color.x = ReadFiniteFloat(next(), option);
      result.background.color.y = ReadFiniteFloat(next(), option);
      result.background.color.z = ReadFiniteFloat(next(), option);
    } else if (option == "--samples") {
      result.options.samples =
          static_cast<std::uint32_t>(ReadUnsigned(next(), option));
    } else if (option == "--switch-samples") {
      result.switch_samples =
          static_cast<std::uint32_t>(ReadUnsigned(next(), option));
    } else if (option == "--switch-file") {
      result.switch_file = next();
    } else if (option == "--hidden") {
      result.visible = false;
    } else if (option == "--usd") {
      result.usd = next();
    } else if (option == "--expect-draws") {
      result.expect_draws = ReadUnsigned(next(), option);
    } else if (option == "--expect-hulls") {
      result.expect_hulls = ReadUnsigned(next(), option);
    } else if (option == "--camera-pan") {
      result.camera_pan_x = ReadFiniteFloat(next(), option);
      result.camera_pan_y = ReadFiniteFloat(next(), option);
    } else if (option == "--camera-dolly") {
      result.camera_dolly = ReadFiniteFloat(next(), option);
    } else if (option == "--time" || option == "--time-step") {
      const std::string value(next());
      std::size_t consumed = 0;
      const double number = std::stod(value, &consumed);
      if (consumed != value.size() || !std::isfinite(number)) {
        throw std::invalid_argument(std::string(option) + " must be finite");
      }
      (option == "--time" ? result.time : result.time_step) = number;
    } else if (option == "--play") {
      result.play = true;
    } else if (option == "--playback-speed") {
      const std::string value(next());
      std::size_t consumed = 0;
      result.playback_speed = std::stod(value, &consumed);
      if (consumed != value.size() || !std::isfinite(result.playback_speed) ||
          result.playback_speed < 0.05 || result.playback_speed > 4.0) {
        throw std::invalid_argument("--playback-speed must be in [0.05, 4]");
      }
    } else if (option == "--loop") {
      const auto value = next();
      if (value != "on" && value != "off")
        throw std::invalid_argument("--loop must be on or off");
      result.loop = value == "on";
    } else if (option == "--direct-strength" || option == "--ambient-strength") {
      const std::string value(next());
      std::size_t consumed = 0;
      const float number = std::stof(value, &consumed);
      if (consumed != value.size() || !std::isfinite(number) || number < 0.0F || number > 4.0F) {
        throw std::invalid_argument(std::string(option) + " must be in [0, 4]");
      }
      (option == "--direct-strength" ? result.lighting.direct_scale : result.lighting.ambient_scale) = number;
    } else if (option == "--lighting") {
      const auto value = next();
      if (value != "scene" && value != "camera")
        throw std::invalid_argument("--lighting must be scene or camera");
      result.lighting.scene_lights = value == "scene";
    } else if (option == "--material-view") {
      const auto value = next();
      const std::vector<std::string_view> modes{"surface", "base", "normal", "direct", "ambient", "shadow"};
      const auto found = std::find(modes.begin(), modes.end(), value);
      if (found == modes.end())
        throw std::invalid_argument("unknown material view");
      result.lighting.material = static_cast<Toon::MaterialDebug>(found - modes.begin());
    } else if (option == "--outline-culling") {
      const auto value = next();
      if (value != "on" && value != "off")
        throw std::invalid_argument("--outline-culling must be on or off");
      result.outline_culling = value == "on";
    } else if (option == "--outlines") {
      const auto value = next();
      if (value != "on" && value != "off") {
        throw std::invalid_argument("--outlines must be on or off");
      }
      result.outlines = value == "on";
    } else if (option == "--shadows") {
      const auto value = next();
      if (value != "on" && value != "off") {
        throw std::invalid_argument("--shadows must be on or off");
      }
      result.shadows = value == "on";
    } else if (option == "--shadow-resolution") {
      const std::uint64_t value = ReadUnsigned(next(), option);
      if (value < 512 || value > 4096 || (value & (value - 1)) != 0) {
        throw std::invalid_argument(
            "--shadow-resolution must be 512, 1024, 2048 or 4096");
      }
      result.shadow_settings.resolution = static_cast<std::uint32_t>(value);
    } else if (option == "--shadow-depth-bias" ||
               option == "--shadow-normal-bias" ||
               option == "--shadow-filter-radius") {
      const std::string value(next());
      std::size_t consumed = 0;
      const float number = std::stof(value, &consumed);
      const float limit = option == "--shadow-filter-radius" ? 8.0F : 16.0F;
      if (consumed != value.size() || !std::isfinite(number) || number < 0.0F ||
          number > limit) {
        throw std::invalid_argument(std::string(option) + " must be in [0, " +
                                    (limit == 8.0F ? "8]" : "16]"));
      }
      (option == "--shadow-depth-bias"    ? result.shadow_settings.depth_bias
       : option == "--shadow-normal-bias" ? result.shadow_settings.normal_bias
                                          : result.shadow_settings.filter_radius) =
          number;
    } else if (option == "--mmd-self-shadow") {
      const auto value = next();
      if (value != "on" && value != "off") {
        throw std::invalid_argument("--mmd-self-shadow must be on or off");
      }
      result.mmd_self_shadow = value == "on";
    } else if (option == "--mmd-morph-tints") {
      const auto value = next();
      if (value != "on" && value != "off") {
        throw std::invalid_argument("--mmd-morph-tints must be on or off");
      }
      result.mmd_morph_tints = value == "on";
    } else if (option == "--screenshot") {
      result.screenshot = next();
    } else if (option == "--foreground-output") {
      result.foreground_output = next();
    } else if (option == "--capture-sequence") {
      result.capture_sequence = next();
    } else if (option == "--camera-output") {
      result.camera_output = next();
    } else if (option == "--vsync") {
      const auto value = next();
      if (value != "on" && value != "off") {
        throw std::invalid_argument("--vsync must be on or off");
      }
      result.vsync = value == "on";
    } else if (option == "--backend") {
      const auto value = next();
      if (value != "vulkan" && value != "webgpu") {
        throw std::invalid_argument("--backend must be vulkan or webgpu");
      }
      result.webgpu = value == "webgpu";
    } else if (option == "--webgpu-adapter") {
      const auto value = next();
      if (value != "d3d12" && value != "vulkan") {
        throw std::invalid_argument("--webgpu-adapter must be d3d12 or vulkan");
      }
      result.webgpu_adapter =
          value == "vulkan" ? Toon::WebGpuAdapter::Vulkan : Toon::WebGpuAdapter::Direct3D12;
    } else if (option == "--overlay") {
      const auto value = next();
      if (value != "on" && value != "off") {
        throw std::invalid_argument("--overlay must be on or off");
      }
      result.overlay = value == "on";
    } else if (option == "--mmd-physics") {
      result.mmd_physics = true;
      result.avatar_runtime = true;
    } else if (option == "--avatar-runtime") {
      result.avatar_runtime = true;
    } else if (option == "--avatar-lookat") {
      result.avatar_look_at = next();
    } else if (option == "--avatar-rate") {
      const std::string value(next());
      std::size_t consumed = 0;
      result.avatar_rate = std::stod(value, &consumed);
      if (consumed != value.size() || !std::isfinite(result.avatar_rate) ||
          result.avatar_rate < 0.0 || result.avatar_rate > 2000.0) {
        throw std::invalid_argument("--avatar-rate must be in [0, 2000]");
      }
    } else if (option == "--avatar-reset-every") {
      result.avatar_reset_every = ReadUnsigned(next(), option);
    } else if (option == "--avatar-motion") {
      result.avatar_motion = next();
    } else if (option == "--avatar-model") {
      result.avatar_model = next();
    } else if (option == "--help") {
      std::cout << "Usage: toon-viewport [options]\n"
                   "  --width N --height N     window size (default 1280x720)\n"
                   "  --frames N               exit after N presented frames\n"
                   "  --telemetry-output FILE  save latency/timing JSON on exit\n"
                   "  --vsync on|off           FIFO or immediate present\n"
                   "  --backend vulkan|webgpu  the backend that presents\n"
                   "                           (webgpu: a TOON_ENABLE_WEBGPU build)\n"
                   "  --webgpu-adapter d3d12|vulkan  Dawn's adapter (d3d12)\n"
                   "  --overlay on|off         the measurements over the\n"
                   "                           scene (default on)\n"
                   "  --outlines on|off        draw hulls (default on)\n"
                   "  --shadows on|off         shared shadows across material\n"
                   "                           families (default on)\n"
                   "  --shadow-resolution N    texels on a side of each shadow\n"
                   "                           view: 512|1024|2048|4096\n"
                   "  --shadow-depth-bias N    receivers' depth bias, texels\n"
                   "  --shadow-normal-bias N   receivers' normal bias, texels\n"
                   "  --shadow-filter-radius N receivers' PCF radius, texels\n"
                   "  --mmd-self-shadow on|off MMD's self-shadow (default on)\n"
                   "  --mmd-morph-tints on|off full.fx's material morph tints,\n"
                   "                           which MMD does not draw\n"
                   "                           (default off)\n"
                   "  --outline-culling on|off omit offscreen hulls (default on)\n"
                   "  --lighting scene|camera  scene lights or camera key\n"
                   "  --direct-strength N      direct multiplier [0, 4]\n"
                   "  --ambient-strength N     ambient multiplier [0, 4]\n"
                   "  --material-view MODE     surface|base|normal|direct|ambient|\n"
                   "                           shadow\n"
                   "  --time T                 initial USD time code\n"
                   "  --time-step T            USD time codes per presented\n"
                   "                           frame (deterministic evaluation)\n"
                   "  --play                   start wall-clock USD playback\n"
                   "  --playback-speed N       playback multiplier [0.05, 4]\n"
                   "  --loop on|off            loop the authored range (on)\n"
                   "  --expect-hulls N         check actual last-frame hulls\n"
                   "  --camera-pan X Y         initial drag in window pixels\n"
                   "  --camera-dolly N         initial wheel notches (in > 0)\n"
                   "  --background solid|checkerboard  preview background\n"
                   "  --background-color R G B  linear RGB in [0,1]\n"
                   "  --samples N              MSAA samples per pixel (default\n"
                   "                           4; 1 turns anti-aliasing off)\n"
                   "  --switch-samples N       ask for N samples halfway\n"
                   "                           through --frames N frames\n"
                   "  --switch-file <file>     open a scene halfway through\n"
                   "                           --frames N frames (Hydra)\n"
                   "  --hidden                 do not show the window\n"
                   "  --usd <stage>            draw a USD stage through Hydra\n"
                   "                           (a build with the Hydra adapter)\n"
                   "  --expect-draws N         fail unless the last frame drew\n"
                   "                           N meshes\n"
                   "  --screenshot <file.ppm>  write the last of --frames N\n"
                   "                           frames as it was presented\n"
                   "  --foreground-output FILE.png  straight sRGB RGBA foreground\n"
                   "                           from the last of --frames N\n"
                   "  --capture-sequence DIR   capture every bounded frame\n"
                   "                           (cannot benchmark with readbacks)\n"
                   "  --camera-output FILE     final view/projection as JSON\n"
#if TOON_VIEWPORT_HAS_AVATAR_RUNTIME
                   "  --mmd-physics            live PMX/VMD physics (toon-viewport-mmd)\n"
                   "  --avatar-model PMX       owner source for a composed MMD stage\n"
                   "  --avatar-runtime         drive the stage's VRM avatar with\n"
                   "                           usd-avatar-runtime's providers\n"
                   "  --avatar-lookat <prim>   the VRM LookAt prim to evaluate\n"
                   "  --avatar-rate N          results per second on a producer\n"
                   "                           thread (120); 0 publishes one per\n"
                   "                           frame, after extraction\n"
                   "  --avatar-reset-every N   reset the runtime instance every\n"
                   "                           N results\n"
                   "  --avatar-motion <stage>  pose the avatar with the runtime's\n"
                   "                           motion provider from this clip\n"
#endif
                   "Left drag orbits, middle or Shift+left drag pans; right\n"
                   "drag to the right zooms in, to the left zooms out; the\n"
                   "wheel dollies. F frames the scene and R\n"
                   "returns to the last framing; P writes the next frame to\n"
                   "toon-viewport-<n>.ppm, without the overlay; 1, 2, 4\n"
                   "and 8 set the MSAA samples per pixel; O shows or hides\n"
                   "the overlay; Ctrl+O opens a scene file in a Hydra build.\n"
                   "Space plays/pauses USD animation. The Animation panel\n"
                   "seeks and steps; manual seeks pause playback.\n"
                   "Esc or closing the window exits.\n";
      std::exit(0);
    } else {
      throw std::invalid_argument("unknown option: " + std::string(option));
    }
  }
  if (result.width == 0 || result.height == 0) {
    throw std::invalid_argument("viewport extent must be non-zero");
  }
  if (result.options.samples == 0) {
    throw std::invalid_argument("--samples must be at least 1");
  }
  if (!result.screenshot.empty() && result.frame_limit == 0) {
    throw std::invalid_argument("--screenshot needs --frames N");
  }
  if (!result.foreground_output.empty() && result.frame_limit == 0) {
    throw std::invalid_argument("--foreground-output needs --frames N");
  }
  if (!result.foreground_output.empty() && result.foreground_output == result.screenshot) {
    throw std::invalid_argument("foreground and screenshot need separate output paths");
  }
  if ((!result.capture_sequence.empty() || !result.camera_output.empty()) &&
      result.frame_limit == 0) {
    throw std::invalid_argument("capture sequence/camera output needs --frames N");
  }
  if (!result.capture_sequence.empty() && !result.screenshot.empty()) {
    throw std::invalid_argument("choose --capture-sequence or --screenshot");
  }
  if (result.switch_samples) {
    if (*result.switch_samples == 0) {
      throw std::invalid_argument("--switch-samples must be at least 1");
    }
    if (result.frame_limit < 2) {
      throw std::invalid_argument("--switch-samples needs --frames N, N >= 2");
    }
  }
  if (result.play && result.time_step) {
    throw std::invalid_argument("--play and --time-step are mutually exclusive");
  }
  if (result.play && result.usd.empty()) {
    throw std::invalid_argument("--play needs --usd <stage>");
  }
#if !TOON_VIEWPORT_HAS_WEBGPU
  if (result.webgpu) {
    throw std::invalid_argument("--backend webgpu needs a build with TOON_ENABLE_WEBGPU "
                                "(the webgpu intent)");
  }
#endif
#if !TOON_VIEWPORT_HAS_HYDRA
  if (!result.usd.empty() || !result.switch_file.empty() || result.time ||
      result.time_step || result.play) {
    throw std::invalid_argument("--usd needs a build with the Hydra adapter "
                                "(the viewport-usd intent)");
  }
#endif
  if ((result.time || result.time_step) && result.usd.empty()) {
    throw std::invalid_argument("--time and --time-step need --usd <stage>");
  }
  if (!result.switch_file.empty() && result.frame_limit < 2) {
    throw std::invalid_argument("--switch-file needs --frames N, N >= 2");
  }
#if TOON_VIEWPORT_HAS_AVATAR_RUNTIME
  if (!result.avatar_model.empty() && !result.mmd_physics)
    throw std::invalid_argument("--avatar-model needs --mmd-physics");
  if (result.mmd_physics) {
#if !TOON_VIEWPORT_HAS_MMD_RUNTIME
    throw std::invalid_argument("--mmd-physics needs toon-viewport-mmd");
#endif
    if (result.time || result.time_step || result.playback_speed != 1.0)
      throw std::invalid_argument("MMD physics uses the runtime clock; USD seek/step/speed are unavailable");
    if (!result.avatar_look_at.empty() || result.avatar_rate != 120.0)
      throw std::invalid_argument("MMD physics does not use VRM LookAt or a renderer producer rate");
  }
#if TOON_VIEWPORT_HAS_MMD_RUNTIME
  if (result.avatar_runtime && !result.mmd_physics)
    throw std::invalid_argument("toon-viewport-mmd requires --mmd-physics for runtime input");
#endif
  if (result.avatar_runtime && result.usd.empty() && result.switch_file.empty()) {
    throw std::invalid_argument("--avatar-runtime needs --usd <stage>");
  }
  if (!result.avatar_motion.empty() && !result.avatar_runtime) {
    throw std::invalid_argument("--avatar-motion needs --avatar-runtime");
  }
#else
  if (result.avatar_runtime) {
    throw std::invalid_argument("--avatar-runtime needs toon-viewport-avatar, a build "
                                "with TOON_AVATAR_RUNTIME_PROVIDERS");
  }
#endif
  return result;
}

} // namespace Toon::viewport
