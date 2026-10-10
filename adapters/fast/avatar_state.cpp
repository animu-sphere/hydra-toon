// SPDX-License-Identifier: Apache-2.0
#include <toon/fast/avatar_state.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace Toon {
namespace {
bool Fail(std::string& error, const char* reason) {
  error = reason;
  return false;
}
bool Name(const char* name) {
  return name && *name;
}
bool Finite(const Matrix4& m) {
  return std::all_of(m.m.begin(), m.m.end(), [](float x) { return std::isfinite(x); });
}
bool Float(double x) {
  return std::isfinite(x) && std::abs(x) <= std::numeric_limits<float>::max();
}
template <class T>
bool Array(const T* p, std::uint32_t n) {
  return !n || p;
}
template <class T>
std::size_t Size(const std::shared_ptr<const std::vector<T>>& a) {
  return a ? a->size() : 0;
}
template <class T>
bool Equal(const std::shared_ptr<const std::vector<T>>& a,
    const std::shared_ptr<const std::vector<T>>& b) {
  return a == b || (a && b && *a == *b);
}
bool Valid(const ArStateView& s, std::string& error) {
  if (s.abi_version != AR_ABI_VERSION || s.struct_size < sizeof(ArStateView))
    return Fail(error, "incompatible runtime state ABI");
  if (!s.instance || !s.generation || !Name(s.layout_id) || !s.layout_version || !std::isfinite(s.evaluation_seconds) ||
      !Array(s.joints, s.joint_count) || !Array(s.blend_shapes, s.blend_shape_count) ||
      !Array(s.materials, s.material_count) || !Array(s.visibility, s.visibility_count) ||
      !Array(s.capabilities, s.capability_count))
    return Fail(error, "invalid runtime state header or array");
  std::set<std::pair<std::string, std::string>> ids;
  for (std::uint32_t i = 0; i < s.capability_count; ++i) {
    const auto& c = s.capabilities[i];
    if (!Name(c.id) || !c.version || !ids.emplace(c.id, "").second)
      return Fail(error, "invalid runtime capability set");
  }
  ids.clear();
  for (std::uint32_t i = 0; i < s.joint_count; ++i) {
    const auto& j = s.joints[i];
    if (!Name(j.skeleton_id) || !Name(j.joint_id) ||
        !ids.emplace(j.skeleton_id, j.joint_id).second || j.parent_index < -1 ||
        (j.parent_index >= 0 && (static_cast<std::uint32_t>(j.parent_index) >= i ||
                                    std::string(j.skeleton_id) != s.joints[j.parent_index].skeleton_id)))
      return Fail(error, "invalid runtime joint identity or hierarchy");
    for (double x : j.local.translation)
      if (!Float(x))
        return Fail(error, "invalid runtime translation");
    for (double x : j.local.scale)
      if (!Float(x))
        return Fail(error, "invalid runtime scale");
    double norm = 0;
    for (double x : j.local.rotation) {
      if (!std::isfinite(x))
        return Fail(error, "invalid runtime quaternion");
      norm += x * x;
    }
    if (std::abs(norm - 1) > 1e-6)
      return Fail(error, "runtime quaternion is not unit length");
  }
  ids.clear();
  for (std::uint32_t i = 0; i < s.blend_shape_count; ++i) {
    const auto& b = s.blend_shapes[i];
    if (!Name(b.mesh_id) || !Name(b.target_id) || !ids.emplace(b.mesh_id, b.target_id).second || !Float(b.weight))
      return Fail(error, "invalid runtime blend-shape identity or weight");
  }
  ids.clear();
  for (std::uint32_t i = 0; i < s.material_count; ++i) {
    const auto& m = s.materials[i];
    if (!Name(m.material_id) || !Name(m.input_id) || !ids.emplace(m.material_id, m.input_id).second ||
        m.overridden > 1 || (m.value_type != AR_VALUE_SCALAR && m.value_type != AR_VALUE_VEC3 && m.value_type != AR_VALUE_VEC4))
      return Fail(error, "invalid runtime material identity or type");
    for (std::uint32_t k = 0; k < 4; ++k)
      if (!Float(m.value[k]) || (k >= m.value_type && m.value[k] != 0))
        return Fail(error, "invalid runtime material value");
  }
  ids.clear();
  for (std::uint32_t i = 0; i < s.visibility_count; ++i) {
    const auto& v = s.visibility[i];
    if (!Name(v.target_id) || !ids.emplace(v.target_id, "").second || v.visible > 1)
      return Fail(error, "invalid runtime visibility");
  }
  return true;
}
template <class T>
T* Find(std::vector<T>& items, std::uint32_t id) {
  auto it = std::find_if(items.begin(), items.end(), [id](const T& x) { return x.id == id; });
  return it == items.end() ? nullptr : &*it;
}
std::uint32_t Type(AvatarMaterialField field) {
  switch (field) {
  case AvatarMaterialField::BaseColor:
    return AR_VALUE_VEC4;
  case AvatarMaterialField::Emissive:
  case AvatarMaterialField::BaseColorRgb:
  case AvatarMaterialField::ShadeColor:
  case AvatarMaterialField::OutlineColor:
  case AvatarMaterialField::Matcap:
  case AvatarMaterialField::RimColor:
    return AR_VALUE_VEC3;
  // Runtime v3 has no vec2: offset/scale explicitly use vec3 with zero z.
  case AvatarMaterialField::BaseTextureOffset:
  case AvatarMaterialField::BaseTextureScale:
    return AR_VALUE_VEC3;
  case AvatarMaterialField::Alpha:
  case AvatarMaterialField::ShadingShift:
  case AvatarMaterialField::ShadingToony:
  case AvatarMaterialField::OutlineWidth:
  case AvatarMaterialField::BaseTextureRotation:
    return AR_VALUE_SCALAR;
  }
  return 0;
}
bool TextureField(AvatarMaterialField f) {
  return f == AvatarMaterialField::BaseTextureOffset || f == AvatarMaterialField::BaseTextureScale;
}
void Material(ToonMaterial& m, AvatarMaterialField f, const double* v) {
  const Float3 color{static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])};
  const float x = static_cast<float>(v[0]);
  switch (f) {
  case AvatarMaterialField::BaseColor:
    m.base_color = color;
    m.alpha = static_cast<float>(v[3]);
    break;
  case AvatarMaterialField::BaseColorRgb:
    m.base_color = color;
    break;
  case AvatarMaterialField::Emissive:
    m.emissive = color;
    break;
  case AvatarMaterialField::ShadeColor:
    m.mtoon.shade_color = color;
    break;
  case AvatarMaterialField::OutlineColor:
    m.outline_color = color;
    break;
  case AvatarMaterialField::Matcap:
    m.mtoon.matcap = color;
    break;
  case AvatarMaterialField::RimColor:
    m.mtoon.rim_color = color;
    break;
  case AvatarMaterialField::Alpha:
    m.alpha = x;
    break;
  case AvatarMaterialField::ShadingShift:
    m.mtoon.shading_shift = x;
    break;
  case AvatarMaterialField::ShadingToony:
    m.mtoon.shading_toony = x;
    break;
  case AvatarMaterialField::OutlineWidth:
    m.outline_width = x;
    break;
  case AvatarMaterialField::BaseTextureOffset:
    m.base_texture.offset = {x, color.y};
    break;
  case AvatarMaterialField::BaseTextureScale:
    m.base_texture.scale = {x, color.y};
    break;
  case AvatarMaterialField::BaseTextureRotation:
    m.base_texture.rotation = x;
    break;
  }
}
Matrix4 Transform(const ArTransform& t, float units) {
  const double x = t.rotation[0], y = t.rotation[1], z = t.rotation[2], w = t.rotation[3];
  Matrix4 m;
  const double r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
      2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
      2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};
  for (std::size_t c = 0; c < 3; ++c)
    for (std::size_t row = 0; row < 3; ++row)
      m.m[c * 4 + row] = static_cast<float>(r[c * 3 + row] * t.scale[c]);
  for (std::size_t k = 0; k < 3; ++k)
    m.m[12 + k] = static_cast<float>(t.translation[k] / units);
  return m;
}
// GfIsClose with usdSkelImaging's blend-shape epsilon.
bool Close(double a, double b) {
  return std::abs(a - b) < 1e-6;
}
// Writes one shape's subshape weights as UsdSkelImagingComputeBlendShapeWeights
// does, in its float arithmetic: the pair of knots around the weight, or the
// outermost pair beyond them, interpolates; the shape's other slots are zero.
template <class Knots>
bool Subshapes(const Knots& knots, float weight, std::vector<float>& out) {
  if (knots.size() == 2) {
    out[static_cast<std::size_t>(knots[1].slot)] = weight;
    return true;
  }
  for (const auto& k : knots)
    if (k.slot >= 0)
      out[static_cast<std::size_t>(k.slot)] = 0;
  const auto upper = std::upper_bound(knots.begin() + 1, knots.end() - 1, weight,
      [](float w, const auto& k) { return w < k.position; });
  const auto lower = upper - 1;
  const float alpha = (weight - lower->position) / (upper->position - lower->position);
  if (!std::isfinite(alpha))
    return false;
  if (lower->slot >= 0 && !Close(alpha, 1.0))
    out[static_cast<std::size_t>(lower->slot)] = static_cast<float>(1.0 - alpha);
  if (upper->slot >= 0 && !Close(alpha, 0.0))
    out[static_cast<std::size_t>(upper->slot)] = alpha;
  return true;
}
std::uint64_t MaxRevision(const FrameSnapshot& s) {
  auto n = std::max(s.revision, s.view_revision);
  for (const auto& m : s.meshes)
    n = std::max({n, m.points_revision, m.normals_revision,
        m.topology_revision, m.uvs_revision, m.skin_revision, m.pose_revision, m.morph_revision, m.morph_weights_revision});
  for (const auto& m : s.materials)
    n = std::max({n, m.parameters_revision, m.structure_revision});
  for (const auto& t : s.textures)
    n = std::max(n, t.revision);
  for (const auto& l : s.lights)
    n = std::max(n, l.revision);
  return n;
}
} // namespace

