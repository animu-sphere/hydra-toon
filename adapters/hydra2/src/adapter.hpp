// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <pxr/pxr.h>

#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/base/vt/types.h>
#include <pxr/usd/sdf/path.h>

#include <toon/render_world.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

// CPU-readable AOV storage is the portable Hydra presentation baseline. A
// generated renderer may replace the copy with explicit Hgi/Vulkan interop,
// but that is an optional project-owned capability rather than this seam.
class HdToonRenderBuffer final : public HdRenderBuffer {
public:
  explicit HdToonRenderBuffer(const SdfPath& id);

  bool Allocate(const GfVec3i& dimensions, HdFormat format,
      bool multi_sampled) override;
  unsigned int GetWidth() const override;
  unsigned int GetHeight() const override;
  unsigned int GetDepth() const override;
  HdFormat GetFormat() const override;
  bool IsMultiSampled() const override;
  void* Map() override;
  void Unmap() override;
  bool IsMapped() const override;
  void Resolve() override;
  bool IsConverged() const override;

  // The sources have their origin at the top left, as the renderer's
  // products do; Hydra buffers start at the bottom row, so rows are flipped,
  // and a source of another size is resampled to the nearest pixel.
  bool WriteColor(const std::vector<std::uint8_t>& rgba8,
      std::uint32_t source_width, std::uint32_t source_height);
  bool WriteDepth(const std::vector<float>& depth,
      std::uint32_t source_width, std::uint32_t source_height);
  bool WriteIds(std::int32_t value);
  void SetConverged(bool converged);

protected:
  void _Deallocate() override;

private:
  mutable std::mutex mutex_;
  GfVec3i dimensions_{0};
  HdFormat format_{HdFormatInvalid};
  bool multi_sampled_{};
  bool converged_{};
  std::size_t map_count_{};
  std::vector<std::uint8_t> data_;
};

class HdToonAdapterState;

// The image a material samples in one role: the path the session's resolver
// gave its asset, empty when the role has none or it did not resolve, and
// whether the role reads it as colour or data. Two roles or materials that
// name the same key share one texture.
struct HdToonTextureKey {
  std::string path;
  Toon::ToonTextureEncoding encoding = Toon::ToonTextureEncoding::Srgb;

  friend auto operator<=>(const HdToonTextureKey&,
      const HdToonTextureKey&) = default;
};

// The roles a material samples a texture in: base colour, shade multiply,
// outline width multiply, MatCap, rim multiply, emissive, normal,
// shading shift and UV animation mask.
constexpr std::size_t kHdToonTextureRoles = 9;

// A material's texture references, one per role, in role order.
std::array<Toon::ToonTextureRef*, kHdToonTextureRoles> HdToonTextureRefs(
    Toon::ToonMaterial& material);

// A material as its prim states it: the normalized values, whose texture
// references hold no texture id yet, and the image each reference names.
struct HdToonMaterialSource {
  Toon::ToonMaterial values;
  // In role order, as HdToonTextureRefs(values) lists the references.
  std::array<HdToonTextureKey, kHdToonTextureRoles> textures;
};

// Selects a material's model and normalizes its values from its Hydra prim's
// own data sources (material policy §2–§4): a `vrm/mtoon` container is
// MToon, read in the locator hierarchy `vrmImaging` froze, with a texture in
// every role; anything else is PreviewSurface. A null container is the
// fallback material.
HdToonMaterialSource HdToonReadMaterial(
    const HdContainerDataSourceHandle& prim);

// Decodes an image to RGBA8 with its first row at the top. False, leaving
// `texture` as it was, when it cannot be read or its format is not 8- or
// 16-bit.
bool HdToonLoadTexture(const std::string& path,
    Toon::ToonTextureEncoding encoding, Toon::ToonTexture& texture);

// Whether a material prim's dirtied locators are a value-only change: `vrm`
// values without `material`, which emulation turns into no dirty bit
// (material policy §8).
bool HdToonIsValueOnlyChange(const HdDataSourceLocatorSet& locators);

class HdToonMaterial final : public HdMaterial {
public:
  HdToonMaterial(const SdfPath& id, std::shared_ptr<HdToonAdapterState> state);
  ~HdToonMaterial() override;

  void Sync(HdSceneDelegate* delegate, HdRenderParam* render_param,
      HdDirtyBits* dirty_bits) override;
  HdDirtyBits GetInitialDirtyBitsMask() const override;

  // A value-only change, which never reaches Sync (material policy §8): the
  // delegate calls this from Update() for a material whose `vrm` locators
  // alone were dirtied.
  void SyncValues(const HdSceneIndexBase& terminal);

  // What the last Sync or SyncValues read.
  const Toon::ToonMaterial& GetToonMaterial() const;

private:
  void Read(const HdContainerDataSourceHandle& prim);

  std::shared_ptr<HdToonAdapterState> state_;
  Toon::MaterialId material_;
  Toon::ToonMaterial values_;
  // Held while this material samples them, in role order.
  std::array<Toon::TextureId, kHdToonTextureRoles> textures_{};
};

// One evaluated subshape slot of a resident mesh, in the order of its morph
// weights: the binding's shapes in order, each shape's subshapes by weight.
struct HdToonResidentSubshape {
  // The mesh's `skel:blendShapes` name and the BlendShape prim it targets.
  TfToken blend_shape;
  SdfPath target;
  // 1 for the primary shape, otherwise the inbetween's weight.
  float weight = 1.0F;

