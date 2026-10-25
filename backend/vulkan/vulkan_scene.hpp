// SPDX-License-Identifier: Apache-2.0
// Scene drawing shared by the offscreen and presentation paths: the device
// features they need, the scene pipelines, GPU copies of mesh geometry and
// textures, and the material and frame/light buffers. Private to backend/vulkan.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include <toon/extraction.hpp>
#include <toon/vulkan_backend.hpp>

#include "gpu_records.hpp"

namespace Toon::vulkan_internal {

// The shader contract's records, flags and writers, which the WebGPU
// backend fills the same way (backend/shared/).
using namespace Toon::backend_shared;

class LateFrameState {
public:
  LateFrameSource source;
  bool enabled() const { return source.read != nullptr; }
  template<class Statistics>
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
        } else ++stats.late_samples_rejected;
      } else if (!error.empty()) return false;
    }
    stats.latency.inputs = frame->inputs;
    stats.latency.latched = SteadyNanoseconds();
    return true;
  }
private:
  FrameSnapshot latest_;
  DrawList prepared_;
};

// Entries in the texture table the surface pipelines index, and a shader
// constant (kTextureCapacity in shaders/bindings/bindings_vulkan.slang). Entry 0 is a
// white placeholder, which a material samples for a texture it has not got
// or that is not resident, so every entry is always a valid image.
constexpr std::uint32_t kTextureCapacity = 128;
// MMD's shared toon ramps, SharedToonRamp(0) to (9), resident from the
// table's creation in the entries after the placeholder.
constexpr std::uint32_t kSharedToonRamps = 10;
constexpr std::uint32_t kFirstRampEntry = 1;

// The shared shadow views' depth image: a two-dimensional array of square
// layers, one per view, at the resolution ShadowSettings sets. Its texels
// go where the camera sees a receiver, and a receiver reads them filtered
// (shadow policy §5).
constexpr VkFormat kShadowFormat = VK_FORMAT_D32_SFLOAT;

