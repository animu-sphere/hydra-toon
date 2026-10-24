// SPDX-License-Identifier: Apache-2.0
// The WebGPU backend's device, scene pipelines and GPU copies of the scene,
// over Dawn's webgpu.h (WebGPU policy §2-§7). Private to backend/webgpu; no
// webgpu.h type crosses a public header.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <webgpu/webgpu.h>

#include <toon/extraction.hpp>
#include <toon/webgpu_backend.hpp>

#include "gpu_records.hpp"

namespace Toon::webgpu_internal {

// The shader contract's records, flags and writers, which the Vulkan
// backend fills the same way (backend/shared/).
using namespace Toon::backend_shared;

// Reference-counted webgpu.h objects, released when the owner is.
template <class Handle, void (*Release)(Handle)>
class Object {
public:
  Object() = default;
  explicit Object(Handle handle) : handle_(handle) {}
  Object(const Object&) = delete;
  Object& operator=(const Object&) = delete;
  Object(Object&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
  Object& operator=(Object&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }
  ~Object() { reset(); }
  void reset(Handle handle = nullptr) {
    if (handle_ != nullptr) Release(handle_);
    handle_ = handle;
  }
  [[nodiscard]] Handle get() const { return handle_; }
  explicit operator bool() const { return handle_ != nullptr; }

private:
  Handle handle_ = nullptr;
};

using Instance = Object<WGPUInstance, wgpuInstanceRelease>;
using Adapter = Object<WGPUAdapter, wgpuAdapterRelease>;
using Device = Object<WGPUDevice, wgpuDeviceRelease>;
using Queue = Object<WGPUQueue, wgpuQueueRelease>;
using Buffer = Object<WGPUBuffer, wgpuBufferRelease>;
using Texture = Object<WGPUTexture, wgpuTextureRelease>;
using TextureView = Object<WGPUTextureView, wgpuTextureViewRelease>;
using Sampler = Object<WGPUSampler, wgpuSamplerRelease>;
using ShaderModule = Object<WGPUShaderModule, wgpuShaderModuleRelease>;
using BindGroupLayout = Object<WGPUBindGroupLayout, wgpuBindGroupLayoutRelease>;
using BindGroup = Object<WGPUBindGroup, wgpuBindGroupRelease>;
using PipelineLayout = Object<WGPUPipelineLayout, wgpuPipelineLayoutRelease>;
using RenderPipeline = Object<WGPURenderPipeline, wgpuRenderPipelineRelease>;
using CommandEncoder = Object<WGPUCommandEncoder, wgpuCommandEncoderRelease>;
using CommandBuffer = Object<WGPUCommandBuffer, wgpuCommandBufferRelease>;
using RenderPassEncoder = Object<WGPURenderPassEncoder, wgpuRenderPassEncoderRelease>;

[[nodiscard]] WGPUStringView View(const char* text);
[[nodiscard]] std::string Text(WGPUStringView view);

// What the device reported outside any call that returns a status: Dawn's
// validation errors, which the backend counts as the Vulkan backend counts
// validation messages, and the device's loss.
struct DeviceEvents {
  std::uint32_t errors = 0;
  std::string first_error;
  bool lost = false;
  std::string lost_reason;
};

// The instance, Dawn's adapter of the requested kind and its device, which
// requests the WebGPU defaults (WebGPU policy §4). `events` is declared
// first so it outlives the device, whose callbacks write it.
struct Gpu {
  std::unique_ptr<DeviceEvents> events = std::make_unique<DeviceEvents>();
  Instance instance;
  Adapter adapter;
  Device device;
  Queue queue;
  std::string device_name;
  std::string backend_name;
  std::string driver;
  std::uint32_t vendor_id = 0;
  std::uint32_t device_id = 0;
};

// Skip when there is no adapter of the requested kind, Fail when one exists
// but its device cannot be created.
FrameStatus CreateGpu(WebGpuAdapter kind, Gpu& gpu, std::string& detail);

// Waits for `future` on `instance`, up to ten seconds.
bool Wait(WGPUInstance instance, WGPUFuture future, const char* what,
    std::string& detail);

// The queue's work so far has completed.
bool WaitForQueue(const Gpu& gpu, std::string& detail);

// Maps `buffer` for reading and waits.
bool MapForRead(const Gpu& gpu, WGPUBuffer buffer, std::uint64_t size,
    std::string& detail);

Buffer CreateBuffer(WGPUDevice device, std::uint64_t size, WGPUBufferUsage usage,
    const char* label);

bool LoadWgsl(const std::string& path, std::string& source, std::string& detail);

// The WGSL each pipeline compiles, loaded before any device exists so a
// missing file is a failure rather than a skip.
struct ShaderSources {
  std::string mesh_vertex;
  std::string mesh_fragment;
  std::string preview_vertex;
  std::string preview_fragment;
  std::string mtoon_vertex;
  std::string mtoon_fragment;
  std::string mtoon_transparent_vertex;
  std::string mtoon_transparent_fragment;
  std::string mtoon_outline_vertex;
  std::string mtoon_outline_fragment;
  std::string mtoon_coverage_vertex;
  std::string mtoon_coverage_fragment;
  std::string mmd_vertex;
  std::string mmd_fragment;
  std::string mmd_transparent_vertex;
  std::string mmd_transparent_fragment;
  std::string mmd_outline_vertex;
  std::string mmd_outline_fragment;
  std::string mipmap_vertex;
  std::string mipmap_fragment;
  std::string depth_resolve_vertex;
  std::string depth_resolve_fragment;
};
bool LoadShaders(const WebGpuShaders& shaders, ShaderSources& sources,
    std::string& detail);

// Every draw's constants sit at a multiple of this in one per-frame
// uniform ring, bound at a dynamic offset (WebGPU policy §4).
constexpr std::uint32_t kDrawStride = 256;
constexpr std::uint32_t kDrawBindingSize = sizeof(SurfaceDrawConstants);

// Must match the bindings' role slots (shaders/bindings/bindings_webgpu.slang):
// role r's texture at binding kFirstRoleTexture + r, its sampler at
// kFirstRoleSampler + r.
constexpr std::uint32_t kTextureRoles = 9;
constexpr std::uint32_t kFirstRoleTexture = 3;
constexpr std::uint32_t kFirstRoleSampler = 12;
// Each model's parameter buffer in the material group.
constexpr std::uint32_t kMToonMaterialsBinding = 0;
constexpr std::uint32_t kMmdMaterialsBinding = 1;
constexpr std::uint32_t kPreviewMaterialsBinding = 2;

// PreviewSurface's roles are its inputs, in PreviewParameters' order
// (kPreview* in shaders/preview/preview_common.slang).
constexpr std::uint32_t kPreviewRoles = 6;

// Must match kMToon* in shaders/mtoon/mtoon_common.slang.
enum MToonRole : std::uint32_t {
  kMToonBase,
  kMToonShade,
  kMToonNormal,
  kMToonEmissive,
  kMToonRim,
  kMToonMatcap,
  kMToonOutlineWidth,
  kMToonMask,
  kMToonShift,
};

// Must match kMmd* in shaders/mmd/mmd_common.slang.
enum MmdRole : std::uint32_t {
  kMmdBase,
  kMmdSphere,
  kMmdToon,
};

// The sampler that clamps both axes, which MMD's toon ramp is read
// through (kClampSampler in shaders/common/surface_common.slang).
constexpr std::uint32_t kClampSampler = 4;

// MMD's shared toon ramps, SharedToonRamp(0) to (9).
constexpr std::uint32_t kSharedToonRamps = 10;

// The multisampled count core WebGPU renders besides one (WebGPU policy §7).
constexpr std::uint32_t kMultisampleCount = 4;

constexpr WGPUTextureFormat kColorFormat = WGPUTextureFormat_RGBA8Unorm;
constexpr WGPUTextureFormat kDepthFormat = WGPUTextureFormat_Depth32Float;

// Clip-from-world for a ToonView: the view's OpenGL-convention projection,
// then z mapped from [-1, 1] to WebGPU's [0, 1]. WebGPU's normalized
// device coordinates point y up, so unlike the Vulkan backend's, nothing
// flips y.
Matrix4 WebGpuClipFromWorld(const ToonView& view);

// The scene's bind group layouts and pipelines (WebGPU policy §4). WebGPU
// has no dynamic cull mode, front face, depth writes or vertex strides, so
// each combination a draw needs is its own pipeline, created the first time
// a draw needs it and kept for the renderer's life.
//
// The kinds are the Vulkan backend's pipelines, with its blending, depth
// tests and outline bias: the unlit mesh; PreviewSurface; mtoon_opaque,
// which multisampled is MToonCoverage; mtoon_transparent and mtoon_outline;
// mmd_opaque, mmd_transparent and mmd_outline. Every surface kind shares
// one layout, frame, material, deformation and draw, and reads four
// streams: position, normal, UV and a fourth, the tangent frame,
// MMD's sub-texture UV or its edge scale.
class ScenePipelines {
public:
  enum class Kind : std::uint8_t {
    Mesh,
    Preview,
    MToon,
    MToonCoverage,
    MToonTransparent,
    MToonOutline,
    Mmd,
    MmdTransparent,
    MmdOutline,
  };
  static constexpr std::size_t kKinds = 9;
  struct Key {
    Kind kind = Kind::Mesh;
    WGPUCullMode cull = WGPUCullMode_None;
    WGPUFrontFace front = WGPUFrontFace_CCW;
    bool depth_write = true;
    // Strides of the UV and fourth streams: 0 binds a single element for a
    // mesh without them, zero or, for an edge scale, one.
    bool uvs = true;
    bool fourth = true;
    friend bool operator==(const Key&, const Key&) = default;
  };

