// SPDX-License-Identifier: Apache-2.0
#include <toon/extraction.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Toon {
namespace {
bool Affine(const Matrix4& matrix) {
  return std::all_of(matrix.m.begin(), matrix.m.end(),
             [](float value) { return std::isfinite(value); }) &&
         matrix.m[3] == 0 && matrix.m[7] == 0 && matrix.m[11] == 0 &&
         matrix.m[15] == 1;
}

// Boxes use double arithmetic; the margin below also covers the shader's
// single-precision matrix/weighted-sum roundoff at clip boundaries.
template <class Box>
void Include(Box& box, const std::array<double, 3>& point) {
  if (!box.valid) {
    box.low = box.high = point;
    box.valid = true;
  } else {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      box.low[axis] = std::min(box.low[axis], point[axis]);
      box.high[axis] = std::max(box.high[axis], point[axis]);
    }
  }
}

template <class Box>
Box Transform(const Box& box, const Matrix4& matrix) {
  Box result;
  for (unsigned corner = 0; corner < 8; ++corner) {
    std::array<double, 3> point{};
    for (std::size_t row = 0; row < 3; ++row) {
      point[row] = matrix.m[12 + row];
      for (std::size_t axis = 0; axis < 3; ++axis) {
        point[row] += matrix.m[4 * axis + row] *
                      ((corner & (1U << axis)) ? box.high[axis] : box.low[axis]);
      }
    }
    // Enclose shader float arithmetic as well as the exact affine result.
    auto low = point;
    auto high = point;
    for (std::size_t row = 0; row < 3; ++row) {
      double magnitude = std::abs(double(matrix.m[12 + row]));
      for (std::size_t axis = 0; axis < 3; ++axis)
        magnitude += std::abs(double(matrix.m[4 * axis + row])) *
                     std::max(std::abs(box.low[axis]), std::abs(box.high[axis]));
      const double margin = 16 * std::numeric_limits<float>::epsilon() * magnitude;
      low[row] -= margin;
      high[row] += margin;
    }
    Include(result, low);
    Include(result, high);
  }
  return result;
}
} // namespace

void OutlineBounds::Update(const MeshSnapshot& mesh) {
  if (points_revision_ == mesh.points_revision &&
      topology_revision_ == mesh.topology_revision &&
      skin_revision_ == mesh.skin_revision)
    return;
  points_revision_ = mesh.points_revision;
  topology_revision_ = mesh.topology_revision;
  skin_revision_ = mesh.skin_revision;
  rest_ = {};
  joints_.clear();
  valid_skin_ = false;
  if (!mesh.points || !mesh.indices || mesh.indices->empty())
    return;
  for (const auto index : *mesh.indices) {
    if (index >= mesh.points->size()) {
      rest_ = {};
      return;
    }
    const auto& point = (*mesh.points)[index];
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
      rest_ = {};
      return;
    }
    Include(rest_, {point.x, point.y, point.z});
  }
  // Build skin envelopes even if the current pose is incomplete: a later
  // pose can enable GPU skinning without a structural change.
  const std::size_t count = mesh.influences_per_point;
  if (!count || !mesh.influences || !Affine(mesh.geom_bind) ||
      mesh.influences->size() / count < (mesh.constant_influences ? 1U : mesh.index_bound))
    return;
  joints_.resize(mesh.joint_bound);
  min_weight_ = std::numeric_limits<double>::infinity();
  max_weight_ = 0;
  for (const auto index : *mesh.indices) {
    const auto& point = (*mesh.points)[index];
    const std::size_t first = mesh.constant_influences ? 0 : index * count;
    double sum = 0;
    for (std::size_t offset = 0; offset < count; ++offset) {
      const auto& influence = (*mesh.influences)[first + offset];
      if (!std::isfinite(influence.weight) || influence.weight < 0 ||
          influence.joint >= joints_.size())
        return;
      sum += influence.weight;
      if (influence.weight > 0)
        Include(joints_[influence.joint], {point.x, point.y, point.z});
    }
    min_weight_ = std::min(min_weight_, sum);
    max_weight_ = std::max(max_weight_, sum);
  }
  valid_skin_ = true;
}

