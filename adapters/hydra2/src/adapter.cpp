// SPDX-License-Identifier: Apache-2.0
#include "adapter.hpp"

#include <pxr/pxr.h>

#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/imaging/glf/simpleLight.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/extComputation.h>
#include <pxr/imaging/hd/extComputationUtils.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/imaging/hd/sceneIndexObserver.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usdLux/blackbody.h>

#include <toon/extraction.hpp>
#include <toon/render_world.hpp>
#include <toon/vulkan_backend.hpp>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

std::filesystem::path PluginDirectory() {
#ifdef _WIN32
  static int module_anchor;
  HMODULE module{};
  const auto address = reinterpret_cast<LPCWSTR>(&module_anchor);
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          address, &module)) {
    throw std::runtime_error("could not locate the hdToon module");
  }
  std::wstring path(32768, L'\0');
  const DWORD length =
      GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) {
    throw std::runtime_error("could not resolve the hdToon module path");
  }
  path.resize(length);
  return std::filesystem::path(path).parent_path();
#else
  static int module_anchor;
  Dl_info info{};
  if (dladdr(&module_anchor, &info) == 0 || info.dli_fname == nullptr) {
    throw std::runtime_error("could not locate the hdToon module");
  }
  return std::filesystem::path(info.dli_fname).parent_path();
#endif
}

void AppendHostEvidence(std::uint64_t frame_index,
    const Toon::OffscreenStatistics& statistics,
    std::uint32_t width, std::uint32_t height,
    std::size_t buffers_written,
    const Toon::FrameSnapshot& snapshot, const Toon::DrawList& draws) {
  const char* path = std::getenv("TOON_HYDRA_EVIDENCE");
  if (path == nullptr || *path == '\0') {
    return;
  }
  // Which model each material selected, so a host session shows whether a
  // VRM material reached MToon or fell back to PreviewSurface (material
  // policy §3) without a probe of its own.
  std::size_t preview_materials{};
  std::size_t mtoon_materials{};
  for (const Toon::MaterialSnapshot& material : snapshot.materials) {
    if (material.material.model == Toon::ToonShadingModel::PreviewSurface) {
      ++preview_materials;
    } else if (material.material.model == Toon::ToonShadingModel::MToon) {
      ++mtoon_materials;
    }
  }
  // And how many of this frame's draws selected MToon, how many of those
  // went through mtoon_transparent, how many added mtoon_outline's hull,
  // how many were skinned on the GPU, and how many drew authored normals.
  std::size_t mtoon_draws{};
  std::size_t transparent_draws{};
  std::size_t outline_draws{};
  std::size_t skinned_draws{};
  std::size_t authored_normal_draws{};
  for (const Toon::MeshSnapshot& mesh : draws.draws) {
    skinned_draws += Toon::IsSkinned(mesh) ? 1U : 0U;
    authored_normal_draws += mesh.authored_normals ? 1U : 0U;
    const auto material = std::lower_bound(snapshot.materials.begin(),
        snapshot.materials.end(), mesh.material,
        [](const Toon::MaterialSnapshot& entry, Toon::MaterialId id) {
          return entry.id < id;
        });
    if (material != snapshot.materials.end() &&
        material->id == mesh.material &&
        material->material.model == Toon::ToonShadingModel::MToon) {
      ++mtoon_draws;
      transparent_draws += Toon::IsTransparent(material->material) ? 1U : 0U;
      outline_draws += Toon::HasOutline(material->material) ? 1U : 0U;
    }
  }
  std::ofstream output(path, std::ios::binary | std::ios::app);
  if (!output) {
    TF_WARN("Could not append Toon Hydra evidence to %s", path);
    return;
  }
  output << "frame=" << frame_index
         << " completion=" << statistics.completion
         << " scene_revision=" << snapshot.revision
         << " width=" << width
         << " height=" << height
         << " buffers_written=" << buffers_written
         << " pipelines=" << statistics.pipelines_created
         << " samples=" << statistics.samples
         << " target_allocations=" << statistics.target_allocations
         << " topology_uploads=" << statistics.topology_uploads
         << " point_uploads=" << statistics.point_uploads
         << " material_writes=" << statistics.material_writes
         << " texture_uploads=" << statistics.texture_uploads
         << " skin_uploads=" << statistics.skin_uploads
         << " pose_writes=" << statistics.pose_writes
         << " textures=" << snapshot.textures.size()
         << " materials_preview=" << preview_materials
         << " materials_mtoon=" << mtoon_materials
         << " draws=" << draws.draws.size()
         << " draws_mtoon=" << mtoon_draws
         << " draws_transparent=" << transparent_draws
         << " draws_outline=" << outline_draws
         << " draws_skinned=" << skinned_draws
         << " draws_authored_normals=" << authored_normal_draws << '\n';
}

// GfMatrix4d is row-major for row vectors, so its storage order is already
// the column-major, column-vector order of Toon::Matrix4.
Toon::Matrix4 ToToon(const GfMatrix4d& matrix) {
  Toon::Matrix4 result;
  const double* data = matrix.data();
  for (std::size_t index = 0; index < result.m.size(); ++index) {
    result.m[index] = static_cast<float>(data[index]);
  }
  return result;
}

Toon::Matrix4 ToToon(const GfMatrix4f& matrix) {
  Toon::Matrix4 result;
  std::copy_n(matrix.data(), result.m.size(), result.m.begin());
  return result;
}

// The inputs usdSkelImaging gives its skinning computations, spelled here so
// the adapter links nothing of UsdSkel imaging.
TF_DEFINE_PRIVATE_TOKENS(SkinTokens,
    (restPoints)
    (geomBindXform)
    (influences)
    (numInfluencesPerComponent)
    (hasConstantInfluences)
    (blendShapeOffsets)
    (blendShapeOffsetRanges)
    (blendShapeWeights)
    (skinningXforms)
    (skinningDualQuats)
    (skelLocalToWorld)
    (primWorldToLocal)
    (restNormals)
    (hasFaceVaryingNormals));

// The delegate's render settings.
TF_DEFINE_PRIVATE_TOKENS(SettingTokens,
    ((msaaSamples, "toon:msaaSamples"))
    ((metersPerUnit, "toon:metersPerUnit"))
    ((timeSeconds, "toon:timeSeconds")));

constexpr int kDefaultSamples = static_cast<int>(Toon::RenderOptions{}.samples);

// The MSAA samples per pixel `toon:msaaSamples` asks for: any number a host
// can set, at least 1.
std::uint32_t RequestedSamples(const HdRenderDelegate& delegate) {
  const VtValue value = delegate.GetRenderSetting(SettingTokens->msaaSamples);
  const VtValue samples = VtValue::Cast<int>(value);
  const int requested =
      samples.IsHolding<int>() ? samples.UncheckedGet<int>() : kDefaultSamples;
  return static_cast<std::uint32_t>(std::max(requested, 1));
}

// The stage's metersPerUnit as `toon:metersPerUnit` states it. Hydra does
// not carry a stage's metadata, so the host that opened the stage sets it;
// 1, a VRM avatar's metre, until it does. A value that is not positive and
// finite leaves the scene's unit as it was.
float RequestedMetersPerUnit(const HdRenderDelegate& delegate) {
  const VtValue value =
      VtValue::Cast<double>(delegate.GetRenderSetting(
          SettingTokens->metersPerUnit));
  return value.IsHolding<double>()
             ? static_cast<float>(value.UncheckedGet<double>())
             : 1.0F;
}

double RequestedTimeSeconds(const HdRenderDelegate& delegate) {
  const auto value = VtValue::Cast<double>(
      delegate.GetRenderSetting(SettingTokens->timeSeconds));
  return value.IsHolding<double>() ? value.UncheckedGet<double>() : 0.0;
}

} // namespace

