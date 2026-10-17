// SPDX-License-Identifier: Apache-2.0
// Controlled linear-light checks, independent of a format plugin or an
// external asset: constant emission, the normal-incidence BRDF, surface
// opacity, Mask, fallback, and the cost of changing material values.
#include <toon/vulkan_backend.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}
std::array<int, 4> Center(const Toon::ColorProduct& image) {
  const auto offset = image.row_pitch * (image.height / 2) + (image.width / 2) * 4;
  return {image.payload[offset], image.payload[offset + 1], image.payload[offset + 2], image.payload[offset + 3]};
}
void Near(int actual, float expected, const char* message) {
  Require(std::abs(static_cast<float>(actual) - expected * 255.0F) <= 2.0F, message);
}
bool Run(const Toon::SceneShaders& shaders, std::uint32_t samples) {
  Toon::FrameStatus status;
  std::string error;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, error, {samples});
  if (status == Toon::FrameStatus::Skip) {
    std::cout << error << '\n';
    return false;
  }
  Require(renderer != nullptr, error.c_str());
  Toon::RenderWorld world;
  world.SetBootstrapTriangle();
  const auto mesh = world.Commit().meshes.front().id;
  world.SetMeshNormals(mesh, {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}});
  const auto id = world.CreateMaterial();
  world.SetMeshMaterial(mesh, id);
  Toon::ToonMaterial material;
  material.preview.enabled = true;
  material.base_color = {0.5F, 0.25F, 0.125F};
  material.emissive = {0.1F, 0.3F, 0.05F};
  Toon::ColorProduct color;
  Toon::DepthProduct depth;
  float direct = 0;
  const auto render = [&] {
    world.SetMaterial(id, material);
    auto draws = Toon::ExtractDrawList(world.Commit());
    draws.lighting.direct_scale = direct;
    draws.lighting.ambient_scale = 0;
    draws.lighting.key_direction = {0, 0, 1};
    Require(renderer->Render(draws, 64, 64, color, depth, error), error.c_str());
    return Center(color);
  };
  const auto center_depth = [&] {
    return depth.payload[(depth.row_pitch / sizeof(float)) * (depth.height / 2) + depth.width / 2];
  };
  const auto emission = render();
  Near(emission[0], 0.1F, "emission R");
  Near(emission[1], 0.3F, "emission G");
  Near(emission[2], 0.05F, "emission B");
  const auto first = renderer->statistics();
  material.emissive = {};
  direct = 0.2F;
  // At N=V=L, F=0.04, Smith visibility is one and GGX D=1/(pi*r^4).
  const auto dielectric = render();
  constexpr float pi = 3.14159265359F;
  const auto expected = [&](float base, float metallic, float roughness) {
    const float f0 = 0.04F * (1 - metallic) + base * metallic;
    return direct * ((1 - f0) * (1 - metallic) * base / pi + f0 / (4 * pi * std::pow(roughness, 4.0F)));
  };
  Near(dielectric[0], expected(0.5F, 0, 0.5F), "dielectric BRDF");
  material.preview.metallic = 1;
  material.preview.roughness = 0.8F;
  const auto metal = render();
  Near(metal[0], expected(0.5F, 1, 0.8F), "metal BRDF");
  Near(metal[1], expected(0.25F, 1, 0.8F), "metal colour");
  const auto changed = renderer->statistics();
  Require(changed.material_writes == first.material_writes + 2 &&
              changed.point_uploads == first.point_uploads && changed.topology_uploads == first.topology_uploads &&
              changed.pipelines_created == first.pipelines_created,
      "value edit rebuilt static resources");

  direct = 0;
  material.emissive = {0.6F, 0.2F, 0.1F};
  material.alpha_mode = Toon::ToonAlphaMode::Mask;
  material.alpha = 0.4F;
  material.alpha_cutoff = 0.5F;
  const auto masked = render();
  Require(center_depth() == 1.0F, "discarded Mask must not write depth");
  material.alpha = 0.5F;
  const auto kept = render();
  Require(center_depth() < 1.0F, "kept Mask must write depth");
  Near(kept[0], 0.6F, "Mask threshold equality must survive");
  Require(masked != kept, "Mask did not discard");
  material.alpha_mode = Toon::ToonAlphaMode::Blend;
  material.alpha = 0.25F;
  const auto blended = render();
  Require(center_depth() == 1.0F, "Blend must not write depth");
  for (int channel = 0; channel < 3; ++channel)
    Near(blended[channel], (0.25F * kept[channel] + 0.75F * masked[channel]) / 255.0F, "Blend over background");
  material.alpha_mode = Toon::ToonAlphaMode::Opaque;
  const auto opaque = render();
  Near(opaque[0], 0.6F, "Opaque must ignore material alpha");
  world.SetMeshTopology(mesh, {0, 2, 1});
  Require(render() == masked, "single-sided back face must be culled");
  material.double_sided = true;
  Require(render() == opaque, "double-sided back face must draw");
  material.preview.enabled = false;
  const auto fallback = render();
  Require(fallback != opaque, "unreadable network must retain mesh display colour");
  Require(renderer->statistics().validation_message_count == 0, "Vulkan validation error");
  std::cout << "preview_surface samples=" << samples << " constants, GGX, Mask, Blend and slot edits passed\n";
  return true;
}
} // namespace
int main(int argc, char** argv) {
  try {
    if (argc != 2)
      return 2;
    const auto shaders = Toon::SceneShadersIn(argv[1]);
    return Run(shaders, 1) && Run(shaders, 4) ? 0 : 77;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
