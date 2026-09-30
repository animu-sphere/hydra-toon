// SPDX-License-Identifier: Apache-2.0
// Scene drawing shared by the offscreen and presentation paths: the device
// features they need, the scene pipelines, GPU copies of mesh geometry and
// textures, and the material parameter buffer. Private to backend/vulkan.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include <toon/extraction.hpp>
#include <toon/vulkan_backend.hpp>

namespace Toon::vulkan_internal {

// Must match DrawConstants in shaders/mesh.slang.
struct DrawConstants {
  float clip_from_object[16];
  float color[4];
  // The kDraw* skinning flags and influence count, as mtoon_opaque's.
  std::uint32_t flags;
  std::uint32_t padding[3];
};

// Must match DrawConstants in shaders/mtoon_common.slang, which both
// mtoon_opaque and mtoon_outline read: 128 bytes, the push constant size
// every Vulkan device offers.
struct MToonDrawConstants {
  float clip_from_object[16];
  // The top three rows of view-from-object: its 3x3 in xyz and the
  // translation in w. The shaders derive the normal matrix from it.
  float view_from_object_rows[12];
  std::uint32_t material_slot;
  // kDrawHasUVs when the mesh has texture coordinates; kDrawOrthographic
  // under an orthographic camera; kDrawBlend when its material is
  // transparent; kDrawAlphaToCoverage when mtoon_opaque is multisampled;
  // kDrawSkinned, kDrawConstantInfluences and the influences
  // per point from kInfluenceCountShift up when it is skinned.
  std::uint32_t flags;
  // |projection[1][1]|, for mtoon_outline's screen-coordinates width.
  float projection_scale;
  // The scene's units per metre, for mtoon_outline's world-coordinates
  // width, which MToon gives in metres.
  float units_per_meter;
};
static_assert(sizeof(MToonDrawConstants) == 128);

constexpr std::uint32_t kDrawHasUVs = 1U;
constexpr std::uint32_t kDrawSkinned = 2U;
constexpr std::uint32_t kDrawConstantInfluences = 4U;
// The camera is orthographic: every fragment is seen along view-space +z.
constexpr std::uint32_t kDrawOrthographic = 16U;
// The material is transparent: mtoon_outline's hull returns the surface's
// alpha rather than 1.
constexpr std::uint32_t kDrawBlend = 32U;
// mtoon_opaque turns its alpha into samples: a Mask fragment returns its
// coverage of the cutoff rather than being kept or cut away whole.
constexpr std::uint32_t kDrawAlphaToCoverage = 64U;
constexpr std::uint32_t kInfluenceCountShift = 8U;

// An MToon slot's outline width mode, as mtoon_outline reads it; 0 draws
// no outline.
constexpr std::uint32_t kOutlineWorld = 1U;
constexpr std::uint32_t kOutlineScreen = 2U;

// Entries in the texture table mtoon_opaque indexes, and a shader constant
// (kTextureCapacity in shaders/mtoon.slang). Entry 0 is a white placeholder,
// which a material samples for a texture it has not got or that is not
// resident, so every entry is always a valid image.
constexpr std::uint32_t kTextureCapacity = 128;

// One sampler per glTF wrap pair: index wrap_s * 3 + wrap_t, in ToonWrap
// order.
constexpr std::uint32_t kSamplerCount = 9;

// One material's slot in the parameter buffer. Must match MToonParameters
// in shaders/mtoon_common.slang.
struct MToonParameters {
  float base_color[4];
  // a: the alpha cutoff, negative when the material is not Mask.
  float shade_color[4];
  float emissive[4];
  // x shading shift, y shading toony, z GI equalization.
  float shading[4];
  // x base texture entry, y its sampler; z shade texture entry, w its
  // sampler.
  std::uint32_t textures[4];
  // Each texture's KHR_texture_transform as two rows of a 2x3 affine map
  // in glTF's UV space, each padded to four floats.
  float base_uv[8];
  float shade_uv[8];
  // rgb linear, a the outline lighting mix.
  float outline_color[4];
  // x the outline width factor.
  float outline[4];
  // x outline width texture entry, y its sampler; z kOutlineWorld,
  // kOutlineScreen or 0.
  std::uint32_t outline_texture[4];
  float outline_uv[8];
  // rgb the parametric rim colour, a the rim lighting mix.
  float rim_color[4];
  // x the parametric rim's fresnel power, y its lift.
  float rim[4];
  // rgb matcapFactor.
  float matcap[4];
  // x MatCap texture entry, y its sampler; z rim multiply texture entry,
  // w its sampler.
  std::uint32_t rim_textures[4];
  float matcap_uv[8];
  float rim_uv[8];
};
static_assert(sizeof(MToonParameters) == 352);

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