  friend bool operator==(const HdToonResidentSubshape&,
      const HdToonResidentSubshape&) = default;
};

// A resident linear blend skin's identities, in palette order.
struct HdToonResidentSkin {
  SdfPath skeleton;
  // The mesh's `skel:joints`, or the skeleton's joints when it names none.
  VtTokenArray joints;
  // Per palette entry, the joint's index in the skeleton, or -1 when the
  // skeleton does not name it and its palette entry stays the identity.
  std::vector<std::int32_t> skeleton_joints;
  // Per palette entry, the inverse of the skeleton's bind transform, in
  // stage units; the identity where `skeleton_joints` is -1.
  std::vector<Toon::Matrix4> inverse_bind;
  // As of the last pose sync: stage world to skeleton space, and skeleton
  // space to the mesh's own, as the mesh's skin pose uses it.
  Toon::Matrix4 world_to_skeleton;
  Toon::Matrix4 skeleton_to_mesh;
};

struct HdToonResidentMesh {
  Toon::MeshId mesh = 0;
  SdfPath path;
  // The bound material's path and its renderer id; 0 when no material
  // prim under that path exists and the mesh draws the fallback.
  SdfPath material_path;
  Toon::MaterialId material = 0;
  // Present for a mesh skinned on the GPU; absent for a rigid mesh and for
  // one whose points usdSkelImaging's CPU kernel computes.
  std::optional<HdToonResidentSkin> skin;
  std::vector<HdToonResidentSubshape> subshapes;
};

struct HdToonResidentMaterial {
  Toon::MaterialId material = 0;
  SdfPath path;
};

// The Hydra identities behind a committed scene's resident meshes and
// materials, ordered by renderer id, for a host that binds evaluated avatar
// targets to resident renderer slots (design policy §34). Read from the
// terminal scene index alongside the values; no format, expression or
// humanoid meaning is applied.
struct HdToonResidentTargets {
  std::vector<HdToonResidentMesh> meshes;
  std::vector<HdToonResidentMaterial> materials;
};

class HdToonRenderDelegate final : public HdRenderDelegate {
public:
  explicit HdToonRenderDelegate(const HdRenderSettingsMap& settings = {});
  ~HdToonRenderDelegate() override;

  const TfTokenVector& GetSupportedRprimTypes() const override;
  const TfTokenVector& GetSupportedSprimTypes() const override;
  const TfTokenVector& GetSupportedBprimTypes() const override;
  HdResourceRegistrySharedPtr GetResourceRegistry() const override;
  HdRenderPassSharedPtr CreateRenderPass(
      HdRenderIndex* index, const HdRprimCollection& collection) override;
  HdInstancer* CreateInstancer(HdSceneDelegate* delegate,
      const SdfPath& id) override;
  void DestroyInstancer(HdInstancer* instancer) override;
  HdRprim* CreateRprim(const TfToken& type_id,
      const SdfPath& rprim_id) override;
  void DestroyRprim(HdRprim* rprim) override;
  HdSprim* CreateSprim(const TfToken& type_id,
      const SdfPath& sprim_id) override;
  HdSprim* CreateFallbackSprim(const TfToken& type_id) override;
  void DestroySprim(HdSprim* sprim) override;
  HdBprim* CreateBprim(const TfToken& type_id,
      const SdfPath& bprim_id) override;
  HdBprim* CreateFallbackBprim(const TfToken& type_id) override;
  void DestroyBprim(HdBprim* bprim) override;
  void CommitResources(HdChangeTracker* tracker) override;
  HdAovDescriptor GetDefaultAovDescriptor(const TfToken& name) const override;
  // `toon:msaaSamples`: MSAA samples per pixel, 4 by default; 1 turns
  // anti-aliasing off. `toon:metersPerUnit`: the stage's metersPerUnit,
  // which Hydra does not carry, 1 by default: it turns a world-coordinates
  // outline's width in metres into the stage's units.
  HdRenderSettingDescriptorList GetRenderSettingDescriptors() const override;
  void SetTerminalSceneIndex(
      const HdSceneIndexBaseRefPtr& terminal_scene_index) override;
  void Update() override;

  // The scene the next frame will draw, committed without rendering it: for
  // a check that needs no GPU, or a host that draws the scene itself, as the
  // viewport does. The second fills `snapshot`, reusing its storage. Neither
  // sets the camera or the scene's unit; the render pass does.
  Toon::FrameSnapshot CommitScene();
  void CommitScene(Toon::FrameSnapshot& snapshot);

  // Host expression path, without USD authoring or Hydra sync. Ids come
  // from CommitScene and are valid only for this delegate's scene lifetime.
  // Overrides survive ordinary value syncs; clear restores scene values.
  bool SetMeshMorphWeightsOverride(Toon::MeshId mesh, std::vector<float> weights);
  void ClearMeshMorphWeightsOverride(Toon::MeshId mesh);
  bool SetMaterialParametersOverride(Toon::MaterialId material, const Toon::ToonMaterial& values);
  void ClearMaterialParametersOverride(Toon::MaterialId material);

  // What the last sync made resident, with the ids CommitScene publishes.
  // Describe after the same sync as the commit a binding is prepared from;
  // a later structural sync can change both.
  HdToonResidentTargets DescribeResidentTargets() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  HdResourceRegistrySharedPtr resources_;
};

PXR_NAMESPACE_CLOSE_SCOPE
