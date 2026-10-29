// SPDX-License-Identifier: Apache-2.0
#include "avatar_binding.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

using Key = std::pair<std::string, std::string>;

// A null identity matches nothing and is reported like an unknown one.
std::string Id(const char* id) {
  return id ? id : "";
}

std::string Subject(const std::string& owner, const std::string& target) {
  return owner + " " + target;
}

template <class T>
bool Array(const T* items, std::uint32_t count) {
  return !count || items;
}

} // namespace

const std::vector<HdToonAvatarMaterialInput>& HdToonCanonicalMaterialInputs() {
  using Field = Toon::AvatarMaterialField;
  // `vrmImaging` serves these attributes under the `vrm` locators the
  // delegate reads; glTF's RGBA base colour is stored as RGB and alpha.
  static const std::vector<HdToonAvatarMaterialInput> inputs{
      {"inputs:vrm:material:baseColorFactor", Field::BaseColorRgb},
      {"inputs:vrm:material:baseColorAlphaFactor", Field::Alpha},
      {"inputs:vrm:material:emissiveFactor", Field::Emissive},
      {"inputs:vrm:mtoon:shadeColorFactor", Field::ShadeColor},
      {"inputs:vrm:mtoon:shadingShiftFactor", Field::ShadingShift},
      {"inputs:vrm:mtoon:shadingToonyFactor", Field::ShadingToony},
      {"inputs:vrm:mtoon:matcapFactor", Field::Matcap},
      {"inputs:vrm:mtoon:parametricRimColorFactor", Field::RimColor},
      {"inputs:vrm:mtoon:outlineWidthFactor", Field::OutlineWidth},
      {"inputs:vrm:mtoon:outlineColorFactor", Field::OutlineColor},
      {"inputs:mmd:material:diffuseColor", Field::BaseColor},
      {"inputs:mmd:material:specularColor", Field::MmdSpecularColor},
      {"inputs:mmd:material:specularPower", Field::MmdSpecularPower},
      {"inputs:mmd:material:ambientColor", Field::MmdAmbientColor},
      {"inputs:mmd:material:edgeColor", Field::MmdEdgeColor},
      {"inputs:mmd:material:edgeSize", Field::MmdEdgeSize},
      {"mmd:textureTint:multiply", Field::MmdTextureMultiply},
      {"mmd:textureTint:add", Field::MmdTextureAdd},
      {"mmd:sphereTint:multiply", Field::MmdSphereMultiply},
      {"mmd:sphereTint:add", Field::MmdSphereAdd},
      {"mmd:toonTint:multiply", Field::MmdToonMultiply},
      {"mmd:toonTint:add", Field::MmdToonAdd}};
  return inputs;
}

