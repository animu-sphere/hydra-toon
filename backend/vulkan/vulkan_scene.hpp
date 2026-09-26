// SPDX-License-Identifier: Apache-2.0
// Scene drawing shared by the offscreen and presentation paths: the device
// features they need, the mesh pipeline, and GPU copies of mesh geometry.
// Private to backend/vulkan.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include <toon/extraction.hpp>

namespace Toon::vulkan_internal {

// Must match DrawConstants in shaders/mesh.slang.
struct DrawConstants {
  float clip_from_object[16];
  float color[4];
};

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

// The mesh pipeline for one colour/depth format pair. Viewport and scissor
// are dynamic, so it is created once and survives every resize.
struct ScenePipeline {
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
};

bool CreateScenePipeline(VkDevice device,
    const std::vector<std::uint32_t>& vertex_words,
    const std::vector<std::uint32_t>& fragment_words, VkFormat color_format,
    VkFormat depth_format, ScenePipeline& pipeline, std::string& detail);
void DestroyScenePipeline(VkDevice device, ScenePipeline& pipeline);

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

// GPU copies of mesh geometry, keyed by mesh id. Points and indices are
// re-uploaded only when their own revision changes, into the existing buffer
// when it is large enough.
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
  void Record(VkCommandBuffer command, const ScenePipeline& pipeline,
      const DrawList& draws, const Matrix4& clip_from_world) const;
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
    HostBuffer indices;
    std::uint64_t points_revision = 0;
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
