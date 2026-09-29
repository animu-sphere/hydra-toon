// SPDX-License-Identifier: Apache-2.0
#include "vulkan_overlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "vulkan_internal.hpp"

namespace Toon::vulkan_internal {
namespace {

void Barrier(VkCommandBuffer command, const VkImageMemoryBarrier2& barrier) {
  VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(command, &dependency);
}

} // namespace

bool OverlayRenderer::Initialize(VkPhysicalDevice physical_device,
    VkDevice device, const std::vector<std::uint32_t>& vertex_words,
    const std::vector<std::uint32_t>& fragment_words, VkFormat color_format,
    bool linearize, std::string& detail) {
  physical_device_ = physical_device;
  device_ = device;
  linearize_ = linearize;

  VkSamplerCreateInfo sampler_create{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_create.magFilter = VK_FILTER_LINEAR;
  sampler_create.minFilter = VK_FILTER_LINEAR;
  sampler_create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_create.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_create.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_create.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (!VulkanOk(vkCreateSampler(device_, &sampler_create, nullptr, &sampler_),
          "vkCreateSampler(overlay)", detail)) {
    return false;
  }
  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  binding.pImmutableSamplers = &sampler_;
  VkDescriptorSetLayoutCreateInfo set_layout_create{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  set_layout_create.bindingCount = 1;
  set_layout_create.pBindings = &binding;
  if (!VulkanOk(vkCreateDescriptorSetLayout(device_, &set_layout_create,
                    nullptr, &set_layout_),
          "vkCreateDescriptorSetLayout(overlay)", detail)) {
    return false;
  }
  // One set per resident texture, freed with it.
  VkDescriptorPoolSize pool_size{};
  pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_size.descriptorCount = kOverlayTextureCapacity;
  VkDescriptorPoolCreateInfo pool_create{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_create.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_create.maxSets = kOverlayTextureCapacity;
  pool_create.poolSizeCount = 1;
  pool_create.pPoolSizes = &pool_size;
  if (!VulkanOk(vkCreateDescriptorPool(device_, &pool_create, nullptr, &pool_),
          "vkCreateDescriptorPool(overlay)", detail)) {
    return false;
  }
  VkPushConstantRange push_range{};
  push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push_range.size = sizeof(OverlayConstants);
  VkPipelineLayoutCreateInfo layout_create{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_create.setLayoutCount = 1;
  layout_create.pSetLayouts = &set_layout_;
  layout_create.pushConstantRangeCount = 1;
  layout_create.pPushConstantRanges = &push_range;
  if (!VulkanOk(vkCreatePipelineLayout(device_, &layout_create, nullptr,
                    &layout_),
          "vkCreatePipelineLayout(overlay)", detail)) {
    return false;
  }

  VkShaderModule vertex_module = CreateShader(device_, vertex_words, detail);
  VkShaderModule fragment_module =
      CreateShader(device_, fragment_words, detail);
  if (vertex_module == VK_NULL_HANDLE || fragment_module == VK_NULL_HANDLE) {
    vkDestroyShaderModule(device_, vertex_module, nullptr);
    vkDestroyShaderModule(device_, fragment_module, nullptr);
    return false;
  }
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertex_module;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragment_module;
  stages[1].pName = "main";
  VkVertexInputBindingDescription vertex_binding{};
  vertex_binding.binding = 0;
  vertex_binding.stride = sizeof(OverlayVertex);
  vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attributes[3]{};
  attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(OverlayVertex, x)};
  attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(OverlayVertex, u)};
  attributes[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM,
      offsetof(OverlayVertex, color)};
  VkPipelineVertexInputStateCreateInfo vertex_input{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &vertex_binding;
  vertex_input.vertexAttributeDescriptionCount = 3;
  vertex_input.pVertexAttributeDescriptions = attributes;
  VkPipelineInputAssemblyStateCreateInfo input_assembly{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo viewport_state{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo raster{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.lineWidth = 1.0F;
  VkPipelineMultisampleStateCreateInfo multisample{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState blend_attachment{};
  blend_attachment.blendEnable = VK_TRUE;
  blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_attachment;
  const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamic_states;
  VkPipelineRenderingCreateInfo rendering{
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &color_format;
  VkGraphicsPipelineCreateInfo pipeline_create{
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipeline_create.pNext = &rendering;
  pipeline_create.stageCount = 2;
  pipeline_create.pStages = stages;
  pipeline_create.pVertexInputState = &vertex_input;
  pipeline_create.pInputAssemblyState = &input_assembly;
  pipeline_create.pViewportState = &viewport_state;
  pipeline_create.pRasterizationState = &raster;
  pipeline_create.pMultisampleState = &multisample;
  pipeline_create.pColorBlendState = &blend;
  pipeline_create.pDynamicState = &dynamic;
  pipeline_create.layout = layout_;
  const VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE,
      1, &pipeline_create, nullptr, &pipeline_);
  vkDestroyShaderModule(device_, vertex_module, nullptr);
  vkDestroyShaderModule(device_, fragment_module, nullptr);
  return VulkanOk(result, "vkCreateGraphicsPipelines(overlay)", detail);
}

bool OverlayRenderer::Update(const OverlayDrawList& overlay,
    std::string& detail) {
  // The frame that copied from these has completed.
  for (HostBuffer& staging : recorded_) {
    DestroyHostBuffer(device_, staging);
  }
  recorded_.clear();
  ++generation_;
  for (const OverlayTexture& texture : overlay.textures) {
    const std::size_t size =
        static_cast<std::size_t>(texture.width) * texture.height * 4U;
    if (texture.width == 0 || texture.height == 0 ||
        texture.pixels.size() != size) {
      detail = "overlay texture " + std::to_string(texture.id) + " holds " +
               std::to_string(texture.pixels.size()) + " bytes for " +
               std::to_string(texture.width) + "x" +
               std::to_string(texture.height) + " RGBA8 pixels";
      return false;
    }
    const auto found = textures_.find(texture.id);
    if (found == textures_.end() &&
        textures_.size() >= kOverlayTextureCapacity) {
      detail = "the overlay holds more than " +
               std::to_string(kOverlayTextureCapacity) + " textures";
      return false;
    }
    Resident& resident = textures_[texture.id];
    resident.generation = generation_;
    if (resident.set != VK_NULL_HANDLE &&
        resident.revision == texture.revision) {
      continue;
    }
    if (resident.set == VK_NULL_HANDLE) {
      VkDescriptorSetAllocateInfo allocate{
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      allocate.descriptorPool = pool_;
      allocate.descriptorSetCount = 1;
      allocate.pSetLayouts = &set_layout_;
      if (!VulkanOk(vkAllocateDescriptorSets(device_, &allocate,
                        &resident.set),
              "vkAllocateDescriptorSets(overlay)", detail)) {
        return false;
      }
    }
    if (resident.image.image == VK_NULL_HANDLE ||
        resident.width != texture.width || resident.height != texture.height) {
      DestroyDeviceImage(device_, resident.image);
      if (!CreateDeviceImage(physical_device_, device_,
              VK_FORMAT_R8G8B8A8_UNORM,
              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
              VK_IMAGE_ASPECT_COLOR_BIT, texture.width, texture.height,
              resident.image, detail)) {
        return false;
      }
      resident.width = texture.width;
      resident.height = texture.height;
      VkDescriptorImageInfo image_info{};
      image_info.imageView = resident.image.view;
      image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      write.dstSet = resident.set;
      write.dstBinding = 0;
      write.descriptorCount = 1;
      write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.pImageInfo = &image_info;
      vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
    Upload upload{resident.image.image, texture.width, texture.height, {}};
    if (!CreateHostBuffer(physical_device_, device_, size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, upload.staging, detail)) {
      DestroyHostBuffer(device_, upload.staging);
      return false;
    }
    std::memcpy(upload.staging.mapped, texture.pixels.data(), size);
    pending_.push_back(upload);
    if (!FlushIfNeeded(device_, upload.staging, detail)) {
      return false;
    }
    resident.revision = texture.revision;
    ++texture_uploads_;
  }
  for (auto texture = textures_.begin(); texture != textures_.end();) {
    if (texture->second.generation != generation_) {
      Release(texture->second);
      texture = textures_.erase(texture);
    } else {
      ++texture;
    }
  }
  return Write(vertices_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
             overlay.vertices.data(),
             overlay.vertices.size() * sizeof(OverlayVertex), detail) &&
         Write(indices_, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
             overlay.indices.data(),
             overlay.indices.size() * sizeof(std::uint16_t), detail);
}

void OverlayRenderer::RecordUploads(VkCommandBuffer command) {
  for (const Upload& upload : pending_) {
    Barrier(command, ImageBarrier(upload.image, VK_IMAGE_ASPECT_COLOR_BIT,
                         VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                         VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {upload.width, upload.height, 1};
    vkCmdCopyBufferToImage(command, upload.staging.buffer, upload.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    Barrier(command, ImageBarrier(upload.image, VK_IMAGE_ASPECT_COLOR_BIT,
                         VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    recorded_.push_back(upload.staging);
  }
  pending_.clear();
}

std::uint32_t OverlayRenderer::Record(VkCommandBuffer command,
    const OverlayDrawList& overlay, VkExtent2D extent) const {
  if (overlay.commands.empty() || overlay.vertices.empty() ||
      overlay.indices.empty()) {
    return 0;
  }
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
  const VkViewport viewport{0.0F, 0.0F, static_cast<float>(extent.width),
      static_cast<float>(extent.height), 0.0F, 1.0F};
  vkCmdSetViewport(command, 0, 1, &viewport);
  const VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(command, 0, 1, &vertices_.buffer, &offset);
  vkCmdBindIndexBuffer(command, indices_.buffer, 0, VK_INDEX_TYPE_UINT16);
  // Pixels from the top left to clip space, whose y points down in Vulkan.
  OverlayConstants constants{};
  constants.scale[0] = 2.0F / static_cast<float>(extent.width);
  constants.scale[1] = 2.0F / static_cast<float>(extent.height);
  constants.translate[0] = -1.0F;
  constants.translate[1] = -1.0F;
  constants.flags = linearize_ ? kOverlayLinearize : 0U;
  vkCmdPushConstants(command, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
      sizeof(constants), &constants);

  const auto width = static_cast<float>(extent.width);
  const auto height = static_cast<float>(extent.height);
  VkDescriptorSet bound = VK_NULL_HANDLE;
  std::uint32_t recorded = 0;
  for (const OverlayCommand& draw : overlay.commands) {
    const auto texture = textures_.find(draw.texture);
    if (texture == textures_.end() || draw.index_count == 0 ||
        static_cast<std::size_t>(draw.first_index) + draw.index_count >
            overlay.indices.size()) {
      continue;
    }
    const float left = std::clamp(draw.clip[0], 0.0F, width);
    const float top = std::clamp(draw.clip[1], 0.0F, height);
    const float right = std::clamp(draw.clip[2], 0.0F, width);
    const float bottom = std::clamp(draw.clip[3], 0.0F, height);
    if (right <= left || bottom <= top) {
      continue;
    }
    VkRect2D scissor{};
    scissor.offset.x = static_cast<std::int32_t>(std::floor(left));
    scissor.offset.y = static_cast<std::int32_t>(std::floor(top));
    scissor.extent.width = static_cast<std::uint32_t>(
        std::ceil(right) - static_cast<float>(scissor.offset.x));
    scissor.extent.height = static_cast<std::uint32_t>(
        std::ceil(bottom) - static_cast<float>(scissor.offset.y));
    vkCmdSetScissor(command, 0, 1, &scissor);
    if (bound != texture->second.set) {
      bound = texture->second.set;
      vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
          layout_, 0, 1, &bound, 0, nullptr);
    }
    vkCmdDrawIndexed(command, draw.index_count, 1, draw.first_index,
        static_cast<std::int32_t>(draw.vertex_offset), 0);
    ++recorded;
  }
  return recorded;
}

void OverlayRenderer::Destroy() {
  if (device_ == VK_NULL_HANDLE) {
    return;
  }
  for (auto& texture : textures_) {
    Release(texture.second);
  }
  textures_.clear();
  for (Upload& upload : pending_) {
    DestroyHostBuffer(device_, upload.staging);
  }
  pending_.clear();
  for (HostBuffer& staging : recorded_) {
    DestroyHostBuffer(device_, staging);
  }
  recorded_.clear();
  DestroyHostBuffer(device_, vertices_);
  DestroyHostBuffer(device_, indices_);
  vkDestroyPipeline(device_, pipeline_, nullptr);
  vkDestroyPipelineLayout(device_, layout_, nullptr);
  vkDestroyDescriptorPool(device_, pool_, nullptr);
  vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
  vkDestroySampler(device_, sampler_, nullptr);
  pipeline_ = VK_NULL_HANDLE;
  layout_ = VK_NULL_HANDLE;
  pool_ = VK_NULL_HANDLE;
  set_layout_ = VK_NULL_HANDLE;
  sampler_ = VK_NULL_HANDLE;
  device_ = VK_NULL_HANDLE;
}

bool OverlayRenderer::Write(HostBuffer& buffer, VkBufferUsageFlags usage,
    const void* data, VkDeviceSize size, std::string& detail) {
  if (size == 0) {
    return true;
  }
  if (buffer.capacity < size) {
    // Doubling keeps a growing overlay from reallocating every frame.
    const VkDeviceSize capacity = std::max(size, buffer.capacity * 2U);
    DestroyHostBuffer(device_, buffer);
    if (!CreateHostBuffer(physical_device_, device_, capacity, usage, buffer,
            detail)) {
      DestroyHostBuffer(device_, buffer);
      return false;
    }
  }
  std::memcpy(buffer.mapped, data, static_cast<std::size_t>(size));
  return FlushIfNeeded(device_, buffer, detail);
}

void OverlayRenderer::Release(Resident& resident) {
  DestroyDeviceImage(device_, resident.image);
  if (resident.set != VK_NULL_HANDLE) {
    vkFreeDescriptorSets(device_, pool_, 1, &resident.set);
    resident.set = VK_NULL_HANDLE;
  }
  resident.width = 0;
  resident.height = 0;
}

} // namespace Toon::vulkan_internal