class HdToonAdapterState {
public:
  Toon::MeshId CreateMesh() {
    std::scoped_lock lock(mutex_);
    return world_.CreateMesh();
  }

  void RemoveMesh(Toon::MeshId mesh) {
    std::scoped_lock lock(mutex_);
    world_.RemoveMesh(mesh);
    bindings_.erase(mesh);
  }

  void SetMeshTopology(Toon::MeshId mesh,
      std::vector<std::uint32_t> triangles) {
    std::scoped_lock lock(mutex_);
    world_.SetMeshTopology(mesh, std::move(triangles));
  }

  void SetMeshPoints(Toon::MeshId mesh, std::vector<Toon::Float3> points) {
    std::scoped_lock lock(mutex_);
    world_.SetMeshPoints(mesh, std::move(points));
  }

  void SetMeshUVs(Toon::MeshId mesh, std::vector<Toon::Float2> uvs) {
    std::scoped_lock lock(mutex_);
    world_.SetMeshUVs(mesh, std::move(uvs));
  }

  void SetMeshNormals(Toon::MeshId mesh, std::vector<Toon::Float3> normals) {
    std::scoped_lock lock(mutex_);
    world_.SetMeshNormals(mesh, std::move(normals));
  }

  void SetMeshTransform(Toon::MeshId mesh, const Toon::Matrix4& transform) {
    std::scoped_lock lock(mutex_);
    world_.SetMeshTransform(mesh, transform);
  }

  void SetMeshColor(Toon::MeshId mesh, Toon::Float3 color) {
    std::scoped_lock lock(mutex_);
    world_.SetMeshColor(mesh, color);
  }

  void SetMeshVisible(Toon::MeshId mesh, bool visible) {
    std::scoped_lock lock(mutex_);
    world_.SetMeshVisible(mesh, visible);
  }

  void SetMeshSkin(Toon::MeshId mesh, Toon::ToonSkin skin) {
    std::scoped_lock lock(mutex_);
    world_.SetMeshSkin(mesh, std::move(skin));
  }

  void SetMeshSkinPose(Toon::MeshId mesh, Toon::ToonSkinPose pose) {
    std::scoped_lock lock(mutex_);
    world_.SetMeshSkinPose(mesh, std::move(pose));
  }

  void SetTimeSeconds(double seconds) {
    std::scoped_lock lock(mutex_);
    world_.SetTimeSeconds(seconds);
  }

  Toon::LightId CreateLight() {
    std::scoped_lock lock(mutex_);
    return world_.CreateLight();
  }

  void RemoveLight(Toon::LightId light) {
    std::scoped_lock lock(mutex_);
    world_.RemoveLight(light);
  }

  void SetLight(Toon::LightId light, const Toon::ToonLight& values) {
    std::scoped_lock lock(mutex_);
    world_.SetLight(light, values);
  }

  Toon::FrameSnapshot Commit() {
    std::scoped_lock lock(mutex_);
    return world_.Commit();
  }

  void Commit(Toon::FrameSnapshot& snapshot) {
    std::scoped_lock lock(mutex_);
    world_.Commit(snapshot);
  }

  // A mesh binds a material by path, and either may be synced first, so a
  // binding is resolved again whenever a material under its path appears or
  // goes away.
  void SetMeshMaterial(Toon::MeshId mesh, const SdfPath& material) {
    std::scoped_lock lock(mutex_);
    bindings_[mesh] = material;
    const auto found = materials_by_path_.find(material);
    world_.SetMeshMaterial(mesh,
        found == materials_by_path_.end() ? 0U : found->second);
  }

  // `path` is empty for the fallback material, which no mesh binds.
  Toon::MaterialId CreateMaterial(const SdfPath& path) {
    std::scoped_lock lock(mutex_);
    const Toon::MaterialId material = world_.CreateMaterial();
    if (!path.IsEmpty()) {
      materials_by_path_[path] = material;
      RebindLocked(path, material);
    }
    return material;
  }

  void RemoveMaterial(Toon::MaterialId material, const SdfPath& path) {
    std::scoped_lock lock(mutex_);
    world_.RemoveMaterial(material);
    const auto found = materials_by_path_.find(path);
    if (found != materials_by_path_.end() && found->second == material) {
      materials_by_path_.erase(found);
      RebindLocked(path, 0U);
    }
  }

  void SetMaterial(Toon::MaterialId material,
      const Toon::ToonMaterial& values) {
    std::scoped_lock lock(mutex_);
    world_.SetMaterial(material, values);
  }

  // One texture per image and encoding, however many materials sample it,
  // decoded when the first of them acquires it and removed when the last
  // releases it. Returns 0 for an empty key or an image that cannot be
  // read. The image is decoded without the lock held.
  Toon::TextureId AcquireTexture(const HdToonTextureKey& key) {
    if (key.path.empty()) {
      return 0;
    }
    {
      std::scoped_lock lock(mutex_);
      if (const Toon::TextureId texture = AcquireLocked(key)) {
        return texture;
      }
      if (unreadable_.count(key) != 0) {
        return 0;
      }
    }
    Toon::ToonTexture decoded;
    const bool read = HdToonLoadTexture(key.path, key.encoding, decoded);
    std::scoped_lock lock(mutex_);
    if (const Toon::TextureId texture = AcquireLocked(key)) {
      return texture;
    }
    if (!read) {
      if (unreadable_.insert(key).second) {
        TF_WARN("Toon could not read texture %s; the material samples "
                "without it", key.path.c_str());
      }
      return 0;
    }
    const Toon::TextureId texture = world_.CreateTexture();
    world_.SetTexture(texture, std::move(decoded));
    textures_[key] = {texture, 1};
    return texture;
  }

  void ReleaseTexture(Toon::TextureId texture) {
    if (texture == 0) {
      return;
    }
    std::scoped_lock lock(mutex_);
    for (auto entry = textures_.begin(); entry != textures_.end(); ++entry) {
      if (entry->second.texture == texture) {
        if (--entry->second.users == 0) {
          world_.RemoveTexture(texture);
          textures_.erase(entry);
        }
        return;
      }
    }
  }

  void Render(const HdRenderPassStateSharedPtr& pass_state,
      std::uint32_t samples, float meters_per_unit, double time_seconds) {
    std::scoped_lock lock(mutex_);
    const HdRenderPassAovBindingVector& bindings =
        pass_state->GetAovBindings();
    // Every AOV is rendered at the colour AOV's resolution; a buffer of
    // another size is resampled when it is written.
    std::uint32_t width{};
    std::uint32_t height{};
    for (const HdRenderPassAovBinding& binding : bindings) {
      auto* buffer = dynamic_cast<HdToonRenderBuffer*>(binding.renderBuffer);
      if (buffer != nullptr &&
          (binding.aovName == HdAovTokens->color || width == 0)) {
        width = buffer->GetWidth();
        height = buffer->GetHeight();
      }
    }
    if (width == 0 || height == 0 || !EnsureRendererLocked(samples)) {
      SetConvergedLocked(bindings, false);
      return;
    }

    world_.SetView({ToToon(pass_state->GetWorldToViewMatrix()),
        ToToon(pass_state->GetProjectionMatrix())});
    world_.SetMetersPerUnit(meters_per_unit);
    world_.SetTimeSeconds(time_seconds);
    world_.Commit(snapshot_);
    Toon::ExtractDrawList(snapshot_, draws_);
    std::string error;
    if (!renderer_->Render(draws_, width, height, color_, depth_, error)) {
      TF_RUNTIME_ERROR("Toon Hydra frame failed: %s", error.c_str());
      SetConvergedLocked(bindings, false);
      return;
    }

    std::size_t buffers_written{};
    for (const HdRenderPassAovBinding& binding : bindings) {
      auto* buffer = dynamic_cast<HdToonRenderBuffer*>(binding.renderBuffer);
      if (buffer == nullptr) {
        continue;
      }
      bool wrote = false;
      if (binding.aovName == HdAovTokens->color) {
        wrote = buffer->WriteColor(color_.payload, color_.width, color_.height);
      } else if (binding.aovName == HdAovTokens->depth) {
        wrote = buffer->WriteDepth(depth_.payload, depth_.width, depth_.height);
      } else if (binding.aovName == HdAovTokens->primId ||
                 binding.aovName == HdAovTokens->instanceId ||
                 binding.aovName == HdAovTokens->elementId) {
        wrote = buffer->WriteIds(-1);
      }
      buffer->SetConverged(wrote);
      if (wrote) {
        ++buffers_written;
      }
    }
    ++frame_index_;
    AppendHostEvidence(frame_index_, renderer_->statistics(), width, height,
        buffers_written, snapshot_, draws_);
  }

private:
  struct SharedTexture {
    Toon::TextureId texture = 0;
    std::size_t users = 0;
  };

