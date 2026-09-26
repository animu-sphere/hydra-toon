// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

#include <toon/render_world.hpp>

namespace Toon {

// What a frame draws: every visible mesh whose topology its points can
// satisfy, and the camera it is seen through.
struct DrawList {
  std::uint64_t source_revision = 0;
  ToonView view;
  std::uint64_t view_revision = 0;
  std::vector<MeshSnapshot> draws;
  std::uint64_t triangle_count = 0;
};

// Fills `draws`, reusing its storage.
void ExtractDrawList(const FrameSnapshot& snapshot, DrawList& draws);
[[nodiscard]] DrawList ExtractDrawList(const FrameSnapshot& snapshot);

} // namespace Toon
