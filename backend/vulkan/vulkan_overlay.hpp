// SPDX-License-Identifier: Apache-2.0
// The present session's overlay pass: an OverlayDrawList's textured,
// vertex-coloured triangles blended over the swapchain image. Private to
// backend/vulkan.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include <toon/overlay.hpp>

#include "vulkan_scene.hpp"

namespace Toon::vulkan_internal {

// Must match OverlayConstants in shaders/overlay.slang.
struct OverlayConstants {
  float scale[2];
  float translate[2];
  std::uint32_t flags;
  std::uint32_t padding[3];
};
static_assert(sizeof(OverlayConstants) == 32);

constexpr std::uint32_t kOverlayLinearize = 1U;

// Overlay textures resident at once. Dear ImGui keeps one, and two for the
// frame its font atlas grows.
constexpr std::uint32_t kOverlayTextureCapacity = 8;

// One pipeline, drawing into a single-sampled `color_format` target with no
// depth, and GPU copies of the overlay's textures, vertices and indices.
// Textures are uploaded only when their revision changes; vertices and
// indices are rewritten every frame, into host-visible buffers that grow.
class OverlayRenderer {
public:
  // `linearize` when `color_format` encodes to sRGB as it is written.
  bool Initialize(VkPhysicalDevice physical_device, VkDevice device,
      const std::vector<std::uint32_t>& vertex_words,
      const std::vector<std::uint32_t>& fragment_words, VkFormat color_format,
      bool linearize, std::string& detail);
  // Stage the textures `overlay` changed, release those it no longer has
  // and write its vertices and indices. No frame that reads them may be in
  // flight.
  bool Update(const OverlayDrawList& overlay, std::string& detail);
  // Record this frame's texture uploads; call before rendering starts.
  void RecordUploads(VkCommandBuffer command);
  // Draw `overlay` into the rendering under way, which covers `extent`;
  // returns the draws recorded.
  std::uint32_t Record(VkCommandBuffer command, const OverlayDrawList& overlay,
      VkExtent2D extent) const;
  void Destroy();

  [[nodiscard]] std::uint64_t texture_uploads() const {
    return texture_uploads_;
  }

private:
  struct Resident {
    DeviceImage image;
    VkDescriptorSet set = VK_NULL_HANDLE;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t revision = 0;
    std::uint64_t generation = 0;
  };
  struct Upload {
    VkImage image = VK_NULL_HANDLE;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    HostBuffer staging;
  };

  bool Write(HostBuffer& buffer, VkBufferUsageFlags usage, const void* data,
      VkDeviceSize size, std::string& detail);
  void Release(Resident& resident);

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  bool linearize_ = false;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  HostBuffer vertices_;
  HostBuffer indices_;
  std::unordered_map<std::uint64_t, Resident> textures_;
  // Staged, not yet recorded.
  std::vector<Upload> pending_;
  // Recorded; freed once the frame that copies from them has completed.
  std::vector<HostBuffer> recorded_;
  std::uint64_t generation_ = 0;
  std::uint64_t texture_uploads_ = 0;
};

} // namespace Toon::vulkan_internal
