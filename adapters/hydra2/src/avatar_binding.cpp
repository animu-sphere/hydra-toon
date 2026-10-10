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
      {"inputs:vrm:mtoon:shadeColorFactor", Field::ShadeColor},
      {"inputs:vrm:mtoon:shadingShiftFactor", Field::ShadingShift},
      {"inputs:vrm:mtoon:shadingToonyFactor", Field::ShadingToony},
      {"inputs:vrm:mtoon:matcapFactor", Field::Matcap},
      {"inputs:vrm:mtoon:parametricRimColorFactor", Field::RimColor},
      {"inputs:vrm:mtoon:outlineWidthFactor", Field::OutlineWidth},
      {"inputs:vrm:mtoon:outlineColorFactor", Field::OutlineColor}};
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

  // Materials: the resident material at the path, the host's field.
  std::map<std::string, Toon::MaterialId> materials;
  for (const HdToonResidentMaterial& material : resident.materials) {
    materials.emplace(material.path.GetString(), material.material);
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
    match.bindings.materials.push_back({i, material->second, input->field});
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