  Toon::TextureId AcquireLocked(const HdToonTextureKey& key) {
    const auto found = textures_.find(key);
    if (found == textures_.end()) {
      return 0;
    }
    ++found->second.users;
    return found->second.texture;
  }

  // Linear in the meshes; it runs when a material prim is added or removed,
  // not per frame.
  void RebindLocked(const SdfPath& path, Toon::MaterialId material) {
    for (const auto& [mesh, bound] : bindings_) {
      if (bound == path) {
        world_.SetMeshMaterial(mesh, material);
      }
    }
  }

  // The renderer is created on the first frame, not with the delegate, so a
  // delegate that never renders (discovery, probing) never creates a device.
  // A new sample count takes a new renderer, which uploads the scene again:
  // a settings change, not a per-frame one.
  bool EnsureRendererLocked(std::uint32_t samples) {
    if (samples != samples_) {
      renderer_.reset();
      renderer_failed_ = false;
      samples_ = samples;
    }
    if (renderer_ != nullptr) {
      return true;
    }
    if (renderer_failed_) {
      return false;
    }
    const std::filesystem::path shaders = PluginDirectory() / "shaders";
    Toon::FrameStatus status = Toon::FrameStatus::Fail;
    std::string detail;
    renderer_ = Toon::CreateOffscreenRenderer(
        Toon::SceneShadersIn(shaders.string()), status, detail,
        Toon::RenderOptions{samples});
    if (renderer_ == nullptr) {
      renderer_failed_ = true;
      TF_RUNTIME_ERROR("Toon could not create its Vulkan renderer: %s",
          detail.c_str());
      return false;
    }
    return true;
  }

  static void SetConvergedLocked(const HdRenderPassAovBindingVector& bindings,
      bool converged) {
    for (const HdRenderPassAovBinding& binding : bindings) {
      if (auto* buffer =
              dynamic_cast<HdToonRenderBuffer*>(binding.renderBuffer)) {
        buffer->SetConverged(converged);
      }
    }
  }

  std::mutex mutex_;
  Toon::RenderWorld world_;
  std::unordered_map<SdfPath, Toon::MaterialId, SdfPath::Hash>
      materials_by_path_;
  std::unordered_map<Toon::MeshId, SdfPath> bindings_;
  std::map<HdToonTextureKey, SharedTexture> textures_;
  // Warned about once, and not decoded again.
  std::set<HdToonTextureKey> unreadable_;
  // Reused every frame, so a steady frame allocates nothing.
  Toon::FrameSnapshot snapshot_;
  Toon::DrawList draws_;
  Toon::ColorProduct color_;
  Toon::DepthProduct depth_;
  std::unique_ptr<Toon::OffscreenRenderer> renderer_;
  // What `renderer_` was, or failed to be, created for.
  std::uint32_t samples_{};
  bool renderer_failed_{};
  std::uint64_t frame_index_{};
};

namespace {

// An ext computation that counts the syncs that changed its inputs. Sprims
// sync before Rprims, so a mesh learns from the count whether its skin
// aggregator's rest points and binding changed, or only its pose did.
// HdExtComputation leaves DirtySceneInput for whoever consumes the values;
// a mesh reads them itself during its own sync, so the count consumes it
// here, or every later sync would count again.
class HdToonExtComputation final : public HdExtComputation {
public:
  explicit HdToonExtComputation(const SdfPath& id) : HdExtComputation(id) {
  }

  void Sync(HdSceneDelegate* delegate, HdRenderParam* render_param,
      HdDirtyBits* dirty_bits) override {
    if ((*dirty_bits & (DirtyInputDesc | DirtySceneInput | DirtyCompInput |
                           DirtyElementCount)) != 0) {
      ++input_revision_;
    }
    HdExtComputation::Sync(delegate, render_param, dirty_bits);
    *dirty_bits &= ~DirtySceneInput;
  }

  // Advances from 1 with every sync that changed an input; 0 before any.
  std::uint64_t GetInputRevision() const {
    return input_revision_.load();
  }

private:
  std::atomic<std::uint64_t> input_revision_{0};
};

template <typename T>
bool ReadInput(HdSceneDelegate* delegate, const SdfPath& computation,
    const TfToken& name, T& value) {
  const VtValue input = delegate->GetExtComputationInput(computation, name);
  if (!input.IsHolding<T>()) {
    return false;
  }
  value = input.UncheckedGet<T>();
  return true;
}

class HdToonMesh final : public HdMesh {
public:
  HdToonMesh(const SdfPath& id, std::shared_ptr<HdToonAdapterState> state)
      : HdMesh(id), state_(std::move(state)), mesh_(state_->CreateMesh()) {
  }

  ~HdToonMesh() override {
    state_->RemoveMesh(mesh_);
  }

  HdDirtyBits GetInitialDirtyBitsMask() const override {
    return HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTopology |
           HdChangeTracker::DirtyNormals | HdChangeTracker::DirtyTransform |
           HdChangeTracker::DirtyVisibility |
           HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyMaterialId |
           HdChangeTracker::DirtyRenderTag;
  }

  // Each kind of change updates only its own data (design policy §14):
  // points never re-triangulate, and a transform or colour change uploads no
  // geometry. Triangulation runs before the shared state is locked, so
  // meshes still sync in parallel.
  void Sync(HdSceneDelegate* delegate, HdRenderParam* render_param,
      HdDirtyBits* dirty_bits, const TfToken& repr_token) override {
    (void)render_param;
    (void)repr_token;
    const SdfPath& id = GetId();
    if (HdChangeTracker::IsTopologyDirty(*dirty_bits, id)) {
      state_->SetMeshTopology(mesh_, Triangulate(GetMeshTopology(delegate)));
    }
    if (HdChangeTracker::IsPrimvarDirty(*dirty_bits, id, HdTokens->points)) {
      SyncPoints(delegate);
    }
    if (HdChangeTracker::IsPrimvarDirty(*dirty_bits, id, HdTokens->normals)) {
      SyncNormals(delegate);
    }
    if (HdChangeTracker::IsPrimvarDirty(*dirty_bits, id, StToken())) {
      state_->SetMeshUVs(mesh_, ReadUVs(delegate));
    }
    if (HdChangeTracker::IsTransformDirty(*dirty_bits, id)) {
      state_->SetMeshTransform(mesh_, ToToon(delegate->GetTransform(id)));
    }
    if (HdChangeTracker::IsVisibilityDirty(*dirty_bits, id)) {
      _UpdateVisibility(delegate, dirty_bits);
      state_->SetMeshVisible(mesh_, IsVisible());
    }
    if (HdChangeTracker::IsPrimvarDirty(*dirty_bits, id,
            HdTokens->displayColor)) {
      state_->SetMeshColor(mesh_,
          ReadDisplayColor(GetPrimvar(delegate, HdTokens->displayColor)));
    }
    if ((*dirty_bits & HdChangeTracker::DirtyMaterialId) != 0) {
      SetMaterialId(delegate->GetMaterialId(id));
      state_->SetMeshMaterial(mesh_, GetMaterialId());
    }
    *dirty_bits &= ~HdChangeTracker::AllSceneDirtyBits;
  }

protected:
  HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override {
    return bits;
  }

