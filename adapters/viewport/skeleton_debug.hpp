// SPDX-License-Identifier: Apache-2.0
// Viewport-only diagnostic values. No skeleton semantics enter the renderer.
#pragma once

#include <toon/render_world.hpp>
#include <optional>
#include <string>
#include <vector>

namespace Toon::viewport {

struct JointDebug {
  std::string name;
  int parent = -1;
  Float3 world;
};

struct SkeletonDebug {
  std::string path;
  std::vector<JointDebug> joints;
  std::string error;
};

// OpenGL/USD clip convention, converted to top-left screen pixels.
// Non-finite values and points outside any of the six planes are omitted.
[[nodiscard]] std::optional<Float2> ProjectJoint(const ToonView& view,
    Float3 point, float width, float height);
struct BoneSegment { Float2 start; Float2 end; };
// Clip before perspective division, including bones crossing the near plane.
[[nodiscard]] std::optional<BoneSegment> ProjectBone(const ToonView& view,
    Float3 start, Float3 end, float width, float height);

} // namespace Toon::viewport
