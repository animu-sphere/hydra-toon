// SPDX-License-Identifier: Apache-2.0
// Standalone viewport host, the renderer's main evaluation host (design
// policy §31). It draws the bootstrap triangle scene or, in a build with the
// Hydra adapter, a USD stage through Hydra (`--usd <stage>`), through its own
// orbit camera, and an overlay over it with the frame's CPU and GPU times and
// the renderer's statistics (design policy §24). `ost renderer viewport`
// builds and launches this executable; it also runs headless-style as a GPU
// smoke test (`--hidden --frames N`).
// Exit codes: 0 success, 1 failure, 77 skip (the environment cannot present).
#include "camera.hpp"
#include "overlay.hpp"
#include "telemetry.hpp"
#include "window.hpp"

#if TOON_VIEWPORT_HAS_HYDRA
#include "hydra_scene.hpp"
#endif

#include <toon/extraction.hpp>
#include <toon/render_world.hpp>
#include <toon/vulkan_present.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr int kExitSkip = 77;
// A right drag of this many pixels dollies by one wheel notch.
constexpr double kPixelsPerDollyStep = 20.0;

using Clock = std::chrono::steady_clock;

struct Arguments {
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
  std::uint64_t frame_limit = 0;
  bool visible = true;
  bool vsync = true;
  // Whether the overlay is laid out and drawn; O shows and hides it.
  bool overlay = true;
  Toon::RenderOptions options;
  // A USD stage to draw through Hydra instead of the bootstrap scene.
  std::string usd;
  // Fail unless the last frame drew this many meshes.
  std::optional<std::uint64_t> expect_draws;
  // Where the last of `frame_limit` frames is written, as a binary PPM.
  std::string screenshot;
  // The sample count asked for halfway through `frame_limit` frames, as the
  // number keys ask for one: the presentation test of a live change.
  std::optional<std::uint32_t> switch_samples;
};

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
    } else if (option == "--samples") {
      result.options.samples =
          static_cast<std::uint32_t>(ReadUnsigned(next(), option));
    } else if (option == "--switch-samples") {
      result.switch_samples =
          static_cast<std::uint32_t>(ReadUnsigned(next(), option));
    } else if (option == "--hidden") {
      result.visible = false;
    } else if (option == "--usd") {
      result.usd = next();
    } else if (option == "--expect-draws") {
      result.expect_draws = ReadUnsigned(next(), option);
    } else if (option == "--screenshot") {
      result.screenshot = next();
    } else if (option == "--vsync") {
      const auto value = next();
      if (value != "on" && value != "off") {
        throw std::invalid_argument("--vsync must be on or off");
      }
      result.vsync = value == "on";
    } else if (option == "--overlay") {
      const auto value = next();
      if (value != "on" && value != "off") {
        throw std::invalid_argument("--overlay must be on or off");
      }
      result.overlay = value == "on";
    } else if (option == "--help") {
      std::cout << "Usage: toon-viewport [options]\n"
                   "  --width N --height N     window size (default 1280x720)\n"
                   "  --frames N               exit after N presented frames\n"
                   "  --vsync on|off           FIFO or immediate present\n"
                   "  --overlay on|off         the measurements over the\n"
                   "                           scene (default on)\n"
                   "  --samples N              MSAA samples per pixel (default\n"
                   "                           4; 1 turns anti-aliasing off)\n"
                   "  --switch-samples N       ask for N samples halfway\n"
                   "                           through --frames N frames\n"
                   "  --hidden                 do not show the window\n"
                   "  --usd <stage>            draw a USD stage through Hydra\n"
                   "                           (a build with the Hydra adapter)\n"
                   "  --expect-draws N         fail unless the last frame drew\n"
                   "                           N meshes\n"
                   "  --screenshot <file.ppm>  write the last of --frames N\n"
                   "                           frames as it was presented\n"
                   "Left drag orbits, middle or Shift+left drag pans, right\n"
                   "drag or the wheel dollies; F frames the scene and R\n"
                   "returns to the last framing; P writes the next frame to\n"
                   "toon-viewport-<n>.ppm, without the overlay; 1, 2, 4\n"
                   "and 8 set the MSAA samples per pixel; O shows or hides\n"
                   "the overlay. Esc or closing the window exits.\n";
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
  if (result.switch_samples) {
    if (*result.switch_samples == 0) {
      throw std::invalid_argument("--switch-samples must be at least 1");
    }
    if (result.frame_limit < 2) {
      throw std::invalid_argument("--switch-samples needs --frames N, N >= 2");
    }
  }
#if !TOON_VIEWPORT_HAS_HYDRA
  if (!result.usd.empty()) {
    throw std::invalid_argument("--usd needs a build with the Hydra adapter "
                                "(the viewport-usd intent)");
  }
#endif
  return result;
}

