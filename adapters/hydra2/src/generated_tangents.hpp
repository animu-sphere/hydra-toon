// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <toon/render_world.hpp>

namespace Toon::detail {

struct GeneratedTangents {
  std::vector<Float4> tangents;
  // Empty when the original vertex layout suffices. Otherwise maps every
  // output vertex to its original vertex, including appended seam vertices.
  std::vector<std::uint32_t> vertices;
  std::vector<std::uint32_t> indices;
};

GeneratedTangents GenerateTangents(const std::vector<Float3>& points,
    const std::vector<Float3>& normals, const std::vector<Float2>& uvs,
    const std::vector<std::uint32_t>& indices);

} // namespace Toon::detail