RetainedAvatarSnapshot::~RetainedAvatarSnapshot() {
  Clear();
}
RetainedAvatarSnapshot::RetainedAvatarSnapshot(RetainedAvatarSnapshot&& other) noexcept
    : api_(other.api_), handle_(std::exchange(other.handle_, 0)), view_(other.view_) {
}
RetainedAvatarSnapshot& RetainedAvatarSnapshot::operator=(RetainedAvatarSnapshot&& other) noexcept {
  if (this != &other) {
    Clear();
    api_ = other.api_;
    handle_ = std::exchange(other.handle_, 0);
    view_ = other.view_;
  }
  return *this;
}
void RetainedAvatarSnapshot::Clear() {
  if (handle_)
    api_.release_snapshot(handle_);
  handle_ = 0;
  view_ = {};
}
bool RetainedAvatarSnapshot::Reset(const ArRuntimeApi& api, ArSnapshot snapshot, std::string& error) {
  error.clear();
  if (api.abi_version != AR_ABI_VERSION || api.struct_size < sizeof(ArRuntimeApi) ||
      !api.get_snapshot || !api.retain_snapshot || !api.release_snapshot || !snapshot)
    return Fail(error, "incompatible runtime snapshot API");
  if (api.retain_snapshot(snapshot) != AR_OK)
    return Fail(error, "runtime snapshot retain failed");
  ArStateView view{};
  view.struct_size = sizeof(view);
  view.abi_version = AR_ABI_VERSION;
  if (api.get_snapshot(snapshot, &view) != AR_OK || !Valid(view, error)) {
    api.release_snapshot(snapshot);
    if (error.empty())
      error = "runtime snapshot read failed";
    return false;
  }
  Clear();
  api_ = api;
  handle_ = snapshot;
  view_ = view;
  error.clear();
  return true;
}

