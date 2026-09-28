// SPDX-License-Identifier: Apache-2.0
#include "camera.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Toon::viewport {
namespace {

constexpr float kPi = 3.14159265358979F;
// Vertical field of view.
constexpr float kFieldOfView = 30.0F * kPi / 180.0F;
constexpr float kOrbitPerPixel = 0.008F;
constexpr float kDollyPerStep = 0.9F;
constexpr float kPitchLimit = 89.0F * kPi / 180.0F;

Float3 Add(const Float3& a, const Float3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Float3 Subtract(const Float3& a, const Float3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Float3 Scale(const Float3& a, float s) {
  return {a.x * s, a.y * s, a.z * s};
}

float Dot(const Float3& a, const Float3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Float3 Cross(const Float3& a, const Float3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x};
}

Float3 Normalize(const Float3& a) {
  const float length = std::sqrt(Dot(a, a));
  return length > 0.0F ? Scale(a, 1.0F / length) : a;
}

Float3 TransformPoint(const Matrix4& matrix, const Float3& p) {
  const auto& m = matrix.m;
  return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
      m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
      m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}

Float3 UpVector(UpAxis up) {
  return up == UpAxis::Z ? Float3{0.0F, 0.0F, 1.0F} : Float3{0.0F, 1.0F, 0.0F};
}

// From the target toward the eye, one unit long.
Float3 Offset(UpAxis up, float yaw, float pitch) {
  const float flat = std::cos(pitch);
  if (up == UpAxis::Z) {
    return {flat * std::sin(yaw), -flat * std::cos(yaw), std::sin(pitch)};
  }
  return {flat * std::sin(yaw), std::sin(pitch), flat * std::cos(yaw)};
}

} // namespace

void Bounds::Add(const Float3& point) {
  if (empty) {
    min = point;
    max = point;
    empty = false;
    return;
  }
  min = {std::min(min.x, point.x), std::min(min.y, point.y),
      std::min(min.z, point.z)};
  max = {std::max(max.x, point.x), std::max(max.y, point.y),
      std::max(max.z, point.z)};
}

Bounds SceneBounds(const FrameSnapshot& snapshot) {
  Bounds bounds;
  for (const MeshSnapshot& mesh : snapshot.meshes) {
    if (!mesh.visible || mesh.points == nullptr) {
      continue;
    }
    // Bind space is the skeleton's at the bind pose.
    const Matrix4 to_world = IsSkinned(mesh)
                                 ? Multiply(mesh.transform,
                                       Multiply(mesh.skeleton_to_mesh,
                                           mesh.geom_bind))
                                 : mesh.transform;
    for (const Float3& point : *mesh.points) {
      const Float3 world = TransformPoint(to_world, point);
      if (std::isfinite(world.x) && std::isfinite(world.y) &&
          std::isfinite(world.z)) {
        bounds.Add(world);
      }
    }
  }
  return bounds;
}

OrbitCamera::OrbitCamera(UpAxis up) : up_(up) {
  Frame({});
}

void OrbitCamera::Frame(const Bounds& bounds) {
  Float3 center;
  float radius = 1.0F;
  if (!bounds.empty) {
    center = Scale(Add(bounds.min, bounds.max), 0.5F);
    radius = std::sqrt(Dot(Subtract(bounds.max, center),
        Subtract(bounds.max, center)));
    if (!(radius > 1e-6F)) {
      radius = 1.0F;
    }
  }
  radius_ = radius;
  // A sphere of the bounds' radius fits the vertical field of view, with a
  // little room around it.
  framed_ = {center, 1.1F * radius / std::sin(kFieldOfView * 0.5F), 0.0F,
      0.0F};
  pose_ = framed_;
}

void OrbitCamera::Reset() {
  pose_ = framed_;
}

void OrbitCamera::Orbit(float dx, float dy) {
  pose_.yaw -= dx * kOrbitPerPixel;
  pose_.pitch = std::clamp(pose_.pitch + dy * kOrbitPerPixel, -kPitchLimit,
      kPitchLimit);
}

void OrbitCamera::Pan(float dx, float dy, float height) {
  if (!(height > 0.0F)) {
    return;
  }
  const Float3 forward = Scale(Offset(up_, pose_.yaw, pose_.pitch), -1.0F);
  const Float3 right = Normalize(Cross(forward, UpVector(up_)));
  const Float3 up = Cross(right, forward);
  // World units per pixel at the target's depth.
  const float scale =
      2.0F * pose_.distance * std::tan(kFieldOfView * 0.5F) / height;
  pose_.target = Add(pose_.target,
      Add(Scale(right, -dx * scale), Scale(up, dy * scale)));
}

void OrbitCamera::Dolly(float steps) {
  pose_.distance = std::clamp(pose_.distance * std::pow(kDollyPerStep, steps),
      radius_ * 1e-3F, radius_ * 1e3F);
}

ToonView OrbitCamera::View(float aspect) const {
  const Float3 eye =
      Add(pose_.target, Scale(Offset(up_, pose_.yaw, pose_.pitch),
                            pose_.distance));
  const Float3 f = Normalize(Subtract(pose_.target, eye));
  const Float3 s = Normalize(Cross(f, UpVector(up_)));
  const Float3 u = Cross(s, f);

  ToonView view;
  auto& v = view.view.m;
  v = {s.x, u.x, -f.x, 0.0F,
      s.y, u.y, -f.y, 0.0F,
      s.z, u.z, -f.z, 0.0F,
      -Dot(s, eye), -Dot(u, eye), Dot(f, eye), 1.0F};

  // The clip planes hold the framed scene's sphere, with room, wherever the
  // eye has gone.
  const Float3 to_scene = Subtract(framed_.target, eye);
  const float scene_distance = std::sqrt(Dot(to_scene, to_scene));
  const float near_plane =
      std::max(scene_distance - 2.0F * radius_, radius_ * 1e-3F);
  const float far_plane =
      std::max(scene_distance + 2.0F * radius_, near_plane * 2.0F);
  const float focal = 1.0F / std::tan(kFieldOfView * 0.5F);
  auto& p = view.projection.m;
  p = {focal / std::max(aspect, 1e-6F), 0.0F, 0.0F, 0.0F,
      0.0F, focal, 0.0F, 0.0F,
      0.0F, 0.0F, (far_plane + near_plane) / (near_plane - far_plane), -1.0F,
      0.0F, 0.0F, 2.0F * far_plane * near_plane / (near_plane - far_plane),
      0.0F};
  return view;
}

} // namespace Toon::viewport
