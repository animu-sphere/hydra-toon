// SPDX-License-Identifier: Apache-2.0
#include <toon/extraction.hpp>

namespace Toon {

void ExtractDrawList(const FrameSnapshot& snapshot, DrawList& draws) {
  draws.inputs = snapshot.inputs;
  draws.source_revision = snapshot.revision;
  draws.view = snapshot.view;
  draws.view_revision = snapshot.view_revision;
  draws.meters_per_unit = snapshot.meters_per_unit;
  draws.time_seconds = snapshot.time_seconds;
  draws.lights = snapshot.lights;
  draws.draws.clear();
  draws.triangle_count = 0;
  for (const MeshSnapshot& mesh : snapshot.meshes) {
    // An index past the last point would read outside the vertex buffer;
    // such a mesh waits for points or topology that agree.
    if (!mesh.visible || mesh.indices == nullptr || mesh.indices->empty() ||
        mesh.points == nullptr || mesh.points->size() < mesh.index_bound) {
      continue;
    }
    draws.draws.push_back(mesh);
    draws.triangle_count += mesh.indices->size() / 3U;
  }
  draws.materials = snapshot.materials;
  draws.textures = snapshot.textures;
}

DrawList ExtractDrawList(const FrameSnapshot& snapshot) {
  DrawList draws;
  ExtractDrawList(snapshot, draws);
  return draws;
}

} // namespace Toon