  bool Initialize(WGPUDevice device, const ShaderSources& sources,
      std::uint32_t samples, std::string& detail);
  // Nullptr on a creation failure, which `detail` explains.
  WGPURenderPipeline Get(const Key& key, std::string& detail);

  [[nodiscard]] WGPUBindGroupLayout frame_layout() const { return frame_layout_.get(); }
  [[nodiscard]] WGPUBindGroupLayout material_layout() const { return material_layout_.get(); }
  [[nodiscard]] WGPUBindGroupLayout empty_layout() const { return empty_layout_.get(); }
  [[nodiscard]] WGPUBindGroupLayout deformation_layout() const { return deformation_layout_.get(); }
  [[nodiscard]] WGPUBindGroupLayout draw_layout() const { return draw_layout_.get(); }
  [[nodiscard]] WGPUSampler sampler(std::uint32_t index) const { return samplers_[index].get(); }
  [[nodiscard]] WGPUSampler shadow_sampler() const { return shadow_sampler_.get(); }
  [[nodiscard]] std::uint32_t created() const { return created_; }
  [[nodiscard]] std::uint32_t samples() const { return samples_; }

private:
  struct Stages {
    ShaderModule vertex;
    ShaderModule fragment;
  };

  WGPUDevice device_ = nullptr;
  std::uint32_t samples_ = 1;
  std::array<Stages, kKinds> stages_;
  BindGroupLayout frame_layout_;
  BindGroupLayout material_layout_;
  BindGroupLayout empty_layout_;
  BindGroupLayout deformation_layout_;
  BindGroupLayout draw_layout_;
  PipelineLayout mesh_layout_;
  PipelineLayout surface_layout_;
  std::array<Sampler, kSamplerCount> samplers_;
  Sampler shadow_sampler_;
  std::vector<std::pair<Key, RenderPipeline>> pipelines_;
  std::uint32_t created_ = 0;
};

// A multisampled frame's depth resolve (WebGPU policy §7): a pass that
// writes each pixel's sample 0 of the scene's depth into the single-sampled
// depth product, as the Vulkan backend's sample-zero resolve does.
class DepthResolve {
public:
  bool Initialize(WGPUDevice device, const ShaderSources& sources, std::string& detail);
  // Record the resolve of `samples`, a multisampled depth view, into
  // `target`, outside any other pass.
  bool Record(WGPUCommandEncoder encoder, WGPUTextureView samples,
      WGPUTextureView target, std::string& detail) const;

private:
  WGPUDevice device_ = nullptr;
  ShaderModule vertex_;
  ShaderModule fragment_;
  BindGroupLayout layout_;
  PipelineLayout pipeline_layout_;
  RenderPipeline pipeline_;
};

// GPU copies of textures, keyed by texture id, uploaded by queue writes
// when their revision changes; each level below the first is drawn from
// the one above in the frame that uploads it (WebGPU policy §6). MMD's
// shared toon ramps are the renderer's own, uploaded with the white and
// resident for the cache's life, as the Vulkan backend's.
class TextureCache {
public:
  bool Initialize(const Gpu& gpu, const ShaderSources& sources,
      std::string& detail);
  // Upload whatever `draws.textures` changed and release textures it no
  // longer has. No frame that samples them may be in flight.
  bool Update(const DrawList& draws, std::string& detail);
  // Record the mip passes of this frame's uploads.
  void RecordMipmaps(WGPUCommandEncoder encoder);
  // The view a role samples: the white 1 x 1 when the texture is none or
  // has no pixels.
  [[nodiscard]] WGPUTextureView ViewOf(TextureId texture) const;
  // Nonzero when the texture is resident, the role's "has a texture" flag.
  [[nodiscard]] std::uint32_t Entry(TextureId texture) const;
  // Which view ViewOf returns, as a number no other view has had: 0 the
  // white, a new one each time a texture's image is created. A bind group
  // holding an older number holds an older view.
  [[nodiscard]] std::uint64_t SerialOf(TextureId texture) const;
  // MMD's shared toon ramp `index`, 0-9, and its serial; outside them, the
  // white and 0.
  [[nodiscard]] WGPUTextureView RampView(std::int32_t index) const;
  [[nodiscard]] std::uint64_t RampSerial(std::int32_t index) const;
  // True only for a resident image whose entire G channel is zero.
  [[nodiscard]] bool ZeroGreen(TextureId texture) const;
  // True only for a resident image with an alpha below 1 anywhere.
  [[nodiscard]] bool Translucent(TextureId texture) const;
  [[nodiscard]] std::uint64_t uploads() const { return uploads_; }

private:
  struct Resident {
    Texture texture;
    TextureView view;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mip_levels = 1;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    std::uint64_t serial = 0;
    std::uint64_t revision = 0;
    std::uint64_t generation = 0;
    bool zero_green = false;
    bool translucent = false;
  };
  struct Ramp {
    Texture texture;
    TextureView view;
    std::uint64_t serial = 0;
  };
  struct Mipmaps {
    WGPUTexture texture = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    std::uint32_t levels = 1;
  };

