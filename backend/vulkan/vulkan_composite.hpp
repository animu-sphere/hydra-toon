// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <vulkan/vulkan.h>
#include <toon/vulkan_present.hpp>

namespace Toon::vulkan_internal {
class CompositeRenderer {
public:
  bool Initialize(VkDevice device, const std::vector<std::uint32_t>& vertex,
      const std::vector<std::uint32_t>& fragment, VkFormat format, std::string& error);
  // Called only after the previous frame completes, when its target changes.
  void SetForeground(VkImageView view);
  void Record(VkCommandBuffer command, VkImageView target, VkExtent2D extent,
      const CompositeBackground& background);
  void Destroy();
private:
  VkDevice device_ = VK_NULL_HANDLE;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkDescriptorSet set_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
};
} // namespace Toon::vulkan_internal