  // The chain to pass as VkDeviceCreateInfo::pNext, with every feature the
  // scene path needs enabled.
  void* EnableRequired();
};

// Whether `device` is a Vulkan 1.3 device with every SceneDeviceFeatures
// feature; otherwise `detail` names what is missing.
bool SupportsSceneFeatures(VkPhysicalDevice device, std::string& detail);

// The highest sample count at or below `requested` that `device` offers for
// rendering into `color_format` and `depth_format` together; 1 when it
// offers no other.
VkSampleCountFlagBits ChooseSampleCount(VkPhysicalDevice device,
    VkFormat color_format, VkFormat depth_format, std::uint32_t requested);

// Clip-from-world for a ToonView: the view's OpenGL-convention projection,
// then y flipped and z mapped from [-1, 1] to Vulkan's [0, 1].
Matrix4 VulkanClipFromWorld(const ToonView& view);

struct ScenePipeline {
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
};

// The scene pipelines for one colour/depth format pair and sample count:
// the unlit mesh pipeline, mtoon_opaque, mtoon_transparent and
// mtoon_outline. The last two blend their source's alpha over the target;
// multisampled, mtoon_opaque turns its alpha into coverage. The MToon ones
// read the material parameter buffer, the texture table and the wrap
// samplers through `material_layout` (set 0), in both stages, since the
// outline's width is the vertex stage's to apply. Every pipeline skins a
// mesh in the vertex stage through `skin_layout` (set 1): its influences
// and its joint buffer. Viewport and scissor are dynamic, and so are the MToon pipelines'
// cull mode, front face, depth writes and vertex strides, so each pipeline
// is created once and survives every resize, every material and every mesh
// with or without UVs or a skin.
struct ScenePipelines {
  ScenePipeline mesh;
  ScenePipeline mtoon;
  ScenePipeline mtoon_transparent;
  ScenePipeline mtoon_outline;
  VkDescriptorSetLayout material_layout = VK_NULL_HANDLE;
  VkDescriptorSetLayout skin_layout = VK_NULL_HANDLE;
  // Immutable in `material_layout`.
  VkSampler samplers[kSamplerCount] = {};
  // What every pipeline rasterizes at; the targets must match it.
  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

  static constexpr std::uint32_t kCount = 4;
};

// The SPIR-V of every scene pipeline, loaded before any device exists so a
// missing file is a failure rather than a skip.
struct SceneShaderWords {
  std::vector<std::uint32_t> mesh_vertex;
  std::vector<std::uint32_t> mesh_fragment;
  std::vector<std::uint32_t> mtoon_vertex;
  std::vector<std::uint32_t> mtoon_fragment;
  std::vector<std::uint32_t> mtoon_transparent_vertex;
  std::vector<std::uint32_t> mtoon_transparent_fragment;
  std::vector<std::uint32_t> mtoon_outline_vertex;
  std::vector<std::uint32_t> mtoon_outline_fragment;
};

bool LoadSceneShaders(const SceneShaders& shaders, SceneShaderWords& words,
    std::string& detail);