  WGPUDevice device_ = nullptr;
  WGPUQueue queue_ = nullptr;
  Texture white_;
  TextureView white_view_;
  ShaderModule mip_vertex_;
  ShaderModule mip_fragment_;
  BindGroupLayout mip_layout_;
  PipelineLayout mip_pipeline_layout_;
  RenderPipeline mip_srgb_;
  RenderPipeline mip_linear_;
  Sampler mip_sampler_;
  std::array<Ramp, kSharedToonRamps> ramps_;
  std::unordered_map<TextureId, Resident> textures_;
  std::vector<Mipmaps> pending_;
  std::uint64_t generation_ = 0;
  std::uint64_t serial_ = 0;
  std::uint64_t uploads_ = 0;
};

// One parameter slot per material in its model's storage buffer, MToon's,
// MMD's or PreviewSurface's, one slot number in all three, as the Vulkan
// backend's; the frame buffer and MMD's shadow parameters (group 0); and
// each material's bind group (group 1): the three buffers and a texture
// view and sampler per role of its model (WebGPU policy §4). A slot is
// rewritten when its parameters revision changes or a texture it samples
// becomes resident or leaves; a bind group is rebuilt only when a role's
// view or sampler, or a buffer, changes.
class MaterialCache {
public:
  struct Entry {
    std::uint32_t slot = 0;
    std::uint64_t parameters_revision = 0;
    MaterialEntries entries;
    ToonShadingModel model = ToonShadingModel::PreviewSurface;
    bool preview_enabled = false;
    Float3 unlit_color;
    bool double_sided = false;
    // As the Vulkan backend's Entry: whether a draw also draws an outline
    // hull, its width and mode for the hull's culling, and whether the
    // width texture is zero throughout.
    bool outline = false;
    float outline_width = 0;
    ToonOutlineWidthMode outline_width_mode = ToonOutlineWidthMode::None;
    bool zero_width_texture = false;
    // Whether its draws are transparent, in `queue` order after every
    // opaque draw, and whether they write depth; an MMD material is
    // transparent when it can blend, decided every frame.
    bool transparent = false;
    std::int32_t queue = 0;
    bool depth_write = true;
    // MMD's material-table index and whether its diffuse alpha is below 1.
    std::int32_t draw_order = 0;
    bool mmd_translucent = false;
    std::array<std::uint64_t, kTextureRoles> views{};
    std::array<std::uint32_t, kTextureRoles> samplers{};
    std::uint64_t buffer_generation = 0;
    BindGroup group;
    std::uint64_t generation = 0;
  };