void AvatarStateAdapter::Clear() {
  *this = AvatarStateAdapter{};
}
bool AvatarStateAdapter::Bind(const ArStateView& layout, const FrameSnapshot& scene,
    std::uint64_t epoch, AvatarBindings bindings, std::string& error) {
  if (!epoch || !Valid(layout, error))
    return epoch ? false : Fail(error, "binding epoch must be nonzero");
  AvatarStateAdapter next;
  next.bound_ = next.previous_ = scene;
  next.epoch_ = epoch;
  next.instance_ = layout.instance;
  next.generation_ = layout.generation;
  next.bindings_ = std::move(bindings);
  next.identity_ = {layout.instance, 0, layout.generation, 0, layout.layout_id, layout.layout_version, epoch, false};
  for (std::uint32_t i = 0; i < layout.capability_count; ++i)
    next.capabilities_.push_back({layout.capabilities[i].id, "", -1, layout.capabilities[i].version});
  std::sort(next.capabilities_.begin(), next.capabilities_.end(), [](const Identity& a, const Identity& b) { return a.owner < b.owner; });
  if (!next.CheckScene(scene, epoch, error))
    return false;
  for (std::uint32_t i = 0; i < layout.joint_count; ++i) {
    const auto& v = layout.joints[i];
    next.joints_.push_back({v.skeleton_id, v.joint_id, v.parent_index, 0});
  }
  for (std::uint32_t i = 0; i < layout.blend_shape_count; ++i) {
    const auto& v = layout.blend_shapes[i];
    next.morphs_.push_back({v.mesh_id, v.target_id});
  }
  for (std::uint32_t i = 0; i < layout.material_count; ++i) {
    const auto& v = layout.materials[i];
    next.materials_.push_back({v.material_id, v.input_id, -1, v.value_type});
  }
  for (std::uint32_t i = 0; i < layout.visibility_count; ++i)
    next.visibility_.push_back({layout.visibility[i].target_id, ""});
  std::set<MeshId> skins;
  for (const auto& b : next.bindings_.skins) {
    const auto* mesh = Find(next.bound_.meshes, b.mesh);
    if (!mesh || !IsSkinned(*mesh) || !skins.insert(b.mesh).second || b.joints.size() != Size(mesh->joints) ||
        b.inverse_bind.size() != b.joints.size() || !Finite(b.world_to_skeleton) || !Finite(b.skeleton_to_mesh))
      return Fail(error, "invalid skin binding or palette count");
    for (std::size_t i = 0; i < b.joints.size(); ++i)
      if (b.joints[i] >= layout.joint_count || !Finite(b.inverse_bind[i]))
        return Fail(error, "invalid skin joint binding");
  }
  std::vector<bool> morphs(layout.blend_shape_count), materials(layout.material_count), visibility(layout.visibility_count);
  std::set<std::pair<MeshId, std::uint32_t>> weights;
  for (const auto& b : next.bindings_.morphs) {
    const auto* mesh = Find(next.bound_.meshes, b.mesh);
    if (b.source >= layout.blend_shape_count || !mesh || !IsMorphed(*mesh))
      return Fail(error, "invalid morph binding");
    std::vector<Knot> knots{{0.0F, -1}, {1.0F, b.weight}};
    for (const auto& inbetween : b.inbetweens) {
      if (!std::isfinite(inbetween.position))
        return Fail(error, "invalid inbetween position");
      knots.push_back({inbetween.position, inbetween.weight});
    }
    std::sort(knots.begin(), knots.end(), [](const Knot& x, const Knot& y) { return x.position < y.position; });
    for (std::size_t i = 0; i < knots.size(); ++i) {
      const auto& k = knots[i];
      // usdSkelImaging drops inbetweens at 0 or 1 or within 1e-6 of another.
      if (i && Close(knots[i - 1].position, k.position))
        return Fail(error, "coincident inbetween position");
      if (k.slot >= 0 && (static_cast<std::size_t>(k.slot) >= Size(mesh->morph_weights) ||
                             !weights.emplace(b.mesh, static_cast<std::uint32_t>(k.slot)).second))
        return Fail(error, "invalid or overlapping morph binding");
    }
    next.knots_.push_back(std::move(knots));
    morphs[b.source] = true;
  }
  std::set<std::pair<MaterialId, AvatarMaterialField>> fields;
  for (const auto& b : next.bindings_.materials) {
    const auto* material = Find(next.bound_.materials, b.material);
    if (b.source >= layout.material_count || !material || material->material.model != ToonShadingModel::MToon ||
        Type(b.field) != layout.materials[b.source].value_type || !fields.emplace(b.material, b.field).second)
      return Fail(error, "unsupported or overlapping material binding");
    materials[b.source] = true;
  }
  for (const auto& [id, field] : fields)
    if (field == AvatarMaterialField::BaseColor &&
        (fields.contains({id, AvatarMaterialField::Alpha}) || fields.contains({id, AvatarMaterialField::BaseColorRgb})))
      return Fail(error, "base colour bindings overlap");
  std::set<MeshId> visible;
  for (const auto& b : next.bindings_.visibility) {
    if (b.source >= layout.visibility_count || !Find(next.bound_.meshes, b.mesh) || !visible.insert(b.mesh).second)
      return Fail(error, "invalid or overlapping visibility binding");
    visibility[b.source] = true;
  }
  if (std::find(morphs.begin(), morphs.end(), false) != morphs.end() ||
      std::find(materials.begin(), materials.end(), false) != materials.end() ||
      std::find(visibility.begin(), visibility.end(), false) != visibility.end() ||
      (layout.joint_count && next.bindings_.skins.empty()))
    return Fail(error, "unmapped runtime output requires an explicit supported binding");
  *this = std::move(next);
  error.clear();
  return true;
}