std::string WindowTitle(std::string_view scene, std::string_view device,
    std::uint32_t width, std::uint32_t height, std::uint32_t samples,
    std::uint64_t frames) {
  std::ostringstream title;
  title << "toon-viewport | " << scene << " | " << device << " | " << width
        << 'x' << height << " | " << samples << "x MSAA | " << frames
        << " frames";
  return title.str();
}

// Everything the session has sent to the GPU for the scene, one count per
// upload or write of any kind.
std::uint64_t Uploads(const Toon::PresentStatistics& statistics) {
  return Toon::viewport::UploadCounts::Of(statistics).scene();
}

// The sample count a number key asks for; 0 for any other key.
std::uint32_t SamplesForKey(Toon::viewport::Key key) {
  switch (key) {
  case Toon::viewport::Key::Digit1:
    return 1;
  case Toon::viewport::Key::Digit2:
    return 2;
  case Toon::viewport::Key::Digit4:
    return 4;
  case Toon::viewport::Key::Digit8:
    return 8;
  default:
    return 0;
  }
}

// What the last frame drew, as the Hydra host evidence counts it: which
// model each material selected, and how many draws selected MToon, blended,
// added a hull, were skinned and drew authored normals.
Toon::viewport::SceneCounts CountScene(const Toon::DrawList& draws) {
  Toon::viewport::SceneCounts counts;
  counts.draws = draws.draws.size();
  counts.textures = draws.textures.size();
  for (const Toon::MaterialSnapshot& material : draws.materials) {
    if (material.material.model == Toon::ToonShadingModel::PreviewSurface) {
      ++counts.preview_materials;
    } else if (material.material.model == Toon::ToonShadingModel::MToon) {
      ++counts.mtoon_materials;
    }
  }
  for (const Toon::MeshSnapshot& mesh : draws.draws) {
    counts.skinned += Toon::IsSkinned(mesh) ? 1U : 0U;
    counts.authored_normals += mesh.authored_normals ? 1U : 0U;
    const auto material = std::lower_bound(draws.materials.begin(),
        draws.materials.end(), mesh.material,
        [](const Toon::MaterialSnapshot& entry, Toon::MaterialId id) {
          return entry.id < id;
        });
    if (material != draws.materials.end() && material->id == mesh.material &&
        material->material.model == Toon::ToonShadingModel::MToon) {
      ++counts.mtoon;
      counts.transparent += Toon::IsTransparent(material->material) ? 1U : 0U;
      counts.outline += Toon::HasOutline(material->material) ? 1U : 0U;
    }
  }
  return counts;
}

std::string SceneSummary(const Toon::DrawList& draws) {
  const Toon::viewport::SceneCounts counts = CountScene(draws);
  std::ostringstream summary;
  summary << "draws=" << counts.draws << " draws_mtoon=" << counts.mtoon
          << " draws_transparent=" << counts.transparent
          << " draws_outline=" << counts.outline
          << " draws_skinned=" << counts.skinned
          << " draws_authored_normals=" << counts.authored_normals
          << " materials_preview=" << counts.preview_materials
          << " materials_mtoon=" << counts.mtoon_materials
          << " textures=" << counts.textures;
  return summary.str();
}

