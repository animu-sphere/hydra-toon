// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <avatarRuntime/api.h>
#include <toon/extraction.hpp>

namespace Toon {
static_assert(AR_ABI_VERSION == 3, "AvatarState requires experimental runtime ABI revision 3");

// The supplying library/function table must outlive all retained snapshots.
// Reset retains before replacing; failure leaves the previous view alive.
class RetainedAvatarSnapshot {
public:
  RetainedAvatarSnapshot() = default;
  ~RetainedAvatarSnapshot();
  RetainedAvatarSnapshot(const RetainedAvatarSnapshot&) = delete;
  RetainedAvatarSnapshot& operator=(const RetainedAvatarSnapshot&) = delete;
  RetainedAvatarSnapshot(RetainedAvatarSnapshot&& other) noexcept;
  RetainedAvatarSnapshot& operator=(RetainedAvatarSnapshot&& other) noexcept;
  bool Reset(const ArRuntimeApi& api, ArSnapshot snapshot, std::string& error);
  void Clear();
  [[nodiscard]] const ArStateView* View() const {
    return handle_ ? &view_ : nullptr;
  }

private:
  ArRuntimeApi api_{};
  ArSnapshot handle_ = 0;
  ArStateView view_{};
};

struct AvatarSkinBinding {
  MeshId mesh = 0;
  // Runtime joint indices in resident palette order. Inverse binds use the
  // renderer scene unit. Parent-local runtime translations are metres.
  std::vector<std::uint32_t> joints;
  std::vector<Matrix4> inverse_bind;
  // Converts runtime world (after unit conversion) to palette skeleton space.
  Matrix4 world_to_skeleton;
  Matrix4 skeleton_to_mesh;
};
struct AvatarMorphBinding {
  std::uint32_t source = 0; // already resolved runtime blend-shape slot
  MeshId mesh = 0;
  std::uint32_t weight = 0; // resident evaluated subshape slot
};
enum class AvatarMaterialField {
  BaseColor,
  Emissive,
  ShadeColor,
  OutlineColor,
  Matcap,
  RimColor,
  Alpha,
  ShadingShift,
  ShadingToony,
  OutlineWidth,
  BaseTextureOffset,
  BaseTextureScale,
  BaseTextureRotation,
  // Separate canonical RGB and alpha inputs may bind independently.
  BaseColorRgb
};
struct AvatarMaterialBinding {
  std::uint32_t source = 0;
  MaterialId material = 0;
  // The host maps the owner's canonical input id to a renderer field.
  // No source-name, expression or gaze interpretation happens here.
  AvatarMaterialField field = AvatarMaterialField::BaseColor;
};
struct AvatarVisibilityBinding {
  std::uint32_t source = 0;
  MeshId mesh = 0;
};
struct AvatarBindings {
  std::vector<AvatarSkinBinding> skins;
  std::vector<AvatarMorphBinding> morphs;
  std::vector<AvatarMaterialBinding> materials;
  std::vector<AvatarVisibilityBinding> visibility;
};

struct AvatarFrameIdentity {
  std::uint64_t instance = 0, frame = 0, generation = 0, input_revision = 0;
  std::string layout;
  std::uint64_t layout_version = 0, binding_epoch = 0;
  bool active = false;
};

// Serialized by the external host. No evaluator calls or USD authoring.
// Bind copies opaque channel identities and the revision-3 layout token;
// every update checks both. Reset generations may advance with
// the same layout; a new instance requires Bind. The nonzero host epoch must
// change on scene replacement, even when numeric renderer ids are reused.
class AvatarStateAdapter {
public:
  bool Bind(const ArStateView& layout, const FrameSnapshot& scene,
      std::uint64_t epoch, AvatarBindings bindings, std::string& error);
  // Complete resolved values over the latest *unoverridden* scene baseline.
  // On failure output and adapter history are unchanged. Visibility changes
  // require ordinary extraction; ApplyFastSnapshot rejects membership changes.
  // Input times come from the host: runtime evaluation_seconds is not a source
  // timestamp. This API copies records and allocates changed dynamic arrays.
  bool Apply(const RetainedAvatarSnapshot& state, const FrameSnapshot& scene,
      std::uint64_t epoch, FrameSnapshot& output, std::string& error);
  // Release uses the same baseline route, with fresh revisions when needed.
  bool Release(const FrameSnapshot& scene, std::uint64_t epoch,
      FrameSnapshot& output, std::string& error);
  void Clear();
  [[nodiscard]] const AvatarFrameIdentity& IdentityInfo() const {
    return identity_;
  }

private:
  struct Identity {
    std::string owner, target;
    std::int32_t parent = -1;
    std::uint32_t type = 0;
    friend bool operator==(const Identity&, const Identity&) = default;
  };
  bool CheckScene(const FrameSnapshot& scene, std::uint64_t epoch, std::string& error) const;
  void Publish(FrameSnapshot candidate, FrameSnapshot& output);
  std::vector<Identity> joints_, morphs_, materials_, visibility_;
  AvatarBindings bindings_;
  AvatarFrameIdentity identity_;
  std::vector<Identity> capabilities_;
  FrameSnapshot bound_, previous_;
  std::uint64_t epoch_ = 0, instance_ = 0, generation_ = 0, frame_ = 0;
};
} // namespace Toon