bool AvatarStateAdapter::CheckScene(const FrameSnapshot& scene, std::uint64_t epoch, std::string& error) const {
  if (!epoch_ || epoch != epoch_)
    return Fail(error, "stale renderer binding epoch");
  if (!(scene.meters_per_unit > 0) || !std::isfinite(scene.meters_per_unit) || scene.meters_per_unit != bound_.meters_per_unit ||
      scene.meshes.size() != bound_.meshes.size() || scene.materials.size() != bound_.materials.size() ||
      scene.textures.size() != bound_.textures.size())
    return Fail(error, "renderer scene structure changed; rebind required");
  for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
    const auto& a = bound_.meshes[i];
    const auto& b = scene.meshes[i];
    if (a.id != b.id || a.material != b.material || a.points != b.points || a.normals != b.normals ||
        a.indices != b.indices || a.uvs != b.uvs || a.influences != b.influences ||
        a.morph_offsets != b.morph_offsets || a.morph_ranges != b.morph_ranges ||
        a.points_revision != b.points_revision || a.normals_revision != b.normals_revision ||
        a.topology_revision != b.topology_revision || a.uvs_revision != b.uvs_revision ||
        a.skin_revision != b.skin_revision || a.morph_revision != b.morph_revision ||
        a.geom_bind != b.geom_bind || a.influences_per_point != b.influences_per_point ||
        a.constant_influences != b.constant_influences || a.joint_bound != b.joint_bound || a.index_bound != b.index_bound ||
        Size(a.joints) != Size(b.joints) || Size(a.morph_weights) != Size(b.morph_weights) || b.morph_weights_overridden)
      return Fail(error, "renderer mesh binding changed or baseline is overridden");
  }
  for (std::size_t i = 0; i < scene.materials.size(); ++i) {
    const auto& a = bound_.materials[i];
    const auto& b = scene.materials[i];
    if (a.id != b.id || a.structure_revision != b.structure_revision || IsStructuralChange(a.material, b.material) || b.parameters_overridden)
      return Fail(error, "renderer material binding changed or baseline is overridden");
  }
  for (std::size_t i = 0; i < scene.textures.size(); ++i) {
    const auto& a = bound_.textures[i];
    const auto& b = scene.textures[i];
    if (a.id != b.id || a.revision != b.revision || a.texture.pixels != b.texture.pixels ||
        a.texture.width != b.texture.width || a.texture.height != b.texture.height || a.texture.encoding != b.texture.encoding)
      return Fail(error, "renderer texture binding changed");
  }
  if (std::max(MaxRevision(scene), MaxRevision(previous_)) == std::numeric_limits<std::uint64_t>::max())
    return Fail(error, "renderer revision exhausted");
  return true;
}