double Milliseconds(Clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

// A binary PPM: RGB, first row at the top, as the capture is laid out.
void WritePpm(const std::string& path, const Toon::ColorProduct& color) {
  std::ofstream output(path, std::ios::binary);
  output << "P6\n"
         << color.width << ' ' << color.height << "\n255\n";
  for (std::size_t pixel = 0; pixel < color.payload.size(); pixel += 4U) {
    output.write(reinterpret_cast<const char*>(&color.payload[pixel]), 3);
  }
  if (!output) {
    throw std::runtime_error("could not write the screenshot " + path);
  }
}

enum class Drag { None, Orbit, Pan, Dolly };

// The pointer's part in the camera: which drag is under way and where the
// cursor last was.
struct PointerState {
  Drag drag = Drag::None;
  Toon::viewport::PointerButton button = Toon::viewport::PointerButton::None;
  double x = 0.0;
  double y = 0.0;
};

void HandlePointer(const Toon::viewport::Event& event, PointerState& pointer,
    Toon::viewport::OrbitCamera& camera, std::uint32_t height) {
  using Toon::viewport::EventType;
  using Toon::viewport::PointerButton;
  switch (event.type) {
  case EventType::PointerDown:
    if (pointer.drag == Drag::None) {
      switch (event.button) {
      case PointerButton::Left:
        pointer.drag = event.shift ? Drag::Pan : Drag::Orbit;
        break;
      case PointerButton::Middle:
        pointer.drag = Drag::Pan;
        break;
      case PointerButton::Right:
        pointer.drag = Drag::Dolly;
        break;
      case PointerButton::None:
        return;
      }
      pointer.button = event.button;
      pointer.x = event.x;
      pointer.y = event.y;
    }
    break;
  case EventType::PointerUp:
    if (event.button == pointer.button) {
      pointer.drag = Drag::None;
      pointer.button = PointerButton::None;
    }
    break;
  case EventType::PointerMove: {
    const auto dx = static_cast<float>(event.x - pointer.x);
    const auto dy = static_cast<float>(event.y - pointer.y);
    pointer.x = event.x;
    pointer.y = event.y;
    switch (pointer.drag) {
    case Drag::Orbit:
      camera.Orbit(dx, dy);
      break;
    case Drag::Pan:
      camera.Pan(dx, dy, static_cast<float>(height));
      break;
    case Drag::Dolly:
      camera.Dolly(static_cast<float>(-dy / kPixelsPerDollyStep));
      break;
    case Drag::None:
      break;
    }
    break;
  }
  case EventType::Scroll:
    camera.Dolly(static_cast<float>(event.y));
    break;
  default:
    break;
  }
}

} // namespace

