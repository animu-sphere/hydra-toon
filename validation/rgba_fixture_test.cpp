// SPDX-License-Identifier: Apache-2.0
// Original, redistributable release fixtures. No format plugin or model needed.
#include <toon/vulkan_backend.hpp>
#include "../adapters/viewport/src/capture.hpp"
#include "rgba_fixture_scenes.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
constexpr unsigned Size = 256;
void Require(bool condition, const std::string& message) {
  if (!condition)
    throw std::runtime_error(message);
}
void Save(const Toon::ColorProduct& image, const std::filesystem::path& path) {
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(image.payload.data()), image.payload.size());
  Require(file.good(), "cannot write " + path.string());
}
bool Run(const Toon::SceneShaders& shaders, unsigned samples, const std::filesystem::path& output) {
  Toon::FrameStatus status;
  std::string error;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, error, {samples});
  if (status == Toon::FrameStatus::Skip) {
    std::cout << error << '\n';
    return false;
  }
  Require(renderer != nullptr, error);
  Require(renderer->statistics().samples == samples, "requested samples unavailable");
  std::cout << "device=" << renderer->statistics().device_name << " samples=" << samples
            << " validation=" << renderer->statistics().validation_available << '\n';
  for (const std::string name : {"avatar", "mmd-shadow"}) {
    Toon::RenderWorld world;
    if (name == "avatar")
      toon_fixtures::Avatar(world);
    else
      toon_fixtures::Shadow(world);
    auto draws = Toon::ExtractDrawList(world.Commit());
    Toon::ColorProduct color;
    Toon::DepthProduct depth;
    const auto render = [&](Toon::BackgroundMode mode, float value) {
      Require(renderer->SetBackground({mode, {value, value, value}}, error), error);
      Require(renderer->Render(draws, Size, Size, color, depth, error), error);
    };
    render(Toon::BackgroundMode::Transparent, 0);
    const auto foreground = color;
    const auto prefix = output / (name + "-" + std::to_string(samples));
    Save(color, prefix.string() + ".rgba");
    Toon::viewport::WriteForegroundPng(prefix.string() + ".png", color);
    // Independent effects controls must change colour, never product coverage.
    if (name == "mmd-shadow") {
      Require(renderer->statistics().shadow_draws == 2, "missing shadow casters");
      draws.mmd_self_shadow = false;
      render(Toon::BackgroundMode::Transparent, 0);
      const auto center = (Size / 2 * Size + Size / 2) * 4;
      Require(foreground.payload[center] + 40 < color.payload[center], "shadow not received");
      for (std::size_t at = 3; at < color.payload.size(); at += 4)
        Require(color.payload[at] == foreground.payload[at], "shadow changed coverage");
      draws.mmd_self_shadow = true;
    } else {
      Require(renderer->statistics().outline_draws > 0, "missing avatar outlines");
      draws.outlines = false;
      render(Toon::BackgroundMode::Transparent, 0);
      Require(color.payload != foreground.payload, "outline control insensitive");
      draws.outlines = true;
    }
    for (const auto value : {0, 1}) {
      render(Toon::BackgroundMode::Opaque, static_cast<float>(value));
      Save(color, prefix.string() + (value ? "-white.rgba" : "-black.rgba"));
    }
    render(Toon::BackgroundMode::Transparent, 1);
    Require(color.payload == foreground.payload, "background contaminated foreground");
    Require(renderer->statistics().validation_message_count == 0, "Vulkan validation messages");
    std::cout << name << ' ' << samples << "x: RGBA fixture and effects controls passed\n";
  }
  return true;
}
} // namespace
int main(int argc, char** argv) {
  try {
    Require(argc == 3, "expected shader directory and output directory");
    std::filesystem::create_directories(argv[2]);
    return Run(Toon::SceneShadersIn(argv[1]), 1, argv[2]) &&
                   Run(Toon::SceneShadersIn(argv[1]), 4, argv[2])
               ? 0
               : 77;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