bool CreateScenePipelines(VkDevice device, const SceneShaderWords& words,
    VkFormat color_format, VkFormat depth_format,
    VkSampleCountFlagBits samples, ScenePipelines& pipelines,
    std::string& detail);
void DestroyScenePipelines(VkDevice device, ScenePipelines& pipelines);

// The four pipelines alone, at `samples`, over the set layouts and samplers
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

bool CreateDeviceImage(VkPhysicalDevice physical_device, VkDevice device,
    VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
    std::uint32_t width, std::uint32_t height, DeviceImage& image,
    std::string& detail, std::uint32_t mip_levels = 1,
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
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
    const SceneAttachments& attachments, VkExtent2D extent);

// A host-visible buffer mapped for its whole life.
struct HostBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  void* mapped = nullptr;
  VkDeviceSize capacity = 0;
  bool coherent = false;
};

bool CreateHostBuffer(VkPhysicalDevice physical_device, VkDevice device,
    VkDeviceSize size, VkBufferUsageFlags usage, HostBuffer& buffer,
    std::string& detail);
void DestroyHostBuffer(VkDevice device, HostBuffer& buffer);
bool FlushIfNeeded(VkDevice device, const HostBuffer& buffer,
    std::string& detail);
bool InvalidateIfNeeded(VkDevice device, const HostBuffer& buffer,
    std::string& detail);

// GPU copies of textures, keyed by texture id, each an entry of the texture
// table in the material descriptor set. A texture is uploaded only when its
// revision changes: its pixels go through a staging buffer, and the copy and
// its mipmaps are recorded at the start of the frame that first samples it.
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
  // True only for a resident image whose entire G channel is zero.
  [[nodiscard]] bool ZeroGreen(TextureId texture) const;
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
  std::unordered_map<TextureId, Resident> textures_;
  std::vector<std::uint32_t> free_entries_;
  std::uint32_t next_entry_ = 1;
  // Staged, not yet recorded.
  std::vector<Upload> pending_;
  // Recorded; freed once the frame that copies from them has completed.
  std::vector<HostBuffer> recorded_;
  std::uint64_t generation_ = 0;
  std::uint64_t uploads_ = 0;
};

// The texture table entry of every texture an MToon slot samples.
struct MaterialEntries {
  std::uint32_t base = 0;
  std::uint32_t shade = 0;
  std::uint32_t outline = 0;
  std::uint32_t matcap = 0;
  std::uint32_t rim = 0;

  friend bool operator==(const MaterialEntries&,
      const MaterialEntries&) = default;
};

// One parameter slot per material, in a host-visible storage buffer that
// mtoon_opaque indexes by slot (material policy §7). A slot is rewritten
// when its material's parameters revision changes or a texture it samples
// moves to another table entry; a new material takes a free slot, and the
// buffer grows by doubling, keeping every slot. Its descriptor set also
// holds the texture table, which a TextureCache fills.
class MaterialCache {
public:
  struct Entry {
    std::uint32_t slot = 0;
    std::uint64_t parameters_revision = 0;
    // The table entries the slot was written with.
    MaterialEntries entries;
    ToonShadingModel model = ToonShadingModel::PreviewSurface;
    bool double_sided = false;
    // Whether a draw with this material also draws mtoon_outline's hull:
    // an MToon material that asks for an outline of some width.
    bool outline = false;
    bool zero_width_texture = false;
    // Whether its draws go through mtoon_transparent, in `queue` order
    // after every opaque draw, and whether they write depth.
    bool transparent = false;
    std::int32_t queue = 0;
    bool depth_write = true;
    std::uint64_t generation = 0;
  };

  bool Initialize(VkPhysicalDevice physical_device, VkDevice device,
      VkDescriptorSetLayout layout, std::string& detail);
  // Write whatever `draws.materials` changed and free the slots of
  // materials it no longer has. No frame that reads the buffer may be in
  // flight, and `textures` has been updated for the same draws.
  bool Update(const DrawList& draws, const TextureCache& textures,
      std::string& detail);
  [[nodiscard]] const Entry* Find(MaterialId material) const;
  [[nodiscard]] VkDescriptorSet descriptor_set() const {
    return set_;
  }
  void Destroy();