  void _InitRepr(const TfToken& repr_token, HdDirtyBits* dirty_bits) override {
    (void)repr_token;
    *dirty_bits |= GetInitialDirtyBitsMask();
  }

private:
  // The core takes triangles counter-clockwise from the front, so a
  // left-handed mesh's are reversed here.
  std::vector<std::uint32_t> Triangulate(const HdMeshTopology& topology) const {
    HdMeshUtil util(&topology, GetId());
    VtVec3iArray triangles;
    VtIntArray primitive_params;
    util.ComputeTriangleIndices(&triangles, &primitive_params);
    const bool left_handed =
        topology.GetOrientation() == HdTokens->leftHanded;
    std::vector<std::uint32_t> indices;
    indices.reserve(triangles.size() * 3U);
    for (const GfVec3i& triangle : triangles) {
      if (triangle[0] < 0 || triangle[1] < 0 || triangle[2] < 0) {
        continue;
      }
      indices.push_back(static_cast<std::uint32_t>(triangle[0]));
      indices.push_back(
          static_cast<std::uint32_t>(triangle[left_handed ? 2 : 1]));
      indices.push_back(
          static_cast<std::uint32_t>(triangle[left_handed ? 1 : 2]));
    }
    return indices;
  }

  // A mesh UsdSkel skins has its points computed: usdSkelImaging gives it an
  // aggregator computation, whose inputs are the rest points and the
  // binding, and a skinning computation, whose scene inputs are the pose.
  // Linear blend skinning runs on the GPU: the binding becomes the mesh's
  // skin, uploaded when the aggregator changes, and a pose change sets the
  // pose alone (design policy §11). Dual quaternion skinning, or inputs of
  // another shape, run the computation's CPU kernel here instead, as
  // HdEmbree does.
  void SyncPoints(HdSceneDelegate* delegate) {
    for (const HdExtComputationPrimvarDescriptor& descriptor :
        delegate->GetExtComputationPrimvarDescriptors(GetId(),
            HdInterpolationVertex)) {
      if (descriptor.name != HdTokens->points) {
        continue;
      }
      if (SyncSkin(delegate, descriptor.sourceComputationId)) {
        return;
      }
      Unskin();
      const HdExtComputationUtils::ValueStore values =
          HdExtComputationUtils::GetComputedPrimvarValues({descriptor},
              delegate);
      const auto found = values.find(HdTokens->points);
      state_->SetMeshPoints(mesh_, ReadPoints(found == values.end()
                                                  ? GetPoints(delegate)
                                                  : found->second));
      return;
    }
    Unskin();
    state_->SetMeshPoints(mesh_, ReadPoints(GetPoints(delegate)));
  }

  // False, having set nothing, when the skinning is not linear blend or an
  // input is not what usdSkelImaging gives.
  bool SyncSkin(HdSceneDelegate* delegate, const SdfPath& skinning) {
    const TfTokenVector scene_inputs =
        delegate->GetExtComputationSceneInputNames(skinning);
    if (std::find(scene_inputs.begin(), scene_inputs.end(),
            SkinTokens->skinningDualQuats) != scene_inputs.end()) {
      return false;
    }
    SdfPath aggregator;
    for (const HdExtComputationInputDescriptor& input :
        delegate->GetExtComputationInputDescriptors(skinning)) {
      if (input.name == SkinTokens->restPoints) {
        aggregator = input.sourceComputationId;
      }
    }
    if (aggregator.IsEmpty()) {
      return false;
    }

    VtMatrix4fArray joints;
    GfMatrix4d skeleton_to_world;
    GfMatrix4d world_to_mesh;
    VtFloatArray weights;
    if (!ReadInput(delegate, skinning, SkinTokens->skinningXforms, joints) ||
        !ReadInput(delegate, skinning, SkinTokens->skelLocalToWorld,
            skeleton_to_world) ||
        !ReadInput(delegate, skinning, SkinTokens->primWorldToLocal,
            world_to_mesh) ||
        !ReadInput(delegate, skinning, SkinTokens->blendShapeWeights,
            weights)) {
      return false;
    }

    // An aggregator this delegate did not create reports no revision, and
    // is read every time.
    const auto* computation = dynamic_cast<const HdToonExtComputation*>(
        delegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->extComputation,
            aggregator));
    const std::uint64_t revision =
        computation == nullptr ? 0U : computation->GetInputRevision();
    const bool rest_changed =
        revision == 0 || revision != rest_revision_ || aggregator != aggregator_;
    if (rest_changed) {
      if (!ReadRest(delegate, aggregator)) {
        return false;
      }
      aggregator_ = aggregator;
      rest_revision_ = revision;
    }

    // Blend shapes are applied to the rest points here, so a weight change
    // uploads the points, until morphs are evaluated on the GPU.
    if (rest_changed || weights != weights_) {
      weights_ = weights;
      state_->SetMeshPoints(mesh_, RestPoints());
    }

    Toon::ToonSkinPose pose;
    pose.joints.reserve(joints.size());
    for (const GfMatrix4f& joint : joints) {
      pose.joints.push_back(ToToon(joint));
    }
    // Row vectors: skeleton space, to world, to the mesh's own.
    pose.skeleton_to_mesh = ToToon(skeleton_to_world * world_to_mesh);
    state_->SetMeshSkinPose(mesh_, std::move(pose));
    return true;
  }

  // The aggregator's rest points, blend shapes and binding. On failure the
  // mesh reads them again next time.
  bool ReadRest(HdSceneDelegate* delegate, const SdfPath& aggregator) {
    rest_revision_ = 0;
    GfMatrix4f geom_bind;
    VtVec2fArray influences;
    int influences_per_point = 0;
    bool constant = false;
    if (!ReadInput(delegate, aggregator, SkinTokens->restPoints,
            rest_points_) ||
        !ReadInput(delegate, aggregator, SkinTokens->geomBindXform,
            geom_bind) ||
        !ReadInput(delegate, aggregator, SkinTokens->influences,
            influences) ||
        !ReadInput(delegate, aggregator,
            SkinTokens->numInfluencesPerComponent, influences_per_point) ||
        !ReadInput(delegate, aggregator, SkinTokens->hasConstantInfluences,
            constant) ||
        !ReadInput(delegate, aggregator, SkinTokens->blendShapeOffsets,
            blend_offsets_) ||
        !ReadInput(delegate, aggregator, SkinTokens->blendShapeOffsetRanges,
            blend_ranges_)) {
      return false;
    }
    // No influences draws the rest points, as the CPU kernel returns them.
    Toon::ToonSkin skin;
    if (influences_per_point > 0) {
      skin.influences_per_point =
          static_cast<std::uint32_t>(influences_per_point);
      skin.constant = constant;
      skin.geom_bind = ToToon(geom_bind);
      skin.influences.reserve(influences.size());
      for (const GfVec2f& influence : influences) {
        // A negative joint index pulls nothing.
        skin.influences.push_back(influence[0] >= 0.0F
                                      ? Toon::ToonJointInfluence{
                                            static_cast<std::uint32_t>(
                                                influence[0]),
                                            influence[1]}
                                      : Toon::ToonJointInfluence{});
      }
    }
    state_->SetMeshSkin(mesh_, std::move(skin));
    skinned_ = true;
    return true;
  }

