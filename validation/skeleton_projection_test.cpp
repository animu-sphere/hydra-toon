// SPDX-License-Identifier: Apache-2.0
#include "../adapters/viewport/skeleton_debug.hpp"
#include "../adapters/viewport/camera.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool Near(float a, float b) { return std::abs(a - b) < 0.001F; }
}
int main() {
  try {
    using namespace Toon::viewport;
    Toon::ToonView view;
    const auto center = ProjectJoint(view, {0,0,0}, 800, 600);
    Require(center && Near(center->x, 400) && Near(center->y, 300), "center projection");
    const auto corner = ProjectJoint(view, {-1,1,0}, 800, 600);
    Require(corner && Near(corner->x, 0) && Near(corner->y, 0), "top-left convention");
    Require(!ProjectJoint(view, {0,0,2}, 800,600) && !ProjectJoint(view, {0,0,-2}, 800,600) &&
        !ProjectJoint(view, {2,0,0}, 800,600) && !ProjectJoint(view, {0,2,0}, 800,600), "six-plane clipping");
    const auto across = ProjectBone(view, {-2,0,0}, {2,0,0}, 800,600);
    Require(across && Near(across->start.x, 0) && Near(across->end.x, 800), "side-plane segment clipping");
    Require(!ProjectBone(view, {0,2,0}, {1,2,0}, 800,600), "outside segment retained");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Require(!ProjectJoint(view, {nan,0,0}, 800,600) && !ProjectBone(view, {0,0,0}, {nan,0,0}, 800,600) &&
        !ProjectJoint(view, {}, 0,600) && !ProjectBone(view, {}, {}, nan,600), "invalid input retained");
    // OpenGL perspective, near=1, far=10. One endpoint behind the camera.
    view.projection.m = {1,0,0,0, 0,1,0,0, 0,0,-11.0F/9,-1, 0,0,-20.0F/9,0};
    Require(!ProjectJoint(view, {0,0,1}, 800,600), "behind-camera joint retained");
    const auto crossing = ProjectBone(view, {0,0,1}, {0.5F,0,-2}, 800,600);
    Require(crossing && Near(crossing->start.x, 533.3333F) && Near(crossing->end.x, 500), "near-plane crossing");
    Require(!ProjectBone(view, {0,0,1}, {0,0,2}, 800,600), "behind-camera bone retained");
    // The viewport camera and projection agree after orbit/pan and for Z up.
    for (const auto up : {UpAxis::Y, UpAxis::Z}) {
      OrbitCamera camera(up);
      const auto origin = ProjectJoint(camera.View(4.0F/3), {}, 800,600);
      Require(origin && Near(origin->x,400) && Near(origin->y,300), "camera framing");
      camera.Orbit(30,20); camera.Pan(15,10,600);
      Require(ProjectJoint(camera.View(4.0F/3), {},800,600).has_value(), "camera movement");
    }
    std::cout << "Skeleton projection PASS\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
