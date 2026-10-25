// SPDX-License-Identifier: Apache-2.0
#include "compare.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace toon_tessera {
namespace {

double Encode(double linear) {
  linear = std::clamp(linear, 0.0, 1.0);
  return linear <= 0.0031308 ? linear * 12.92
                             : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

std::uint32_t Quantize(double encoded) {
  return static_cast<std::uint32_t>(std::floor(encoded * 255.0 + 0.5));
}

} // namespace

Comparison CompareComposite(const std::vector<std::uint8_t>& presented,
    std::uint32_t width, std::uint32_t height, bool bgra,
    const Toon::ColorProduct& product, const Toon::Float3& background,
    const PixelRect& excluded) {
  if (product.width != width || product.height != height ||
      product.payload.size() < static_cast<std::size_t>(product.row_pitch) * height ||
      presented.size() < static_cast<std::size_t>(width) * height * 4) {
    throw std::runtime_error("the presented image and the product differ in extent");
  }
  const float clear[3] = {background.x, background.y, background.z};
  Comparison result;
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      if (x >= excluded.x0 && x < excluded.x1 && y >= excluded.y0 && y < excluded.y1) {
        continue;
      }
      const std::uint8_t* source =
          product.payload.data() + static_cast<std::size_t>(y) * product.row_pitch + x * 4;
      const std::uint8_t* shown =
          presented.data() + (static_cast<std::size_t>(y) * width + x) * 4;
      const double alpha = source[3] / 255.0;
      bool exact = true;
      for (std::uint32_t c = 0; c < 3; ++c) {
        // Premultiplied source-over in linear light, as Tessera blends.
        const double linear = source[c] / 255.0 + clear[c] * (1.0 - alpha);
        const std::uint32_t expected = Quantize(Encode(linear));
        const std::uint32_t actual = shown[bgra ? 2 - c : c];
        const std::uint32_t difference =
            expected > actual ? expected - actual : actual - expected;
        if (difference != 0) exact = false;
        if (difference > result.max_difference) {
          result.max_difference = difference;
          result.x = x;
          result.y = y;
          result.channel = c;
        }
      }
      ++result.pixels;
      if (exact) ++result.exact;
    }
  }
  return result;
}

void WritePpm(const std::string& path, const std::vector<std::uint8_t>& presented,
    std::uint32_t width, std::uint32_t height, bool bgra) {
  std::ofstream out(path, std::ios::binary);
  out << "P6\n" << width << ' ' << height << "\n255\n";
  for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
    const std::uint8_t* p = presented.data() + i * 4;
    const char rgb[3] = {static_cast<char>(p[bgra ? 2 : 0]), static_cast<char>(p[1]),
        static_cast<char>(p[bgra ? 0 : 2])};
    out.write(rgb, 3);
  }
  if (!out) throw std::runtime_error("cannot write " + path);
}

} // namespace toon_tessera
