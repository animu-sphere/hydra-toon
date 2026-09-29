// SPDX-License-Identifier: Apache-2.0
// A 2D overlay a present session draws over the presented image: the
// viewport's measurements and debug controls. Plain data, so no UI library's
// type reaches the backend; the viewport converts Dear ImGui's draw data
// into it.
#pragma once

#include <cstdint>
#include <vector>

namespace Toon {

struct OverlayVertex {
  // Framebuffer pixels from the top left.
  float x = 0.0F;
  float y = 0.0F;
  // Into the command's texture.
  float u = 0.0F;
  float v = 0.0F;
  // sRGB-encoded RGBA8, red in the lowest byte, straight alpha.
  std::uint32_t color = 0;
};
static_assert(sizeof(OverlayVertex) == 20);

// RGBA8 pixels, straight alpha, rows from the top. The backend uploads a
// texture again only when its revision changes.
struct OverlayTexture {
  std::uint64_t id = 0;
  std::uint64_t revision = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> pixels;
};

// One indexed draw: `index_count` indices from `first_index`, each offset by
// `vertex_offset`, textured by the texture with id `texture`, and cut to
// `clip`: min x, min y, max x, max y in framebuffer pixels.
struct OverlayCommand {
  std::uint32_t first_index = 0;
  std::uint32_t index_count = 0;
  std::uint32_t vertex_offset = 0;
  std::uint64_t texture = 0;
  float clip[4] = {};
};

// Drawn in order, source over, after the scene and after any capture is
// copied, so a screenshot never shows it. A texture that has left
// `textures` is released; a command whose texture is not there is skipped.
struct OverlayDrawList {
  std::vector<OverlayVertex> vertices;
  std::vector<std::uint16_t> indices;
  std::vector<OverlayCommand> commands;
  std::vector<OverlayTexture> textures;
};

} // namespace Toon