bool OutlineBounds::OutsideView(const MeshSnapshot& mesh, const ToonView& view,
    float width, ToonOutlineWidthMode mode, float meters_per_unit) const {
  if (!rest_.valid || !std::isfinite(width) || width <= 0 ||
      !std::isfinite(meters_per_unit) || meters_per_unit <= 0 ||
      !Affine(mesh.transform) || !Affine(view.view) ||
      !std::all_of(view.projection.m.begin(), view.projection.m.end(),
          [](float value) { return std::isfinite(value); }))
    return false;
  Box bounds = rest_;
  if (IsSkinned(mesh)) {
    if (!valid_skin_ || !Affine(mesh.skeleton_to_mesh))
      return false;
    bounds = {};
    for (std::size_t joint = 0; joint < joints_.size(); ++joint) {
      if (!joints_[joint].valid)
        continue;
      if (!Affine((*mesh.joints)[joint]))
        return false;
      const auto moved = Transform(joints_[joint], Multiply((*mesh.joints)[joint], mesh.geom_bind));
      Include(bounds, moved.low);
      Include(bounds, moved.high);
    }
    // Nonnegative weights are a scaled convex combination of joint boxes.
    // Keep the scale range rather than assuming weights sum exactly to one.
    if (!bounds.valid) {
      Include(bounds, {0, 0, 0});
    } else {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto low = bounds.low[axis];
        const auto high = bounds.high[axis];
        bounds.low[axis] = std::min(low * min_weight_, low * max_weight_);
        bounds.high[axis] = std::max(high * min_weight_, high * max_weight_);
        const double roundoff = 32 * std::numeric_limits<float>::epsilon() *
                                mesh.influences_per_point * max_weight_ *
                                std::max(std::abs(low), std::abs(high));
        bounds.low[axis] -= roundoff;
        bounds.high[axis] += roundoff;
      }
    }
    bounds = Transform(bounds, mesh.skeleton_to_mesh);
  }
  const auto model_view = Multiply(view.view, mesh.transform);
  // The hull shader's normal matrix is an inverse only for nonsingular
  // transforms. Retain degenerate transforms rather than bound that fallback.
  const auto& m = model_view.m;
  const double determinant = double(m[0]) * (double(m[5]) * m[10] - double(m[9]) * m[6]) -
                             double(m[4]) * (double(m[1]) * m[10] - double(m[9]) * m[2]) +
                             double(m[8]) * (double(m[1]) * m[6] - double(m[5]) * m[2]);
  if (!std::isfinite(determinant) || std::abs(determinant) < 1e-6)
    return false;
  double matrix_norm = 0;
  double inverse_norm = 0;
  for (std::size_t row = 0; row < 3; ++row) {
    double sum = 0;
    double inverse_sum = 0;
    for (std::size_t column = 0; column < 3; ++column) {
      sum += std::abs(double(m[column * 4 + row]));
      const auto a = (column + 1) % 3;
      const auto b = (column + 2) % 3;
      const auto c = (row + 1) % 3;
      const auto d = (row + 2) % 3;
      inverse_sum += std::abs((double(m[c * 4 + a]) * m[d * 4 + b] -
                                  double(m[d * 4 + a]) * m[c * 4 + b]) /
                              determinant);
    }
    matrix_norm = std::max(matrix_norm, sum);
    inverse_norm = std::max(inverse_norm, inverse_sum);
  }
  const double condition = matrix_norm * inverse_norm;
  if (!std::isfinite(condition) || condition > 1000)
    return false;
  bounds = Transform(bounds, model_view);
  const auto& p = view.projection.m;
  double radius = double(width) / meters_per_unit;
  if (mode == ToonOutlineWidthMode::Screen) {
    double max_w = 0;
    for (unsigned corner = 0; corner < 8; ++corner) {
      double w = p[15];
      for (std::size_t axis = 0; axis < 3; ++axis)
        w += p[4 * axis + 3] * ((corner & (1U << axis)) ? bounds.high[axis] : bounds.low[axis]);
      max_w = std::max(max_w, std::abs(w));
    }
    radius = 2.0 * width * max_w / std::max(std::abs(double(p[5])), 1e-6);
  } else if (mode != ToonOutlineWidthMode::World) {
    return false;
  }
  radius *= 1 + 1e-4 * condition;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(bounds.low[axis]) || !std::isfinite(bounds.high[axis]))
      return false;
  }
  // The width image's filtered G is in [0,1], including white fallbacks.
  // Animated UVs and partially zero maps cannot exceed the full-width ball.
  // ToonView uses OpenGL clip coordinates (-w <= z <= w). The backend
  // maps z to (z + w) / 2 for Vulkan, preserving these two depth planes.
  // Clipping precedes rasterization's slope depth bias; with depth clamp
  // disabled a biased hull cannot restore a wholly clipped primitive.
  for (std::size_t row = 0; row < 3; ++row) {
    for (const double sign : {-1.0, 1.0}) {
      double maximum = p[15] + sign * p[12 + row];
      double normal_squared = 0;
      double magnitude = std::abs(maximum);
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const double coefficient = p[4 * axis + 3] + sign * p[4 * axis + row];
        maximum += coefficient * (coefficient >= 0 ? bounds.high[axis] : bounds.low[axis]);
        magnitude += std::abs(coefficient) * std::max(std::abs(bounds.low[axis]), std::abs(bounds.high[axis]));
        normal_squared += coefficient * coefficient;
      }
      const double expansion = radius * std::sqrt(normal_squared);
      const double margin = 1e-4 * std::max(1.0, magnitude + expansion);
      if (std::isfinite(maximum) && std::isfinite(expansion) && maximum + expansion < -margin)
        return true;
    }
  }
  return false;
}
} // namespace Toon
