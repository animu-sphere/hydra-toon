// SPDX-License-Identifier: Apache-2.0
// Controlled linear-light checks, independent of a format plugin or an
// external asset: constant emission, the normal-incidence BRDF, surface
// opacity, Mask, fallback, and the cost of changing material values. The
// same checks run on either backend (WebGPU policy §9).
#include <toon/vulkan_backend.hpp>
#if defined(TOON_HAS_WEBGPU)
#include <toon/webgpu_backend.hpp>
#endif

#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using Factory = std::function<std::unique_ptr<Toon::OffscreenRenderer>(
    Toon::FrameStatus&, std::string&, const Toon::RenderOptions&)>;
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
bool Normals(const Factory& create, std::uint32_t samples) {
  Toon::FrameStatus status;
  std::string error;
  auto renderer = create(status, error, {samples});
  if (status == Toon::FrameStatus::Skip) return false;
  auto reference = create(status, error, {samples});
  Require(renderer && reference, error.c_str());
  Toon::RenderWorld world, oracle;
  world.SetBootstrapTriangle();
  oracle.SetBootstrapTriangle();
  const auto mesh = world.Commit().meshes.front().id;
  const auto ref = oracle.Commit().meshes.front().id;
  const auto id = world.CreateMaterial(), ref_id = oracle.CreateMaterial();
  world.SetMeshMaterial(mesh, id);
  oracle.SetMeshMaterial(ref, ref_id);
  world.SetMeshNormals(mesh, std::vector<Toon::Float3>(3, {0, 0, 1}));
  Toon::ToonMaterial material;
  material.preview.enabled = true;
  material.base_color = {0.7F, 0.4F, 0.2F};
  const auto image = world.CreateTexture();
  Toon::ToonTexture texture;
  texture.width = texture.height = 2;
  texture.encoding = Toon::ToonTextureEncoding::Linear;
  // Four different directions make the sampling origin and transform visible.
  texture.pixels = std::make_shared<const std::vector<std::uint8_t>>(
      std::vector<std::uint8_t>{191,230,204,255, 50,190,220,255,
          80,60,230,255, 200,80,180,255});
  world.SetTexture(image, texture);
  const auto compare = [&](Toon::Float3 normal) {
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    normal = length > 1e-6F ? Toon::Float3{normal.x / length, normal.y / length, normal.z / length}
                           : Toon::Float3{0, 0, 1};
    oracle.SetMeshNormals(ref, std::vector<Toon::Float3>(3, normal));
    world.SetMaterial(id, material);
    auto neutral = material;
    neutral.preview.normal = {0, 0, 1};
    neutral.preview.inputs[5].connected = false;
    neutral.normal_texture.texture = 0;
    oracle.SetMaterial(ref_id, neutral);
    auto actual = Toon::ExtractDrawList(world.Commit());
    auto expected = Toon::ExtractDrawList(oracle.Commit());
    for (auto* draws : {&actual, &expected}) {
      draws->lighting.direct_scale = 0.7F;
      draws->lighting.ambient_scale = 0;
      draws->lighting.key_direction = {0.3F, -0.4F, 1};
    }
    Toon::ColorProduct color, target;
    Toon::DepthProduct depth, target_depth;
    Require(renderer->Render(actual, 96, 96, color, depth, error), error.c_str());
    Require(reference->Render(expected, 96, 96, target, target_depth, error), error.c_str());
    for (std::size_t i = 0; i < color.payload.size(); ++i)
      Require(std::abs(int(color.payload[i]) - int(target.payload[i])) <= 2,
          "PreviewSurface normal differs from CPU-baked normal");
    Require(depth.payload == target_depth.payload, "normal mapping changed depth or coverage");
    Require(renderer->statistics().validation_message_count == 0, "normal map validation error");
  };
  // No texture: signed constants use the derivative frame, including a mirrored U seam.
  // A third case rotates the UV basis by 90 degrees (T=+Y, B=-X).
  for (int basis = 0; basis < 3; ++basis) {
    const bool mirrored = basis == 1;
    world.SetMeshUVs(mesh, basis == 2 ? std::vector<Toon::Float2>{{0.4F,0.6F},{0.4F,0.4F},{0.6F,0.5F}}
        : std::vector<Toon::Float2>{{mirrored ? 0.6F : 0.4F,0.4F},
              {mirrored ? 0.4F : 0.6F,0.4F},{0.5F,0.6F}});
    material.preview.normal = {0.35F, -0.45F, 0.8F};
    compare(basis == 2 ? Toon::Float3{0.45F, 0.35F, 0.8F}
                      : Toon::Float3{mirrored ? -0.35F : 0.35F, -0.45F, 0.8F});
  }
  // Authored frames work with constant/absent UVs. +Y is bottom-left +V.
  world.SetMeshUVs(mesh, std::vector<Toon::Float2>(3, {0.25F, 0.25F}));
  world.SetMeshTangents(mesh, std::vector<Toon::Float4>(3, {1, 0, 0, 1}));
  material.preview.inputs[5].connected = true;
  material.preview.inputs[5].scale = {2, 2, 2, 1};
  material.preview.inputs[5].bias = {-1, -1, -1, 0};
  material.normal_texture.texture = image;
  const auto decode = [](float r, float g, float b) {
    return Toon::Float3{2 * r / 255 - 1, 2 * g / 255 - 1, 2 * b / 255 - 1};
  };
  compare(decode(80, 60, 230)); // bottom-left, not top-left
  const auto resident = renderer->statistics();
  material.normal_texture.rotation = 1.57079632679F;
  material.normal_texture.offset = {-0.5F, 0.5F};
  compare(decode(191, 230, 204)); // sampling rotates; the tangent frame stays fixed
  const auto edited = renderer->statistics();
  Require(edited.material_writes == resident.material_writes + 1 &&
              edited.point_uploads == resident.point_uploads && edited.texture_uploads == resident.texture_uploads &&
              edited.pipelines_created == resident.pipelines_created, "normal UV edit rebuilt static resources");
  material.preview.inputs[5].scale = {1.25F, 1.75F, 2, 1};
  material.preview.inputs[5].bias = {-0.4F, -0.6F, -1, 0};
  compare({191 / 255.0F * 1.25F - 0.4F, 230 / 255.0F * 1.75F - 0.6F, 204 / 255.0F * 2 - 1});
  world.SetMeshUVs(mesh, {});
  material.preview.inputs[5].uv_fallback = {0.25F, 0.25F};
  compare({191 / 255.0F * 1.25F - 0.4F, 230 / 255.0F * 1.75F - 0.6F, 204 / 255.0F * 2 - 1});
  // A missing image still consumes fallback*scale+bias; zero vectors and
  // degenerate frames retain the vertex normal, never a NaN.
  material.normal_texture.texture = 0;
  material.preview.inputs[5].fallback = {0.4F, 0.8F, 0.9F, 1};
  compare({0.1F, 0.8F, 0.8F});
  material.preview.inputs[5].fallback = {};
  material.preview.inputs[5].bias = {};
  compare({0, 0, 1});
  material.preview.inputs[5].connected = false;
  world.SetMeshTangents(mesh, {});
  compare({0, 0, 1});
  world.SetMeshUVs(mesh, std::vector<Toon::Float2>(3, {0.5F, 0.5F}));
  compare({0, 0, 1});
  std::cout << "PreviewSurface normals: " << samples << "x constants, mirrored/rotated derivatives, transformed textures and fallback passed\n";
  return true;
}
bool Run(const Factory& create, std::uint32_t samples) {
  Toon::FrameStatus status;
  std::string error;
  auto renderer = create(status, error, {samples});
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
  float ambient = 0;
  Toon::Float3 key_direction{0, 0, 1};
  const auto render = [&] {
    world.SetMaterial(id, material);
    auto draws = Toon::ExtractDrawList(world.Commit());
    draws.lighting.direct_scale = direct;
    draws.lighting.ambient_scale = ambient;
    draws.lighting.key_direction = key_direction;
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

  // At grazing incidence a pure metal keeps its albedo at both Fresnel
  // endpoints. Set H=N, N.V=N.L=0.1 analytically: a white F90 would brighten
  // and desaturate this copper-like colour even though F0 is correct.
  constexpr float cosine = 0.1F;
  const float sine = std::sqrt(1 - cosine * cosine);
  world.SetMeshNormals(mesh, std::vector<Toon::Float3>(3, {sine, 0, cosine}));
  key_direction = {2 * sine * cosine, 0, 2 * cosine * cosine - 1};
  direct = 0.8F;
  const auto grazing = render();
  const float alpha2 = std::pow(material.preview.roughness, 4.0F);
  const float smith = 2 * cosine / (cosine + std::sqrt(alpha2 + (1 - alpha2) * cosine * cosine));
  const float response = direct * smith * smith / (4 * pi * alpha2 * cosine);
  Near(grazing[0], 0.5F * response, "metal grazing F90 R");
  Near(grazing[1], 0.25F * response, "metal grazing F90 G");
  Near(grazing[2], 0.125F * response, "metal grazing F90 B");
  world.SetMeshNormals(mesh, std::vector<Toon::Float3>(3, {0, 0, 1}));
  key_direction = {0, 0, 1};

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
  material.base_color = {1, 0, 1};
  const auto fallback = render();
  Require(fallback == std::array<int, 4>{255, 0, 255, 255},
      "bound fallback must draw its own opaque unlit colour, including back faces");
  const auto fallback_resident = renderer->statistics();
  material.base_color = {0.2F, 0.4F, 0.6F};
  const auto edited_fallback = render();
  Near(edited_fallback[0], 0.2F, "unlit material colour edit R");
  Near(edited_fallback[1], 0.4F, "unlit material colour edit G");
  Near(edited_fallback[2], 0.6F, "unlit material colour edit B");
  const auto fallback_edited = renderer->statistics();
  Require(fallback_edited.point_uploads == fallback_resident.point_uploads &&
              fallback_edited.topology_uploads == fallback_resident.topology_uploads &&
              fallback_edited.pipelines_created == fallback_resident.pipelines_created &&
              fallback_edited.material_writes == fallback_resident.material_writes + 1,
      "unlit material value edit must retain geometry and pipelines");
  const auto mesh_color = world.Commit().meshes.front().color;
  world.SetMeshMaterial(mesh, 0);
  Require(render() == std::array<int, 4>{0, 0, 0, 255},
      "unbound draw without illumination must be opaque black");
  Require(center_depth() < 1, "unbound back face must write depth");
  world.SetMeshTopology(mesh, {0, 1, 2});
  direct = 1;
  const auto lit = render();
  Near(lit[0], expected(mesh_color.x, 0, 1), "display colour dielectric R");
  Near(lit[1], expected(mesh_color.y, 0, 1), "display colour dielectric G");
  Near(lit[2], expected(mesh_color.z, 0, 1), "display colour dielectric B");
  const auto display_resident = renderer->statistics();
  world.SetMeshColor(mesh, {0.2F, 0.4F, 0.7F});
  const auto recolored = render();
  Near(recolored[0], expected(0.2F, 0, 1), "display colour edit R");
  Near(recolored[1], expected(0.4F, 0, 1), "display colour edit G");
  Near(recolored[2], expected(0.7F, 0, 1), "display colour edit B");
  key_direction = {0, 0, -1};
  Require(render() == std::array<int, 4>{0, 0, 0, 255}, "display colour must respond to light direction");
  direct = 0;
  ambient = 1;
  const auto indirect = render();
  Near(indirect[0], 0.2F * 0.96F * 0.25F, "display colour ambient R");
  Near(indirect[1], 0.4F * 0.96F * 0.25F, "display colour ambient G");
  Near(indirect[2], 0.7F * 0.96F * 0.25F, "display colour ambient B");
  const auto display_edited = renderer->statistics();
  Require(display_edited.point_uploads == display_resident.point_uploads &&
              display_edited.topology_uploads == display_resident.topology_uploads &&
              display_edited.material_writes == display_resident.material_writes &&
              display_edited.texture_uploads == display_resident.texture_uploads &&
              display_edited.pipelines_created == display_resident.pipelines_created,
      "display colour and light edits must retain geometry, materials, textures and pipelines");
  Require(renderer->statistics().validation_message_count == 0, "validation error");
  std::cout << "preview_surface samples=" << samples << " constants, textures, UV transforms, GGX, Mask, Blend and slot edits passed\n";
  return true;
}
} // namespace
int main(int argc, char** argv) {
  try {
    if (argc != 2 && !(argc == 4 && std::string_view(argv[2]) == "--backend"))
      return 2;
    const std::string directory = argv[1];
    if (argc == 4 && std::string_view(argv[3]) == "webgpu") {
#if defined(TOON_HAS_WEBGPU)
      const auto shaders = Toon::WebGpuShadersIn(directory);
      const Factory create = [&](Toon::FrameStatus& status, std::string& error,
                                 const Toon::RenderOptions& options) {
        return Toon::CreateWebGpuOffscreenRenderer(shaders, status, error, options);
      };
      // One sample until the backend's stage 2 brings 4x.
      return Run(create, 1) && Normals(create, 1) ? 0 : 77;
#else
      return 2;
#endif
    }
    const auto shaders = Toon::SceneShadersIn(directory);
    const Factory create = [&](Toon::FrameStatus& status, std::string& error,
                               const Toon::RenderOptions& options) {
      return Toon::CreateOffscreenRenderer(shaders, status, error, options);
    };
    return Run(create, 1) && Run(create, 4) && Normals(create, 1) && Normals(create, 4) ? 0 : 77;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
