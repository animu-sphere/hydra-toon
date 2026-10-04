// SPDX-License-Identifier: Apache-2.0
#include <toon/extraction.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Toon {
namespace {
using Point = std::array<double, 3>;
using Magnitudes = std::array<double, 16>;

bool Affine(const Matrix4& matrix) {
  return std::all_of(matrix.m.begin(), matrix.m.end(),
             [](float value) { return std::isfinite(value); }) &&
         matrix.m[3] == 0 && matrix.m[7] == 0 && matrix.m[11] == 0 && matrix.m[15] == 1;
}

Magnitudes Absolute(const Matrix4& matrix) {
  Magnitudes result{};
  std::transform(matrix.m.begin(), matrix.m.end(), result.begin(),
      [](float value) { return std::abs(double(value)); });
  return result;
}

Magnitudes Product(const Magnitudes& a, const Magnitudes& b) {
  Magnitudes result{};
  for (std::size_t column = 0; column < 4; ++column)
    for (std::size_t row = 0; row < 4; ++row)
      for (std::size_t k = 0; k < 4; ++k)
        result[4 * column + row] += a[4 * k + row] * b[4 * column + k];
  return result;
}

// Bound float arithmetic before division, including matrix composition and
// Vulkan's depth remapping. Absolute products preserve cancellation errors.
bool Project(const Point& point, const Matrix4& matrix, const Magnitudes& magnitude,
    Point& projected, double& error) {
  std::array<double, 4> clip{}, roundoff{};
  for (std::size_t row = 0; row < 4; ++row) {
    clip[row] = matrix.m[12 + row];
    double scale = magnitude[12 + row];
    for (std::size_t axis = 0; axis < 3; ++axis) {
      clip[row] += double(matrix.m[4 * axis + row]) * point[axis];
      scale += magnitude[4 * axis + row] * std::abs(point[axis]);
    }
    roundoff[row] = 256 * std::numeric_limits<float>::epsilon() * scale + 1e-7;
    if (!std::isfinite(clip[row]) || !std::isfinite(roundoff[row]))
      return false;
  }
  const double w_low = clip[3] - roundoff[3];
  if (w_low <= 1e-6)
    return false;
  error = 0;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    projected[axis] = clip[axis] / clip[3];
    error = std::max(error,
        (roundoff[axis] + std::abs(projected[axis]) * roundoff[3]) / w_low);
  }
  // A clipped depth triangle/candidate is retained rather than relying on
  // clipping interpolation. Side clipping is safe inside the original triangle.
  return projected[2] - error > -1 && projected[2] + error < 1;
}

double Cross(const Point& a, const Point& b, const Point& p) {
  return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
}
} // namespace

void OutlineOcclusion::Update(const DrawList& draws, std::uint32_t width,
    std::uint32_t height) {
  count_ = 0;
  view_ = draws.view;
  if (!width || !height || !Affine(draws.view.view))
    return;
  pixel_margin_ = 2.0 / std::min(width, height);
  const auto clip_from_world = Multiply(draws.view.projection, draws.view.view);
  const auto world_magnitude = Product(Absolute(draws.view.projection), Absolute(draws.view.view));
  std::size_t inspected = 0;
  for (const auto& mesh : draws.draws) {
    if (!mesh.visible || mesh.influences_per_point || !mesh.points || !mesh.indices ||
        mesh.indices->empty() || mesh.indices->size() > 64 * 3 || !Affine(mesh.transform))
      continue;
    const auto material = std::lower_bound(draws.materials.begin(), draws.materials.end(), mesh.material,
        [](const MaterialSnapshot& item, MaterialId id) { return item.id < id; });
    if (material != draws.materials.end() && material->id == mesh.material &&
        material->material.model == ToonShadingModel::MToon &&
        (material->material.alpha_mode != ToonAlphaMode::Opaque || !material->material.double_sided))
      continue;
    // Unlit fallback meshes are opaque and unculled, regardless of the
    // unused material parameters. MToon must be Opaque and double-sided.
    const auto matrix = Multiply(clip_from_world, mesh.transform);
    const auto magnitude = Product(world_magnitude, Absolute(mesh.transform));
    for (std::size_t first = 0; first + 2 < mesh.indices->size(); first += 3) {
      if (count_ == triangles_.size() || inspected++ == 256)
        return;
      Triangle triangle;
      triangle.mesh = mesh.id;
      bool valid = true;
      for (std::size_t vertex = 0; vertex < 3; ++vertex) {
        const auto index = (*mesh.indices)[first + vertex];
        if (index >= mesh.points->size()) {
          valid = false;
          break;
        }
        const auto& point = (*mesh.points)[index];
        double error = 0;
        if (!Project({point.x, point.y, point.z}, matrix, magnitude,
                triangle.points[vertex], error)) {
          valid = false;
          break;
        }
        triangle.error = std::max(triangle.error, error);
      }
      if (valid && std::abs(Cross(triangle.points[0], triangle.points[1], triangle.points[2])) >
                       16 * (triangle.error + pixel_margin_))
        triangles_[count_++] = triangle;
    }
  }
}

