// SPDX-License-Identifier: Apache-2.0
#include "vulkan_composite.hpp"
#include "vulkan_internal.hpp"

namespace Toon::vulkan_internal {
bool CompositeRenderer::Initialize(VkDevice device,
    const std::vector<std::uint32_t>& vertex,
    const std::vector<std::uint32_t>& fragment, VkFormat format, std::string& error) {
  device_ = device;
  VkSamplerCreateInfo sampler_create{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_create.magFilter = VK_FILTER_NEAREST;
  sampler_create.minFilter = VK_FILTER_NEAREST;
  sampler_create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_create.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_create.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_create.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (!VulkanOk(vkCreateSampler(device_, &sampler_create, nullptr, &sampler_),
          "vkCreateSampler(composite)", error)) {
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
          "vkCreateDescriptorSetLayout(composite)", error)) {
    return false;
  }
  // One set for the current foreground, retained across resizes.
  VkDescriptorPoolSize pool_size{};
  pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_size.descriptorCount = 1;
  VkDescriptorPoolCreateInfo pool_create{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_create.maxSets = 1;
  pool_create.poolSizeCount = 1;
  pool_create.pPoolSizes = &pool_size;
  if (!VulkanOk(vkCreateDescriptorPool(device_, &pool_create, nullptr, &pool_),
          "vkCreateDescriptorPool(composite)", error)) {
    return false;
  }
  VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate.descriptorPool = pool_;
  allocate.descriptorSetCount = 1;
  allocate.pSetLayouts = &set_layout_;
  if (!VulkanOk(vkAllocateDescriptorSets(device_, &allocate, &set_),
          "vkAllocateDescriptorSets(composite)", error)) return false;
  VkPushConstantRange push_range{};
  push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  push_range.size = sizeof(float) * 8;
  VkPipelineLayoutCreateInfo layout_create{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_create.setLayoutCount = 1;
  layout_create.pSetLayouts = &set_layout_;
  layout_create.pushConstantRangeCount = 1;
  layout_create.pPushConstantRanges = &push_range;
  if (!VulkanOk(vkCreatePipelineLayout(device_, &layout_create, nullptr,
                    &layout_),
          "vkCreatePipelineLayout(composite)", error)) {
    return false;
  }

  VkShaderModule vertex_module = CreateShader(device_, vertex, error);
  VkShaderModule fragment_module =
      CreateShader(device_, fragment, error);
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
  VkPipelineVertexInputStateCreateInfo vertex_input{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
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
  rendering.pColorAttachmentFormats = &format;
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
  return VulkanOk(result, "vkCreateGraphicsPipelines(composite)", error);
}

void CompositeRenderer::SetForeground(VkImageView view) {
  VkDescriptorImageInfo image{};
  image.imageView = view;
  image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = set_;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &image;
  vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void CompositeRenderer::Record(VkCommandBuffer command, VkImageView target,
    VkExtent2D extent, const CompositeBackground& background) {
  VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  attachment.imageView = target;
  attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
  rendering.renderArea.extent = extent;
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &attachment;
  vkCmdBeginRendering(command, &rendering);
  const VkViewport viewport{0, 0, static_cast<float>(extent.width),
      static_cast<float>(extent.height), 0, 1};
  const VkRect2D scissor{{0, 0}, extent};
  vkCmdSetViewport(command, 0, 1, &viewport);
  vkCmdSetScissor(command, 0, 1, &scissor);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0,
      1, &set_, 0, nullptr);
  const float constants[8] = {background.color.x, background.color.y, background.color.z,
      static_cast<float>(background.checker_size), background.alternate.x,
      background.alternate.y, background.alternate.z, background.checkerboard ? 1.0F : 0.0F};
  vkCmdPushConstants(command, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
      sizeof(constants), constants);
  vkCmdDraw(command, 3, 1, 0, 0);
  vkCmdEndRendering(command);
}

void CompositeRenderer::Destroy() {
  if (device_ == VK_NULL_HANDLE) return;
  vkDestroyPipeline(device_, pipeline_, nullptr);
  vkDestroyPipelineLayout(device_, layout_, nullptr);
  vkDestroyDescriptorPool(device_, pool_, nullptr);
  vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
  vkDestroySampler(device_, sampler_, nullptr);
  device_ = VK_NULL_HANDLE;
}
} // namespace Toon::vulkan_internal
