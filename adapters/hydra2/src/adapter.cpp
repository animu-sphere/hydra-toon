// SPDX-License-Identifier: Apache-2.0
#include "adapter.hpp"

#include <pxr/pxr.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/tokens.h>

#include <toon/extraction.hpp>
#include <toon/render_world.hpp>
#include <toon/vulkan_backend.hpp>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

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
    const Toon::GpuFrameEvidence& frame,
    std::uint32_t width, std::uint32_t height,
    std::size_t buffers_written,
    std::uint64_t scene_revision) {
  const char* path = std::getenv("TOON_HYDRA_EVIDENCE");
  if (path == nullptr || *path == '\0') {
    return;
  }
  std::ofstream output(path, std::ios::binary | std::ios::app);
  if (!output) {
    TF_WARN("Could not append Toon Hydra evidence to %s", path);
    return;
  }
  output << "frame=" << frame_index
         << " completion=" << frame.completion
         << " scene_revision=" << scene_revision
         << " width=" << width
         << " height=" << height
         << " buffers_written=" << buffers_written << '\n';
}

class AdapterState {
public:
  void SyncMesh(const SdfPath& id, bool renderable) {
    std::scoped_lock lock(mutex_);
    meshes_[id.GetString()] = renderable;
    world_.MarkChanged();
    UpdateWorldLocked();
  }

  void RemoveMesh(const SdfPath& id) {
    std::scoped_lock lock(mutex_);
    meshes_.erase(id.GetString());
    UpdateWorldLocked();
  }

  void Render(const HdRenderPassAovBindingVector& bindings) {
    std::scoped_lock lock(mutex_);
    const Toon::FrameSnapshot snapshot = world_.Commit();
    const Toon::DrawSummary draw = Toon::ExtractDrawSummary(snapshot);
    if (draw.triangle_count == 0) {
      for (const HdRenderPassAovBinding& binding : bindings) {
        if (auto* buffer =
                dynamic_cast<HdToonRenderBuffer*>(binding.renderBuffer)) {
          buffer->SetConverged(false);
        }
      }
      return;
    }

    const std::filesystem::path shaders = PluginDirectory() / "shaders";
    const Toon::GpuFrameEvidence frame = Toon::RenderOffscreen(
        draw, (shaders / "triangle.vert.spv").string(),
        (shaders / "triangle.frag.spv").string(), 1);
    if (frame.status != Toon::FrameStatus::Pass) {
      TF_RUNTIME_ERROR("Toon Hydra frame failed: %s", frame.detail.c_str());
      return;
    }

    std::size_t buffers_written{};
    std::uint32_t width{};
    std::uint32_t height{};
    for (const HdRenderPassAovBinding& binding : bindings) {
      auto* buffer =
          dynamic_cast<HdToonRenderBuffer*>(binding.renderBuffer);
      if (buffer == nullptr) {
        continue;
      }
      width = std::max(width, buffer->GetWidth());
      height = std::max(height, buffer->GetHeight());
      bool wrote = false;
      if (binding.aovName == HdAovTokens->color) {
        wrote = buffer->WriteColor(frame.color.payload, frame.color.width,
            frame.color.height);
      } else if (binding.aovName == HdAovTokens->depth) {
        wrote = buffer->WriteDepth(frame.depth.payload, frame.depth.width,
            frame.depth.height);
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
    AppendHostEvidence(frame_index_, frame, width, height, buffers_written,
        snapshot.revision);
  }

private:
  void UpdateWorldLocked() {
    const bool any_renderable =
        std::any_of(meshes_.begin(), meshes_.end(),
            [](const auto& entry) { return entry.second; });
    world_.SetTriangleCount(any_renderable ? 1U : 0U);
  }

  std::mutex mutex_;
  std::unordered_map<std::string, bool> meshes_;
  Toon::RenderWorld world_;
  std::uint64_t frame_index_{};
};

class HdToonMesh final : public HdMesh {
public:
  HdToonMesh(const SdfPath& id, std::shared_ptr<AdapterState> state)
      : HdMesh(id), state_(std::move(state)) {
  }

  ~HdToonMesh() override {
    state_->RemoveMesh(GetId());
  }

  HdDirtyBits GetInitialDirtyBitsMask() const override {
    return HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTopology |
           HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility |
           HdChangeTracker::DirtyRenderTag;
  }

  void Sync(HdSceneDelegate* delegate, HdRenderParam* render_param,
      HdDirtyBits* dirty_bits, const TfToken& repr_token) override {
    (void)render_param;
    (void)repr_token;
    const bool visible = delegate->GetVisible(GetId());
    const HdMeshTopology topology = GetMeshTopology(delegate);
    const bool has_face =
        std::any_of(topology.GetFaceVertexCounts().begin(),
            topology.GetFaceVertexCounts().end(),
            [](int count) { return count >= 3; });
    const bool has_points = !GetPoints(delegate).IsEmpty();
    state_->SyncMesh(GetId(), visible && has_face && has_points);
    *dirty_bits = HdChangeTracker::Clean;
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
  std::shared_ptr<AdapterState> state_;
};

class HdToonCamera final : public HdCamera {
public:
  explicit HdToonCamera(const SdfPath& id) : HdCamera(id) {
  }
};

class HdToonRenderPass final : public HdRenderPass {
public:
  HdToonRenderPass(HdRenderIndex* index,
      const HdRprimCollection& collection,
      std::shared_ptr<AdapterState> state)
      : HdRenderPass(index, collection), state_(std::move(state)) {
  }

private:
  void _Execute(const HdRenderPassStateSharedPtr& render_pass_state,
      const TfTokenVector& render_tags) override {
    (void)render_tags;
    state_->Render(render_pass_state->GetAovBindings());
  }

  std::shared_ptr<AdapterState> state_;
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
    const std::uint32_t source_y = y * source_height / height;
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
    const std::uint32_t source_y = y * source_height / height;
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

class HdToonRenderDelegate::Impl {
public:
  std::shared_ptr<AdapterState> state = std::make_shared<AdapterState>();
};

HdToonRenderDelegate::HdToonRenderDelegate(
    const HdRenderSettingsMap& settings)
    : HdRenderDelegate(settings),
      impl_(std::make_unique<Impl>()),
      resources_(std::make_shared<HdResourceRegistry>()) {
}

HdToonRenderDelegate::~HdToonRenderDelegate() = default;

const TfTokenVector& HdToonRenderDelegate::GetSupportedRprimTypes() const {
  static const TfTokenVector types{HdPrimTypeTokens->mesh};
  return types;
}

const TfTokenVector& HdToonRenderDelegate::GetSupportedSprimTypes() const {
  static const TfTokenVector types{HdPrimTypeTokens->camera};
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
  if (type_id == HdPrimTypeTokens->camera) {
    return new HdToonCamera(sprim_id);
  }
  return nullptr;
}

HdSprim* HdToonRenderDelegate::CreateFallbackSprim(
    const TfToken& type_id) {
  if (type_id == HdPrimTypeTokens->camera) {
    return new HdToonCamera(SdfPath("/__toonFallbackCamera"));
  }
  return nullptr;
}

void HdToonRenderDelegate::DestroySprim(HdSprim* sprim) {
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

PXR_NAMESPACE_CLOSE_SCOPE