void AvatarStateAdapter::Publish(FrameSnapshot candidate, FrameSnapshot& output) {
  // A single monotonic stamp suffices; each resource advances only on a value
  // change, including release to a scene whose revision predates the override.
  const auto stamp = std::max(MaxRevision(candidate), MaxRevision(previous_)) + 1;
  bool changed = candidate.time_seconds != previous_.time_seconds;
  for (std::size_t i = 0; i < candidate.meshes.size(); ++i) {
    auto& b = candidate.meshes[i];
    const auto& a = previous_.meshes[i];
    if (!Equal(a.joints, b.joints) || a.skeleton_to_mesh != b.skeleton_to_mesh) {
      b.pose_revision = stamp;
      changed = true;
    } else {
      b.joints = a.joints;
      b.pose_revision = a.pose_revision;
    }
    if (!Equal(a.morph_weights, b.morph_weights)) {
      b.morph_weights_revision = stamp;
      changed = true;
    } else {
      b.morph_weights = a.morph_weights;
      b.morph_weights_revision = a.morph_weights_revision;
    }
    changed |= a.visible != b.visible || a.transform != b.transform || a.color != b.color;
  }
  for (std::size_t i = 0; i < candidate.materials.size(); ++i) {
    auto& b = candidate.materials[i];
    const auto& a = previous_.materials[i];
    if (a.material != b.material) {
      b.parameters_revision = stamp;
      changed = true;
    } else
      b.parameters_revision = a.parameters_revision;
  }
  if (candidate.view.view != previous_.view.view || candidate.view.projection != previous_.view.projection) {
    candidate.view_revision = stamp;
    changed = true;
  } else
    candidate.view_revision = previous_.view_revision;
  candidate.revision = changed ? stamp : std::max(candidate.revision, previous_.revision);
  previous_ = candidate;
  output = std::move(candidate);
}