HdToonAvatarTargetMatch HdToonMatchAvatarTargets(const ArStateView& layout,
    const HdToonResidentTargets& resident,
    const std::vector<HdToonAvatarMaterialInput>& inputs) {
  HdToonAvatarTargetMatch match;
  auto mismatch = [&](std::string subject, std::string reason) {
    match.mismatches.push_back({std::move(subject), std::move(reason)});
  };
  if (!Array(layout.joints, layout.joint_count) ||
      !Array(layout.blend_shapes, layout.blend_shape_count) ||
      !Array(layout.materials, layout.material_count) ||
      !Array(layout.visibility, layout.visibility_count)) {
    mismatch(Id(layout.layout_id), "runtime layout array is missing");
    return match;
  }
#if AR_ABI_VERSION >= 5
  if (!Array(layout.vertex_uvs, layout.vertex_uv_count)) {
    mismatch(Id(layout.layout_id), "runtime primary UV array is missing");
    return match;
  }
  // Match absolute st by opaque mesh path and source point identity, never
  // by runtime array order or resident split-vertex order.
  std::map<std::string, std::map<std::uint32_t, std::uint32_t>> uvMeshes;
  std::set<std::string> invalidUvMeshes;
  for (std::uint32_t i = 0; i < layout.vertex_uv_count; ++i) {
    const auto& uv = layout.vertex_uvs[i];
    const std::string mesh = Id(uv.mesh_id);
    if (!uvMeshes[mesh].emplace(uv.vertex_index, i).second || mesh.empty())
      invalidUvMeshes.insert(mesh);
  }
  for (const auto& [path, slots] : uvMeshes) {
    const HdToonResidentMesh* target = nullptr;
    bool ambiguous = false;
    for (const auto& mesh : resident.meshes) {
      if (mesh.path.GetString() != path) continue;
      ambiguous |= target != nullptr;
      target = &mesh;
    }
    if (invalidUvMeshes.contains(path) || slots.rbegin()->first != slots.size() - 1) {
      mismatch(path, "runtime primary UV source identities are not unique and dense");
      continue;
    }
    if (!target || ambiguous || !target->primary_uv) {
      mismatch(path, !target ? "no resident mesh at this path" :
          ambiguous ? "resident mesh path is ambiguous" :
          "no resident point-domain primary UV mapping");
      continue;
    }
    const auto& mapping = *target->primary_uv;
    if (mapping.source_point_count != slots.size() || mapping.vertices.empty()) {
      mismatch(path, "primary UV source point count differs from the resident mapping");
      continue;
    }
    Toon::AvatarUvBinding binding{target->mesh, {}};
    std::set<std::uint32_t> covered;
    for (const auto point : mapping.vertices) {
      const auto slot = slots.find(point);
      if (slot == slots.end()) break;
      binding.sources.push_back(slot->second);
      covered.insert(point);
    }
    if (binding.sources.size() != mapping.vertices.size() || covered.size() != slots.size()) {
      mismatch(path, "resident primary UV mapping does not cover every source point");
      continue;
    }
    match.bindings.uvs.push_back(std::move(binding));
  }
#endif

  // Skins: each GPU-skinned palette of a skeleton the runtime publishes.
  std::map<Key, std::uint32_t> joints;
  std::set<std::string> skeletons, driven;
  for (std::uint32_t i = 0; i < layout.joint_count; ++i) {
    const ArJoint& joint = layout.joints[i];
    joints.emplace(Key{Id(joint.skeleton_id), Id(joint.joint_id)}, i);
    skeletons.insert(Id(joint.skeleton_id));
  }
  for (const HdToonResidentMesh& mesh : resident.meshes) {
    if (!mesh.skin) continue;
    const HdToonResidentSkin& skin = *mesh.skin;
    const std::string skeleton = skin.skeleton.GetString();
    if (!skeletons.contains(skeleton)) continue;
    Toon::AvatarSkinBinding binding{mesh.mesh, {}, skin.inverse_bind,
        skin.world_to_skeleton, skin.skeleton_to_mesh};
    bool complete = true;
    for (std::size_t i = 0; i < skin.joints.size(); ++i) {
      const std::string token = skin.joints[i].GetString();
      const auto joint = joints.find(Key{skeleton, token});
      if (skin.skeleton_joints[i] < 0 || joint == joints.end()) {
        mismatch(Subject(mesh.path.GetString(), token),
            skin.skeleton_joints[i] < 0
                ? "palette joint is not in its skeleton"
                : "runtime skeleton does not publish this palette joint");
        complete = false;
        continue;
      }
      binding.joints.push_back(joint->second);
    }
    if (!complete) continue;
    match.bindings.skins.push_back(std::move(binding));
    driven.insert(skeleton);
  }
  for (const std::string& skeleton : skeletons) {
    if (!driven.contains(skeleton)) {
      mismatch(skeleton, "runtime skeleton drives no resident GPU skin");
    }
  }

  // Morphs: the primary and inbetween slots described for the shape.
  std::map<Key, Toon::AvatarMorphBinding> shapes;
  for (const HdToonResidentMesh& mesh : resident.meshes) {
    for (std::uint32_t slot = 0; slot < mesh.subshapes.size(); ++slot) {
      const HdToonResidentSubshape& subshape = mesh.subshapes[slot];
      auto& binding = shapes[Key{mesh.path.GetString(),
          subshape.blend_shape.GetString()}];
      binding.mesh = mesh.mesh;
      if (subshape.weight == 1.0F) {
        binding.weight = slot;
      } else {
        binding.inbetweens.push_back({slot, subshape.weight});
      }
    }
  }
  for (std::uint32_t i = 0; i < layout.blend_shape_count; ++i) {
    const ArBlendShape& shape = layout.blend_shapes[i];
    const Key key{Id(shape.mesh_id), Id(shape.target_id)};
    const auto found = shapes.find(key);
    if (found == shapes.end()) {
      mismatch(Subject(key.first, key.second),
          "no resident subshape slot for this blend shape");
      continue;
    }
    Toon::AvatarMorphBinding binding = found->second;
    binding.source = i;
    match.bindings.morphs.push_back(std::move(binding));
  }

  // Materials: the resident material at the path, the host's field, and
  // for emission the strength the delegate scales it by.
  std::map<std::string, const HdToonResidentMaterial*> materials;
  for (const HdToonResidentMaterial& material : resident.materials) {
    materials.emplace(material.path.GetString(), &material);
  }
  for (std::uint32_t i = 0; i < layout.material_count; ++i) {
    const ArMaterialInput& value = layout.materials[i];
    const Key key{Id(value.material_id), Id(value.input_id)};
    const auto material = materials.find(key.first);
    const auto input = std::find_if(inputs.begin(), inputs.end(),
        [&](const HdToonAvatarMaterialInput& x) { return x.input == key.second; });
    if (material == materials.end() || input == inputs.end()) {
      mismatch(Subject(key.first, key.second),
          material == materials.end() ? "no resident material at this path"
                                      : "no renderer field for this canonical input");
      continue;
    }
    match.bindings.materials.push_back({i, material->second->material,
        input->field,
        input->field == Toon::AvatarMaterialField::Emissive
            ? material->second->emissive_strength
            : 1.0F});
  }

  // Visibility: the resident mesh whose path is the target.
  std::map<std::string, Toon::MeshId> meshes;
  for (const HdToonResidentMesh& mesh : resident.meshes) {
    meshes.emplace(mesh.path.GetString(), mesh.mesh);
  }
  for (std::uint32_t i = 0; i < layout.visibility_count; ++i) {
    const std::string target = Id(layout.visibility[i].target_id);
    const auto mesh = meshes.find(target);
    if (mesh == meshes.end()) {
      mismatch(target, "no resident mesh at this path");
      continue;
    }
    match.bindings.visibility.push_back({i, mesh->second});
  }
  return match;
}

PXR_NAMESPACE_CLOSE_SCOPE