bool OutlineOcclusion::Occludes(const MeshSnapshot& mesh, const OutlineBounds& envelope,
    const ToonView& view, float width, ToonOutlineWidthMode mode, float meters_per_unit) const {
  if (!count_ || view.view != view_.view || view.projection != view_.projection)
    return false;
  OutlineBounds::Box bounds;
  double radius = 0;
  if (!envelope.ViewBounds(mesh, view, width, mode, meters_per_unit, bounds, radius))
    return false;
  Point low{1e300, 1e300, 1e300}, high{-1e300, -1e300, -1e300};
  const auto magnitude = Absolute(view.projection);
  for (unsigned corner = 0; corner < 8; ++corner) {
    Point point{};
    for (std::size_t axis = 0; axis < 3; ++axis)
      point[axis] = (corner & (1U << axis)) ? bounds.high[axis] + radius : bounds.low[axis] - radius;
    Point projected{};
    double error = 0;
    if (!Project(point, view.projection, magnitude, projected, error))
      return false;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      low[axis] = std::min(low[axis], projected[axis] - error);
      high[axis] = std::max(high[axis], projected[axis] + error);
    }
  }
  for (std::size_t index = 0; index < count_; ++index) {
    const auto& triangle = triangles_[index];
    if (triangle.mesh == mesh.id)
      continue;
    double depth = -1;
    for (const auto& point : triangle.points)
      depth = std::max(depth, point[2] + triangle.error);
    // The backend uses LESS on the remapped depth; positive hull bias can
    // only push it farther away. Ties and uncertain depths keep the hull.
    if (low[2] <= depth + 1e-4)
      continue;
    const double sign = Cross(triangle.points[0], triangle.points[1], triangle.points[2]) > 0 ? 1 : -1;
    bool inside = true;
    for (std::size_t edge = 0; edge < 3 && inside; ++edge) {
      const auto& a = triangle.points[edge];
      const auto& b = triangle.points[(edge + 1) % 3];
      for (unsigned corner = 0; corner < 4; ++corner) {
        Point point{(corner & 1) ? high[0] + pixel_margin_ : low[0] - pixel_margin_,
            (corner & 2) ? high[1] + pixel_margin_ : low[1] - pixel_margin_, 0};
        const double scale = std::abs(b[0] - a[0]) + std::abs(b[1] - a[1]) +
                             std::abs(point[0] - a[0]) + std::abs(point[1] - a[1]);
        const double margin = 8 * triangle.error * (scale + triangle.error) +
                              pixel_margin_ * (std::abs(b[0] - a[0]) + std::abs(b[1] - a[1]));
        if (sign * Cross(a, b, point) <= margin) {
          inside = false;
          break;
        }
      }
    }
    if (inside)
      return true;
  }
  return false;
}
} // namespace Toon