int main(int argc, char** argv) {
  try {
    const auto arguments = ParseArguments(argc, argv);

    // The scene, before a window, so a stage that cannot be read fails
    // rather than skips.
    Toon::FrameSnapshot snapshot;
    Toon::DrawList draws;
    Toon::viewport::UpAxis up_axis = Toon::viewport::UpAxis::Y;
    float meters_per_unit = 1.0F;
    std::string scene_name = "bootstrap";
#if TOON_VIEWPORT_HAS_HYDRA
    std::unique_ptr<Toon::viewport::HydraScene> hydra;
    if (!arguments.usd.empty()) {
      hydra = Toon::viewport::HydraScene::Open(arguments.usd);
      hydra->Update(snapshot);
      up_axis = hydra->up_axis();
      meters_per_unit = hydra->meters_per_unit();
      scene_name = std::filesystem::path(arguments.usd).filename().string();
    }
    const bool bootstrap = hydra == nullptr;
#else
    const bool bootstrap = true;
#endif
    if (bootstrap) {
      Toon::RenderWorld world;
      world.SetBootstrapTriangle();
      world.Commit(snapshot);
    }
    Toon::viewport::OrbitCamera camera(up_axis);
    camera.Frame(Toon::viewport::SceneBounds(snapshot));

    std::unique_ptr<Toon::viewport::Window> window;
    Toon::PresentSurfaceProvider provider;
    try {
      window = Toon::viewport::Window::Create(
          "toon-viewport", arguments.width, arguments.height,
          arguments.visible);
      provider = Toon::viewport::MakeSurfaceProvider(*window);
    } catch (const std::exception& environment) {
      std::cerr << "toon-viewport: skip: " << environment.what() << '\n';
      return kExitSkip;
    }

    const auto shader_directory =
        std::filesystem::absolute(argv[0]).parent_path() / "shaders";
    Toon::PresentSetupStatus status = Toon::PresentSetupStatus::Error;
    std::string error;
    auto session = Toon::CreatePresentSession(provider,
        Toon::SceneShadersIn(shader_directory.string()), arguments.vsync,
        status, error, arguments.options);
    if (session == nullptr) {
      if (status == Toon::PresentSetupStatus::Unavailable) {
        std::cerr << "toon-viewport: skip: " << error << '\n';
        return kExitSkip;
      }
      std::cerr << "toon-viewport: " << error << '\n';
      return 1;
    }
    // The labels `ost renderer viewport` reads into its launch record.
    std::cout << "Selected backend: vulkan\n"
              << "Device: " << session->statistics().device_name << '\n'
              << "Presentation: "
              << (arguments.visible ? "GPU swapchain" : "hidden GPU swapchain")
              << ", " << session->statistics().samples
              << " sample(s) per pixel, "
              << (session->statistics().srgb_encoded ? "sRGB-encoded"
                                                     : "linear, unencoded")
              << '\n'
              << "Scene: " << (bootstrap ? "bootstrap" : arguments.usd)
              << '\n';

    std::unique_ptr<Toon::viewport::Overlay> overlay;
    if (arguments.overlay) {
      overlay =
          std::make_unique<Toon::viewport::Overlay>(window->content_scale());
    }
    bool overlay_shown = overlay != nullptr;
    Toon::OverlayDrawList overlay_draws;
    Toon::viewport::FrameTelemetry telemetry;
    std::uint64_t gpu_frame = 0;
    Toon::viewport::UploadCounts uploads_before;
    Toon::viewport::UploadCounts frame_uploads;
    std::optional<Clock::time_point> last_present;
    auto last_overlay = Clock::now();

    bool running = true;
    PointerState pointer;
    // Where the capture under way is written, and how many were written.
    std::string capture_path;
    std::uint64_t captures = 0;
    Toon::ColorProduct capture;
    // The count the last frame drew at, to report a change once it lands.
    std::uint32_t samples = session->statistics().samples;
    // Uploads before --switch-samples asked, which the change must not add
    // to.
    std::optional<std::uint64_t> uploads_at_switch;
    auto title_update = Clock::now();
    while (running && (arguments.frame_limit == 0 ||
                          session->statistics().frames_presented <
                              arguments.frame_limit)) {
      Toon::viewport::Event event;
      while (window->PollEvent(event)) {
        // The overlay sees every event; a press or the wheel over it is not
        // the camera's.
        const bool over_overlay = overlay_shown && overlay->WantsPointer();
        if (overlay_shown) {
          overlay->HandleEvent(event);
        }
        switch (event.type) {
        case Toon::viewport::EventType::Close:
          running = false;
          break;
        case Toon::viewport::EventType::KeyDown:
          if (event.key == Toon::viewport::Key::Escape) {
            running = false;
          } else if (event.key == Toon::viewport::Key::F) {
            camera.Frame(Toon::viewport::SceneBounds(snapshot));
          } else if (event.key == Toon::viewport::Key::R) {
            camera.Reset();
          } else if (event.key == Toon::viewport::Key::O &&
                     overlay != nullptr) {
            overlay_shown = !overlay_shown;
            if (!overlay_shown) {
              Toon::viewport::Overlay::Hide(overlay_draws);
            }
          } else if (event.key == Toon::viewport::Key::P &&
                     capture_path.empty()) {
            capture_path =
                "toon-viewport-" + std::to_string(captures + 1) + ".ppm";
            session->RequestCapture();
          } else if (const std::uint32_t requested = SamplesForKey(event.key);
                     requested != 0) {
            session->SetSamples(requested);
          }
          break;
        case Toon::viewport::EventType::Resize:
          break;
        case Toon::viewport::EventType::PointerDown:
        case Toon::viewport::EventType::Scroll:
          if (!over_overlay) {
            HandlePointer(event, pointer, camera, window->height());
          }
          break;
        default:
          HandlePointer(event, pointer, camera, window->height());
          break;
        }
      }
      if (!running) {
        break;
      }
      const std::uint32_t width = window->width();
      const std::uint32_t height = window->height();
      if (width == 0 || height == 0) {
        window->WaitForEvent();
        continue;
      }
      Toon::viewport::CpuSample cpu;
      const auto hydra_start = Clock::now();
#if TOON_VIEWPORT_HAS_HYDRA
      if (hydra != nullptr) {
        hydra->Update(snapshot);
      }
#endif
      const auto extract_start = Clock::now();
      cpu.hydra = Milliseconds(extract_start - hydra_start);
      Toon::ExtractDrawList(snapshot, draws);
      const auto overlay_start = Clock::now();
      cpu.extract = Milliseconds(overlay_start - extract_start);
      draws.view = camera.View(static_cast<float>(width) /
                               static_cast<float>(height));
      draws.meters_per_unit = meters_per_unit;
      if (arguments.switch_samples && !uploads_at_switch &&
          session->statistics().frames_presented ==
              arguments.frame_limit / 2) {
        uploads_at_switch = Uploads(session->statistics());
        session->SetSamples(*arguments.switch_samples);
      }
      if (!arguments.screenshot.empty() && capture_path.empty() &&
          session->statistics().frames_presented + 1 ==
              arguments.frame_limit) {
        capture_path = arguments.screenshot;
        session->RequestCapture();
      }
      // The overlay shows the frame before this one: its statistics are
      // the last RenderFrame's.
      if (overlay_shown) {
        Toon::viewport::OverlayFrame shown;
        shown.scene = scene_name;
        shown.width = width;
        shown.height = height;
        shown.vsync = arguments.vsync;
        shown.statistics = &session->statistics();
        shown.telemetry = &telemetry;
        shown.counts = CountScene(draws);
        shown.frame_uploads = frame_uploads;
        shown.uploads = Toon::viewport::UploadCounts::Of(session->statistics());
        const Toon::viewport::OverlayControls controls = overlay->Build(shown,
            Milliseconds(overlay_start - last_overlay) / 1000.0,
            overlay_draws);
        last_overlay = overlay_start;
        if (controls.samples != 0) {
          session->SetSamples(controls.samples);
        }
      }
      bool presented = false;
      const auto render_start = Clock::now();
      cpu.overlay = Milliseconds(render_start - overlay_start);
      if (!session->RenderFrame(draws, overlay_draws, width, height,
              presented, error)) {
        std::cerr << "toon-viewport: " << error << '\n';
        return 1;
      }
      const auto render_end = Clock::now();
      if (presented) {
        cpu.wait = session->statistics().cpu_wait;
        cpu.submit = session->statistics().cpu_submit;
        // The first frame has no interval before it.
        if (last_present) {
          cpu.interval = Milliseconds(render_end - *last_present);
          telemetry.PushCpu(cpu);
        }
        last_present = render_end;
      }
      if (session->statistics().gpu_frame != gpu_frame) {
        gpu_frame = session->statistics().gpu_frame;
        telemetry.PushGpu(session->statistics().gpu);
      }
      const auto uploads_after =
          Toon::viewport::UploadCounts::Of(session->statistics());
      frame_uploads = uploads_after - uploads_before;
      uploads_before = uploads_after;
      if (session->statistics().samples != samples) {
        samples = session->statistics().samples;
        // The frame that changed the count also rebuilt the pipelines.
        std::ostringstream line;
        line << std::fixed << std::setprecision(2) << "Samples: " << samples
             << " per pixel, in a "
             << Milliseconds(render_end - render_start) << " ms frame";
        std::cout << line.str() << '\n';
      }
      if (session->TakeCapture(capture, error)) {
        WritePpm(capture_path, capture);
        std::cout << "Captured " << capture.width << 'x' << capture.height
                  << " to " << capture_path << '\n';
        capture_path.clear();
        ++captures;
      } else if (!error.empty()) {
        std::cerr << "toon-viewport: " << error << '\n';
        return 1;
      }
      const auto now = Clock::now();
      if (now - title_update >= std::chrono::milliseconds(250)) {
        window->SetTitle(WindowTitle(scene_name,
            session->statistics().device_name, width, height, samples,
            session->statistics().frames_presented));
        title_update = now;
      }
    }

    const Toon::PresentStatistics& statistics = session->statistics();
    std::cout << "Scene summary: " << SceneSummary(draws) << '\n'
              << "Uploads: topology=" << statistics.topology_uploads
              << " points=" << statistics.point_uploads
              << " materials=" << statistics.material_writes
              << " textures=" << statistics.texture_uploads
              << " skins=" << statistics.skin_uploads
              << " poses=" << statistics.pose_writes
              << " overlay_textures=" << statistics.overlay_texture_uploads
              << '\n'
              << "Draw calls: unlit=" << statistics.draws.unlit
              << " outline=" << statistics.draws.outline
              << " opaque=" << statistics.draws.opaque
              << " transparent=" << statistics.draws.transparent
              << " triangles=" << statistics.draws.triangles
              << " pipeline_binds=" << statistics.draws.pipeline_binds
              << " overlay=" << statistics.draws.overlay << '\n'
              << telemetry.Report();
    if (!statistics.gpu_timing) {
      std::cout << "Timing: this queue writes no GPU timestamps\n";
    }
    if (statistics.validation_message_count != 0) {
      std::cerr << "toon-viewport: Vulkan validation reported "
                << statistics.validation_message_count
                << " message(s): " << statistics.validation_detail << '\n';
      return 1;
    }
    if (arguments.frame_limit != 0 &&
        statistics.frames_presented < arguments.frame_limit) {
      std::cerr << "toon-viewport: presented "
                << statistics.frames_presented << " of "
                << arguments.frame_limit << " requested frames\n";
      return 1;
    }
    // Only a capture reads a frame back (design policy §31).
    if (statistics.readbacks != captures) {
      std::cerr << "toon-viewport: " << statistics.readbacks
                << " frame(s) read back for " << captures << " capture(s)\n";
      return 1;
    }
    // A live change lands at the device's count for what was asked, only a
    // count that differs rebuilds anything, and nothing is uploaded again.
    if (arguments.switch_samples) {
      if (statistics.samples > *arguments.switch_samples ||
          statistics.sample_changes > 1) {
        std::cerr << "toon-viewport: asked for " << *arguments.switch_samples
                  << " sample(s), drew at " << statistics.samples
                  << " after " << statistics.sample_changes
                  << " change(s)\n";
        return 1;
      }
      if (!uploads_at_switch || Uploads(statistics) != *uploads_at_switch) {
        std::cerr << "toon-viewport: the sample change uploaded "
                  << (uploads_at_switch
                             ? Uploads(statistics) - *uploads_at_switch
                             : 0U)
                  << " time(s), or was never asked for\n";
        return 1;
      }
    }
    if (!arguments.screenshot.empty() && captures == 0) {
      std::cerr << "toon-viewport: no frame was captured to "
                << arguments.screenshot << '\n';
      return 1;
    }
    if (arguments.expect_draws &&
        draws.draws.size() != *arguments.expect_draws) {
      std::cerr << "toon-viewport: the last frame drew "
                << draws.draws.size() << " mesh(es), not "
                << *arguments.expect_draws << '\n';
      return 1;
    }
    std::cout << "Presented " << statistics.frames_presented << " frames on "
              << statistics.device_name
              << " (swapchain recreates: " << statistics.swapchain_recreates
              << ", sample changes: " << statistics.sample_changes
              << ", frames read back: " << statistics.readbacks << ")\n";
    return 0;
  } catch (const std::exception& fatal) {
    std::cerr << "toon-viewport: " << fatal.what() << '\n';
    return 1;
  }
}
