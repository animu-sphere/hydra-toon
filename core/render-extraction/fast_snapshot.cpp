// SPDX-License-Identifier: Apache-2.0
#include <toon/extraction.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace Toon {
namespace {
bool Drawable(const MeshSnapshot& mesh) {
  return mesh.visible && mesh.indices && !mesh.indices->empty() &&
      mesh.points && mesh.points->size() >= mesh.index_bound;
}
bool Finite(const Matrix4& matrix) {
  return std::all_of(matrix.m.begin(), matrix.m.end(), [](float x) { return std::isfinite(x); });
}
bool Finite(const Float3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool Finite(const ToonTextureRef& ref) {
  return std::isfinite(ref.offset.x) && std::isfinite(ref.offset.y) &&
      std::isfinite(ref.scale.x) && std::isfinite(ref.scale.y) && std::isfinite(ref.rotation);
}
bool Finite(const ToonMaterial& material) {
  const auto& m = material.mtoon;
  const float values[] = {material.alpha, material.alpha_cutoff, material.normal_scale,
      material.outline_width, m.shading_shift, m.shading_shift_texture_scale,
      m.shading_toony, m.gi_equalization, m.rim_fresnel_power, m.rim_lift,
      m.rim_lighting_mix, m.outline_lighting_mix, m.uv_scroll_x_speed,
      m.uv_scroll_y_speed, m.uv_rotation_speed};
  return std::all_of(std::begin(values), std::end(values), [](float x) { return std::isfinite(x); }) &&
      Finite(material.base_color) && Finite(material.emissive) && Finite(material.outline_color) &&
      Finite(m.shade_color) && Finite(m.matcap) && Finite(m.rim_color) &&
      Finite(material.base_texture) && Finite(material.emissive_texture) && Finite(material.normal_texture) &&
      Finite(m.shade_texture) && Finite(m.shading_shift_texture) && Finite(m.matcap_texture) &&
      Finite(m.rim_multiply_texture) && Finite(m.outline_width_texture) && Finite(m.uv_animation_mask_texture);
}
template<class T> std::size_t Size(const std::shared_ptr<const std::vector<T>>& array) {
  return array ? array->size() : 0;
}
}

bool ApplyFastSnapshot(const FrameSnapshot& latest, DrawList& draws, std::string& error) {
  const auto reject = [&](const char* reason) { error = reason; return false; };
  if (latest.meters_per_unit != draws.meters_per_unit ||
      latest.materials.size() != draws.materials.size() ||
      latest.textures.size() != draws.textures.size()) return reject("late frame changed scene structure");
  if (!Finite(latest.view.view) || !Finite(latest.view.projection) || !std::isfinite(latest.time_seconds))
    return reject("late frame has non-finite camera or time");
  std::size_t index = 0;
  for (const auto& mesh : latest.meshes) {
    if (!Drawable(mesh)) continue;
    if (index == draws.draws.size()) return reject("late frame changed draw membership");
    const auto& old = draws.draws[index++];
    if (mesh.id != old.id || mesh.material != old.material ||
        mesh.points != old.points || mesh.normals != old.normals || mesh.indices != old.indices ||
        mesh.uvs != old.uvs || mesh.influences != old.influences ||
        mesh.morph_offsets != old.morph_offsets || mesh.morph_ranges != old.morph_ranges ||
        mesh.points_revision != old.points_revision || mesh.normals_revision != old.normals_revision ||
        mesh.topology_revision != old.topology_revision || mesh.uvs_revision != old.uvs_revision ||
        mesh.skin_revision != old.skin_revision || mesh.morph_revision != old.morph_revision ||
        mesh.geom_bind != old.geom_bind || mesh.influences_per_point != old.influences_per_point ||
        mesh.constant_influences != old.constant_influences || mesh.joint_bound != old.joint_bound ||
        mesh.index_bound != old.index_bound || Size(mesh.joints) != Size(old.joints) ||
        Size(mesh.morph_weights) != Size(old.morph_weights) ||
        IsSkinned(mesh) != IsSkinned(old) || IsMorphed(mesh) != IsMorphed(old))
      return reject("late frame changed mesh structure");
    if (!Finite(mesh.transform) || !Finite(mesh.skeleton_to_mesh) || !Finite(mesh.color) ||
        (mesh.joints && !std::all_of(mesh.joints->begin(), mesh.joints->end(),
            [](const Matrix4& matrix) { return Finite(matrix); })) ||
        (mesh.morph_weights && !std::all_of(mesh.morph_weights->begin(), mesh.morph_weights->end(),
            [](float x) { return std::isfinite(x); }))) return reject("late frame has non-finite motion");
  }
  if (index != draws.draws.size()) return reject("late frame changed draw membership");
  for (std::size_t i = 0; i < draws.materials.size(); ++i) {
    const auto& a = draws.materials[i];
    const auto& b = latest.materials[i];
    if (a.id != b.id || a.structure_revision != b.structure_revision ||
        IsStructuralChange(a.material, b.material)) return reject("late frame changed material structure");
    if (!Finite(b.material)) return reject("late frame has non-finite material values");
  }
  for (const auto& item : latest.lights) {
    const auto& light = item.light;
    if (!Finite(light.color) || !Finite(light.position) || !Finite(light.direction) ||
        !std::isfinite(light.radius) || !std::isfinite(light.cone_angle) || !std::isfinite(light.cone_softness))
      return reject("late frame has non-finite lights");
  }
  for (std::size_t i = 0; i < draws.textures.size(); ++i) {
    const auto& a = draws.textures[i];
    const auto& b = latest.textures[i];
    if (a.id != b.id || a.revision != b.revision || a.texture.pixels != b.texture.pixels ||
        a.texture.width != b.texture.width || a.texture.height != b.texture.height ||
        a.texture.encoding != b.texture.encoding) return reject("late frame changed textures");
  }
  // Validation is complete. Reuse draw storage and retain immutable arrays.
  index = 0;
  for (const auto& mesh : latest.meshes) if (Drawable(mesh)) draws.draws[index++] = mesh;
  draws.materials = latest.materials;
  draws.view = latest.view;
  draws.view_revision = latest.view_revision;
  draws.time_seconds = latest.time_seconds;
  draws.lights = latest.lights;
  draws.source_revision = latest.revision;
  draws.inputs = latest.inputs;
  error.clear();
  return true;
}
} // namespace Toon
