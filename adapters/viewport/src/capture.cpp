// SPDX-License-Identifier: Apache-2.0
#include "capture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <stdexcept>

// Keep upstream warnings confined to the vendored implementation.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../../third_party/stb/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Toon::viewport {
namespace {

std::filesystem::path OutputPath(const std::string& path) {
  return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

void ValidateRgba(const ColorProduct& color) {
  const auto row = static_cast<std::uint64_t>(color.width) * 4U;
  const auto extent = color.height == 0 ? 0 :
      static_cast<std::uint64_t>(color.height - 1U) * color.row_pitch + row;
  if (color.width == 0 || color.height == 0 || color.pixel_format != "rgba8-unorm" ||
      color.origin != "top-left" || color.row_pitch < row || extent > color.payload.size()) {
    throw std::invalid_argument("capture needs a complete top-left RGBA8 product");
  }
}

std::uint8_t EncodeSrgb(float linear) {
  const float value = std::clamp(linear, 0.0F, 1.0F);
  const float encoded = value <= 0.0031308F ? value * 12.92F :
      1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
  return static_cast<std::uint8_t>(std::lround(encoded * 255.0F));
}

} // namespace

// A binary PPM: RGB, first row at the top, as the capture is laid out.
void WritePpm(const std::string& path, const Toon::ColorProduct& color) {
  ValidateRgba(color);
  std::ofstream output(OutputPath(path), std::ios::binary);
  output << "P6\n"
         << color.width << ' ' << color.height << "\n255\n";
  for (std::uint32_t y = 0; y < color.height; ++y) {
    for (std::uint32_t x = 0; x < color.width; ++x) {
      const auto pixel = static_cast<std::size_t>(y) * color.row_pitch + x * 4U;
      output.write(reinterpret_cast<const char*>(&color.payload[pixel]), 3);
    }
  }
  output.close();
  if (!output) {
    throw std::runtime_error("could not write the screenshot " + path);
  }
}

void WriteForegroundPng(const std::string& path, const ColorProduct& color) {
  ValidateRgba(color);
  if (color.color_space != "linear" || color.transfer_function != "linear" ||
      color.alpha_convention != "premultiplied") {
    throw std::invalid_argument("foreground PNG needs premultiplied linear-sRGB RGBA");
  }
  const auto row = static_cast<std::uint64_t>(color.width) * 4U;
  // stb's encoder uses signed int sizes, including filtered rows and zlib storage.
  if ((row + 1U) * color.height > static_cast<std::uint64_t>(std::numeric_limits<int>::max() / 2)) {
    throw std::invalid_argument("foreground PNG exceeds encoder size limit");
  }
  std::vector<std::uint8_t> straight(static_cast<std::size_t>(row * color.height));
  for (std::uint32_t y = 0; y < color.height; ++y) {
    for (std::uint32_t x = 0; x < color.width; ++x) {
      const auto from = static_cast<std::size_t>(y) * color.row_pitch + x * 4U;
      const auto to = static_cast<std::size_t>(y * row) + x * 4U;
      const auto alpha = color.payload[from + 3U];
      for (std::size_t channel = 0; channel < 3; ++channel) {
        // Unpremultiply in linear space, then encode RGB; alpha stays linear.
        straight[to + channel] = alpha == 0 ? 0 :
            EncodeSrgb(static_cast<float>(color.payload[from + channel]) / alpha);
      }
      straight[to + 3U] = alpha;
    }
  }
  int size = 0;
  std::unique_ptr<unsigned char, decltype(&std::free)> png(
      stbi_write_png_to_mem(straight.data(), static_cast<int>(row),
          static_cast<int>(color.width), static_cast<int>(color.height), 4, &size),
      &std::free);
  if (!png || size < 33) throw std::runtime_error("could not encode foreground PNG");
  std::ofstream output(OutputPath(path), std::ios::binary);
  // PNG signature + IHDR, then the sRGB chunk: perceptual intent 0, with CRC.
  constexpr std::array<std::uint8_t, 13> srgb{
      0, 0, 0, 1, 's', 'R', 'G', 'B', 0, 0xae, 0xce, 0x1c, 0xe9};
  output.write(reinterpret_cast<const char*>(png.get()), 33);
  output.write(reinterpret_cast<const char*>(srgb.data()), srgb.size());
  output.write(reinterpret_cast<const char*>(png.get() + 33), size - 33);
  output.close();
  if (!output) throw std::runtime_error("could not write foreground PNG " + path);
}

void WriteCamera(const std::string& path, const Toon::ToonView& view,
    std::uint32_t width, std::uint32_t height) {
  std::ofstream output(OutputPath(path));
  output << std::setprecision(9) << "{\"width\":" << width
         << ",\"height\":" << height << ",\"view\":[";
  for (std::size_t i = 0; i < 16; ++i) {
    output << (i ? "," : "") << view.view.m[i];
  }
  output << "],\"projection\":[";
  for (std::size_t i = 0; i < 16; ++i) {
    output << (i ? "," : "") << view.projection.m[i];
  }
  output << "]}\n";
  output.close();
  if (!output) throw std::runtime_error("could not write camera " + path);
}

} // namespace Toon::viewport