  // The rest points with the current blend shape weights applied, as
  // usdSkelImaging's kernels apply them before skinning.
  std::vector<Toon::Float3> RestPoints() const {
    std::vector<Toon::Float3> points = ReadPoints(VtValue(rest_points_));
    const std::size_t count = std::min(blend_ranges_.size(), points.size());
    for (std::size_t point = 0; point < count; ++point) {
      const GfVec2i range = blend_ranges_[point];
      for (int offset = std::max(range[0], 0);
           offset < range[1] &&
           static_cast<std::size_t>(offset) < blend_offsets_.size();
           ++offset) {
        const GfVec4f& shape = blend_offsets_[static_cast<std::size_t>(offset)];
        const int index = static_cast<int>(shape[3]);
        if (index < 0 || static_cast<std::size_t>(index) >= weights_.size()) {
          continue;
        }
        const float weight = weights_[static_cast<std::size_t>(index)];
        points[point].x += shape[0] * weight;
        points[point].y += shape[1] * weight;
        points[point].z += shape[2] * weight;
      }
    }
    return points;
  }

  void Unskin() {
    if (skinned_) {
      state_->SetMeshSkin(mesh_, {});
      skinned_ = false;
    }
    rest_revision_ = 0;
    aggregator_ = SdfPath();
  }

  // The mesh's authored normals, as a VRM importer writes glTF's NORMAL: a
  // `normals` primvar with one value per point or, for a mesh UsdSkel skins,
  // the rest normals its normals computation aggregates, which the GPU skins
  // with the points. usdSkelImaging makes that computation only under
  // USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS and for a mesh whose
  // subdivision scheme is none; otherwise it hides a skinned mesh's normals.
  // Face-varying normals need split vertices, as a face-varying `st` does,
  // and blend shapes' normal offsets are not applied; without authored
  // normals the core derives smooth ones.
  void SyncNormals(HdSceneDelegate* delegate) {
    for (const HdExtComputationPrimvarDescriptor& descriptor :
        delegate->GetExtComputationPrimvarDescriptors(GetId(),
            HdInterpolationVertex)) {
      if (descriptor.name == HdTokens->normals) {
        SyncRestNormals(delegate, descriptor.sourceComputationId);
        return;
      }
    }
    normals_aggregator_ = SdfPath();
    normals_revision_ = 0;
    for (const HdInterpolation interpolation :
        {HdInterpolationVertex, HdInterpolationVarying}) {
      for (const HdPrimvarDescriptor& descriptor :
          GetPrimvarDescriptors(delegate, interpolation)) {
        if (descriptor.name == HdTokens->normals) {
          state_->SetMeshNormals(mesh_,
              ReadPoints(GetPrimvar(delegate, HdTokens->normals)));
          return;
        }
      }
    }
    state_->SetMeshNormals(mesh_, {});
  }

  // The aggregator's rest normals are read again only when its inputs
  // changed, so a pose that dirties the normals reads nothing.
  void SyncRestNormals(HdSceneDelegate* delegate, const SdfPath& skinning) {
    SdfPath aggregator;
    for (const HdExtComputationInputDescriptor& input :
        delegate->GetExtComputationInputDescriptors(skinning)) {
      if (input.name == SkinTokens->restNormals) {
        aggregator = input.sourceComputationId;
      }
    }
    const auto* computation = dynamic_cast<const HdToonExtComputation*>(
        delegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->extComputation,
            aggregator));
    const std::uint64_t revision =
        computation == nullptr ? 0U : computation->GetInputRevision();
    if (revision != 0 && revision == normals_revision_ &&
        aggregator == normals_aggregator_) {
      return;
    }
    normals_aggregator_ = aggregator;
    normals_revision_ = revision;
    VtVec3fArray normals;
    bool face_varying = false;
    if (aggregator.IsEmpty() ||
        !ReadInput(delegate, aggregator, SkinTokens->restNormals, normals) ||
        (ReadInput(delegate, aggregator, SkinTokens->hasFaceVaryingNormals,
             face_varying) &&
            face_varying)) {
      normals.clear();
    }
    state_->SetMeshNormals(mesh_, ReadPoints(VtValue(normals)));
  }

  static const TfToken& StToken() {
    static const TfToken st("st");
    return st;
  }

  // `st`, the set a VRM importer writes TEXCOORD_0 to, when it has one
  // value per point. A face-varying `st` needs split vertices, which the
  // core does not make yet, so it is not read.
  std::vector<Toon::Float2> ReadUVs(HdSceneDelegate* delegate) const {
    std::vector<Toon::Float2> uvs;
    for (const HdInterpolation interpolation :
        {HdInterpolationVertex, HdInterpolationVarying}) {
      for (const HdPrimvarDescriptor& descriptor :
          GetPrimvarDescriptors(delegate, interpolation)) {
        if (descriptor.name != StToken()) {
          continue;
        }
        const VtValue value = GetPrimvar(delegate, StToken());
        if (value.IsHolding<VtVec2fArray>()) {
          const VtVec2fArray& source = value.UncheckedGet<VtVec2fArray>();
          uvs.reserve(source.size());
          for (const GfVec2f& uv : source) {
            uvs.push_back({uv[0], uv[1]});
          }
        }
        return uvs;
      }
    }
    return uvs;
  }

  static std::vector<Toon::Float3> ReadPoints(const VtValue& value) {
    std::vector<Toon::Float3> points;
    if (value.IsHolding<VtVec3fArray>()) {
      const VtVec3fArray& source = value.UncheckedGet<VtVec3fArray>();
      points.reserve(source.size());
      for (const GfVec3f& point : source) {
        points.push_back({point[0], point[1], point[2]});
      }
    }
    return points;
  }

  // Constant display colour only, for a mesh that draws unlit; anything
  // else falls back to grey.
  static Toon::Float3 ReadDisplayColor(const VtValue& value) {
    if (value.IsHolding<VtVec3fArray>()) {
      const VtVec3fArray& colors = value.UncheckedGet<VtVec3fArray>();
      if (colors.size() == 1) {
        return {colors[0][0], colors[0][1], colors[0][2]};
      }
    } else if (value.IsHolding<GfVec3f>()) {
      const GfVec3f& color = value.UncheckedGet<GfVec3f>();
      return {color[0], color[1], color[2]};
    }
    return {0.5F, 0.5F, 0.5F};
  }

  std::shared_ptr<HdToonAdapterState> state_;
  Toon::MeshId mesh_;
  // What the last skinned sync read from the aggregator, and as of which of
  // its revisions; 0 when it must be read again.
  SdfPath aggregator_;
  std::uint64_t rest_revision_ = 0;
  VtVec3fArray rest_points_;
  VtVec4fArray blend_offsets_;
  VtVec2iArray blend_ranges_;
  VtFloatArray weights_;
  bool skinned_ = false;
  // The aggregator the rest normals were last read from, and as of which of
  // its revisions; 0 when they must be read again.
  SdfPath normals_aggregator_;
  std::uint64_t normals_revision_ = 0;
};

// UsdLux distant, sphere (point or shaped spot), and uniform dome ambient.
// No area integration, shadows, IES, light linking or environment textures.
class HdToonLight final : public HdLight {
public:
  HdToonLight(const SdfPath& path, const TfToken& type,
      std::shared_ptr<HdToonAdapterState> state)
      : HdLight(path), type_(type), state_(std::move(state)),
        light_(0) {
  }

  ~HdToonLight() override {
    state_->RemoveLight(light_);
  }

  HdDirtyBits GetInitialDirtyBitsMask() const override {
    return AllDirty;
  }

