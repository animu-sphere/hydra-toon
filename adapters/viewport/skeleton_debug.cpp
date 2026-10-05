// SPDX-License-Identifier: Apache-2.0
#include "skeleton_debug.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace Toon::viewport {
namespace {
using ClipPoint = std::array<double, 4>;
ClipPoint Clip(const ToonView& view, Float3 point) {
  const auto matrix = Multiply(view.projection, view.view);
  ClipPoint result{};
  for (std::size_t row = 0; row < 4; ++row) {
    result[row] = static_cast<double>(matrix.m[row]) * point.x +
        static_cast<double>(matrix.m[row + 4]) * point.y +
        static_cast<double>(matrix.m[row + 8]) * point.z + matrix.m[row + 12];
  }
  return result;
}
bool Finite(const ClipPoint& point) {
  return std::all_of(point.begin(), point.end(), [](double v) { return std::isfinite(v); });
}
std::array<double, 6> Planes(const ClipPoint& p) {
  return {p[3] + p[0], p[3] - p[0], p[3] + p[1], p[3] - p[1],
      p[3] + p[2], p[3] - p[2]};
}
bool Extent(float width, float height) {
  return std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0;
}
Float2 Pixel(const ClipPoint& p, float width, float height) {
  return {static_cast<float>(std::clamp((p[0] / p[3] + 1) * 0.5, 0.0, 1.0) * width),
      static_cast<float>(std::clamp((1 - p[1] / p[3]) * 0.5, 0.0, 1.0) * height)};
}
} // namespace

std::optional<Float2> ProjectJoint(const ToonView& view, Float3 point, float width, float height) {
  if (!Extent(width, height)) return std::nullopt;
  const auto clip = Clip(view, point);
  if (!Finite(clip) || clip[3] <= 0) return std::nullopt;
  for (double plane : Planes(clip)) if (plane < 0) return std::nullopt;
  return Pixel(clip, width, height);
}

std::optional<BoneSegment> ProjectBone(const ToonView& view, Float3 start, Float3 end,
    float width, float height) {
  if (!Extent(width, height)) return std::nullopt;
  const auto a = Clip(view, start), b = Clip(view, end);
  if (!Finite(a) || !Finite(b)) return std::nullopt;
  const auto pa = Planes(a), pb = Planes(b);
  double first = 0, last = 1;
  for (std::size_t i = 0; i < pa.size(); ++i) {
    if (pa[i] < 0 && pb[i] < 0) return std::nullopt;
    if (pa[i] < 0) first = std::max(first, pa[i] / (pa[i] - pb[i]));
    if (pb[i] < 0) last = std::min(last, pa[i] / (pa[i] - pb[i]));
  }
  if (first > last) return std::nullopt;
  ClipPoint clipped_a{}, clipped_b{};
  for (std::size_t i = 0; i < 4; ++i) {
    clipped_a[i] = a[i] + first * (b[i] - a[i]);
    clipped_b[i] = a[i] + last * (b[i] - a[i]);
  }
  if (clipped_a[3] <= 0 || clipped_b[3] <= 0) return std::nullopt;
  return BoneSegment{Pixel(clipped_a, width, height), Pixel(clipped_b, width, height)};
}
} // namespace Toon::viewport
