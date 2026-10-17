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

  // Connected inputs replace the surface's constants. One packed image
  // supplies B metallic, G roughness and A opacity independently of RGB.
  const auto image = world.CreateTexture();
  Toon::ToonTexture texture;
  texture.width = texture.height = 1;
  texture.encoding = Toon::ToonTextureEncoding::Linear;
  texture.pixels = std::make_shared<const std::vector<std::uint8_t>>(
      std::vector<std::uint8_t>{64, 128, 192, 96});
  world.SetTexture(image, texture);
  world.SetMeshUVs(mesh, {{0.25F, 0.25F}, {0.25F, 0.25F}, {0.25F, 0.25F}});
  material = {};
  material.preview.enabled = true;
  material.emissive_texture.texture = image;
  material.preview.inputs[1].connected = true;
  material.preview.inputs[1].scale = {2, 0.5F, 1, 1};
  material.preview.inputs[1].bias = {0.1F, 0.05F, 0, 0};
  auto texel = render();
  Near(texel[0], 2 * 64.0F / 255 + 0.1F, "emission texture scale/bias R");
  Near(texel[1], 0.5F * 128.0F / 255 + 0.05F, "emission texture scale/bias G");
  Near(texel[2], 192.0F / 255, "emission texture replaces zero emission");
  const auto resident = renderer->statistics();
  material.preview.inputs[1].scale.x = 1;
  render();
  const auto edited = renderer->statistics();
  Require(edited.material_writes == resident.material_writes + 1 &&
              edited.texture_uploads == resident.texture_uploads &&
              edited.point_uploads == resident.point_uploads &&
              edited.topology_uploads == resident.topology_uploads &&
              edited.pipelines_created == resident.pipelines_created,
      "texture scale edit rebuilt resident resources");
  material.preview.inputs[1].connected = false;
  material.base_texture.texture = image;
  material.preview.metallic_texture.texture = image;
  material.preview.roughness_texture.texture = image;
  for (const auto i : {0, 2, 3}) material.preview.inputs[i].connected = true;
  material.preview.inputs[2].channel = 2;
  material.preview.inputs[3].channel = 1;
  direct = 0.2F;
  texel = render();
  Near(texel[0], expected(64.0F / 255, 192.0F / 255, 128.0F / 255), "textured BRDF R");
  Near(texel[1], expected(128.0F / 255, 192.0F / 255, 128.0F / 255), "textured BRDF G");
  Near(texel[2], expected(192.0F / 255, 192.0F / 255, 128.0F / 255), "textured BRDF B");
  // sRGB conversion affects RGB alone, before scale/bias and lighting.
  texture.encoding = Toon::ToonTextureEncoding::Srgb;
  world.SetTexture(image, texture);
  material.preview.inputs[0].connected = false;
  material.preview.inputs[1].connected = true;
  material.preview.inputs[1].scale = {1, 1, 1, 1};
  material.preview.inputs[1].bias = {};
  direct = 0;
  texel = render();
  const auto linear = [](float x) { return x <= 0.04045F ? x / 12.92F : std::pow((x + 0.055F) / 1.055F, 2.4F); };
  Near(texel[0], linear(64.0F / 255), "sRGB emission R");
  Near(texel[1], linear(128.0F / 255), "sRGB emission G");
  const auto texture_opaque = texel;
  material.preview.inputs[4].connected = true;
  material.preview.inputs[4].channel = 3;
  material.preview.opacity_texture.texture = image;
  material.alpha_mode = Toon::ToonAlphaMode::Mask;
  material.alpha_cutoff = 0.5F;
  const auto texture_discard = render();
  Require(center_depth() == 1 && texture_discard == masked, "texture alpha Mask discard");
  material.alpha_cutoff = 96.0F / 255;
  Require(render() == texture_opaque && center_depth() < 1, "texture alpha Mask threshold equality");
  material.alpha_mode = Toon::ToonAlphaMode::Blend;
  texel = render();
  Near(texel[0], (texture_opaque[0] * (96.0F / 255) + masked[0] * (159.0F / 255)) / 255,
      "texture alpha must remain linear in Blend");
  Require(center_depth() == 1, "texture Blend must not write depth");
  // Failed images use the texture fallback, before scale/bias.
  material.alpha_mode = Toon::ToonAlphaMode::Opaque;
  material.emissive_texture.texture = 0;
  material.preview.inputs[1].fallback = {0.2F, 0.3F, 0.4F, 1};
  material.preview.inputs[1].scale = {2, 1, 1, 1};
  material.preview.inputs[1].bias = {0.1F, 0, 0, 0};
  texel = render();
  Near(texel[0], 0.5F, "missing image fallback scale/bias");
  material.emissive_texture.texture = image;
  world.SetMeshUVs(mesh, {});
  texel = render();
  Near(texel[0], 2 * linear(64.0F / 255) + 0.1F, "absent UVs must sample at the primvar fallback");
  // A four-colour image locates samples in USD's bottom-left coordinates.
  // The rotated map is the conjugate of USD R(90) + (0.5,0.5).
  texture.width = texture.height = 2;
  texture.encoding = Toon::ToonTextureEncoding::Linear;
  texture.pixels = std::make_shared<const std::vector<std::uint8_t>>(
      std::vector<std::uint8_t>{255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255});
  world.SetTexture(image, texture);
  world.SetMeshUVs(mesh, {{0.25F,0.25F}, {0.25F,0.25F}, {0.25F,0.25F}});
  material.preview.inputs[1].scale = {1,1,1,1};
  material.preview.inputs[1].bias = {};
  texel = render();
  Near(texel[2], 1, "USD UV origin must sample bottom-left blue");
  material.emissive_texture.rotation = 1.57079632679F;
  material.emissive_texture.offset = {-0.5F, 0.5F};
  texel = render();
  Near(texel[0], 1, "rotated USD transform must sample top-left red");
  Near(texel[2], 0, "rotated USD transform V direction");
  const auto uv_resident = renderer->statistics();
  material.emissive_texture.offset.x += 0.5F;
  texel = render();
  Near(texel[1], 1, "USD translated transform must sample top-right green");
  const auto uv_edited = renderer->statistics();
  Require(uv_edited.texture_uploads == uv_resident.texture_uploads &&
              uv_edited.point_uploads == uv_resident.point_uploads &&
              uv_edited.pipelines_created == uv_resident.pipelines_created &&
              uv_edited.material_writes == uv_resident.material_writes + 1,
      "UV edit must update one slot alone");
  world.SetMeshUVs(mesh, {});
  material.emissive_texture.scale = {0, 0};
  material.emissive_texture.offset = {0.25F, 0.25F};
  texel = render();
  Near(texel[0], 1, "constant texture coordinates must sample even without mesh UVs");
  // Restore the original constant case for culling/fallback regressions.
  material = {};
  material.preview.enabled = true;
  material.emissive = {0.6F, 0.2F, 0.1F};
  world.SetMeshTopology(mesh, {0, 2, 1});
  Require(render() == masked, "single-sided back face must be culled");
  material.double_sided = true;
  Require(render() == opaque, "double-sided back face must draw");
  material.preview.enabled = false;
  const auto fallback = render();
  Require(fallback != opaque, "unreadable network must retain mesh display colour");
  Require(renderer->statistics().validation_message_count == 0, "Vulkan validation error");
  std::cout << "preview_surface samples=" << samples << " constants, textures, UV transforms, GGX, Mask, Blend and slot edits passed\n";
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
