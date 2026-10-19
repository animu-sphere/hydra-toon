// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <toon/vulkan_present.hpp>
#include <cstdint>
#include <string>

namespace Toon::viewport {

void WritePpm(const std::string& path, const ColorProduct& color);
void WriteCamera(const std::string& path, const ToonView& view,
    std::uint32_t width, std::uint32_t height);

// Export a top-left premultiplied linear-sRGB RGBA8 product as straight sRGB PNG.
void WriteForegroundPng(const std::string& path, const ColorProduct& color);

} // namespace Toon::viewport
