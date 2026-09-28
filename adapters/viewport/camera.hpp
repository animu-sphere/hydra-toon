// SPDX-License-Identifier: Apache-2.0
// The viewport's own camera: it orbits, pans and dollies around a target and
// frames the scene, whatever camera the scene has.
#pragma once

#include <toon/render_world.hpp>

namespace Toon::viewport {

// A box in world space. Empty until a point is added.
struct Bounds {
  Float3 min;
  Float3 max;
  bool empty = true;

  void Add(const Float3& point);
};

// The visible meshes' points in world space. A skinned mesh is placed in
// its bind pose, which is near enough to frame.
[[nodiscard]] Bounds SceneBounds(const FrameSnapshot& snapshot);

// Which world axis is up, as UsdGeom's upAxis states it.
enum class UpAxis { Y, Z };

class OrbitCamera {
public:
  explicit OrbitCamera(UpAxis up = UpAxis::Y);

  // Looks at the bounds from the front (+Z with Y up, -Y with Z up) at a
  // distance that fits them, and remembers that as what Reset returns to.
  // Empty bounds frame the unit cube around the origin.
  void Frame(const Bounds& bounds);
  void Reset();

  // Turns around the target by a drag of this many pixels.
  void Orbit(float dx, float dy);
  // Moves the target with the cursor: a drag of this many pixels in a view
  // `height` pixels tall.
  void Pan(float dx, float dy, float height);
  // Moves toward the target by `steps` wheel notches; negative moves away.
  void Dolly(float steps);

  // The camera for a view of this width over height.
  [[nodiscard]] ToonView View(float aspect) const;

private:
  struct Pose {
    Float3 target;
    float distance = 3.0F;
    float yaw = 0.0F;
    float pitch = 0.0F;
  };

  UpAxis up_;
  Pose pose_;
  Pose framed_;
  // The framed scene's radius, which keeps the clip planes around it.
  float radius_ = 1.0F;
};

} // namespace Toon::viewport
