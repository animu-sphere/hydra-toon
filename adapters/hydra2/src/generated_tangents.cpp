// SPDX-License-Identifier: Apache-2.0
#include "generated_tangents.hpp"
#include "mikktspace.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace Toon::detail {
namespace {
struct Input {
  const std::vector<Float3>& points;
  const std::vector<Float3>& normals;
  const std::vector<Float2>& uvs;
  const std::vector<std::uint32_t>& indices;
  std::vector<Float4> corners;
  std::uint32_t Vertex(int face, int corner) const {
    return indices[static_cast<std::size_t>(face) * 3U + corner];
  }
};

Input& Data(const SMikkTSpaceContext* context) {
  return *static_cast<Input*>(context->m_pUserData);
}
} // namespace

GeneratedTangents GenerateTangents(const std::vector<Float3>& points,
    const std::vector<Float3>& normals, const std::vector<Float2>& uvs,
    const std::vector<std::uint32_t>& indices) {
  GeneratedTangents result;
  if (indices.empty() || indices.size() % 3U != 0 ||
      indices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      points.size() > std::numeric_limits<std::uint32_t>::max() ||
      normals.size() < points.size() || uvs.size() < points.size()) return result;
  bool usable_uvs = false;
  for (const auto index : indices) {
    if (index >= points.size()) return result;
    const auto& p = points[index];
    const auto& n = normals[index];
    const auto& uv = uvs[index];
    const float length2 = n.x * n.x + n.y * n.y + n.z * n.z;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
        !std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z) ||
        !std::isfinite(length2) || length2 <= 1e-12F ||
        !std::isfinite(uv.x) || !std::isfinite(uv.y)) return result;
  }
  for (std::size_t i = 0; i < indices.size(); i += 3U) {
    const auto& a = uvs[indices[i]];
    const auto& b = uvs[indices[i + 1U]];
    const auto& c = uvs[indices[i + 2U]];
    const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    usable_uvs |= std::isfinite(area) && area != 0;
  }
  // There is no meaningful UV frame for an entirely collapsed UV set.
  if (!usable_uvs) return result;

  Input input{points, normals, uvs, indices, std::vector<Float4>(indices.size())};
  SMikkTSpaceInterface callbacks{};
  callbacks.m_getNumFaces = [](const SMikkTSpaceContext* c) {
    return static_cast<int>(Data(c).indices.size() / 3U);
  };
  callbacks.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) { return 3; };
  callbacks.m_getPosition = [](const SMikkTSpaceContext* c, float out[], int f, int v) {
    const auto& d = Data(c);
    const auto& p = d.points[d.Vertex(f, v)];
    out[0] = p.x; out[1] = p.y; out[2] = p.z;
  };
  callbacks.m_getNormal = [](const SMikkTSpaceContext* c, float out[], int f, int v) {
    const auto& d = Data(c);
    const auto& n = d.normals[d.Vertex(f, v)];
    const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    out[0] = n.x / length; out[1] = n.y / length; out[2] = n.z / length;
  };
  callbacks.m_getTexCoord = [](const SMikkTSpaceContext* c, float out[], int f, int v) {
    const auto& d = Data(c);
    const auto& uv = d.uvs[d.Vertex(f, v)];
    out[0] = uv.x; out[1] = uv.y;
  };
  callbacks.m_setTSpaceBasic = [](const SMikkTSpaceContext* c, const float t[], float sign,
                                  int f, int v) {
    Data(c).corners[static_cast<std::size_t>(f) * 3U + v] = {t[0], t[1], t[2], sign};
  };
  const SMikkTSpaceContext context{&callbacks, &input};
  if (!genTangSpaceDefault(&context)) return result;

  result.indices = indices;
  result.tangents.resize(points.size(), {1, 0, 0, 1});
  std::vector<std::vector<std::uint32_t>> variants(points.size());
  for (std::size_t corner = 0; corner < indices.size(); ++corner) {
    const auto& tangent = input.corners[corner];
    const float length2 = tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z;
    if (!std::isfinite(length2) || length2 <= 1e-12F ||
        (tangent.w != -1 && tangent.w != 1)) return {};
    const auto original = indices[corner];
    auto& options = variants[original];
    if (options.empty()) {
      result.tangents[original] = tangent;
      options.push_back(original);
      continue;
    }
    const auto found = std::find_if(options.begin(), options.end(), [&](auto vertex) {
      return result.tangents[vertex] == tangent;
    });
    if (found != options.end()) {
      result.indices[corner] = *found;
      continue;
    }
    // MikkTSpace returns an unindexed corner frame. Never overwrite a
    // shared vertex with a different frame (in particular a mirrored sign).
    if (result.tangents.size() >= std::numeric_limits<std::uint32_t>::max()) return {};
    if (result.vertices.empty()) {
      result.vertices.resize(points.size());
      std::iota(result.vertices.begin(), result.vertices.end(), 0U);
    }
    const auto vertex = static_cast<std::uint32_t>(result.tangents.size());
    result.vertices.push_back(original);
    result.tangents.push_back(tangent);
    options.push_back(vertex);
    result.indices[corner] = vertex;
  }
  return result;
}
} // namespace Toon::detail
