// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "adapter.hpp"

#include <toon/fast/avatar_state.hpp>

#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

// What a host maps one canonical material input id to. The ids are the
// source owner's (`ArMaterialInput::input_id`); the renderer gives them no
// meaning beyond this table.
struct HdToonAvatarMaterialInput {
  std::string input;
  Toon::AvatarMaterialField field = Toon::AvatarMaterialField::BaseColor;
};

// The canonical Material attributes the delegate's own VRM normalization
// reads (`material.cpp`), with the field each fills. The delegate scales
// `emissiveFactor` by `emissiveStrength`, so the matcher gives an `Emissive`
// binding the resident material's strength as its scale.
const std::vector<HdToonAvatarMaterialInput>& HdToonCanonicalMaterialInputs();

// Why a runtime output, or a resident skin its runtime skeleton should
// drive, has no binding. `subject` names the runtime or resident identity.
struct HdToonAvatarMismatch {
  std::string subject;
  std::string reason;
};

struct HdToonAvatarTargetMatch {
  Toon::AvatarBindings bindings;
  // Empty when every runtime output and every resident skin of a published
  // skeleton is bound. Bind still has the last word on the scene's values.
  std::vector<HdToonAvatarMismatch> mismatches;
};

// Pairs a runtime layout's opaque identities with a resident description
// (design policy §34): skeleton path and joint tokens to each GPU-skinned
// palette, mesh path and `skel:blendShapes` token to the shape's described
// subshape slots, material path and canonical input to a material field,
// a visibility target to the mesh with that path, and point-domain primary
// UV identities through the described corner/tangent-seam vertex map.
// A mesh whose skeleton
// the runtime does not publish keeps its scene pose. Nothing evaluates a
// format, expression or inbetween here.
HdToonAvatarTargetMatch HdToonMatchAvatarTargets(const ArStateView& layout,
    const HdToonResidentTargets& resident,
    const std::vector<HdToonAvatarMaterialInput>& inputs);

PXR_NAMESPACE_CLOSE_SCOPE