  bool Initialize(const Gpu& gpu, const ScenePipelines& pipelines,
      const TextureCache& textures, std::string& detail);
  // Write whatever `draws.materials` changed, the frame buffer when
  // `fast_values`, and free the slots of materials it no longer has.
  bool Update(const DrawList& draws, const TextureCache& textures,
      std::string& detail, bool fast_values = true);
  [[nodiscard]] const Entry* Find(MaterialId material) const;
  // Group 1 for a draw with no material: the display colour, which reads
  // no slot or texture.
  [[nodiscard]] WGPUBindGroup display_group() const { return display_group_.get(); }
  [[nodiscard]] WGPUBindGroup frame_group() const { return frame_group_.get(); }
  [[nodiscard]] std::uint64_t writes() const { return writes_; }
  [[nodiscard]] double parameter_write_ms() const { return parameter_write_ms_; }

private:
  bool Reserve(std::uint32_t slots, std::string& detail);
  BindGroup MaterialGroup(const std::array<WGPUTextureView, kTextureRoles>& views,
      const std::array<std::uint32_t, kTextureRoles>& samplers) const;

  WGPUDevice device_ = nullptr;
  WGPUQueue queue_ = nullptr;
  const ScenePipelines* pipelines_ = nullptr;
  WGPUTextureView white_ = nullptr;
  Buffer mtoon_buffer_;
  Buffer mmd_buffer_;
  Buffer preview_buffer_;
  std::uint64_t buffer_generation_ = 0;
  std::vector<MToonParameters> mtoon_slots_;
  std::vector<MmdParameters> mmd_slots_;
  std::vector<PreviewParameters> preview_slots_;
  Buffer frame_buffer_;
  Texture shadow_map_;
  TextureView shadow_view_;
  Buffer shadow_views_;
  Buffer mmd_shadow_;
  BindGroup frame_group_;
  BindGroup display_group_;
  std::uint32_t capacity_ = 0;
  std::uint32_t next_slot_ = 0;
  std::vector<std::uint32_t> free_slots_;
  std::unordered_map<MaterialId, Entry> entries_;
  std::uint64_t generation_ = 0;
  std::uint64_t writes_ = 0;
  double parameter_write_ms_ = 0;
};

// What MeshCache::Record drew, counted as the Vulkan backend counts it.
struct SceneRecord {
  // All recorded hulls, including those in the transparent part.
  std::uint32_t hulls = 0;
  std::uint32_t unlit = 0;
  std::uint32_t outline = 0;
  std::uint32_t opaque = 0;
  std::uint32_t transparent = 0;
  std::uint64_t triangles = 0;
  std::uint32_t pipeline_binds = 0;
};

// GPU copies of mesh geometry, keyed by mesh id, each stream re-uploaded
// only when its own revision changes, and each skinned or morphed mesh's
// deformation buffers (group 2), its joints written when its pose changes
// and nothing else does (design policy §11), as the Vulkan backend's.
class MeshCache {
public:
  bool Initialize(const Gpu& gpu, const ScenePipelines& pipelines,
      std::string& detail);
  bool Update(const DrawList& draws, std::string& detail, bool fast_values = true);
  // The most draws Record can make of `draws`: three per part, a
  // double-sided transparent MToon part's back, front and hull.
  [[nodiscard]] static std::size_t DrawBound(const DrawList& draws);
  // The Vulkan backend's order (its MeshCache::Record; material policy
  // §6): unlit draws; the hull of every opaque MToon draw that asks for
  // one; every opaque surface, MToon's multisampled through MToonCoverage;
  // the transparent draws by render queue, then as `draws` and each mesh's
  // parts list them, an MMD mesh's parts in MMD's material order, an MToon
  // surface followed by its hull; then MMD's edges. The draw constants of
  // the frame go into `constants`, a draw's at kDrawStride times its index,
  // which the caller writes to `ring` before the pass is submitted.
  // `width` and `height` are the target's, for the hulls' occlusion test.
  bool Record(WGPURenderPassEncoder pass, ScenePipelines& pipelines,
      const MaterialCache& materials, const DrawList& draws,
      std::uint32_t width, std::uint32_t height, WGPUBindGroup ring,
      std::vector<std::uint8_t>& constants, SceneRecord& record, std::string& detail);