  [[nodiscard]] std::uint64_t writes() const {
    return writes_;
  }

private:
  bool Reserve(std::uint32_t slots, std::string& detail);

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkDescriptorSet set_ = VK_NULL_HANDLE;
  HostBuffer buffer_;
  std::uint32_t capacity_ = 0;
  std::uint32_t next_slot_ = 0;
  std::vector<std::uint32_t> free_slots_;
  std::unordered_map<MaterialId, Entry> entries_;
  std::uint64_t generation_ = 0;
  std::uint64_t writes_ = 0;
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
  bool Update(const DrawList& draws, std::string& detail);
  // Unlit draws first; then the outline hull of every opaque MToon draw
  // that asks for one (design policy §10's first pass); then every opaque
  // draw whose material selected MToon, a Mask one by alpha to coverage
  // when `pipelines` are multisampled. Transparent MToon draws come last,
  // ordered by their material's render queue and then as `draws` lists
  // them, each surface followed by its hull (material policy §6). `record`,
  // when given, counts what was drawn and marks where each part ends.
  void Record(VkCommandBuffer command, const ScenePipelines& pipelines,
      const MaterialCache& materials, const DrawList& draws,
      SceneRecord* record = nullptr);
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

private:
  struct Entry {
    HostBuffer vertices;
    HostBuffer normals;
    HostBuffer uvs;
    HostBuffer indices;
    HostBuffer influences;
    HostBuffer joints;
    std::uint64_t points_revision = 0;
    std::uint64_t normals_revision = 0;
    std::uint64_t uvs_revision = 0;
    std::uint64_t topology_revision = 0;
    std::uint64_t skin_revision = 0;
    std::uint64_t pose_revision = 0;
    std::uint32_t index_count = 0;
    // Whether `uvs` holds a coordinate for every vertex the indices reach.
    bool has_uvs = false;
    // Whether this frame's draw skins the mesh, and the flags that say how.
    bool skinned = false;
    std::uint32_t skin_flags = 0;
    // Allocated when the mesh is first skinned, from `skin_pools_[pool]`.
    VkDescriptorSet skin_set = VK_NULL_HANDLE;
    std::size_t skin_pool = 0;
    std::uint64_t generation = 0;
  };

  bool Upload(HostBuffer& buffer, VkBufferUsageFlags usage, const void* data,
      VkDeviceSize size, std::string& detail);
  bool UpdateSkin(const MeshSnapshot& mesh, Entry& entry,
      std::string& detail);
  bool AllocateSkinSet(VkDescriptorSet& set, std::size_t& pool,
      std::string& detail);
  void PointSkinSet(VkDescriptorSet set, VkBuffer influences,
      VkBuffer joints);
  void Release(Entry& entry);

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout skin_layout_ = VK_NULL_HANDLE;
  // Bound with stride 0 in place of a mesh's missing UVs.
  HostBuffer zero_uv_;
  // Bound, never read, in place of an unskinned mesh's skin.
  HostBuffer zero_skin_;
  VkDescriptorSet unskinned_set_ = VK_NULL_HANDLE;
  // Skin sets come from fixed-size pools, a new one when the last is full.
  std::vector<VkDescriptorPool> skin_pools_;
  std::vector<Matrix4> joint_scratch_;
  // This frame's transparent draws, as indices into `draws.draws`, kept so
  // a steady frame sorts without allocating.
  std::vector<std::size_t> transparent_scratch_;
  std::unordered_map<MeshId, Entry> entries_;
  std::uint64_t generation_ = 0;
  std::uint64_t topology_uploads_ = 0;
  std::uint64_t point_uploads_ = 0;
  std::uint64_t skin_uploads_ = 0;
  std::uint64_t pose_writes_ = 0;
};

} // namespace Toon::vulkan_internal
