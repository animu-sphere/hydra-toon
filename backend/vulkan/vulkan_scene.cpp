// SPDX-License-Identifier: Apache-2.0
#include "vulkan_scene.hpp"

#include <cstring>
#include <limits>
#include <utility>

#include "vulkan_internal.hpp"

namespace Toon::vulkan_internal {

SceneDeviceFeatures::SceneDeviceFeatures() {
  vulkan12.pNext = &vulkan13;
}

void* SceneDeviceFeatures::EnableRequired() {
  vulkan12.timelineSemaphore = VK_TRUE;
  vulkan13.dynamicRendering = VK_TRUE;
  vulkan13.synchronization2 = VK_TRUE;
  return &vulkan12;
}

bool SupportsSceneFeatures(VkPhysicalDevice device, std::string& detail) {
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(device, &properties);
  if (properties.apiVersion < VK_API_VERSION_1_3) {
    detail = std::string(properties.deviceName) + " is not a Vulkan 1.3 device";
    return false;
  }
  SceneDeviceFeatures supported;
  VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  features.pNext = &supported.vulkan12;
  vkGetPhysicalDeviceFeatures2(device, &features);
  if (supported.vulkan12.timelineSemaphore != VK_TRUE ||
      supported.vulkan13.dynamicRendering != VK_TRUE ||
      supported.vulkan13.synchronization2 != VK_TRUE) {
    detail = std::string(properties.deviceName) +
             " lacks timelineSemaphore, dynamicRendering or synchronization2";
    return false;
  }
  return true;
}

Matrix4 VulkanClipFromWorld(const ToonView& view) {
  Matrix4 vulkan_clip;
  vulkan_clip.m[5] = -1.0F;
  vulkan_clip.m[10] = 0.5F;
  vulkan_clip.m[14] = 0.5F;
  return Multiply(vulkan_clip, Multiply(view.projection, view.view));
}

namespace {

// What distinguishes the scene pipelines; everything else they share.
struct PipelineDescription {
  const std::vector<std::uint32_t>* vertex_words = nullptr;
  const std::vector<std::uint32_t>* fragment_words = nullptr;
  std::uint32_t push_constant_size = 0;
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  // Position, then normal when there are two.
  std::uint32_t vertex_streams = 1;
  // Cull mode and front face set per draw rather than baked in.
  bool dynamic_culling = false;
};

bool CreateScenePipeline(VkDevice device,
    const PipelineDescription& description, VkFormat color_format,
    VkFormat depth_format, ScenePipeline& pipeline, std::string& detail) {
  VkPushConstantRange push_range{};
  push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  push_range.size = description.push_constant_size;
  VkPipelineLayoutCreateInfo layout_create{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  if (description.set_layout != VK_NULL_HANDLE) {
    layout_create.setLayoutCount = 1;
    layout_create.pSetLayouts = &description.set_layout;
  }
  layout_create.pushConstantRangeCount = 1;
  layout_create.pPushConstantRanges = &push_range;
  if (!VulkanOk(vkCreatePipelineLayout(device, &layout_create, nullptr,
                    &pipeline.layout),
          "vkCreatePipelineLayout", detail)) {
    return false;
  }

  VkShaderModule vertex_module =
      CreateShader(device, *description.vertex_words, detail);
  VkShaderModule fragment_module =
      CreateShader(device, *description.fragment_words, detail);
  if (vertex_module == VK_NULL_HANDLE || fragment_module == VK_NULL_HANDLE) {
    vkDestroyShaderModule(device, vertex_module, nullptr);
    vkDestroyShaderModule(device, fragment_module, nullptr);
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
  VkVertexInputBindingDescription bindings[2]{};
  VkVertexInputAttributeDescription attributes[2]{};
  for (std::uint32_t stream = 0; stream < description.vertex_streams;
       ++stream) {
    bindings[stream].binding = stream;
    bindings[stream].stride = sizeof(Float3);
    bindings[stream].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    attributes[stream].location = stream;
    attributes[stream].binding = stream;
    attributes[stream].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[stream].offset = 0;
  }
  VkPipelineVertexInputStateCreateInfo vertex_input{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertex_input.vertexBindingDescriptionCount = description.vertex_streams;
  vertex_input.pVertexBindingDescriptions = bindings;
  vertex_input.vertexAttributeDescriptionCount = description.vertex_streams;
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
  // The Vulkan clip transform flips y, so a triangle counter-clockwise on
  // screen under OpenGL's convention stays counter-clockwise here.
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0F;
  VkPipelineMultisampleStateCreateInfo multisample{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo depth_state{
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth_state.depthTestEnable = VK_TRUE;
  depth_state.depthWriteEnable = VK_TRUE;
  depth_state.depthCompareOp = VK_COMPARE_OP_LESS;
  VkPipelineColorBlendAttachmentState blend_attachment{};
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_attachment;
  // Cull mode and front face are core dynamic state in Vulkan 1.3.
  const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_CULL_MODE,
      VK_DYNAMIC_STATE_FRONT_FACE};
  VkPipelineDynamicStateCreateInfo dynamic{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = description.dynamic_culling ? 4U : 2U;
  dynamic.pDynamicStates = dynamic_states;
  VkPipelineRenderingCreateInfo rendering{
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &color_format;
  rendering.depthAttachmentFormat = depth_format;
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
  pipeline_create.pDepthStencilState = &depth_state;
  pipeline_create.pColorBlendState = &blend;
  pipeline_create.pDynamicState = &dynamic;
  pipeline_create.layout = pipeline.layout;
  const VkResult result = vkCreateGraphicsPipelines(
      device, VK_NULL_HANDLE, 1, &pipeline_create, nullptr, &pipeline.pipeline);
  vkDestroyShaderModule(device, vertex_module, nullptr);
  vkDestroyShaderModule(device, fragment_module, nullptr);
  return VulkanOk(result, "vkCreateGraphicsPipelines", detail);
}

void DestroyScenePipeline(VkDevice device, ScenePipeline& pipeline) {
  vkDestroyPipeline(device, pipeline.pipeline, nullptr);
  vkDestroyPipelineLayout(device, pipeline.layout, nullptr);
  pipeline = {};
}

Float3 Cross(const Float3& a, const Float3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Writes the rows of the inverse transpose of `matrix`'s upper 3x3, each
// padded to four floats, and returns whether the 3x3 mirrors, which turns a
// counter-clockwise triangle clockwise. The inverse transpose's columns are
// the cross products of the matrix's columns over its determinant.
bool NormalRows(const Matrix4& matrix, float rows[12]) {
  const Float3 a0{matrix.m[0], matrix.m[1], matrix.m[2]};
  const Float3 a1{matrix.m[4], matrix.m[5], matrix.m[6]};
  const Float3 a2{matrix.m[8], matrix.m[9], matrix.m[10]};
  const Float3 c0 = Cross(a1, a2);
  const Float3 c1 = Cross(a2, a0);
  const Float3 c2 = Cross(a0, a1);
  const float determinant = a0.x * c0.x + a0.y * c0.y + a0.z * c0.z;
  const float scale = determinant == 0.0F ? 1.0F : 1.0F / determinant;
  const Float3 columns[3] = {c0, c1, c2};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      const Float3& source = columns[column];
      const float value = row == 0 ? source.x : row == 1 ? source.y : source.z;
      rows[row * 4 + column] = value * scale;
    }
    rows[row * 4 + 3] = 0.0F;
  }
  return determinant < 0.0F;
}

void WriteParameters(const ToonMaterial& material, MToonParameters& slot) {
  slot = {};
  slot.base_color[0] = material.base_color.x;
  slot.base_color[1] = material.base_color.y;
  slot.base_color[2] = material.base_color.z;
  slot.base_color[3] = material.alpha;
  slot.shade_color[0] = material.mtoon.shade_color.x;
  slot.shade_color[1] = material.mtoon.shade_color.y;
  slot.shade_color[2] = material.mtoon.shade_color.z;
  slot.shade_color[3] = material.alpha_mode == ToonAlphaMode::Mask
                            ? material.alpha_cutoff
                            : -1.0F;
  slot.emissive[0] = material.emissive.x;
  slot.emissive[1] = material.emissive.y;
  slot.emissive[2] = material.emissive.z;
  slot.shading[0] = material.mtoon.shading_shift;
  slot.shading[1] = material.mtoon.shading_toony;
  slot.shading[2] = material.mtoon.gi_equalization;
}

} // namespace

bool LoadSceneShaders(const SceneShaders& shaders, SceneShaderWords& words,
    std::string& detail) {
  return LoadSpirv(shaders.mesh_vertex, words.mesh_vertex, detail) &&
         LoadSpirv(shaders.mesh_fragment, words.mesh_fragment, detail) &&
         LoadSpirv(shaders.mtoon_vertex, words.mtoon_vertex, detail) &&
         LoadSpirv(shaders.mtoon_fragment, words.mtoon_fragment, detail);
}

bool CreateScenePipelines(VkDevice device, const SceneShaderWords& words,
    VkFormat color_format, VkFormat depth_format, ScenePipelines& pipelines,
    std::string& detail) {
  VkDescriptorSetLayoutBinding material_binding{};
  material_binding.binding = 0;
  material_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  material_binding.descriptorCount = 1;
  material_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo set_layout_create{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  set_layout_create.bindingCount = 1;
  set_layout_create.pBindings = &material_binding;
  if (!VulkanOk(vkCreateDescriptorSetLayout(device, &set_layout_create,
                    nullptr, &pipelines.material_layout),
          "vkCreateDescriptorSetLayout", detail)) {
    return false;
  }

  PipelineDescription mesh;
  mesh.vertex_words = &words.mesh_vertex;
  mesh.fragment_words = &words.mesh_fragment;
  mesh.push_constant_size = sizeof(DrawConstants);
  PipelineDescription mtoon;
  mtoon.vertex_words = &words.mtoon_vertex;
  mtoon.fragment_words = &words.mtoon_fragment;
  mtoon.push_constant_size = sizeof(MToonDrawConstants);
  mtoon.set_layout = pipelines.material_layout;
  mtoon.vertex_streams = 2;
  mtoon.dynamic_culling = true;
  return CreateScenePipeline(device, mesh, color_format, depth_format,
             pipelines.mesh, detail) &&
         CreateScenePipeline(device, mtoon, color_format, depth_format,
             pipelines.mtoon, detail);
}

void DestroyScenePipelines(VkDevice device, ScenePipelines& pipelines) {
  DestroyScenePipeline(device, pipelines.mtoon);
  DestroyScenePipeline(device, pipelines.mesh);
  vkDestroyDescriptorSetLayout(device, pipelines.material_layout, nullptr);
  pipelines = {};
}

bool CreateDeviceImage(VkPhysicalDevice physical_device, VkDevice device,
    VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
    std::uint32_t width, std::uint32_t height, DeviceImage& image,
    std::string& detail) {
  VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  create.imageType = VK_IMAGE_TYPE_2D;
  create.format = format;
  create.extent = {width, height, 1};
  create.mipLevels = 1;
  create.arrayLayers = 1;
  create.samples = VK_SAMPLE_COUNT_1_BIT;
  create.tiling = VK_IMAGE_TILING_OPTIMAL;
  create.usage = usage;
  create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!VulkanOk(vkCreateImage(device, &create, nullptr, &image.image),
          "vkCreateImage", detail)) {
    return false;
  }
  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device, image.image, &requirements);
  const std::uint32_t memory_type =
      FindMemoryType(physical_device, requirements.memoryTypeBits,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
  if (memory_type == std::numeric_limits<std::uint32_t>::max()) {
    detail = "no device-local image memory type is available";
    return false;
  }
  VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocate.allocationSize = requirements.size;
  allocate.memoryTypeIndex = memory_type;
  if (!VulkanOk(vkAllocateMemory(device, &allocate, nullptr, &image.memory),
          "vkAllocateMemory(image)", detail) ||
      !VulkanOk(vkBindImageMemory(device, image.image, image.memory, 0),
          "vkBindImageMemory", detail)) {
    return false;
  }
  VkImageViewCreateInfo view_create{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view_create.image = image.image;
  view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_create.format = format;
  view_create.subresourceRange.aspectMask = aspect;
  view_create.subresourceRange.levelCount = 1;
  view_create.subresourceRange.layerCount = 1;
  return VulkanOk(vkCreateImageView(device, &view_create, nullptr, &image.view),
      "vkCreateImageView", detail);
}

void DestroyDeviceImage(VkDevice device, DeviceImage& image) {
  vkDestroyImageView(device, image.view, nullptr);
  vkDestroyImage(device, image.image, nullptr);
  vkFreeMemory(device, image.memory, nullptr);
  image = {};
}

VkImageMemoryBarrier2 ImageBarrier(VkImage image, VkImageAspectFlags aspect,
    VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
    VkPipelineStageFlags2 destination_stage,
    VkAccessFlags2 destination_access, VkImageLayout old_layout,
    VkImageLayout new_layout) {
  VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  barrier.srcStageMask = source_stage;
  barrier.srcAccessMask = source_access;
  barrier.dstStageMask = destination_stage;
  barrier.dstAccessMask = destination_access;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = aspect;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;
  return barrier;
}

void BeginSceneRendering(VkCommandBuffer command, VkImageView color,
    VkImageView depth, VkExtent2D extent) {
  VkRenderingAttachmentInfo color_attachment{
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color_attachment.imageView = color;
  color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.clearValue.color = {{0.05F, 0.10F, 0.15F, 1.0F}};
  VkRenderingAttachmentInfo depth_attachment{
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depth_attachment.imageView = depth;
  depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth_attachment.clearValue.depthStencil = {1.0F, 0};
  VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
  rendering.renderArea.extent = extent;
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color_attachment;
  rendering.pDepthAttachment = &depth_attachment;
  vkCmdBeginRendering(command, &rendering);
  const VkViewport viewport{0.0F, 0.0F, static_cast<float>(extent.width),
      static_cast<float>(extent.height), 0.0F, 1.0F};
  const VkRect2D scissor{{0, 0}, extent};
  vkCmdSetViewport(command, 0, 1, &viewport);
  vkCmdSetScissor(command, 0, 1, &scissor);
}

bool CreateHostBuffer(VkPhysicalDevice physical_device, VkDevice device,
    VkDeviceSize size, VkBufferUsageFlags usage, HostBuffer& buffer,
    std::string& detail) {
  VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  create.size = size;
  create.usage = usage;
  create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (!VulkanOk(vkCreateBuffer(device, &create, nullptr, &buffer.buffer),
          "vkCreateBuffer", detail)) {
    return false;
  }
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device, buffer.buffer, &requirements);
  const std::uint32_t memory_type = FindMemoryType(physical_device,
      requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.coherent);
  if (memory_type == std::numeric_limits<std::uint32_t>::max()) {
    detail = "no host-visible buffer memory type is available";
    return false;
  }
  VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocate.allocationSize = requirements.size;
  allocate.memoryTypeIndex = memory_type;
  if (!VulkanOk(vkAllocateMemory(device, &allocate, nullptr, &buffer.memory),
          "vkAllocateMemory(buffer)", detail) ||
      !VulkanOk(vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0),
          "vkBindBufferMemory", detail) ||
      !VulkanOk(vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0,
                    &buffer.mapped),
          "vkMapMemory", detail)) {
    return false;
  }
  buffer.capacity = size;
  return true;
}

void DestroyHostBuffer(VkDevice device, HostBuffer& buffer) {
  if (buffer.mapped != nullptr) {
    vkUnmapMemory(device, buffer.memory);
  }
  vkDestroyBuffer(device, buffer.buffer, nullptr);
  vkFreeMemory(device, buffer.memory, nullptr);
  buffer = {};
}

bool FlushIfNeeded(VkDevice device, const HostBuffer& buffer,
    std::string& detail) {
  if (buffer.coherent) {
    return true;
  }
  VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
  range.memory = buffer.memory;
  range.size = VK_WHOLE_SIZE;
  return VulkanOk(vkFlushMappedMemoryRanges(device, 1, &range),
      "vkFlushMappedMemoryRanges", detail);
}

bool InvalidateIfNeeded(VkDevice device, const HostBuffer& buffer,
    std::string& detail) {
  if (buffer.coherent) {
    return true;
  }
  VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
  range.memory = buffer.memory;
  range.size = VK_WHOLE_SIZE;
  return VulkanOk(vkInvalidateMappedMemoryRanges(device, 1, &range),
      "vkInvalidateMappedMemoryRanges", detail);
}

bool MaterialCache::Initialize(VkPhysicalDevice physical_device,
    VkDevice device, VkDescriptorSetLayout layout, std::string& detail) {
  physical_device_ = physical_device;
  device_ = device;
  VkDescriptorPoolSize pool_size{};
  pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  pool_size.descriptorCount = 1;
  VkDescriptorPoolCreateInfo pool_create{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_create.maxSets = 1;
  pool_create.poolSizeCount = 1;
  pool_create.pPoolSizes = &pool_size;
  if (!VulkanOk(vkCreateDescriptorPool(device_, &pool_create, nullptr, &pool_),
          "vkCreateDescriptorPool", detail)) {
    return false;
  }
  VkDescriptorSetAllocateInfo allocate{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate.descriptorPool = pool_;
  allocate.descriptorSetCount = 1;
  allocate.pSetLayouts = &layout;
  if (!VulkanOk(vkAllocateDescriptorSets(device_, &allocate, &set_),
          "vkAllocateDescriptorSets", detail)) {
    return false;
  }
  // The set is valid before any material exists.
  return Reserve(64, detail);
}

bool MaterialCache::Update(const DrawList& draws, std::string& detail) {
  ++generation_;
  bool written = false;
  for (const MaterialSnapshot& material : draws.materials) {
    auto found = entries_.find(material.id);
    if (found == entries_.end()) {
      std::uint32_t slot = 0;
      if (!free_slots_.empty()) {
        slot = free_slots_.back();
        free_slots_.pop_back();
      } else {
        if (next_slot_ == capacity_ && !Reserve(capacity_ * 2U, detail)) {
          return false;
        }
        slot = next_slot_++;
      }
      found = entries_.emplace(material.id, Entry{slot}).first;
    }
    Entry& entry = found->second;
    if (entry.parameters_revision != material.parameters_revision) {
      auto* slots = static_cast<MToonParameters*>(buffer_.mapped);
      WriteParameters(material.material, slots[entry.slot]);
      entry.parameters_revision = material.parameters_revision;
      entry.model = material.material.model;
      entry.double_sided = material.material.double_sided;
      ++writes_;
      written = true;
    }
    entry.generation = generation_;
  }
  for (auto entry = entries_.begin(); entry != entries_.end();) {
    if (entry->second.generation != generation_) {
      free_slots_.push_back(entry->second.slot);
      entry = entries_.erase(entry);
    } else {
      ++entry;
    }
  }
  return !written || FlushIfNeeded(device_, buffer_, detail);
}

const MaterialCache::Entry* MaterialCache::Find(MaterialId material) const {
  const auto found = entries_.find(material);
  return found == entries_.end() ? nullptr : &found->second;
}

void MaterialCache::Destroy() {
  DestroyHostBuffer(device_, buffer_);
  // Frees the set with it.
  vkDestroyDescriptorPool(device_, pool_, nullptr);
  pool_ = VK_NULL_HANDLE;
  set_ = VK_NULL_HANDLE;
  entries_.clear();
  free_slots_.clear();
  capacity_ = 0;
  next_slot_ = 0;
}

bool MaterialCache::Reserve(std::uint32_t slots, std::string& detail) {
  HostBuffer grown;
  if (!CreateHostBuffer(physical_device_, device_,
          static_cast<VkDeviceSize>(slots) * sizeof(MToonParameters),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, grown, detail)) {
    DestroyHostBuffer(device_, grown);
    return false;
  }
  // Every slot moves with its contents, so growing rewrites no material.
  if (buffer_.mapped != nullptr) {
    std::memcpy(grown.mapped, buffer_.mapped,
        static_cast<std::size_t>(capacity_) * sizeof(MToonParameters));
  }
  DestroyHostBuffer(device_, buffer_);
  buffer_ = grown;
  capacity_ = slots;
  VkDescriptorBufferInfo buffer_info{};
  buffer_info.buffer = buffer_.buffer;
  buffer_info.range = VK_WHOLE_SIZE;
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = set_;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  write.pBufferInfo = &buffer_info;
  vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
  return FlushIfNeeded(device_, buffer_, detail);
}

void MeshCache::Initialize(VkPhysicalDevice physical_device, VkDevice device) {
  physical_device_ = physical_device;
  device_ = device;
}

bool MeshCache::Update(const DrawList& draws, std::string& detail) {
  ++generation_;
  for (const MeshSnapshot& mesh : draws.draws) {
    Entry& entry = entries_[mesh.id];
    if (entry.topology_revision != mesh.topology_revision) {
      if (!Upload(entry.indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
              mesh.indices->data(),
              mesh.indices->size() * sizeof(std::uint32_t), detail)) {
        return false;
      }
      entry.topology_revision = mesh.topology_revision;
      entry.index_count = static_cast<std::uint32_t>(mesh.indices->size());
      ++topology_uploads_;
    }
    if (entry.points_revision != mesh.points_revision) {
      if (!Upload(entry.vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
              mesh.points->data(), mesh.points->size() * sizeof(Float3),
              detail)) {
        return false;
      }
      entry.points_revision = mesh.points_revision;
      ++point_uploads_;
    }
    // Normals follow the points, so they are not counted apart from them.
    if (entry.normals_revision != mesh.normals_revision) {
      if (!Upload(entry.normals, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
              mesh.normals->data(), mesh.normals->size() * sizeof(Float3),
              detail)) {
        return false;
      }
      entry.normals_revision = mesh.normals_revision;
    }
    entry.generation = generation_;
  }
  for (auto entry = entries_.begin(); entry != entries_.end();) {
    if (entry->second.generation != generation_) {
      DestroyHostBuffer(device_, entry->second.vertices);
      DestroyHostBuffer(device_, entry->second.normals);
      DestroyHostBuffer(device_, entry->second.indices);
      entry = entries_.erase(entry);
    } else {
      ++entry;
    }
  }
  return true;
}

void MeshCache::Record(VkCommandBuffer command,
    const ScenePipelines& pipelines, const MaterialCache& materials,
    const DrawList& draws) const {
  const Matrix4 clip_from_world = VulkanClipFromWorld(draws.view);
  const auto mtoon_material =
      [&materials](const MeshSnapshot& mesh) -> const MaterialCache::Entry* {
    const MaterialCache::Entry* material = materials.Find(mesh.material);
    return material != nullptr && material->model == ToonShadingModel::MToon
               ? material
               : nullptr;
  };

  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      pipelines.mesh.pipeline);
  for (const MeshSnapshot& mesh : draws.draws) {
    const auto found = entries_.find(mesh.id);
    if (found == entries_.end() || mtoon_material(mesh) != nullptr) {
      continue;
    }
    const Entry& entry = found->second;
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &entry.vertices.buffer, &offset);
    vkCmdBindIndexBuffer(command, entry.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    DrawConstants constants{};
    const Matrix4 clip_from_object = Multiply(clip_from_world, mesh.transform);
    std::memcpy(constants.clip_from_object, clip_from_object.m.data(),
        sizeof(constants.clip_from_object));
    constants.color[0] = mesh.color.x;
    constants.color[1] = mesh.color.y;
    constants.color[2] = mesh.color.z;
    constants.color[3] = 1.0F;
    vkCmdPushConstants(command, pipelines.mesh.layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        sizeof(constants), &constants);
    vkCmdDrawIndexed(command, entry.index_count, 1, 0, 0, 0);
  }

  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      pipelines.mtoon.pipeline);
  const VkDescriptorSet material_set = materials.descriptor_set();
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      pipelines.mtoon.layout, 0, 1, &material_set, 0, nullptr);
  for (const MeshSnapshot& mesh : draws.draws) {
    const auto found = entries_.find(mesh.id);
    const MaterialCache::Entry* material = mtoon_material(mesh);
    if (found == entries_.end() || material == nullptr) {
      continue;
    }
    const Entry& entry = found->second;
    const VkBuffer streams[] = {entry.vertices.buffer, entry.normals.buffer};
    const VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(command, 0, 2, streams, offsets);
    vkCmdBindIndexBuffer(command, entry.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    MToonDrawConstants constants{};
    const Matrix4 clip_from_object = Multiply(clip_from_world, mesh.transform);
    std::memcpy(constants.clip_from_object, clip_from_object.m.data(),
        sizeof(constants.clip_from_object));
    const bool mirrored = NormalRows(Multiply(draws.view.view, mesh.transform),
        constants.view_normal_rows);
    constants.material_slot = material->slot;
    vkCmdSetCullMode(command, material->double_sided ? VK_CULL_MODE_NONE
                                                     : VK_CULL_MODE_BACK_BIT);
    vkCmdSetFrontFace(command, mirrored ? VK_FRONT_FACE_CLOCKWISE
                                        : VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdPushConstants(command, pipelines.mtoon.layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        sizeof(constants), &constants);
    vkCmdDrawIndexed(command, entry.index_count, 1, 0, 0, 0);
  }
}

void MeshCache::Destroy() {
  for (auto& entry : entries_) {
    DestroyHostBuffer(device_, entry.second.vertices);
    DestroyHostBuffer(device_, entry.second.normals);
    DestroyHostBuffer(device_, entry.second.indices);
  }
  entries_.clear();
}

bool MeshCache::Upload(HostBuffer& buffer, VkBufferUsageFlags usage,
    const void* data, VkDeviceSize size, std::string& detail) {
  if (buffer.capacity < size) {
    DestroyHostBuffer(device_, buffer);
    if (!CreateHostBuffer(physical_device_, device_, size, usage, buffer,
            detail)) {
      DestroyHostBuffer(device_, buffer);
      return false;
    }
  }
  std::memcpy(buffer.mapped, data, static_cast<std::size_t>(size));
  return FlushIfNeeded(device_, buffer, detail);
}

} // namespace Toon::vulkan_internal
