// SPDX-License-Identifier: Apache-2.0
#include <toon/extraction.hpp>

#include <algorithm>
#include <cmath>
#include <span>

namespace Toon {
namespace {

// The parts a mesh draws in, as a backend draws them: its own, or the
// whole triangle list with its material when it has none.
std::span<const MeshPart> PartsOf(const MeshSnapshot& mesh,
    std::uint32_t index_count, MeshPart& whole) {
  if (mesh.parts != nullptr && !mesh.parts->empty()) {
    return *mesh.parts;
  }
  whole = {0, index_count, mesh.material};
  return {&whole, 1};
}

bool Fits(const MeshPart& part, std::uint32_t index_count) {
  return part.index_count != 0 && part.first_index <= index_count &&
         part.index_count <= index_count - part.first_index;
}

std::array<double, 3> CornerOf(const std::array<double, 3>& low,
    const std::array<double, 3>& high, unsigned corner) {
  std::array<double, 3> point{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    point[axis] = (corner & (1U << axis)) ? high[axis] : low[axis];
  }
  return point;
}

// The light's frame, from the scene's space: it looks down its -z, along
// `travel`, from `origin`. False for a travel of no length.
bool LightFrame(const Float3& travel, const Float3& origin,
    Matrix4& light_from_world) {
  const double travel_length =
      std::sqrt(double(travel.x) * travel.x + double(travel.y) * travel.y +
                double(travel.z) * travel.z);
  if (!(travel_length > 0.0) || !std::isfinite(travel_length)) {
    return false;
  }
  const std::array<double, 3> z{-travel.x / travel_length,
      -travel.y / travel_length, -travel.z / travel_length};
  const std::array<double, 3> up = std::abs(z[1]) < 0.99
                                       ? std::array<double, 3>{0.0, 1.0, 0.0}
                                       : std::array<double, 3>{1.0, 0.0, 0.0};
  std::array<double, 3> x{up[1] * z[2] - up[2] * z[1],
      up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]};
  const double x_length = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
  for (double& value : x) {
    value /= x_length;
  }
  const std::array<double, 3> y{z[1] * x[2] - z[2] * x[1],
      z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
  light_from_world = Matrix4{};
  for (std::size_t column = 0; column < 3; ++column) {
    light_from_world.m[column * 4 + 0] = static_cast<float>(x[column]);
    light_from_world.m[column * 4 + 1] = static_cast<float>(y[column]);
    light_from_world.m[column * 4 + 2] = static_cast<float>(z[column]);
  }
  if (origin != Float3{}) {
    const std::array<double, 3> at{origin.x, origin.y, origin.z};
    for (std::size_t row = 0; row < 3; ++row) {
      const auto& axis = row == 0 ? x : row == 1 ? y : z;
      light_from_world.m[12 + row] = static_cast<float>(
          -(axis[0] * at[0] + axis[1] * at[1] + axis[2] * at[2]));
    }
  }
  return true;
}

} // namespace

ShadowRole ShadowRoleOf(const ToonMaterial* material) {
  ShadowRole role;
  if (material == nullptr) {
    role.casts = true;
    role.both_faces = true;
    role.receives = true;
    return role;
  }
  if (material->model == ToonShadingModel::MMD) {
    // Both faces, no texel's alpha cutting it away, as full.fx draws its
    // map (material policy §10).
    role.mmd = true;
    role.casts = material->mmd.cast_self_shadow;
    role.both_faces = true;
    role.receives = material->mmd.receive_self_shadow;
    return role;
  }
  if (material->model == ToonShadingModel::PreviewSurface &&
      !material->preview.enabled) {
    // Drawn unlit and opaque, both faces: it casts, and is not shaded.
    role.casts = true;
    role.both_faces = true;
    return role;
  }
  role.casts = material->alpha_mode != ToonAlphaMode::Blend;
  role.both_faces = material->double_sided;
  role.cut = material->alpha_mode == ToonAlphaMode::Mask;
  role.receives = true;
  return role;
}

const ToonMaterial* FindMaterial(const DrawList& draws, MaterialId id) {
  if (id == 0) {
    return nullptr;
  }
  const auto found = std::lower_bound(draws.materials.begin(),
      draws.materials.end(), id,
      [](const MaterialSnapshot& material, MaterialId value) {
        return material.id < value;
      });
  return found != draws.materials.end() && found->id == id ? &found->material
                                                           : nullptr;
}

bool ShadowFit::Casts(const ShadowRole& role, const DrawList& draws) {
  return role.casts && (role.mmd || draws.shadows);
}

bool ShadowFit::Receives(const ShadowRole& role, const DrawList& draws,
    const ShadowView& view) {
  return role.receives &&
         (role.mmd ? draws.mmd_self_shadow && view.mmd : draws.shadows);
}

std::uint32_t ShadowResolution(std::uint32_t requested) {
  std::uint32_t resolution = 512;
  while (resolution < 4096 && resolution * 2U <= requested) {
    resolution *= 2U;
  }
  return resolution;
}

void ShadowFit::Update(const DrawList& draws) {
  views_.clear();
  unviewed_ = 0;
  ++generation_;
  for (const MeshSnapshot& mesh : draws.draws) {
    Bounds& bounds = bounds_[mesh.id];
    bounds.bounds.Update(mesh);
    bounds.generation = generation_;
  }
  std::erase_if(bounds_, [&](const auto& entry) {
    return entry.second.generation != generation_;
  });
  Gather(draws);
  const double size = ShadowResolution(draws.shadow_settings.resolution);

  // The fallback key stands in for a missing rig, as the light buffer
  // does, and is MMD's light then; it is in view space.
  if (!draws.lighting.scene_lights || draws.lights.empty()) {
    Float3 key{-draws.lighting.key_direction.x,
        -draws.lighting.key_direction.y, -draws.lighting.key_direction.z};
    if (key == Float3{}) {
      key = {-0.25F, -0.5F, -1.0F};
    }
    const auto& w = AffineInverse(draws.view.view).m;
    const Float3 travel{w[0] * key.x + w[4] * key.y + w[8] * key.z,
        w[1] * key.x + w[5] * key.y + w[9] * key.z,
        w[2] * key.x + w[6] * key.y + w[10] * key.z};
    ShadowView view;
    view.mmd = draws.mmd_self_shadow;
    if (Fit(draws, travel, size, view)) {
      views_.push_back(view);
    }
    return;
  }
  // Shadowing lights in the light buffer's order. MMD reads the first
  // direct light alone, and only when it is directional.
  bool first_direct = true;
  for (const LightSnapshot& light : draws.lights) {
    if (!light.light.visible || light.light.type == ToonLightType::Ambient) {
      continue;
    }
    const bool mmd_light = first_direct;
    first_direct = false;
    if (!light.light.shadow || light.light.type == ToonLightType::Point) {
      continue;
    }
    const bool spot = light.light.type == ToonLightType::Spot;
    if (views_.size() == kShadowViewCapacity ||
        (spot && !(light.light.cone_angle <= kShadowSpotLimit))) {
      ++unviewed_;
      continue;
    }
    ShadowView view;
    view.light = light.id;
    view.mmd = !spot && mmd_light && draws.mmd_self_shadow;
    if (spot ? FitSpot(draws, light.light, size, view)
             : Fit(draws, light.light.direction, size, view)) {
      views_.push_back(view);
    }
  }
}

// Each mesh's box, roles and seen corners, which every view of the frame
// reads: the receiver's parts the frustum reaches, each cut to it, as the
// camera alone decides them.
void ShadowFit::Gather(const DrawList& draws) {
  shadowed_.clear();
  seen_.clear();
  // OpenGL's clip space: a point is in the frustum where -w <= x, y, z <= w.
  const Matrix4 clip_from_world = Multiply(draws.view.projection, draws.view.view);
  const auto& c = clip_from_world.m;
  const auto& v = draws.view.view.m;
  const Matrix4 world_from_view = AffineInverse(draws.view.view);
  const auto& w = world_from_view.m;
  // An unskewed projection, perspective or orthographic, whose frustum a
  // part's box is cut to.
  const auto& p = draws.view.projection.m;
  const bool unskewed = p[0] != 0.0F && p[5] != 0.0F && p[1] == 0.0F &&
                        p[4] == 0.0F;
  const bool perspective = unskewed && p[11] == -1.0F && p[15] == 0.0F;
  const bool orthographic = unskewed && p[11] == 0.0F && p[15] == 1.0F;
  MeshPart whole;
  for (const MeshSnapshot& mesh : draws.draws) {
    const auto found = bounds_.find(mesh.id);
    if (found == bounds_.end()) {
      continue;
    }
    const OutlineBounds& bounds = found->second.bounds;
    const std::uint32_t index_count = mesh.indices != nullptr
        ? static_cast<std::uint32_t>(mesh.indices->size())
        : 0U;
    Shadowed entry;
    ShadowView mmd_view;
    mmd_view.mmd = true;
    for (const MeshPart& part : PartsOf(mesh, index_count, whole)) {
      if (!Fits(part, index_count)) {
        continue;
      }
      const ShadowRole role = ShadowRoleOf(FindMaterial(draws, part.material));
      entry.casts = entry.casts || Casts(role, draws);
      entry.receives = entry.receives || Receives(role, draws, ShadowView{});
      entry.receives_mmd = entry.receives_mmd || Receives(role, draws, mmd_view);
    }
    if ((!entry.casts && !entry.receives && !entry.receives_mmd) ||
        !bounds.SceneBounds(mesh, entry.low, entry.high)) {
      continue;
    }
    entry.first_seen = seen_.size();
    if (entry.receives || entry.receives_mmd) {
      // A part is left out only when every corner of its box lies beyond
      // one of the frustum's planes.
      if (!bounds.ScenePieces(mesh, pieces_)) {
        pieces_.assign(1, {entry.low, entry.high});
      }
      for (const OutlineBounds::SceneBox& piece : pieces_) {
        std::array<std::array<double, 4>, 8> clip{};
        for (unsigned corner = 0; corner < 8U; ++corner) {
          const std::array<double, 3> point = CornerOf(piece.low, piece.high, corner);
          for (std::size_t row = 0; row < 4; ++row) {
            clip[corner][row] = c[12 + row] + c[row] * point[0] +
                                c[4 + row] * point[1] + c[8 + row] * point[2];
          }
        }
        bool outside = false;
        for (std::size_t row = 0; row < 3 && !outside; ++row) {
          for (const double sign : {-1.0, 1.0}) {
            bool beyond = true;
            for (const auto& point : clip) {
              beyond = beyond && sign * point[row] > point[3];
            }
            outside = outside || beyond;
          }
        }
        if (outside) {
          continue;
        }
        // The part's box in view space, cut to the frustum's cross-section
        // at the part's far side, which holds every nearer one, so the parts
        // the camera sees only the edge of add only that edge.
        std::array<double, 3> view_low{};
        std::array<double, 3> view_high{};
        for (unsigned corner = 0; corner < 8U; ++corner) {
          const std::array<double, 3> point = CornerOf(piece.low, piece.high, corner);
          for (std::size_t row = 0; row < 3; ++row) {
            const double value = v[12 + row] + v[row] * point[0] +
                                 v[4 + row] * point[1] + v[8 + row] * point[2];
            view_low[row] = corner ? std::min(view_low[row], value) : value;
            view_high[row] = corner ? std::max(view_high[row], value) : value;
          }
        }
        if (perspective || orthographic) {
          const double ahead = std::max(-view_low[2], 0.0);
          for (std::size_t row = 0; row < 2; ++row) {
            const double scale = p[5 * row];
            const double shift = p[8 + row];
            const double offset = p[12 + row];
            const double from = perspective ? ahead * (-1.0 + shift) / scale
                                            : (-1.0 - offset) / scale;
            const double to = perspective ? ahead * (1.0 + shift) / scale
                                          : (1.0 - offset) / scale;
            view_low[row] = std::max(view_low[row], std::min(from, to));
            view_high[row] = std::min(view_high[row], std::max(from, to));
          }
          if (!(view_low[0] <= view_high[0]) || !(view_low[1] <= view_high[1])) {
            continue;
          }
        }
        for (unsigned corner = 0; corner < 8U; ++corner) {
          const std::array<double, 3> at = CornerOf(view_low, view_high, corner);
          std::array<double, 3> point{};
          for (std::size_t row = 0; row < 3; ++row) {
            point[row] = w[12 + row] + w[row] * at[0] + w[4 + row] * at[1] +
                         w[8 + row] * at[2];
          }
          seen_.push_back(point);
        }
      }
    }
    entry.seen_count = seen_.size() - entry.first_seen;
    shadowed_.push_back(entry);
  }
}

// The view looks along the light with an orthographic projection. Along
// the light it spans the box around every draw that casts into it or
// receives it, its depth running from the box's face nearest the light.
// Across the light it spans what of that box the camera can see of a
// receiver: the receivers' parts the frustum reaches, each cut to it. Only
// a receiver reads the view, and along the light a caster outside that
// span cannot shadow one inside it. So that a moving camera does not make
// the shadow's edges crawl, each axis's span changes in steps of an eighth
// of an octave and its centre in whole texels, and it is padded so a
// filtered edge and a point moved along its normal stay inside.
bool ShadowFit::Fit(const DrawList& draws, const Float3& travel, double size,
    ShadowView& view) {
  Matrix4 light_from_world;
  if (!LightFrame(travel, Float3{}, light_from_world)) {
    return false;
  }
  const auto& m = light_from_world.m;
  const auto light = [&](const std::array<double, 3>& point, std::size_t row) {
    return m[12 + row] + m[row] * point[0] + m[4 + row] * point[1] +
           m[8 + row] * point[2];
  };

  // In the light's frame: around every draw that casts or receives, and
  // around what the camera sees of the receivers.
  std::array<double, 3> low{};
  std::array<double, 3> high{};
  bool bounded = false;
  bool receivers = false;
  std::array<double, 3> seen_low{};
  std::array<double, 3> seen_high{};
  bool seen = false;
  for (const Shadowed& entry : shadowed_) {
    const bool receives = entry.ReceivesView(view);
    if (!entry.casts && !receives) {
      continue;
    }
    receivers = receivers || receives;
    for (unsigned corner = 0; corner < 8U; ++corner) {
      const std::array<double, 3> point = CornerOf(entry.low, entry.high, corner);
      for (std::size_t row = 0; row < 3; ++row) {
        const double value = light(point, row);
        low[row] = bounded ? std::min(low[row], value) : value;
        high[row] = bounded ? std::max(high[row], value) : value;
      }
      bounded = true;
    }
    if (!receives) {
      continue;
    }
    for (std::size_t index = 0; index < entry.seen_count; ++index) {
      const std::array<double, 3>& point = seen_[entry.first_seen + index];
      for (std::size_t row = 0; row < 3; ++row) {
        const double value = light(point, row);
        seen_low[row] = seen ? std::min(seen_low[row], value) : value;
        seen_high[row] = seen ? std::max(seen_high[row], value) : value;
      }
      seen = true;
    }
  }
  if (!bounded || !receivers) {
    return false;
  }

  // What of the box across the light the camera sees of a receiver.
  std::array<double, 2> across_low{low[0], low[1]};
  std::array<double, 2> across_high{high[0], high[1]};
  if (seen) {
    for (std::size_t axis = 0; axis < 2; ++axis) {
      across_low[axis] = std::max(across_low[axis], seen_low[axis]);
      across_high[axis] = std::min(across_high[axis], seen_high[axis]);
    }
  }
  // A span the camera does not see, or one that leaves nothing, falls back
  // to the whole box.
  std::array<double, 2> center{};
  std::array<double, 2> across{};
  for (std::size_t axis = 0; axis < 2; ++axis) {
    const double box = std::max(high[axis] - low[axis], 1e-4) * 1.02;
    center[axis] = 0.5 * (low[axis] + high[axis]);
    across[axis] = box;
    if (!seen || !(across_high[axis] > across_low[axis])) {
      continue;
    }
    const double span = std::exp2(std::ceil(std::log2(
        (across_high[axis] - across_low[axis]) * 1.02 *
        (1.0 + 16.0 / size)) * 8.0) / 8.0);
    if (span < box) {
      const double texel = span / size;
      across[axis] = span;
      center[axis] =
          std::round(0.5 * (across_low[axis] + across_high[axis]) / texel) *
          texel;
    }
  }
  const double depth = std::max(high[2] - low[2], 1e-4) * 1.04;
  const double nearest = high[2] + 0.02 * depth;
  Matrix4 clip;
  clip.m[0] = static_cast<float>(2.0 / across[0]);
  clip.m[12] = static_cast<float>(-2.0 * center[0] / across[0]);
  clip.m[5] = static_cast<float>(2.0 / across[1]);
  clip.m[13] = static_cast<float>(-2.0 * center[1] / across[1]);
  clip.m[10] = static_cast<float>(-1.0 / depth);
  clip.m[14] = static_cast<float>(nearest / depth);
  view.clip_from_world = Multiply(clip, light_from_world);
  view.depth = depth;
  view.texel = std::max(across[0], across[1]) / size;
  return true;
}

// The view looks from the light down its axis with a perspective
// projection over its cone, padded as a directional view is so a filtered
// edge stays inside. Its near and far planes span the box around every
// draw that casts into it or receives it, the near plane no closer than a
// thousandth of the far one, so the depth keeps its precision.
bool ShadowFit::FitSpot(const DrawList& draws, const ToonLight& light,
    double size, ShadowView& view) const {
  Matrix4 light_from_world;
  if (!LightFrame(light.direction, light.position, light_from_world)) {
    return false;
  }
  const auto& m = light_from_world.m;
  double nearest = 0.0;
  double farthest = 0.0;
  bool bounded = false;
  bool receivers = false;
  for (const Shadowed& entry : shadowed_) {
    const bool receives = entry.ReceivesView(view);
    if (!entry.casts && !receives) {
      continue;
    }
    double mesh_far = 0.0;
    for (unsigned corner = 0; corner < 8U; ++corner) {
      const std::array<double, 3> point = CornerOf(entry.low, entry.high, corner);
      const double distance = -(m[14] + m[2] * point[0] + m[6] * point[1] +
                                  m[10] * point[2]);
      nearest = bounded ? std::min(nearest, distance) : distance;
      farthest = bounded ? std::max(farthest, distance) : distance;
      mesh_far = corner ? std::max(mesh_far, distance) : distance;
      bounded = true;
    }
    receivers = receivers || (receives && mesh_far > 0.0);
  }
  if (!bounded || !receivers || !(farthest > 0.0)) {
    return false;
  }
  const double pad = 0.02 * (farthest - nearest);
  const double far_plane = farthest + pad;
  const double near_plane = std::max(nearest - pad, far_plane * 1e-3);
  constexpr double radians = 0.017453292519943295;
  const double spread = std::tan(double(light.cone_angle) * radians) *
                        (1.0 + 16.0 / size);
  if (!(spread > 0.0) || !std::isfinite(spread)) {
    return false;
  }
  // Vulkan's depth: 0 at the near plane, 1 at the far, w the distance.
  Matrix4 clip;
  clip.m[0] = static_cast<float>(1.0 / spread);
  clip.m[5] = static_cast<float>(1.0 / spread);
  clip.m[10] = static_cast<float>(-far_plane / (far_plane - near_plane));
  clip.m[11] = -1.0F;
  clip.m[14] = static_cast<float>(-far_plane * near_plane / (far_plane - near_plane));
  clip.m[15] = 0.0F;
  view.clip_from_world = Multiply(clip, light_from_world);
  view.perspective = true;
  view.depth = far_plane - near_plane;
  view.near_plane = near_plane;
  view.texel = 2.0 * spread / size;
  return true;
}

} // namespace Toon