  [[nodiscard]] std::uint64_t topology_uploads() const { return topology_uploads_; }
  [[nodiscard]] std::uint64_t point_uploads() const { return point_uploads_; }
  [[nodiscard]] std::uint64_t skin_uploads() const { return skin_uploads_; }
  [[nodiscard]] std::uint64_t pose_writes() const { return pose_writes_; }
  [[nodiscard]] std::uint64_t morph_uploads() const { return morph_uploads_; }
  [[nodiscard]] std::uint64_t morph_weight_writes() const { return morph_weight_writes_; }
  [[nodiscard]] double pose_write_ms() const { return pose_write_ms_; }
  [[nodiscard]] double morph_write_ms() const { return morph_write_ms_; }

private:
  struct Stream {
    Buffer buffer;
    std::uint64_t capacity = 0;
  };
  struct Entry {
    Stream vertices;
    OutlineBounds outline_bounds;
    Stream normals;
    Stream uvs;
    Stream tangents;
    Stream edge_scales;
    Stream sub_texture_uvs;
    Stream indices;
    Stream influences;
    Stream joints;
    Stream morph_offsets;
    Stream morph_ranges;
    Stream morph_weights;
    std::uint64_t morph_weight_bytes = 0;
    std::uint64_t points_revision = 0;
    std::uint64_t normals_revision = 0;
    std::uint64_t uvs_revision = 0;
    std::uint64_t tangents_revision = 0;
    std::uint64_t edge_scales_revision = 0;
    std::uint64_t sub_texture_uvs_revision = 0;
    std::uint64_t topology_revision = 0;
    std::uint64_t skin_revision = 0;
    std::uint64_t pose_revision = 0;
    std::uint64_t morph_revision = 0;
    std::uint64_t morph_weights_revision = 0;
    std::uint32_t index_count = 0;
    // Whether each optional stream holds a value for every vertex the
    // indices reach.
    bool has_uvs = false;
    bool has_tangents = false;
    bool has_edge_scales = false;
    bool has_sub_texture_uvs = false;
    bool skinned = false;
    bool morphed = false;
    std::uint32_t skin_flags = 0;
    BindGroup deformation;
    std::uint64_t generation = 0;
  };