  void Sync(HdSceneDelegate* delegate, HdRenderParam*, HdDirtyBits* bits) override {
    if (GetId().IsEmpty() || (*bits & (DirtyTransform | DirtyParams | DirtyResource)) == 0) {
      *bits = Clean;
      return;
    }
    // Hdx converts its application-owned GlfSimpleLights into distant/dome
    // sprims for non-Storm delegates, with a 15,000-intensity distant key.
    // They are host helpers, not the authored UsdLux rig this renderer reads.
    // Keep the same camera-key fallback as the standalone viewport. Inspect
    // the typed payload rather than reserving a host-specific prim path.
    if (delegate->GetLightParamValue(GetId(), HdLightTokens->params)
            .IsHolding<GlfSimpleLight>()) {
      state_->RemoveLight(light_);
      light_ = 0;
      *bits = Clean;
      return;
    }
    if (light_ == 0) light_ = state_->CreateLight();
    const auto scalar = [&](const TfToken& name, float fallback) {
      const VtValue value = VtValue::Cast<float>(delegate->GetLightParamValue(GetId(), name));
      return value.IsHolding<float>() ? value.UncheckedGet<float>() : fallback;
    };
    Toon::ToonLight light;
    light.visible = delegate->GetVisible(GetId());
    GfVec3f color(1.0F);
    const VtValue color_value = delegate->GetLightParamValue(GetId(), HdLightTokens->color);
    if (color_value.IsHolding<GfVec3f>()) {
      color = color_value.UncheckedGet<GfVec3f>();
    }
    const VtValue temperature = delegate->GetLightParamValue(GetId(), HdLightTokens->enableColorTemperature);
    if (temperature.IsHolding<bool>() && temperature.UncheckedGet<bool>()) {
      const GfVec3f tint = UsdLuxBlackbodyTemperatureAsRgb(
          scalar(HdLightTokens->colorTemperature, 6500.0F));
      for (int component = 0; component < 3; ++component)
        color[component] *= tint[component];
    }
    const float intensity = scalar(HdLightTokens->intensity, 1.0F) *
                            std::exp2(scalar(HdLightTokens->exposure, 0.0F)) *
                            scalar(HdLightTokens->diffuse, 1.0F);
    light.color = {color[0] * intensity, color[1] * intensity, color[2] * intensity};
    const GfMatrix4d transform = delegate->GetTransform(GetId());
    const GfVec3d position = transform.Transform(GfVec3d(0.0));
    const GfVec3d direction = transform.TransformDir(GfVec3d(0.0, 0.0, -1.0));
    light.position = {float(position[0]), float(position[1]), float(position[2])};
    light.direction = {float(direction[0]), float(direction[1]), float(direction[2])};
    if (type_ == HdPrimTypeTokens->domeLight) {
      light.type = Toon::ToonLightType::Ambient;
    } else if (type_ == HdPrimTypeTokens->sphereLight) {
      light.type = Toon::ToonLightType::Point;
      const float cone = scalar(HdLightTokens->shapingConeAngle, 180.0F);
      if (cone <= 90.0F) {
        light.type = Toon::ToonLightType::Spot;
        light.cone_angle = cone;
        light.cone_softness = scalar(HdLightTokens->shapingConeSoftness, 0.0F);
      }
      // Radius is a numerical attenuation floor in metres, not a source area.
    }
    state_->SetLight(light_, light);
    *bits = Clean;
  }

private:
  TfToken type_;
  std::shared_ptr<HdToonAdapterState> state_;
  Toon::LightId light_;
};

bool IsSceneLight(const TfToken& type) {
  return type == HdPrimTypeTokens->distantLight ||
         type == HdPrimTypeTokens->sphereLight || type == HdPrimTypeTokens->domeLight;
}

// HdCamera's own Sync reads the camera; the render pass reads its view and
// projection through HdRenderPassState, which also applies the viewer's
// framing and window policy.
class HdToonCamera final : public HdCamera {
public:
  explicit HdToonCamera(const SdfPath& id) : HdCamera(id) {
  }
};

class HdToonRenderPass final : public HdRenderPass {
public:
  HdToonRenderPass(HdRenderIndex* index,
      const HdRprimCollection& collection,
      std::shared_ptr<HdToonAdapterState> state)
      : HdRenderPass(index, collection), state_(std::move(state)) {
  }

private:
  void _Execute(const HdRenderPassStateSharedPtr& render_pass_state,
      const TfTokenVector& render_tags) override {
    (void)render_tags;
    const HdRenderDelegate& delegate = *GetRenderIndex()->GetRenderDelegate();
    state_->Render(render_pass_state, RequestedSamples(delegate),
        RequestedMetersPerUnit(delegate), RequestedTimeSeconds(delegate));
  }

  std::shared_ptr<HdToonAdapterState> state_;
};
} // namespace

HdToonRenderBuffer::HdToonRenderBuffer(const SdfPath& id)
    : HdRenderBuffer(id) {
}

bool HdToonRenderBuffer::Allocate(const GfVec3i& dimensions,
    HdFormat format,
    bool multi_sampled) {
  std::scoped_lock lock(mutex_);
  if (map_count_ != 0 || dimensions[0] < 0 || dimensions[1] < 0 ||
      dimensions[2] < 0 || multi_sampled || format == HdFormatInvalid) {
    return false;
  }
  const std::size_t pixel_size = HdDataSizeOfFormat(format);
  const std::size_t width = static_cast<std::size_t>(dimensions[0]);
  const std::size_t height = static_cast<std::size_t>(dimensions[1]);
  const std::size_t depth = static_cast<std::size_t>(dimensions[2]);
  if (pixel_size == 0 ||
      (width != 0 && height > std::numeric_limits<std::size_t>::max() / width) ||
      (width * height != 0 &&
          depth > std::numeric_limits<std::size_t>::max() / (width * height)) ||
      (width * height * depth != 0 &&
          pixel_size > std::numeric_limits<std::size_t>::max() /
                           (width * height * depth))) {
    return false;
  }
  dimensions_ = dimensions;
  format_ = format;
  multi_sampled_ = multi_sampled;
  converged_ = false;
  data_.assign(width * height * depth * pixel_size, 0);
  return true;
}

unsigned int HdToonRenderBuffer::GetWidth() const {
  std::scoped_lock lock(mutex_);
  return static_cast<unsigned int>(dimensions_[0]);
}

unsigned int HdToonRenderBuffer::GetHeight() const {
  std::scoped_lock lock(mutex_);
  return static_cast<unsigned int>(dimensions_[1]);
}

unsigned int HdToonRenderBuffer::GetDepth() const {
  std::scoped_lock lock(mutex_);
  return static_cast<unsigned int>(dimensions_[2]);
}

HdFormat HdToonRenderBuffer::GetFormat() const {
  std::scoped_lock lock(mutex_);
  return format_;
}

bool HdToonRenderBuffer::IsMultiSampled() const {
  std::scoped_lock lock(mutex_);
  return multi_sampled_;
}

void* HdToonRenderBuffer::Map() {
  std::scoped_lock lock(mutex_);
  if (data_.empty()) {
    return nullptr;
  }
  ++map_count_;
  return data_.data();
}

void HdToonRenderBuffer::Unmap() {
  std::scoped_lock lock(mutex_);
  if (map_count_ != 0) {
    --map_count_;
  }
}

bool HdToonRenderBuffer::IsMapped() const {
  std::scoped_lock lock(mutex_);
  return map_count_ != 0;
}

void HdToonRenderBuffer::Resolve() {
}

bool HdToonRenderBuffer::IsConverged() const {
  std::scoped_lock lock(mutex_);
  return converged_;
}

