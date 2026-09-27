// SPDX-License-Identifier: Apache-2.0
// Scene drawing shared by the offscreen and presentation paths: the device
// features they need, the scene pipelines, GPU copies of mesh geometry and
// the material parameter buffer. Private to backend/vulkan.
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
};

// Must match DrawConstants in shaders/mtoon.slang: 128 bytes, the push
// constant size every Vulkan device offers.
struct MToonDrawConstants {
  float clip_from_object[16];
  // Rows of the view-space normal matrix, each padded to four floats.
  float view_normal_rows[12];
  std::uint32_t material_slot;
  std::uint32_t padding[3];
};
static_assert(sizeof(MToonDrawConstants) == 128);

// One material's slot in the parameter buffer. Must match MToonParameters
// in shaders/mtoon.slang.
struct MToonParameters {
  float base_color[4];
  // a: the alpha cutoff, negative when the material is not Mask.
  float shade_color[4];
  float emissive[4];
  // x shading shift, y shading toony, z GI equalization.
  float shading[4];
};
static_assert(sizeof(MToonParameters) == 64);

// Vulkan 1.3 features the scene path uses: dynamic rendering (no render
// pass or framebuffer to rebuild on resize), Synchronization2 and timeline
// semaphores (design policy §19).
struct SceneDeviceFeatures {
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

// Clip-from-world for a ToonView: the view's OpenGL-convention projection,
// then y flipped and z mapped from [-1, 1] to Vulkan's [0, 1].
Matrix4 VulkanClipFromWorld(const ToonView& view);

struct ScenePipeline {
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
};

// The scene pipelines for one colour/depth format pair: the unlit mesh
// pipeline and mtoon_opaque, which reads the material parameter buffer
// through `material_layout`. Viewport and scissor are dynamic, and so are
// mtoon_opaque's cull mode and front face, so each pipeline is created once
// and survives every resize and every material.
struct ScenePipelines {
  ScenePipeline mesh;
  ScenePipeline mtoon;
  VkDescriptorSetLayout material_layout = VK_NULL_HANDLE;

  static constexpr std::uint32_t kCount = 2;
};

// The SPIR-V of every scene pipeline, loaded before any device exists so a
// missing file is a failure rather than a skip.
struct SceneShaderWords {
  std::vector<std::uint32_t> mesh_vertex;
  std::vector<std::uint32_t> mesh_fragment;
  std::vector<std::uint32_t> mtoon_vertex;
  std::vector<std::uint32_t> mtoon_fragment;
};

bool LoadSceneShaders(const SceneShaders& shaders, SceneShaderWords& words,
    std::string& detail);
bool CreateScenePipelines(VkDevice device, const SceneShaderWords& words,
    VkFormat color_format, VkFormat depth_format, ScenePipelines& pipelines,
    std::string& detail);
void DestroyScenePipelines(VkDevice device, ScenePipelines& pipelines);

// A device-local 2D image with one view.
struct DeviceImage {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};

bool CreateDeviceImage(VkPhysicalDevice physical_device, VkDevice device,
    VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
    std::uint32_t width, std::uint32_t height, DeviceImage& image,
    std::string& detail);
void DestroyDeviceImage(VkDevice device, DeviceImage& image);

VkImageMemoryBarrier2 ImageBarrier(VkImage image, VkImageAspectFlags aspect,
    VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
    VkPipelineStageFlags2 destination_stage,
    VkAccessFlags2 destination_access, VkImageLayout old_layout,
    VkImageLayout new_layout);

// Begin dynamic rendering into `color` (cleared to the scene background) and
// `depth` (cleared to 1), with the viewport and scissor covering `extent`.
void BeginSceneRendering(VkCommandBuffer command, VkImageView color,
    VkImageView depth, VkExtent2D extent);

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

// One parameter slot per material, in a host-visible storage buffer that
// mtoon_opaque indexes by slot (material policy §7). A slot is rewritten
// only when its material's parameters revision changes; a new material
// takes a free slot, and the buffer grows by doubling, keeping every slot.
class MaterialCache {
public:
  struct Entry {
    std::uint32_t slot = 0;
    std::uint64_t parameters_revision = 0;
    ToonShadingModel model = ToonShadingModel::PreviewSurface;
    bool double_sided = false;
    std::uint64_t generation = 0;
  };

  bool Initialize(VkPhysicalDevice physical_device, VkDevice device,
      VkDescriptorSetLayout layout, std::string& detail);
  // Write whatever `draws.materials` changed and free the slots of
  // materials it no longer has. No frame that reads the buffer may be in
  // flight.
  bool Update(const DrawList& draws, std::string& detail);
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

// GPU copies of mesh geometry, keyed by mesh id. Points, normals and
// indices are re-uploaded only when their own revision changes, into the
// existing buffer when it is large enough.
//
// Geometry is written through host-visible memory on the render thread. That
// is the Renderer Phase 0 stand-in for the staged, off-thread upload of
// design policy §20.
class MeshCache {
public:
  void Initialize(VkPhysicalDevice physical_device, VkDevice device);
  // Upload whatever `draws` changed and release meshes it no longer draws.
  // No frame that reads these buffers may be in flight.
  bool Update(const DrawList& draws, std::string& detail);
  // Unlit draws first, then every draw whose material selected MToon.
  void Record(VkCommandBuffer command, const ScenePipelines& pipelines,
      const MaterialCache& materials, const DrawList& draws) const;
  void Destroy();

  [[nodiscard]] std::uint64_t topology_uploads() const {
    return topology_uploads_;
  }
  [[nodiscard]] std::uint64_t point_uploads() const {
    return point_uploads_;
  }

private:
  struct Entry {
    HostBuffer vertices;
    HostBuffer normals;
    HostBuffer indices;
    std::uint64_t points_revision = 0;
    std::uint64_t normals_revision = 0;
    std::uint64_t topology_revision = 0;
    std::uint32_t index_count = 0;
    std::uint64_t generation = 0;
  };

  bool Upload(HostBuffer& buffer, VkBufferUsageFlags usage, const void* data,
      VkDeviceSize size, std::string& detail);

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  std::unordered_map<MeshId, Entry> entries_;
  std::uint64_t generation_ = 0;
  std::uint64_t topology_uploads_ = 0;
  std::uint64_t point_uploads_ = 0;
};

} // namespace Toon::vulkan_internal