  // Writes `size` bytes into `stream`, growing it when they do not fit; a
  // grown stream is a new buffer, so `grown` asks for a new bind group.
  bool Upload(Stream& stream, WGPUBufferUsage usage, const void* data,
      std::uint64_t size, bool* grown = nullptr);
  bool UpdateDeformation(const MeshSnapshot& mesh, Entry& entry, bool fast_values);
  void BindDeformation(Entry& entry);

  WGPUDevice device_ = nullptr;
  WGPUQueue queue_ = nullptr;
  const ScenePipelines* pipelines_ = nullptr;
  Buffer zero_uv_;
  Buffer zero_tangent_;
  // Bound with stride 0 in place of a mesh's missing edge scales: 1.
  Buffer unit_edge_scale_;
  Buffer zero_skin_;
  BindGroup undeformed_;
  BindGroup empty_group_;
  std::vector<Matrix4> joint_scratch_;
  std::vector<std::pair<std::size_t, std::size_t>> transparent_scratch_;
  OutlineOcclusion outline_occlusion_;
  std::unordered_map<MeshId, Entry> entries_;
  std::uint64_t generation_ = 0;
  std::uint64_t topology_uploads_ = 0;
  std::uint64_t point_uploads_ = 0;
  std::uint64_t skin_uploads_ = 0;
  std::uint64_t pose_writes_ = 0;
  std::uint64_t morph_uploads_ = 0;
  std::uint64_t morph_weight_writes_ = 0;
  double pose_write_ms_ = 0;
  double morph_write_ms_ = 0;
};

// The late-frame source's sample, read after the wait for the previous
// frame and before any fast value is written (WebGPU policy §6), as the
// Vulkan backend reads it.
class LateFrameState {
public:
  LateFrameSource source;
  [[nodiscard]] bool enabled() const { return source.read != nullptr; }
  template <class Statistics>
  bool Read(const DrawList& extracted, const DrawList*& frame,
      Statistics& stats, std::string& error) {
    frame = &extracted;
    stats.latency = {};
    stats.late_rejection.clear();
    if (enabled()) {
      error.clear();
      if (source.read(source.context, latest_, error)) {
        prepared_ = extracted;
        if (ApplyFastSnapshot(latest_, prepared_, stats.late_rejection)) {
          frame = &prepared_;
          ++stats.late_samples_applied;
          stats.latency.late_sample_applied = true;
        } else {
          ++stats.late_samples_rejected;
        }
      } else if (!error.empty()) {
        return false;
      }
    }
    stats.latency.inputs = frame->inputs;
    stats.latency.latched = SteadyNanoseconds();
    return true;
  }

private:
  FrameSnapshot latest_;
  DrawList prepared_;
};

} // namespace Toon::webgpu_internal