bool HdToonRenderBuffer::WriteColor(
    const std::vector<std::uint8_t>& rgba8, std::uint32_t source_width,
    std::uint32_t source_height) {
  std::scoped_lock lock(mutex_);
  const std::uint32_t width = static_cast<std::uint32_t>(dimensions_[0]);
  const std::uint32_t height = static_cast<std::uint32_t>(dimensions_[1]);
  if (map_count_ != 0 || format_ != HdFormatUNorm8Vec4 ||
      dimensions_[2] != 1 || source_width == 0 || source_height == 0 ||
      rgba8.size() != static_cast<std::size_t>(source_width) * source_height * 4U ||
      data_.size() != static_cast<std::size_t>(width) * height * 4U) {
    return false;
  }
  for (std::uint32_t y = 0; y < height; ++y) {
    const std::uint32_t source_y = (height - 1U - y) * source_height / height;
    for (std::uint32_t x = 0; x < width; ++x) {
      const std::uint32_t source_x = x * source_width / width;
      const std::size_t source =
          (static_cast<std::size_t>(source_y) * source_width + source_x) * 4U;
      const std::size_t target =
          (static_cast<std::size_t>(y) * width + x) * 4U;
      std::copy_n(rgba8.data() + source, 4, data_.data() + target);
    }
  }
  return true;
}

bool HdToonRenderBuffer::WriteDepth(const std::vector<float>& depth,
    std::uint32_t source_width,
    std::uint32_t source_height) {
  std::scoped_lock lock(mutex_);
  const std::uint32_t width = static_cast<std::uint32_t>(dimensions_[0]);
  const std::uint32_t height = static_cast<std::uint32_t>(dimensions_[1]);
  if (map_count_ != 0 || format_ != HdFormatFloat32 || dimensions_[2] != 1 ||
      source_width == 0 || source_height == 0 ||
      depth.size() != static_cast<std::size_t>(source_width) * source_height ||
      data_.size() != static_cast<std::size_t>(width) * height * sizeof(float)) {
    return false;
  }
  for (std::uint32_t y = 0; y < height; ++y) {
    const std::uint32_t source_y = (height - 1U - y) * source_height / height;
    for (std::uint32_t x = 0; x < width; ++x) {
      const std::uint32_t source_x = x * source_width / width;
      const float value = depth[static_cast<std::size_t>(source_y) *
                                    source_width +
                                source_x];
      const std::size_t target =
          (static_cast<std::size_t>(y) * width + x) * sizeof(float);
      std::memcpy(data_.data() + target, &value, sizeof(value));
    }
  }
  return true;
}

bool HdToonRenderBuffer::WriteIds(std::int32_t value) {
  std::scoped_lock lock(mutex_);
  if (map_count_ != 0 || format_ != HdFormatInt32 || dimensions_[2] != 1 ||
      data_.size() % sizeof(value) != 0) {
    return false;
  }
  for (std::size_t offset = 0; offset < data_.size(); offset += sizeof(value)) {
    std::memcpy(data_.data() + offset, &value, sizeof(value));
  }
  return true;
}

void HdToonRenderBuffer::SetConverged(bool converged) {
  std::scoped_lock lock(mutex_);
  converged_ = converged;
}

void HdToonRenderBuffer::_Deallocate() {
  std::scoped_lock lock(mutex_);
  if (map_count_ != 0) {
    return;
  }
  dimensions_ = GfVec3i(0);
  format_ = HdFormatInvalid;
  multi_sampled_ = false;
  converged_ = false;
  data_.clear();
}

HdToonMaterial::HdToonMaterial(const SdfPath& id,
    std::shared_ptr<HdToonAdapterState> state)
    : HdMaterial(id), state_(std::move(state)),
      material_(state_->CreateMaterial(id)) {
}

HdToonMaterial::~HdToonMaterial() {
  state_->RemoveMaterial(material_, GetId());
  for (const Toon::TextureId texture : textures_) {
    state_->ReleaseTexture(texture);
  }
}

// The canonical values are not in the material network: a format
// repository's realization does not connect to them (renderer report 02).
// They are read from the prim's own data sources on the render index's
// terminal scene index, where the format's UsdImaging adapter puts them.
void HdToonMaterial::Sync(HdSceneDelegate* delegate,
    HdRenderParam* render_param, HdDirtyBits* dirty_bits) {
  (void)render_param;
  if (*dirty_bits != Clean) {
    HdContainerDataSourceHandle prim;
    if (const HdSceneIndexBaseRefPtr terminal =
            delegate->GetRenderIndex().GetTerminalSceneIndex()) {
      prim = terminal->GetPrim(GetId()).dataSource;
    } else {
      static std::once_flag warned;
      std::call_once(warned, [] {
        TF_WARN("Toon reads materials from the terminal scene index, which "
                "this render index does not have; every material is "
                "PreviewSurface");
      });
    }
    Read(prim);
  }
  *dirty_bits = Clean;
}

HdDirtyBits HdToonMaterial::GetInitialDirtyBitsMask() const {
  return AllDirty;
}

// Read as Sync reads, so a value lands in the same slot; the render world
// finds that only values changed and advances the parameters revision alone.
void HdToonMaterial::SyncValues(const HdSceneIndexBase& terminal) {
  Read(terminal.GetPrim(GetId()).dataSource);
}

// The textures are acquired before the old ones are released, so a texture
// the material keeps sampling is never decoded again.
void HdToonMaterial::Read(const HdContainerDataSourceHandle& prim) {
  HdToonMaterialSource source = HdToonReadMaterial(prim);
  std::array<Toon::TextureId, kHdToonTextureRoles> textures{};
  for (std::size_t role = 0; role < kHdToonTextureRoles; ++role) {
    textures[role] = state_->AcquireTexture(source.textures[role]);
  }
  for (const Toon::TextureId texture : textures_) {
    state_->ReleaseTexture(texture);
  }
  textures_ = textures;
  const auto references = HdToonTextureRefs(source.values);
  for (std::size_t role = 0; role < kHdToonTextureRoles; ++role) {
    references[role]->texture = textures[role];
  }
  values_ = source.values;
  state_->SetMaterial(material_, values_);
}

const Toon::ToonMaterial& HdToonMaterial::GetToonMaterial() const {
  return values_;
}

// Scene index emulation turns only locators under `material` into a
// material's dirty bits, so a `vrm/<group>/<field>` dirtied alone — a time
// move across a sample — leaves the Sprim clean (renderer report 02). The
// delegate observes the terminal scene index for those and syncs the values
// itself in Update(), before any Sprim sync (material policy §8).
class HdToonRenderDelegate::Impl final : public HdSceneIndexObserver {
public:
  ~Impl() override {
    Observe(nullptr);
  }

  void Observe(const HdSceneIndexBaseRefPtr& terminal) {
    if (terminal_) {
      terminal_->RemoveObserver(HdSceneIndexObserverPtr(this));
    }
    terminal_ = terminal;
    if (terminal_) {
      terminal_->AddObserver(HdSceneIndexObserverPtr(this));
    }
  }

  void AddMaterial(HdToonMaterial* material) {
    std::scoped_lock lock(mutex_);
    materials_[material->GetId()] = material;
  }

  void RemoveMaterial(HdToonMaterial* material) {
    std::scoped_lock lock(mutex_);
    const auto found = materials_.find(material->GetId());
    if (found != materials_.end() && found->second == material) {
      materials_.erase(found);
    }
  }

  void SyncValueChanges() {
    std::scoped_lock lock(mutex_);
    if (terminal_) {
      for (const SdfPath& id : pending_) {
        const auto found = materials_.find(id);
        if (found != materials_.end()) {
          found->second->SyncValues(*terminal_);
        }
      }
    }
    pending_.clear();
  }

  void PrimsAdded(const HdSceneIndexBase& sender,
      const AddedPrimEntries& entries) override {
    (void)sender;
    (void)entries;
  }

