// SPDX-License-Identifier: Apache-2.0
#include <toon/vulkan_backend.hpp>
#if defined(TOON_HAS_WEBGPU)
#include <toon/webgpu_backend.hpp>
#endif

#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
// A backend's offscreen renderer, so the same checks run on Vulkan and on
// WebGPU (WebGPU policy §9).
using Factory = std::function<std::unique_ptr<Toon::OffscreenRenderer>(
    Toon::FrameStatus&, std::string&, const Toon::RenderOptions&)>;

void Require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void Near(int actual, float expected, const char* message) {
  if (std::abs(actual - expected * 255) > 2.1F) {
    std::cerr << message << ": " << actual << " expected " << expected * 255 << '\n';
    throw std::runtime_error(message);
  }
}
float Linear(float value) {
  return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

bool Run(const Factory& create, std::uint32_t samples) {
  Toon::FrameStatus status;
  std::string error;
  auto renderer = create(status, error, {samples});
  if (status == Toon::FrameStatus::Skip) { std::cout << error << '\n'; return false; }
  Require(renderer != nullptr, error.c_str());
  Require(renderer->statistics().samples == samples, "requested MSAA unavailable");
  Toon::ColorProduct color;
  Toon::DepthProduct depth;
  Toon::RenderWorld world;
  auto draws = Toon::ExtractDrawList(world.Commit());
  const auto background = [&](Toon::BackgroundMode mode, Toon::Float3 rgb = {}) {
    Require(renderer->SetBackground({mode, rgb}, error), error.c_str());
  };
  const auto render = [&] {
    Require(renderer->Render(draws, 96, 96, color, depth, error), error.c_str());
    Require(color.color_space == "linear" && color.transfer_function == "linear" &&
        color.alpha_convention == "premultiplied" && color.pixel_format == "rgba8-unorm" &&
        color.origin == "top-left" && color.row_pitch == color.width * 4,
        "offscreen output interpretation");
  };
  const auto center = [&] { return color.row_pitch * 48 + 48 * 4; };
  render(); // Backwards-compatible default clear.
  Near(color.payload[0], 0.05F, "default clear R");
  Near(color.payload[1], 0.10F, "default clear G");
  Near(color.payload[2], 0.15F, "default clear B");
  Require(color.payload[3] == 255, "default clear alpha");
  if (!renderer->SetBackground({Toon::BackgroundMode::Transparent, {0.9F, 0.2F, 0.7F}}, error)) {
    Require(samples > 1 && error.find("alphaToOne") != std::string::npos, error.c_str());
    std::cout << "SKIP: " << error << '\n';
    return false;
  }
  render();
  for (auto value : color.payload) Require(value == 0, "transparent clear must be zero RGBA");
  for (auto value : depth.payload) Require(value == 1, "empty depth clear");
  for (const Toon::BackgroundSettings invalid : {
      Toon::BackgroundSettings{Toon::BackgroundMode::Opaque, {-0.1F, 0, 0}},
      Toon::BackgroundSettings{Toon::BackgroundMode::Opaque, {0, 1.1F, 0}},
      Toon::BackgroundSettings{Toon::BackgroundMode::Opaque, {0, 0, std::numeric_limits<float>::quiet_NaN()}},
      Toon::BackgroundSettings{static_cast<Toon::BackgroundMode>(42), {}}}) {
    Require(!renderer->SetBackground(invalid, error), "invalid clear accepted");
    render();
    for (auto value : color.payload) Require(value == 0, "invalid settings changed the clear");
  }

  world.SetBootstrapTriangle();
  const auto mesh = world.Commit().meshes.front().id;
  world.SetMeshNormals(mesh, std::vector<Toon::Float3>(3, {0, 0, 1}));
  const auto id = world.CreateMaterial();
  world.SetMeshMaterial(mesh, id);
  // Constant shading gives an independent RGB/alpha oracle at every sample,
  // including silhouettes. MMD's ambient colour is in its stored sRGB space.
  const Toon::Float3 source{0.6F, 0.3F, 0.15F};
  for (const auto model : {Toon::ToonShadingModel::MToon,
           Toon::ToonShadingModel::MMD, Toon::ToonShadingModel::PreviewSurface}) {
    Toon::ToonMaterial material;
    material.model = model;
    material.preview.enabled = true;
    material.base_color = {};
    material.emissive = source;
    material.mmd.ambient_color = source;
    const std::array<float, 3> rgb = model == Toon::ToonShadingModel::MMD
        ? std::array{Linear(source.x), Linear(source.y), Linear(source.z)}
        : std::array{source.x, source.y, source.z};
    for (const float opacity : {1.0F, 0.25F, 0.0F}) {
      material.alpha_mode = opacity == 1 ? Toon::ToonAlphaMode::Opaque : Toon::ToonAlphaMode::Blend;
      material.alpha = opacity;
      world.SetMaterial(id, material);
      draws = Toon::ExtractDrawList(world.Commit());
      draws.lighting.direct_scale = draws.lighting.ambient_scale = 0;
      background(Toon::BackgroundMode::Transparent);
      render();
      Near(color.payload[center() + 3], opacity, "surface alpha");
      for (int channel = 0; channel < 3; ++channel)
        Near(color.payload[center() + channel], rgb[channel] * opacity, "premultiplied surface RGB");
      bool partial = false;
      for (std::size_t at = 0; at < color.payload.size(); at += 4) {
        const float alpha = color.payload[at + 3] / 255.0F;
        for (int channel = 0; channel < 3; ++channel)
          Near(color.payload[at + channel], rgb[channel] * alpha, "silhouette premultiplication");
        partial |= alpha > 0 && alpha < opacity - 0.01F;
      }
      if (samples > 1 && opacity > 0) Require(partial, "missing partial silhouette coverage");
      const auto foreground = color.payload;
      const auto foreground_depth = depth.payload;
      const auto before = renderer->statistics();
      for (const auto bg : {Toon::Float3{0, 0, 0}, Toon::Float3{1, 1, 1}, Toon::Float3{0.17F, 0.43F, 0.81F}}) {
        background(Toon::BackgroundMode::Opaque, bg);
        render();
        const std::array back{bg.x, bg.y, bg.z};
        for (std::size_t at = 0; at < color.payload.size(); at += 4) {
          const float alpha = foreground[at + 3] / 255.0F;
          Require(color.payload[at + 3] == 255, "opaque background lost alpha");
          for (int channel = 0; channel < 3; ++channel)
            Near(color.payload[at + channel], foreground[at + channel] / 255.0F + back[channel] * (1 - alpha),
                "source-over consumer differs from rendered opaque background");
        }
        Require(depth.payload == foreground_depth, "background changed scene depth");
      }
      background(Toon::BackgroundMode::Transparent, {1, 0, 1});
      render();
      Require(color.payload == foreground, "foreground depends on preview background");
      const auto after = renderer->statistics();
      Require(after.target_allocations == before.target_allocations && after.pipelines_created == before.pipelines_created &&
          after.point_uploads == before.point_uploads && after.topology_uploads == before.topology_uploads &&
          after.material_writes == before.material_writes && after.texture_uploads == before.texture_uploads,
          "background change rebuilt scene resources");
    }
  }

  // Constant alpha exactly at cutoff has half coverage under MToon's A2C.
  // Selected samples must store alpha ONE, not coverage a second time.
  Toon::ToonMaterial mask;
  mask.model = Toon::ToonShadingModel::MToon;
  mask.base_color = {};
  mask.emissive = source;
  mask.alpha_mode = Toon::ToonAlphaMode::Mask;
  mask.alpha = mask.alpha_cutoff = 0.5F;
  world.SetMaterial(id, mask);
  draws = Toon::ExtractDrawList(world.Commit());
  draws.lighting.direct_scale = draws.lighting.ambient_scale = 0;
  render();
  const float coverage = color.payload[center() + 3] / 255.0F;
  if (samples == 1) Near(color.payload[center() + 3], 1, "1x Mask cutoff equality");
  else Require(coverage > 0 && coverage < 1, "Mask must exercise partial A2C");
  for (int channel = 0; channel < 3; ++channel) {
    const std::array rgb{source.x, source.y, source.z};
    Near(color.payload[center() + channel], rgb[channel] * coverage, "Mask coverage applied twice");
  }
  mask.alpha = 0.4F;
  world.SetMaterial(id, mask);
  draws = Toon::ExtractDrawList(world.Commit());
  render();
  Require(color.payload[center() + 3] == 0, "discarded Mask retained alpha");

  // Source-over onto an already translucent destination must accumulate
  // alpha as As + Ad * (1-As), independently of the RGB equation.
  Toon::ToonMaterial rear;
  rear.preview.enabled = true;
  rear.base_color = {};
  rear.emissive = {0.8F, 0, 0};
  rear.alpha_mode = Toon::ToonAlphaMode::Blend;
  rear.alpha = 0.5F;
  world.SetMaterial(id, rear);
  const auto front_mesh = world.CreateMesh();
  world.SetMeshPoints(front_mesh, {{-0.7F, -0.65F, -0.6F}, {0.7F, -0.65F, -0.6F}, {0, 0.7F, -0.6F}});
  world.SetMeshTopology(front_mesh, {0, 1, 2});
  world.SetMeshNormals(front_mesh, std::vector<Toon::Float3>(3, {0, 0, 1}));
  const auto front_id = world.CreateMaterial();
  auto front = rear;
  front.alpha = 0.25F;
  front.emissive = {0, 0.8F, 0};
  world.SetMaterial(front_id, front);
  world.SetMeshMaterial(front_mesh, front_id);
  draws = Toon::ExtractDrawList(world.Commit());
  draws.lighting.direct_scale = draws.lighting.ambient_scale = 0;
  render();
  Near(color.payload[center()], 0.8F * 0.5F * 0.75F, "layered source-over R");
  Near(color.payload[center() + 1], 0.8F * 0.25F, "layered source-over G");
  Near(color.payload[center() + 3], 0.25F + 0.5F * 0.75F, "layered source-over alpha");
  world.RemoveMesh(front_mesh);

  // A back-facing hull without a visible surface still contributes foreground.
  world.SetMeshTopology(mesh, {0, 2, 1});
  for (const auto model : {Toon::ToonShadingModel::MToon, Toon::ToonShadingModel::MMD}) {
    Toon::ToonMaterial outline;
    outline.model = model;
    outline.outline = true;
    outline.outline_width = 0.05F;
    outline.outline_color = {0.5F, 0.25F, 0.125F};
    outline.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
    outline.mtoon.outline_lighting_mix = 0;
    for (const float opacity : {1.0F, 0.35F}) {
      outline.alpha_mode = opacity == 1 ? Toon::ToonAlphaMode::Opaque : Toon::ToonAlphaMode::Blend;
      outline.alpha = outline.mmd.edge_alpha = opacity;
      world.SetMaterial(id, outline);
      draws = Toon::ExtractDrawList(world.Commit());
      render();
      Require(renderer->statistics().outline_draws == 1, "missing hull");
      Near(color.payload[center() + 3], opacity, "outline foreground alpha");
      const std::array rgb{0.5F, 0.25F, 0.125F};
      for (int channel = 0; channel < 3; ++channel)
        Near(color.payload[center() + channel], opacity * (model == Toon::ToonShadingModel::MMD
            ? Linear(rgb[channel]) : rgb[channel]), "outline premultiplied RGB");
      draws.outlines = false;
      render();
      Require(color.payload[center() + 3] == 0, "outline fixture has a visible surface");
    }
  }
  Require(renderer->statistics().validation_message_count == 0, "validation messages");
  std::cout << "RGBA output: " << samples << "x clear, three shading models, Blend, Mask, silhouettes, hulls and source-over passed\n";
  return true;
}
} // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 2 || (argc == 4 && std::string_view(argv[2]) == "--backend"),
        "expected shader directory [--backend vulkan|webgpu]");
    Factory create;
    if (argc == 4 && std::string_view(argv[3]) == "webgpu") {
#if defined(TOON_HAS_WEBGPU)
      const auto shaders = Toon::WebGpuShadersIn(argv[1]);
      create = [shaders](Toon::FrameStatus& status, std::string& error,
                   const Toon::RenderOptions& options) {
        return Toon::CreateWebGpuOffscreenRenderer(shaders, status, error, options);
      };
#else
      return 2;
#endif
    } else {
      const auto shaders = Toon::SceneShadersIn(argv[1]);
      create = [shaders](Toon::FrameStatus& status, std::string& error,
                   const Toon::RenderOptions& options) {
        return Toon::CreateOffscreenRenderer(shaders, status, error, options);
      };
    }
    if (!Run(create, 1) || !Run(create, 4)) return 77;
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
