// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

#include <toon/render_world.hpp>

namespace Toon {

// What a frame draws: every visible mesh whose topology its points can
// satisfy, the materials they bind, and the camera they are seen through.
struct DrawList {
  std::uint64_t source_revision = 0;
  ToonView view;
  std::uint64_t view_revision = 0;
  // The snapshot's unit in metres.
  float meters_per_unit = 1.0F;
  double time_seconds = 0.0;
  // Evaluation switch: omit hulls without changing material slots or uploads.
  bool outlines = true;
  std::vector<MeshSnapshot> draws;
  // Every material of the snapshot, ordered by id, so a consumer keeps one
  // parameter slot per material whether or not a draw binds it this frame.
  std::vector<MaterialSnapshot> materials;
  // Every texture of the snapshot, ordered by id, for the same reason.
  std::vector<TextureSnapshot> textures;
  std::uint64_t triangle_count = 0;
};

// Fills `draws`, reusing its storage.
void ExtractDrawList(const FrameSnapshot& snapshot, DrawList& draws);
[[nodiscard]] DrawList ExtractDrawList(const FrameSnapshot& snapshot);

} // namespace Toon