  void PrimsRemoved(const HdSceneIndexBase& sender,
      const RemovedPrimEntries& entries) override {
    (void)sender;
    (void)entries;
  }

  // A dirtied `material` locator reaches Sync, which reads everything.
  void PrimsDirtied(const HdSceneIndexBase& sender,
      const DirtiedPrimEntries& entries) override {
    (void)sender;
    std::scoped_lock lock(mutex_);
    for (const DirtiedPrimEntry& entry : entries) {
      if (HdToonIsValueOnlyChange(entry.dirtyLocators)) {
        pending_.insert(entry.primPath);
      }
    }
  }

  void PrimsRenamed(const HdSceneIndexBase& sender,
      const RenamedPrimEntries& entries) override {
    (void)sender;
    (void)entries;
  }

  std::shared_ptr<HdToonAdapterState> state =
      std::make_shared<HdToonAdapterState>();

private:
  // Weak, so the delegate never keeps the scene index graph alive.
  HdSceneIndexBasePtr terminal_;
  std::mutex mutex_;
  std::unordered_map<SdfPath, HdToonMaterial*, SdfPath::Hash> materials_;
  SdfPathSet pending_;
};

HdToonRenderDelegate::HdToonRenderDelegate(
    const HdRenderSettingsMap& settings)
    : HdRenderDelegate(settings),
      impl_(std::make_unique<Impl>()),
      resources_(std::make_shared<HdResourceRegistry>()) {
  _PopulateDefaultSettings(GetRenderSettingDescriptors());
}

HdRenderSettingDescriptorList
HdToonRenderDelegate::GetRenderSettingDescriptors() const {
  return {{"MSAA samples per pixel", SettingTokens->msaaSamples,
              VtValue(kDefaultSamples)},
      // A float, which usdview's settings panel can show; a double it
      // leaves out of the list.
      {"Stage meters per unit", SettingTokens->metersPerUnit,
          VtValue(1.0F)},
      {"Evaluation time in seconds", SettingTokens->timeSeconds,
          VtValue(0.0F)}};
}

HdToonRenderDelegate::~HdToonRenderDelegate() = default;

const TfTokenVector& HdToonRenderDelegate::GetSupportedRprimTypes() const {
  static const TfTokenVector types{HdPrimTypeTokens->mesh};
  return types;
}

const TfTokenVector& HdToonRenderDelegate::GetSupportedSprimTypes() const {
  static const TfTokenVector types{HdPrimTypeTokens->camera,
      HdPrimTypeTokens->extComputation, HdPrimTypeTokens->material,
      HdPrimTypeTokens->distantLight, HdPrimTypeTokens->sphereLight,
      HdPrimTypeTokens->domeLight};
  return types;
}

const TfTokenVector& HdToonRenderDelegate::GetSupportedBprimTypes() const {
  static const TfTokenVector types{HdPrimTypeTokens->renderBuffer};
  return types;
}

HdResourceRegistrySharedPtr HdToonRenderDelegate::GetResourceRegistry() const {
  return resources_;
}

HdRenderPassSharedPtr HdToonRenderDelegate::CreateRenderPass(
    HdRenderIndex* index, const HdRprimCollection& collection) {
  return std::make_shared<HdToonRenderPass>(index, collection,
      impl_->state);
}

HdInstancer* HdToonRenderDelegate::CreateInstancer(
    HdSceneDelegate* delegate, const SdfPath& id) {
  (void)delegate;
  (void)id;
  return nullptr;
}

void HdToonRenderDelegate::DestroyInstancer(HdInstancer* instancer) {
  delete instancer;
}

HdRprim* HdToonRenderDelegate::CreateRprim(const TfToken& type_id,
    const SdfPath& rprim_id) {
  if (type_id == HdPrimTypeTokens->mesh) {
    return new HdToonMesh(rprim_id, impl_->state);
  }
  return nullptr;
}

void HdToonRenderDelegate::DestroyRprim(HdRprim* rprim) {
  delete rprim;
}

HdSprim* HdToonRenderDelegate::CreateSprim(const TfToken& type_id,
    const SdfPath& sprim_id) {
  if (IsSceneLight(type_id)) {
    return new HdToonLight(sprim_id, type_id, impl_->state);
  }
  if (type_id == HdPrimTypeTokens->camera) {
    return new HdToonCamera(sprim_id);
  }
  if (type_id == HdPrimTypeTokens->extComputation) {
    return new HdToonExtComputation(sprim_id);
  }
  if (type_id == HdPrimTypeTokens->material) {
    auto* material = new HdToonMaterial(sprim_id, impl_->state);
    impl_->AddMaterial(material);
    return material;
  }
  return nullptr;
}

HdSprim* HdToonRenderDelegate::CreateFallbackSprim(
    const TfToken& type_id) {
  if (IsSceneLight(type_id)) {
    return new HdToonLight(SdfPath::EmptyPath(), type_id, impl_->state);
  }
  if (type_id == HdPrimTypeTokens->camera) {
    return new HdToonCamera(SdfPath("/__toonFallbackCamera"));
  }
  if (type_id == HdPrimTypeTokens->extComputation) {
    return new HdToonExtComputation(SdfPath::EmptyPath());
  }
  // Never synced, so it keeps the fallback material's values.
  if (type_id == HdPrimTypeTokens->material) {
    return new HdToonMaterial(SdfPath::EmptyPath(), impl_->state);
  }
  return nullptr;
}

void HdToonRenderDelegate::DestroySprim(HdSprim* sprim) {
  if (auto* material = dynamic_cast<HdToonMaterial*>(sprim)) {
    impl_->RemoveMaterial(material);
  }
  delete sprim;
}

HdBprim* HdToonRenderDelegate::CreateBprim(const TfToken& type_id,
    const SdfPath& bprim_id) {
  if (type_id == HdPrimTypeTokens->renderBuffer) {
    return new HdToonRenderBuffer(bprim_id);
  }
  return nullptr;
}

HdBprim* HdToonRenderDelegate::CreateFallbackBprim(
    const TfToken& type_id) {
  if (type_id == HdPrimTypeTokens->renderBuffer) {
    return new HdToonRenderBuffer(
        SdfPath("/__toonFallbackRenderBuffer"));
  }
  return nullptr;
}

void HdToonRenderDelegate::DestroyBprim(HdBprim* bprim) {
  delete bprim;
}

void HdToonRenderDelegate::CommitResources(HdChangeTracker* tracker) {
  (void)tracker;
}

HdAovDescriptor HdToonRenderDelegate::GetDefaultAovDescriptor(
    const TfToken& name) const {
  if (name == HdAovTokens->color) {
    return {HdFormatUNorm8Vec4, false, VtValue(GfVec4f(0.0F))};
  }
  if (name == HdAovTokens->depth) {
    return {HdFormatFloat32, false, VtValue(1.0F)};
  }
  if (name == HdAovTokens->primId || name == HdAovTokens->instanceId ||
      name == HdAovTokens->elementId) {
    return {HdFormatInt32, false, VtValue(-1)};
  }
  return {};
}

void HdToonRenderDelegate::SetTerminalSceneIndex(
    const HdSceneIndexBaseRefPtr& terminal_scene_index) {
  impl_->Observe(terminal_scene_index);
}

void HdToonRenderDelegate::Update() {
  impl_->SyncValueChanges();
}

Toon::FrameSnapshot HdToonRenderDelegate::CommitScene() {
  impl_->state->SetTimeSeconds(RequestedTimeSeconds(*this));
  return impl_->state->Commit();
}

void HdToonRenderDelegate::CommitScene(Toon::FrameSnapshot& snapshot) {
  impl_->state->SetTimeSeconds(RequestedTimeSeconds(*this));
  impl_->state->Commit(snapshot);
}

PXR_NAMESPACE_CLOSE_SCOPE