// Vulkan 1.3 features the scene path uses: dynamic rendering (no render
// pass or framebuffer to rebuild on resize), Synchronization2 and timeline
// semaphores (design policy §19), and a texture table indexed by a value
// uniform across a draw.
struct SceneDeviceFeatures {
  VkPhysicalDeviceFeatures2 core{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  VkPhysicalDeviceVulkan12Features vulkan12{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceVulkan13Features vulkan13{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};

  // Chains the structures; the object must not move afterwards.
  SceneDeviceFeatures();
  SceneDeviceFeatures(const SceneDeviceFeatures&) = delete;
  SceneDeviceFeatures& operator=(const SceneDeviceFeatures&) = delete;

  // The chain to pass as VkDeviceCreateInfo::pNext, with required features
  // and the device's optional alphaToOne enabled.
  void* EnableRequired(VkPhysicalDevice device);
};

// Whether `device` has the required scene features; alphaToOne is optional.
// Otherwise `detail` names what is missing.
bool SupportsSceneFeatures(VkPhysicalDevice device, std::string& detail);

// The highest sample count at or below `requested` that `device` offers for
// rendering into `color_format` and `depth_format` together; 1 when it
// offers no other.
VkSampleCountFlagBits ChooseSampleCount(VkPhysicalDevice device,
    VkFormat color_format, VkFormat depth_format, std::uint32_t requested);

// The offscreen renderer's colour product and depth formats, which the GPU
// handoff publishes as device requirements.
constexpr VkFormat kOffscreenColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kOffscreenDepthFormat = VK_FORMAT_D32_SFLOAT;

// Whether `device` can draw both formats and copy them to the CPU.
bool SupportsOffscreenTargets(VkPhysicalDevice device);

// The device an offscreen renderer draws on: one it created, or one a host
// lent (GPU handoff policy §2). The scene code below it is the same either
// way.
struct OffscreenDevice {
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  std::uint32_t queue_family = 0;
  // Whether the device was created with alphaToOne enabled.
  bool alpha_to_one = false;
};


// Clip-from-world for a ToonView: the view's OpenGL-convention projection,
// then y flipped and z mapped from [-1, 1] to Vulkan's [0, 1].
Matrix4 VulkanClipFromWorld(const ToonView& view);

struct ScenePipeline {
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
};

// The scene pipelines for one colour/depth format pair and sample count:
// the unlit mesh pipeline, mtoon_opaque, mtoon_transparent, mtoon_outline,
// mmd_opaque, mmd_transparent, mmd_outline and preview_surface; and
// shadow, the shared caster pass, which draws depth alone into a shadow
// view, single-sampled, whatever the scene's count. The transparent ones and the
// outlines blend their source's alpha over the target; mmd_outline also
// reads the mesh's edge scale as a fourth stream, and mmd_opaque and
// mmd_transparent its sub-texture UV. Multisampled, mtoon_opaque
// turns its alpha into coverage. The MToon and MMD ones read their model's
// parameter buffer, the texture table and the wrap samplers through
// `material_layout` (set 0), in both stages, since the outline's width is
// the vertex stage's to apply. Every pipeline skins a mesh in the vertex
// stage through `skin_layout` (set 1): its influences and its joint buffer.
// Viewport and scissor are dynamic, and so are the surface pipelines' cull
// mode, front face, depth writes and vertex strides, so each pipeline is
// created once and survives every resize, every material and every mesh
// with or without UVs or a skin.
struct ScenePipelines {
  // Optional alphaToOne is enabled on the device when available. Without
  // it Mask keeps the opaque clear's alpha; transparent MSAA is unavailable.
  bool alpha_to_one = false;
  ScenePipeline mesh;
  ScenePipeline preview;
  ScenePipeline mtoon;
  ScenePipeline mtoon_transparent;
  ScenePipeline mtoon_outline;
  ScenePipeline mmd;
  ScenePipeline mmd_transparent;
  ScenePipeline mmd_outline;
  ScenePipeline shadow;
  VkDescriptorSetLayout material_layout = VK_NULL_HANDLE;
  VkDescriptorSetLayout skin_layout = VK_NULL_HANDLE;
  // Immutable in `material_layout`.
  VkSampler samplers[kSamplerCount] = {};
  // What every pipeline rasterizes at; the targets must match it.
  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

  static constexpr std::uint32_t kCount = 9;
};

// The SPIR-V of every scene pipeline, loaded before any device exists so a
// missing file is a failure rather than a skip.
struct SceneShaderWords {
  std::vector<std::uint32_t> preview_vertex;
  std::vector<std::uint32_t> preview_fragment;
  std::vector<std::uint32_t> mesh_vertex;
  std::vector<std::uint32_t> mesh_fragment;
  std::vector<std::uint32_t> mtoon_vertex;
  std::vector<std::uint32_t> mtoon_fragment;
  std::vector<std::uint32_t> mtoon_transparent_vertex;
  std::vector<std::uint32_t> mtoon_transparent_fragment;
  std::vector<std::uint32_t> mtoon_outline_vertex;
  std::vector<std::uint32_t> mtoon_outline_fragment;
  std::vector<std::uint32_t> mmd_vertex;
  std::vector<std::uint32_t> mmd_fragment;
  std::vector<std::uint32_t> mmd_transparent_vertex;
  std::vector<std::uint32_t> mmd_transparent_fragment;
  std::vector<std::uint32_t> mmd_outline_vertex;
  std::vector<std::uint32_t> mmd_outline_fragment;
  std::vector<std::uint32_t> shadow_vertex;
  std::vector<std::uint32_t> shadow_fragment;
};

bool LoadSceneShaders(const SceneShaders& shaders, SceneShaderWords& words,
    std::string& detail);
bool CreateScenePipelines(VkDevice device, const SceneShaderWords& words,
    VkFormat color_format, VkFormat depth_format,
    VkSampleCountFlagBits samples, ScenePipelines& pipelines,
    std::string& detail);
void DestroyScenePipelines(VkDevice device, ScenePipelines& pipelines);

// The pipelines alone, at `samples`, over the set layouts and samplers
// `pipelines` already holds: another sample count destroys and creates these
// and keeps the layouts, so every descriptor set allocated from them, and
// the material, texture and skin data they point at, stays as it is.
bool CreateScenePipelineObjects(VkDevice device, const SceneShaderWords& words,
    VkFormat color_format, VkFormat depth_format,
    VkSampleCountFlagBits samples, ScenePipelines& pipelines,
    std::string& detail);
void DestroyScenePipelineObjects(VkDevice device, ScenePipelines& pipelines);

// A device-local 2D image with one view.
struct DeviceImage {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};

// More than one layer, or `array`, gives the image a two-dimensional
// array view of every layer.
bool CreateDeviceImage(VkPhysicalDevice physical_device, VkDevice device,
    VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
    std::uint32_t width, std::uint32_t height, DeviceImage& image,
    std::string& detail, std::uint32_t mip_levels = 1,
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
    std::uint32_t layers = 1, bool array = false);
void DestroyDeviceImage(VkDevice device, DeviceImage& image);

VkImageMemoryBarrier2 ImageBarrier(VkImage image, VkImageAspectFlags aspect,
    VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
    VkPipelineStageFlags2 destination_stage,
    VkAccessFlags2 destination_access, VkImageLayout old_layout,
    VkImageLayout new_layout);

// What a scene pass draws into. Multisampled, `color` and `depth` hold the
// samples, which the pass resolves as it ends and does not keep: colour, by
// averaging, into `color_resolve`, and depth, by taking sample 0, into
// `depth_resolve` when there is one. A resolve runs at the colour output
// stage as a colour attachment write, depth's included (the Vulkan
// specification's multisample resolve operations), which a barrier on a
// resolve target names.
struct SceneAttachments {
  VkImageView color = VK_NULL_HANDLE;
  VkImageView depth = VK_NULL_HANDLE;
  VkImageView color_resolve = VK_NULL_HANDLE;
  VkImageView depth_resolve = VK_NULL_HANDLE;
};

// Begin dynamic rendering into `attachments`, colour cleared to the scene
// background and depth to 1, with the viewport and scissor covering
// `extent`.
void BeginSceneRendering(VkCommandBuffer command,
    const SceneAttachments& attachments, VkExtent2D extent,
    const BackgroundSettings& background = {});

// A host-visible buffer mapped for its whole life.
struct HostBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  void* mapped = nullptr;
  VkDeviceSize capacity = 0;
  bool coherent = false;
};

// Host-visible memory, mapped; `preferred` when a memory type has it.
// Coherent suits what the CPU writes, cached what it reads back.
bool CreateHostBuffer(VkPhysicalDevice physical_device, VkDevice device,
    VkDeviceSize size, VkBufferUsageFlags usage, HostBuffer& buffer,
    std::string& detail,
    VkMemoryPropertyFlags preferred = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
void DestroyHostBuffer(VkDevice device, HostBuffer& buffer);
bool FlushIfNeeded(VkDevice device, const HostBuffer& buffer,
    std::string& detail);
bool InvalidateIfNeeded(VkDevice device, const HostBuffer& buffer,
    std::string& detail);

// GPU copies of textures, keyed by texture id, each an entry of the texture
// table in the material descriptor set. A texture is uploaded only when its
// revision changes: its pixels go through a staging buffer, and the copy and
// its mipmaps are recorded at the start of the frame that first samples it.
// MMD's shared toon ramps are the renderer's own, uploaded with the
// placeholder and resident for the cache's life.
//
// That upload is recorded on the render thread, on the graphics queue: a
// stand-in for design policy §20's off-thread transfer, as geometry's is.
class TextureCache {
public:
  bool Initialize(VkPhysicalDevice physical_device, VkDevice device,
      VkDescriptorSet set, std::string& detail);
  // Stage whatever `draws.textures` changed and release textures it no
  // longer has. No frame that samples them may be in flight.
  bool Update(const DrawList& draws, std::string& detail);
  // Record this frame's uploads; call before rendering starts.
  void RecordUploads(VkCommandBuffer command);
  // The table entry a texture samples through: 0, the placeholder, when it
  // is none, has no pixels or did not fit in the table.
  [[nodiscard]] std::uint32_t Entry(TextureId texture) const;
  // The table entry of MMD's shared toon ramp `index`: 0, white, outside
  // 0-9.
  [[nodiscard]] static std::uint32_t SharedRampEntry(std::int32_t index);
  // True only for a resident image whose entire G channel is zero.
  [[nodiscard]] bool ZeroGreen(TextureId texture) const;
  // True only for a resident image with an alpha below 1 anywhere.
  [[nodiscard]] bool Translucent(TextureId texture) const;
  void Destroy();

  [[nodiscard]] std::uint64_t uploads() const {
    return uploads_;
  }

private:
  struct Resident {
    // 0 while the texture has no pixels or no free entry.
    std::uint32_t entry = 0;
    DeviceImage image;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mip_levels = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint64_t revision = 0;
    std::uint64_t generation = 0;
    bool zero_green = false;
    bool translucent = false;
  };
  struct Upload {
    VkImage image = VK_NULL_HANDLE;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mip_levels = 1;
    HostBuffer staging;
  };

  bool Stage(const std::vector<std::uint8_t>& pixels, VkImage image,
      std::uint32_t width, std::uint32_t height, std::uint32_t mip_levels,
      std::string& detail);
  void Point(std::uint32_t entry, VkImageView view);
  void Release(Resident& resident);

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkDescriptorSet set_ = VK_NULL_HANDLE;
  // Whether each format can make its own mipmaps by linear blits.
  bool srgb_mipmaps_ = false;
  bool linear_mipmaps_ = false;
  DeviceImage placeholder_;
  DeviceImage ramps_[kSharedToonRamps];
  std::unordered_map<TextureId, Resident> textures_;
  std::vector<std::uint32_t> free_entries_;
  std::uint32_t next_entry_ = kFirstRampEntry + kSharedToonRamps;
  // Staged, not yet recorded.
  std::vector<Upload> pending_;
  // Recorded; freed once the frame that copies from them has completed.
  std::vector<HostBuffer> recorded_;
  std::uint64_t generation_ = 0;
  std::uint64_t uploads_ = 0;
};

// One parameter slot per material, in its model's host-visible storage
// buffer, which that model's pipelines index by slot (material policy §7):
// MToonParameters for MToon, MmdParameters for MMD and PreviewParameters for
// PreviewSurface, one slot number in all three. A slot is rewritten when its
// material's parameters revision
// changes or a texture it samples moves to another table entry; a new
// material takes a free slot, and the buffers grow by doubling, keeping
// every slot. Its descriptor set also holds the texture table, which a
// TextureCache fills.
class MaterialCache {
public:
  struct Entry {
    std::uint32_t slot = 0;
    std::uint64_t parameters_revision = 0;
    // The table entries the slot was written with.
    MaterialEntries entries;
    ToonShadingModel model = ToonShadingModel::PreviewSurface;
    bool preview_enabled = false;
    Float3 unlit_color;
    bool double_sided = false;
    // Whether a draw with this material also draws an outline's hull: an
    // MToon material that asks for an outline of some width, through
    // mtoon_outline, or an MMD one that draws its edge, through mmd_outline.
    bool outline = false;
    float outline_width = 0;
    ToonOutlineWidthMode outline_width_mode = ToonOutlineWidthMode::None;
    bool zero_width_texture = false;
    // Whether its draws go through mtoon_transparent or mmd_transparent,
    // in `queue` order after every opaque draw, and whether they write
    // depth. An MMD material is transparent when it can blend: its diffuse
    // alpha is below 1, or a base or sphere texture it samples is
    // translucent. Its queue is 3000, and it writes depth, as MMD does.
    bool transparent = false;
    std::int32_t queue = 0;
    bool depth_write = true;
    // MMD's material-table index, which orders an MMD material's
    // transparent draws within one mesh.
    std::int32_t draw_order = 0;
    // An MMD material's diffuse alpha is below 1.
    bool mmd_translucent = false;
    std::uint64_t generation = 0;
  };

  bool Initialize(VkPhysicalDevice physical_device, VkDevice device,
      VkDescriptorSetLayout layout, std::string& detail);
  // Write whatever `draws.materials` changed and free the slots of
  // materials it no longer has. No frame that reads the buffer may be in
  // flight, and `textures` has been updated for the same draws. A light's
  // entry names the view of `views` it casts into, while `draws.shadows`
  // has every family but MMD receive.
  bool Update(const DrawList& draws, const TextureCache& textures,
      std::string& detail, bool fast_values = true,
      const std::vector<ShadowView>* views = nullptr);
  [[nodiscard]] const Entry* Find(MaterialId material) const;
  [[nodiscard]] VkDescriptorSet descriptor_set() const {
    return set_;
  }
  void Destroy();

  [[nodiscard]] std::uint64_t writes() const {
    return writes_;
  }
  double parameter_write_ms() const { return parameter_write_ms_; }

private:
  bool Reserve(std::uint32_t slots, std::string& detail);

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkDescriptorSet set_ = VK_NULL_HANDLE;
  HostBuffer buffer_;
  HostBuffer mmd_buffer_;
  HostBuffer preview_buffer_;
  // Fixed frame/light buffer, kFrameBytes bytes: time, debug, uniform ambient
  // and up to kLightCapacity direct sources. Updated after the prior frame.
  HostBuffer frame_buffer_;
  std::uint32_t capacity_ = 0;
  std::uint32_t next_slot_ = 0;
  std::vector<std::uint32_t> free_slots_;
  std::unordered_map<MaterialId, Entry> entries_;
  std::uint64_t generation_ = 0;
  std::uint64_t writes_ = 0;
  double parameter_write_ms_ = 0;
};

// What MeshCache::Record drew, part by part, and where it marks the parts'
// ends on the GPU.
struct SceneRecord {
  // All recorded hulls, including those in the transparent part.
  std::uint32_t hulls = 0;
  // Timestamps written into `timestamps` as the unlit draws, the opaque
  // hulls and the opaque surfaces end, at `first_query` and the two after
  // it; none when it is null.
  VkQueryPool timestamps = VK_NULL_HANDLE;
  std::uint32_t first_query = 0;
  std::uint32_t unlit = 0;
  std::uint32_t outline = 0;
  std::uint32_t opaque = 0;
  std::uint32_t transparent = 0;
  std::uint64_t triangles = 0;
  std::uint32_t pipeline_binds = 0;
};

// GPU copies of mesh geometry, keyed by mesh id. Points, normals and
// indices are re-uploaded only when their own revision changes, into the
// existing buffer when it is large enough.
//
// A skinned mesh also has its influences, uploaded when its skin changes,
// and a joint buffer, rewritten when its pose changes and nothing else does
// (design policy §11): the skeleton-to-mesh transform, then each joint's
// skinning transform composed with the geometry bind transform. Both are
// bound through the mesh's own skin descriptor set; an unskinned mesh binds
// a shared one it never reads.
//
// Geometry is written through host-visible memory on the render thread. That
// is a stand-in for the staged, off-thread upload of design policy §20.
class MeshCache {
public:
  bool Initialize(VkPhysicalDevice physical_device, VkDevice device,
      VkDescriptorSetLayout skin_layout, std::string& detail);
  // Upload whatever `draws` changed and release meshes it no longer draws.
  // No frame that reads these buffers may be in flight.
  bool Update(const DrawList& draws, std::string& detail, bool fast_values = true);
  // A draw is one part of a mesh, a range of its triangles with the
  // material its face subset binds. Unlit draws first; then the outline
  // hull of every opaque MToon draw that asks for one (design policy §10's
  // first pass); then every opaque draw whose material selected MToon, a
  // Mask one by alpha to coverage when `pipelines` are multisampled, or
  // MMD. Transparent draws come last, ordered by their material's render
  // queue and then as `draws` and each mesh's parts list them, an MMD
  // mesh's parts in MMD's material order; an MToon surface is followed by
  // its hull (material policy §6). MMD's edges come after every surface,
  // as MMD draws a model's edge after its materials. `record`, when given,
  // counts what was drawn and marks where each pass ends.
  void Record(VkCommandBuffer command, const ScenePipelines& pipelines,
      const MaterialCache& materials, const DrawList& draws,
      VkExtent2D extent, SceneRecord* record = nullptr);
  void Destroy();

  [[nodiscard]] std::uint64_t topology_uploads() const {
    return topology_uploads_;
  }
  [[nodiscard]] std::uint64_t point_uploads() const {
    return point_uploads_;
  }
  [[nodiscard]] std::uint64_t skin_uploads() const {
    return skin_uploads_;
  }
  [[nodiscard]] std::uint64_t pose_writes() const {
    return pose_writes_;
  }

  // Draw every part that casts into a shadow view (ShadowFit::Casts)
  // through the shadow pipeline, inside rendering into the view, as the
  // draws' poses and morphs place them, and count them in `casters` and,
  // MMD's, in `mmd_casters`.
  void RecordShadowCasters(VkCommandBuffer command,
      const ScenePipelines& pipelines, const MaterialCache& materials,
      const DrawList& draws, const Matrix4& clip_from_world,
      std::uint32_t& casters, std::uint32_t& mmd_casters) const;

  [[nodiscard]] std::uint64_t morph_uploads() const { return morph_uploads_; }
  [[nodiscard]] std::uint64_t morph_weight_writes() const { return morph_weight_writes_; }
  double pose_write_ms() const { return pose_write_ms_; }
  double morph_write_ms() const { return morph_write_ms_; }
private:
  struct Entry {
    HostBuffer vertices;
    OutlineBounds outline_bounds;
    HostBuffer normals;
    HostBuffer uvs;
    HostBuffer tangents;
    HostBuffer edge_scales;
    HostBuffer sub_texture_uvs;
    HostBuffer indices;
    HostBuffer influences;
    HostBuffer joints;
    HostBuffer morph_offsets;
    HostBuffer morph_ranges;
    HostBuffer morph_weights;
    std::uint64_t morph_revision = 0;
    std::uint64_t morph_weights_revision = 0;
    VkDeviceSize morph_weight_bytes = 0;
    std::uint64_t points_revision = 0;
    std::uint64_t normals_revision = 0;
    std::uint64_t uvs_revision = 0;
    std::uint64_t tangents_revision = 0;
    bool has_tangents = false;
    std::uint64_t edge_scales_revision = 0;
    std::uint64_t sub_texture_uvs_revision = 0;
    std::uint64_t topology_revision = 0;
    std::uint64_t skin_revision = 0;
    std::uint64_t pose_revision = 0;
    std::uint32_t index_count = 0;
    // Whether `uvs` holds a coordinate for every vertex the indices reach.
    bool has_uvs = false;
    // Whether `edge_scales` holds a scale for every vertex the indices
    // reach; mmd_outline otherwise scales every vertex by 1.
    bool has_edge_scales = false;
    // Whether `sub_texture_uvs` holds a UV for every vertex the indices
    // reach; mmd_opaque and mmd_transparent otherwise sample a sub-texture
    // sphere at (0, 0).
    bool has_sub_texture_uvs = false;
    // Whether this frame's draw skins the mesh, and the flags that say how.
    bool skinned = false;
    bool morphed = false;
    std::uint32_t skin_flags = 0;
    // Allocated when the mesh is first skinned, from `skin_pools_[pool]`.
    VkDescriptorSet skin_set = VK_NULL_HANDLE;
    std::size_t skin_pool = 0;
    std::uint64_t generation = 0;
  };
  double pose_write_ms_ = 0;
  double morph_write_ms_ = 0;

  bool Upload(HostBuffer& buffer, VkBufferUsageFlags usage, const void* data,
      VkDeviceSize size, std::string& detail);
  bool UpdateSkin(const MeshSnapshot& mesh, Entry& entry,
      std::string& detail, bool fast_values);
  bool AllocateSkinSet(VkDescriptorSet& set, std::size_t& pool,
      std::string& detail);
  void PointSkinSet(VkDescriptorSet set, const Entry* entry = nullptr);
  void Release(Entry& entry);

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout skin_layout_ = VK_NULL_HANDLE;
  // Bound with stride 0 in place of a mesh's missing UVs or sub-texture
  // UVs.
  HostBuffer zero_uv_;
  HostBuffer zero_tangent_;
  // Bound with stride 0 in place of a mesh's missing edge scales: 1.
  HostBuffer unit_edge_scale_;
  // Bound, never read, in place of an unskinned mesh's skin.
  HostBuffer zero_skin_;
  VkDescriptorSet unskinned_set_ = VK_NULL_HANDLE;
  // Skin sets come from fixed-size pools, a new one when the last is full.
  std::vector<VkDescriptorPool> skin_pools_;
  std::vector<Matrix4> joint_scratch_;
  // This frame's transparent draws, as a mesh's index into `draws.draws`
  // and its part's index, kept so a steady frame sorts without allocating.
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
};

// The shared shadow views (shadow policy §5): each frame the core's
// ShadowFit decides which views are drawn and from where, and each is
// drawn before the scene by the shared caster pass into its layer of one
// depth image. MMD's receivers read the view of the scene's first direct
// light as MMD's self-shadow map (material policy §10). The image, MMD's
// parameters and every view's sit in the material descriptor set, at
// bindings 5, 6 and 8.
class ShadowMap {
public:
  bool Initialize(VkPhysicalDevice physical_device, VkDevice device,
      VkDescriptorSet set, std::string& detail);
  // Fit this frame's views, timing the fit, and size the image for them:
  // another resolution rebuilds it, and more views than it has layers
  // grow it, keeping every other resource. No frame that reads the image
  // or the parameters may be in flight.
  bool Update(const DrawList& draws, std::string& detail, double& fit_ms);
  // Record the views' passes, outside any other rendering. A frame that
  // draws no view records nothing, once the image has been readable since
  // a first, empty pass.
  void Record(VkCommandBuffer command, const ScenePipelines& pipelines,
      const MaterialCache& materials, const MeshCache& meshes,
      const DrawList& draws);
  void Destroy();

  [[nodiscard]] const ShadowFit& fit() const { return fit_; }
  // The last recorded frame's caster draws, every family's and MMD's.
  [[nodiscard]] std::uint32_t caster_draws() const { return caster_draws_; }
  [[nodiscard]] std::uint32_t mmd_draws() const { return mmd_draws_; }
  // The image's texels on a side and its layers, its device memory, and
  // a view of each layer, which reads as depth once Record has run; a
  // rebuild changes them, and counts in `rebuilds`.
  [[nodiscard]] std::uint32_t resolution() const { return resolution_; }
  [[nodiscard]] std::uint32_t layers() const {
    return static_cast<std::uint32_t>(layer_views_.size());
  }
  [[nodiscard]] VkDeviceSize memory_bytes() const { return memory_bytes_; }
  [[nodiscard]] VkImageView layer_view(std::uint32_t layer) const {
    return layer_views_[layer];
  }
  [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }

private:
  bool Build(std::uint32_t resolution, std::uint32_t layers,
      std::string& detail);
  void Release();

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkDescriptorSet set_ = VK_NULL_HANDLE;
  ShadowFit fit_;
  DeviceImage image_;
  // One two-dimensional view per layer, which a pass draws into.
  std::vector<VkImageView> layer_views_;
  std::uint32_t resolution_ = 0;
  VkDeviceSize memory_bytes_ = 0;
  std::uint64_t rebuilds_ = 0;
  HostBuffer buffer_;
  HostBuffer views_buffer_;
  std::uint32_t caster_draws_ = 0;
  std::uint32_t mmd_draws_ = 0;
  // Whether the image has been laid out for reading.
  bool ready_ = false;
};

} // namespace Toon::vulkan_internal