bool AvatarStateAdapter::Apply(const RetainedAvatarSnapshot& state, const FrameSnapshot& scene,
    std::uint64_t epoch, FrameSnapshot& output, std::string& error) {
  const auto* s = state.View();
  if (!s)
    return Fail(error, "no retained runtime snapshot");
  if (!Valid(*s, error) || !CheckScene(scene, epoch, error))
    return false;
  if (s->instance != instance_ || s->generation < generation_ || !s->frame_id ||
      (s->generation == generation_ && s->frame_id < frame_))
    return Fail(error, "stale runtime instance, generation or frame");
  if (identity_.layout != s->layout_id || identity_.layout_version != s->layout_version)
    return Fail(error, "runtime layout identity changed; rebind required");
  std::vector<Identity> capabilities;
  for (std::uint32_t i = 0; i < s->capability_count; ++i)
    capabilities.push_back({s->capabilities[i].id, "", -1, s->capabilities[i].version});
  std::sort(capabilities.begin(), capabilities.end(), [](const Identity& a, const Identity& b) { return a.owner < b.owner; });
  if (capabilities != capabilities_)
    return Fail(error, "runtime capability set changed; renegotiate binding");
  if (s->joint_count != joints_.size() || s->blend_shape_count != morphs_.size() ||
      s->material_count != materials_.size() || s->visibility_count != visibility_.size())
    return Fail(error, "runtime layout count changed; rebind required");
  for (std::uint32_t i = 0; i < s->joint_count; ++i) {
    const auto& v = s->joints[i];
    if (joints_[i] != Identity{v.skeleton_id, v.joint_id, v.parent_index, 0})
      return Fail(error, "runtime joint layout changed");
  }
  for (std::uint32_t i = 0; i < s->blend_shape_count; ++i) {
    const auto& v = s->blend_shapes[i];
    if (morphs_[i] != Identity{v.mesh_id, v.target_id})
      return Fail(error, "runtime morph layout changed");
  }
  for (std::uint32_t i = 0; i < s->material_count; ++i) {
    const auto& v = s->materials[i];
    if (materials_[i] != Identity{v.material_id, v.input_id, -1, v.value_type})
      return Fail(error, "runtime material layout changed");
  }
  for (std::uint32_t i = 0; i < s->visibility_count; ++i)
    if (visibility_[i].owner != s->visibility[i].target_id)
      return Fail(error, "runtime visibility layout changed");
  FrameSnapshot candidate = scene;
  candidate.time_seconds = s->evaluation_seconds;
  std::vector<Matrix4> world(s->joint_count);
  for (std::uint32_t i = 0; i < s->joint_count; ++i) {
    world[i] = Transform(s->joints[i].local, scene.meters_per_unit);
    if (s->joints[i].parent_index >= 0)
      world[i] = Multiply(world[static_cast<std::size_t>(s->joints[i].parent_index)], world[i]);
    if (!Finite(world[i]))
      return Fail(error, "runtime pose conversion overflow");
  }
  for (const auto& b : bindings_.skins) {
    auto& mesh = *Find(candidate.meshes, b.mesh);
    std::vector<Matrix4> palette;
    for (std::size_t i = 0; i < b.joints.size(); ++i) {
      auto joint = Multiply(Multiply(b.world_to_skeleton, world[b.joints[i]]), b.inverse_bind[i]);
      if (!Finite(joint))
        return Fail(error, "runtime palette conversion overflow");
      palette.push_back(joint);
    }
    mesh.joints = std::make_shared<const std::vector<Matrix4>>(std::move(palette));
    mesh.skeleton_to_mesh = b.skeleton_to_mesh;
  }
  // Multiple channels to one mesh share one weight array allocation.
  std::map<MeshId, std::vector<float>> weights;
  for (std::size_t i = 0; i < bindings_.morphs.size(); ++i) {
    const auto& b = bindings_.morphs[i];
    auto& mesh = *Find(candidate.meshes, b.mesh);
    auto [it, inserted] = weights.try_emplace(b.mesh);
    if (inserted)
      it->second = *mesh.morph_weights;
    if (!Subshapes(knots_[i], static_cast<float>(s->blend_shapes[b.source].weight), it->second))
      return Fail(error, "runtime inbetween conversion overflow");
    mesh.morph_weights_overridden = true;
  }
  for (auto& [id, values] : weights)
    Find(candidate.meshes, id)->morph_weights =
        std::make_shared<const std::vector<float>>(std::move(values));
  for (const auto& b : bindings_.materials) {
    const auto& value = s->materials[b.source];
    if (TextureField(b.field) && value.value[2] != 0)
      return Fail(error, "texture transform vec3 must have zero z");
    if (!value.overridden)
      continue;
    auto& material = *Find(candidate.materials, b.material);
    Material(material.material, b.field, value.value);
    material.parameters_overridden = true;
  }
  for (const auto& b : bindings_.visibility)
    Find(candidate.meshes, b.mesh)->visible = s->visibility[b.source].visible != 0;
  Publish(std::move(candidate), output);
  identity_.frame = s->frame_id;
  identity_.generation = s->generation;
  identity_.input_revision = s->input_revision;
  identity_.active = true;
  generation_ = s->generation;
  frame_ = s->frame_id;
  error.clear();
  return true;
}
bool AvatarStateAdapter::Release(const FrameSnapshot& scene, std::uint64_t epoch,
    FrameSnapshot& output, std::string& error) {
  if (!CheckScene(scene, epoch, error))
    return false;
  Publish(scene, output);
  identity_.active = false;
  error.clear();
  return true;
}
} // namespace Toon
