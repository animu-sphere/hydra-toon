// SPDX-License-Identifier: Apache-2.0
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <toon/vulkan_present.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
void Require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template<class T> T Decode(std::uintptr_t value) {
  if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value);
  else return static_cast<T>(value);
}
template<class T> std::uintptr_t Encode(T value) {
  if constexpr (std::is_pointer_v<T>) return reinterpret_cast<std::uintptr_t>(value);
  else return static_cast<std::uintptr_t>(value);
}
float EncodeSrgb(float x) {
  return x <= 0.0031308F ? x * 12.92F : 1.055F * std::pow(x, 1 / 2.4F) - 0.055F;
}
void Save(const Toon::ColorProduct& image, const std::string& path) {
  std::ofstream file(path, std::ios::binary);
  file << "P6\n" << image.width << ' ' << image.height << "\n255\n";
  for (std::size_t at = 0; at < image.payload.size(); at += 4)
    file.write(reinterpret_cast<const char*>(image.payload.data() + at), 3);
}
void CheckComposite(const Toon::ColorProduct& foreground, const Toon::ColorProduct& presented,
    const Toon::CompositeBackground& background, bool srgb) {
  for (std::uint32_t y = 0; y < foreground.height; ++y) {
    for (std::uint32_t x = 0; x < foreground.width; ++x) {
      const auto at = y * foreground.row_pitch + x * 4;
      const float alpha = foreground.payload[at + 3] / 255.0F;
      const auto rgb = background.checkerboard &&
          ((x / background.checker_size + y / background.checker_size) & 1)
          ? background.alternate : background.color;
      const std::array channels{rgb.x, rgb.y, rgb.z};
      Require(presented.payload[at + 3] == 255, "nonopaque presentation");
      for (int channel = 0; channel < 3; ++channel) {
        float expected = foreground.payload[at + channel] / 255.0F + channels[channel] * (1 - alpha);
        if (srgb) expected = EncodeSrgb(expected);
        Require(std::abs(presented.payload[at + channel] - expected * 255) < 1.6F,
            "presentation differs from linear source-over then display transfer");
      }
    }
  }
}
bool Run(GLFWwindow* window, const Toon::SceneShaders& shaders, std::uint32_t samples,
    const std::string& output) {
  std::uint32_t count = 0;
  const char** extensions = glfwGetRequiredInstanceExtensions(&count);
  Toon::PresentSurfaceProvider provider;
  provider.instance_extensions.assign(extensions, extensions + count);
  provider.user_data = window;
  provider.create_surface = [](void* user, std::uintptr_t instance, std::uintptr_t* result) {
    VkSurfaceKHR surface{};
    const auto code = glfwCreateWindowSurface(Decode<VkInstance>(instance),
        static_cast<GLFWwindow*>(user), nullptr, &surface);
    *result = Encode(surface);
    return static_cast<std::int32_t>(code);
  };
  Toon::PresentSetupStatus status;
  std::string error;
  auto session = Toon::CreatePresentSession(provider, shaders, false, status, error, {samples});
  if (status == Toon::PresentSetupStatus::Unavailable) {
    std::cout << "SKIP: " << error << '\n'; return false;
  }
  Require(session != nullptr, error.c_str());
  Require(session->statistics().samples == samples, "required MSAA unavailable");
  Toon::FrameStatus offscreen_status;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, offscreen_status, error, {samples});
  Require(renderer != nullptr, error.c_str());
  Require(renderer->SetBackground({Toon::BackgroundMode::Transparent, {}}, error), error.c_str());

  Toon::RenderWorld world;
  auto draws = Toon::ExtractDrawList(world.Commit());
  Toon::ColorProduct foreground, presented, reference;
  Toon::DepthProduct depth;
  Toon::OverlayDrawList overlay;
  // A conspicuous UI triangle must appear in neither kind of capture.
  overlay.textures.push_back({1, 1, 1, 1, {255, 255, 255, 255}});
  overlay.vertices = {{0, 0, 0, 0, 0xff00ffff}, {96, 0, 0, 0, 0xff00ffff},
      {0, 96, 0, 0, 0xff00ffff}};
  overlay.indices = {0, 1, 2};
  overlay.commands.push_back({0, 3, 0, 1, {0, 0, 200, 200}});
  const auto frame = [&](bool capture) {
    if (capture) { session->RequestCapture(); session->RequestForegroundCapture(); }
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    bool did_present = false;
    Require(session->RenderFrame(draws, overlay, width, height, did_present, error), error.c_str());
    Require(did_present, "frame not presented");
    if (capture) {
      Require(session->TakeCapture(presented, error), error.c_str());
      Require(session->TakeForegroundCapture(foreground, error), error.c_str());
      Require(foreground.color_space == "linear" && foreground.transfer_function == "linear" &&
          foreground.alpha_convention == "premultiplied" && foreground.origin == "top-left" &&
          foreground.pixel_format == "rgba8-unorm" && foreground.row_pitch == foreground.width * 4,
          "foreground interpretation");
      Require(presented.alpha_convention == "opaque", "presentation interpretation");
      Require(renderer->Render(draws, foreground.width, foreground.height, reference, depth, error), error.c_str());
      Require(foreground.payload == reference.payload, "viewport foreground differs from offscreen product");
    }
  };
  frame(false);
  frame(false);
  Require(session->statistics().readbacks == 0, "ordinary frames read back");
  Require(!session->TakeCapture(presented, error) && error.empty(), "unsolicited capture");
  Require(!session->TakeForegroundCapture(foreground, error) && error.empty(), "unsolicited foreground");
  frame(true);
  for (auto value : foreground.payload) Require(value == 0, "nonzero empty foreground");

  world.SetBootstrapTriangle();
  auto mesh = world.Commit().meshes.front().id;
  world.SetMeshNormals(mesh, std::vector<Toon::Float3>(3, {0, 0, 1}));
  const auto id = world.CreateMaterial();
  world.SetMeshMaterial(mesh, id);
  for (auto model : {Toon::ToonShadingModel::MToon, Toon::ToonShadingModel::MMD,
           Toon::ToonShadingModel::PreviewSurface}) {
    for (auto mode : {Toon::ToonAlphaMode::Opaque, Toon::ToonAlphaMode::Mask,
             Toon::ToonAlphaMode::Blend}) {
      Toon::ToonMaterial material;
      material.model = model;
      material.preview.enabled = true;
      material.base_color = {};
      material.emissive = {0.6F, 0.3F, 0.15F};
      material.mmd.ambient_color = {0.6F, 0.3F, 0.15F};
      material.alpha_mode = mode;
      material.alpha = mode == Toon::ToonAlphaMode::Opaque ? 1.0F :
          mode == Toon::ToonAlphaMode::Blend ? 0.25F : 0.5F;
      material.alpha_cutoff = 0.5F;
      world.SetMaterial(id, material);
      draws = Toon::ExtractDrawList(world.Commit());
      draws.lighting.direct_scale = draws.lighting.ambient_scale = 0;
      std::vector<std::uint8_t> stable;
      Toon::PresentStatistics before;
      for (int back = 0; back < 4; ++back) {
        Toon::CompositeBackground background;
        background.color = back == 0 ? Toon::Float3{0, 0, 0} :
            back == 1 ? Toon::Float3{1, 1, 1} : Toon::Float3{0.17F, 0.43F, 0.81F};
        background.checkerboard = back == 3;
        background.checker_size = 7; // Odd, with partial cells at the edges.
        Require(session->SetCompositeBackground(background, error), error.c_str());
        frame(true);
        if (back == 0) { stable = foreground.payload; before = session->statistics(); }
        else Require(stable == foreground.payload, "background changed foreground");
        CheckComposite(foreground, presented, background, session->statistics().srgb_encoded);
        bool partial = false;
        for (std::size_t at = 3; at < foreground.payload.size(); at += 4) {
          const float alpha = foreground.payload[at] / 255.0F;
          partial |= alpha > 0 && alpha < material.alpha - 0.01F;
        }
        if (samples > 1 && mode != Toon::ToonAlphaMode::Mask)
          Require(partial, "missing partial silhouette coverage");
        if (!output.empty() && samples == 4 && model == Toon::ToonShadingModel::PreviewSurface &&
            mode == Toon::ToonAlphaMode::Blend)
          Save(presented, output + "/composite-" + std::to_string(back) + ".ppm");
      }
      const auto after = session->statistics();
      Require(after.material_writes == before.material_writes && after.point_uploads == before.point_uploads &&
          after.topology_uploads == before.topology_uploads && after.texture_uploads == before.texture_uploads &&
          after.sample_changes == before.sample_changes, "background rebuilt scene resources");
      const auto previous = presented.payload;
      for (auto invalid : {Toon::CompositeBackground{false, {-1, 0, 0}},
               Toon::CompositeBackground{false, {0, 2, 0}},
               Toon::CompositeBackground{false, {0, 0, std::numeric_limits<float>::quiet_NaN()}},
               Toon::CompositeBackground{true, {}, {}, 0}}) {
        Require(!session->SetCompositeBackground(invalid, error), "invalid background accepted");
        frame(true);
        Require(presented.payload == previous, "invalid background replaced accepted settings");
      }
    }
  }
  // Hull-only pixels are foreground even without a surface/depth footprint.
  world.SetMeshTopology(mesh, {0, 2, 1});
  for (auto model : {Toon::ToonShadingModel::MToon, Toon::ToonShadingModel::MMD}) {
    Toon::ToonMaterial hull;
    hull.model = model;
    hull.outline = true;
    hull.outline_width = 0.05F;
    hull.outline_color = {0.5F, 0.25F, 0.125F};
    hull.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
    hull.mtoon.outline_lighting_mix = 0;
    for (float opacity : {1.0F, 0.35F}) {
      hull.alpha_mode = opacity == 1 ? Toon::ToonAlphaMode::Opaque : Toon::ToonAlphaMode::Blend;
      hull.alpha = hull.mmd.edge_alpha = opacity;
      world.SetMaterial(id, hull);
      draws = Toon::ExtractDrawList(world.Commit());
      std::vector<std::uint8_t> stable;
      for (int back = 0; back < 4; ++back) {
        Toon::CompositeBackground background;
        background.color = back == 0 ? Toon::Float3{0, 0, 0} :
            back == 1 ? Toon::Float3{1, 1, 1} : Toon::Float3{0.17F, 0.43F, 0.81F};
        background.checkerboard = back == 3;
        Require(session->SetCompositeBackground(background, error), error.c_str());
        frame(true);
        const auto center = (foreground.height / 2) * foreground.row_pitch + (foreground.width / 2) * 4;
        Require(std::abs(foreground.payload[center + 3] - opacity * 255) < 2,
            "hull foreground opacity");
        Require(session->statistics().draws.hulls == 1, "missing hull draw");
        if (back == 0) stable = foreground.payload;
        else Require(stable == foreground.payload, "background changed hull foreground");
        CheckComposite(foreground, presented, background, session->statistics().srgb_encoded);
      }
      draws.outlines = false;
      frame(true);
      for (auto value : foreground.payload) Require(value == 0, "hull fixture has a surface");
      draws.outlines = true;
    }
  }
  // Rebuild MSAA targets, resize the foreground and replace the scene.
  if (samples == 4) {
    session->SetSamples(1);
    auto single = Toon::CreateOffscreenRenderer(shaders, offscreen_status, error, {1});
    Require(single && single->SetBackground({Toon::BackgroundMode::Transparent, {}}, error), error.c_str());
    renderer = std::move(single);
    frame(true);
    Require(session->statistics().samples == 1 && session->statistics().sample_changes == 1,
        "sample change did not apply");
    glfwSetWindowSize(window, 128, 112);
    glfwPollEvents();
    frame(true);
    Require(foreground.width == 128 && foreground.height == 112, "foreground resize");
    session->ResetScene();
    Toon::RenderWorld empty;
    draws = Toon::ExtractDrawList(empty.Commit());
    frame(true);
    for (auto value : foreground.payload) Require(value == 0, "stale scene after reset");
  }
  // Read completed validation messages, including those generated by the final frame.
  frame(false);
  Require(session->statistics().validation_message_count == 0,
      session->statistics().validation_detail.c_str());
  std::cout << samples << "x: foreground/offscreen equality, source-over, isolation and lifetime passed\n";
  return true;
}
} // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 1;
  if (!glfwInit() || !glfwVulkanSupported()) return 77;
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  auto* window = glfwCreateWindow(96, 96, "RGBA compositor regression", nullptr, nullptr);
  if (!window) { glfwTerminate(); return 77; }
  int code = 0;
  try {
    const auto shaders = Toon::SceneShadersIn(argv[1]);
    if (!Run(window, shaders, 1, argc > 2 ? argv[2] : "") ||
        !Run(window, shaders, 4, argc > 2 ? argv[2] : "")) code = 77;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; code = 1; }
  glfwDestroyWindow(window);
  glfwTerminate();
  return code;
}
