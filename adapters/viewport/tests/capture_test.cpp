// SPDX-License-Identifier: Apache-2.0
#include "../src/capture.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}

int Run(const std::string& output_path) {
  try {
    Toon::ColorProduct image;
    image.width = 256;
    image.height = 3;
    image.row_pitch = image.width * 4 + 7; // Padding must never become pixels.
    image.pixel_format = "rgba8-unorm";
    image.origin = "top-left";
    image.color_space = image.transfer_function = "linear";
    image.alpha_convention = "premultiplied";
    image.payload.resize(image.row_pitch * image.height, 0xef);
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const auto alpha = static_cast<std::uint8_t>(x);
      const auto at = x * 4;
      image.payload[at] = alpha / 4;
      image.payload[at + 1] = alpha / 2;
      image.payload[at + 2] = alpha;
      image.payload[at + 3] = alpha;
      image.payload[image.row_pitch + at] = x ? 1 : 255;
      image.payload[image.row_pitch + at + 1] = 0;
      image.payload[image.row_pitch + at + 2] = x ? alpha : 255;
      image.payload[image.row_pitch + at + 3] = alpha;
      const auto last = image.row_pitch * 2 + at;
      image.payload[last] = 1;
      image.payload[last + 1] = 64;
      image.payload[last + 2] = 255;
      image.payload[last + 3] = 255;
    }
    const auto original = image.payload;
    Toon::viewport::WriteForegroundPng(output_path, image);
    Require(image.payload == original, "export mutated the render product");
    const auto rejected = [&](const Toon::ColorProduct& candidate) {
      try { Toon::viewport::WriteForegroundPng(output_path, candidate); }
      catch (const std::invalid_argument&) { return; }
      throw std::runtime_error("accepted invalid foreground metadata or extent");
    };
    auto invalid = image;
    invalid.origin = "bottom-left";
    rejected(invalid);
    invalid = image;
    invalid.color_space = "as-presented";
    rejected(invalid);
    invalid = image;
    invalid.transfer_function = "srgb";
    rejected(invalid);
    invalid = image;
    invalid.alpha_convention = "straight";
    rejected(invalid);
    invalid = image;
    invalid.pixel_format = "bgra8-unorm";
    rejected(invalid);
    invalid = image;
    invalid.width = 0;
    rejected(invalid);
    invalid = image;
    invalid.row_pitch = 1;
    rejected(invalid);
    invalid = image;
    invalid.payload.resize(5);
    rejected(invalid);
    bool refused = false;
    try { Toon::viewport::WriteForegroundPng(output_path + "/missing.png", image); }
    catch (const std::runtime_error&) { refused = true; }
    Require(refused, "failed output was reported as successful");
    std::cout << "PNG export metadata, padding, source isolation and failures passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 1;
  const auto utf8 = std::filesystem::path(argv[1]).u8string();
  return Run(std::string(utf8.begin(), utf8.end()));
}
#else
int main(int argc, char** argv) {
  return argc == 2 ? Run(argv[1]) : 1;
}
#endif
