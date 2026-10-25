// SPDX-License-Identifier: Apache-2.0
// The Tessera host's smoke comparison (GPU handoff policy §10): a presented
// swapchain image against the CPU composite of the same frame's product,
// source-over in linear light onto the host's clear colour and encoded as
// the sRGB swapchain encodes it. The panel's pixels are left out.
#pragma once

#include <toon/backend.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace toon_tessera {

// Pixels [x0, x1) x [y0, y1).
struct PixelRect {
  std::uint32_t x0 = 0;
  std::uint32_t y0 = 0;
  std::uint32_t x1 = 0;
  std::uint32_t y1 = 0;
};

struct Comparison {
  std::uint64_t pixels = 0;
  // The largest absolute difference of an 8-bit sRGB channel, and where.
  std::uint32_t max_difference = 0;
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t channel = 0;
  // Pixels whose channels all match exactly.
  std::uint64_t exact = 0;
};

// `presented` is tightly packed 8-bit sRGB, BGRA when `bgra`, top row first.
// `background` is the clear colour in linear RGB.
[[nodiscard]] Comparison CompareComposite(const std::vector<std::uint8_t>& presented,
    std::uint32_t width, std::uint32_t height, bool bgra,
    const Toon::ColorProduct& product, const Toon::Float3& background,
    const PixelRect& excluded);

// A binary PPM of the presented image, for inspection.
void WritePpm(const std::string& path, const std::vector<std::uint8_t>& presented,
    std::uint32_t width, std::uint32_t height, bool bgra);

} // namespace toon_tessera
