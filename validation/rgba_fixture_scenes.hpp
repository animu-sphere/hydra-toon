// SPDX-License-Identifier: Apache-2.0
// The RGBA release fixtures' scenes (render output policy §6), shared by the
// fixture capture and the GPU handoff's lending host. Original and
// redistributable; no format plugin or model needed.
#pragma once

#include <toon/render_world.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace toon_fixtures {
inline Toon::MeshId Mesh(Toon::RenderWorld& world, const std::vector<Toon::Float3>& points,
    const std::vector<std::uint32_t>& indices, const Toon::ToonMaterial& material) {
  const auto mesh = world.CreateMesh();
  world.SetMeshPoints(mesh, points);
  world.SetMeshTopology(mesh, indices);
  std::vector<Toon::Float2> uv;
  for (const auto p : points)
    uv.push_back({(p.x + 1) * 2, (p.y + 1) * 2});
  world.SetMeshUVs(mesh, uv);
  const auto id = world.CreateMaterial();
  world.SetMaterial(id, material);
  world.SetMeshMaterial(mesh, id);
  return mesh;
}
inline Toon::MeshId Quad(Toon::RenderWorld& world, float l, float r, float b, float t,
    float z, const Toon::ToonMaterial& material) {
  return Mesh(world, {{l, b, z}, {r, b, z}, {r, t, z}, {l, t, z}}, {0, 1, 2, 0, 2, 3}, material);
}
inline void View(Toon::RenderWorld& world) {
  Toon::ToonView view;
  view.projection.m[10] = -1;
  world.SetView(view);
  const auto key = world.CreateLight();
  Toon::ToonLight light;
  light.direction = {1, 0, -1};
  world.SetLight(key, light);
}
inline Toon::TextureId Texture(Toon::RenderWorld& world, bool normal) {
  Toon::ToonTexture texture;
  texture.width = texture.height = 8;
  std::vector<std::uint8_t> pixels;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x) {
      const std::vector<std::uint8_t> texel = normal
                                                  ? std::vector<std::uint8_t>{static_cast<std::uint8_t>(x < 4 ? 70 : 185), 170, 230, 255}
                                                  : std::vector<std::uint8_t>{210, 150, 245, static_cast<std::uint8_t>(x * 255 / 7)};
      pixels.insert(pixels.end(), texel.begin(), texel.end());
    }
  texture.pixels = std::make_shared<const std::vector<std::uint8_t>>(pixels);
  const auto id = world.CreateTexture();
  world.SetTexture(id, texture);
  return id;
}
// An octahedral head and dress, thin transparent hair, textured cutout ribbon,
// an emissive badge and a normal-mapped PreviewSurface swatch in one product.
inline void Avatar(Toon::RenderWorld& world) {
  View(world);
  const auto alpha = Texture(world, false);
  const auto normal = Texture(world, true);
  Toon::ToonMaterial skin;
  skin.model = Toon::ToonShadingModel::MToon;
  skin.base_color = {0.8F, 0.45F, 0.25F};
  skin.mtoon.shade_color = {0.3F, 0.15F, 0.12F};
  skin.outline = true;
  skin.outline_width = 0.025F;
  skin.outline_color = {0.035F, 0.015F, 0.06F};
  skin.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
  Mesh(world, {{0.32F, 0.35F, 0}, {-0.32F, 0.35F, 0}, {0, 0.75F, 0}, {0, -0.05F, 0}, {0, 0.35F, 0.3F}, {0, 0.35F, -0.3F}},
      {0, 2, 4, 1, 4, 2, 0, 4, 3, 1, 3, 4, 0, 5, 2, 1, 2, 5, 0, 3, 5, 1, 5, 3}, skin);
  Mesh(world, {{-0.2F, -0.04F, 0}, {-0.48F, -0.75F, 0}, {0.48F, -0.75F, 0}, {0.2F, -0.04F, 0}, {0, -0.4F, 0.25F}, {0, -0.4F, -0.2F}},
      {0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4, 1, 0, 5, 2, 1, 5, 3, 2, 5, 0, 3, 5}, skin);
  auto hair = skin;
  hair.base_color = {0.08F, 0.28F, 0.65F};
  hair.alpha_mode = Toon::ToonAlphaMode::Blend;
  hair.alpha = 0.45F;
  hair.base_texture.texture = alpha;
  hair.double_sided = true;
  hair.outline = false;
  for (int side : {-1, 1})
    for (int strand = 0; strand < 4; ++strand) {
      const float x = side * (0.24F + 0.075F * strand);
      Mesh(world, {{x - 0.035F, 0.66F, 0.34F}, {x + 0.04F, 0.66F, 0.34F}, {x + side * 0.18F, -0.6F + strand * 0.09F, 0.34F}}, {0, 1, 2}, hair);
    }
  auto mask = skin;
  mask.alpha_mode = Toon::ToonAlphaMode::Mask;
  mask.base_texture.texture = alpha;
  mask.outline = false;
  Quad(world, -0.42F, 0.42F, -0.68F, -0.5F, 0.35F, mask);
  Toon::ToonMaterial pbr;
  pbr.preview.enabled = true;
  pbr.base_color = {0.25F, 0.6F, 0.35F};
  pbr.normal_texture.texture = normal;
  pbr.preview.inputs[5].connected = true;
  pbr.preview.inputs[5].scale = {2, 2, 2, 1};
  pbr.preview.inputs[5].bias = {-1, -1, -1, 0};
  Quad(world, 0.63F, 0.92F, -0.35F, 0.3F, 0.1F, pbr);
  pbr.normal_texture.texture = 0;
  pbr.preview.inputs[5].connected = false;
  pbr.base_color = {};
  pbr.emissive = {0.65F, 0.15F, 0.03F};
  Quad(world, -0.09F, 0.09F, -0.3F, -0.12F, 0.4F, pbr);
}
inline void Shadow(Toon::RenderWorld& world) {
  View(world);
  Toon::ToonMaterial wall;
  wall.model = Toon::ToonShadingModel::MMD;
  wall.base_color = {};
  wall.mmd.ambient_color = {1, 1, 1};
  wall.mmd.toon_source = Toon::ToonRampSource::Shared;
  wall.mmd.shared_toon_index = 0;
  wall.mmd.cast_self_shadow = wall.mmd.receive_self_shadow = true;
  Quad(world, -0.9F, 0.9F, -0.9F, 0.9F, 0, wall);
  wall.mmd.receive_self_shadow = false;
  Quad(world, -0.6F, -0.2F, -0.3F, 0.3F, 0.4F, wall);
}
} // namespace toon_fixtures
