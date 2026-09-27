// SPDX-License-Identifier: Apache-2.0
#include "vulkan_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "vulkan_internal.hpp"

namespace Toon::vulkan_internal {

SceneDeviceFeatures::SceneDeviceFeatures() {
  core.pNext = &vulkan12;
  vulkan12.pNext = &vulkan13;
}

void* SceneDeviceFeatures::EnableRequired() {
  core.features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
  vulkan12.timelineSemaphore = VK_TRUE;
  vulkan13.dynamicRendering = VK_TRUE;
  vulkan13.synchronization2 = VK_TRUE;
  return &core;
}

bool SupportsSceneFeatures(VkPhysicalDevice device, std::string& detail) {
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(device, &properties);
  if (properties.apiVersion < VK_API_VERSION_1_3) {
    detail = std::string(properties.deviceName) + " is not a Vulkan 1.3 device";
    return false;
  }
  SceneDeviceFeatures supported;
  vkGetPhysicalDeviceFeatures2(device, &supported.core);
  if (supported.vulkan12.timelineSemaphore != VK_TRUE ||
      supported.vulkan13.dynamicRendering != VK_TRUE ||
      supported.vulkan13.synchronization2 != VK_TRUE ||
      supported.core.features.shaderSampledImageArrayDynamicIndexing !=
          VK_TRUE) {
    detail = std::string(properties.deviceName) +
             " lacks timelineSemaphore, dynamicRendering, synchronization2 or "
             "shaderSampledImageArrayDynamicIndexing";
    return false;
  }
  // The material set: its parameter buffer, the texture table and the
  // samplers, each stage that reads them counting them once.
  const VkPhysicalDeviceLimits& limits = properties.limits;
  if (limits.maxPerStageDescriptorSampledImages < kTextureCapacity ||
      limits.maxDescriptorSetSampledImages < kTextureCapacity ||
      limits.maxPerStageDescriptorSamplers < kSamplerCount ||
      limits.maxPerStageResources < kTextureCapacity + kSamplerCount + 2U) {
    detail = std::string(properties.deviceName) + " cannot bind a table of " +
             std::to_string(kTextureCapacity) + " textures";
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
  // Position, then normal and UV when there are three.
  std::uint32_t vertex_streams = 1;
  // Cull mode, front face, depth writes and vertex strides set per draw
  // rather than baked in.
  bool dynamic_draw_state = false;
  // Source over: the fragment's alpha blends it over the target, so one
  // that returns 1 draws as an opaque one would.
  bool blend = false;
};

// Every scene pipeline shares the set layouts, material then skin, so a
// skin set binds at set 1 whichever pipeline draws.
bool CreateScenePipeline(VkDevice device,
    const PipelineDescription& description, const ScenePipelines& pipelines,
    VkFormat color_format, VkFormat depth_format, ScenePipeline& pipeline,
    std::string& detail) {
  VkPushConstantRange push_range{};
  push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  push_range.size = description.push_constant_size;
  const VkDescriptorSetLayout set_layouts[] = {pipelines.material_layout,
      pipelines.skin_layout};
  VkPipelineLayoutCreateInfo layout_create{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_create.setLayoutCount = 2;
  layout_create.pSetLayouts = set_layouts;
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
  const VkFormat stream_formats[3] = {VK_FORMAT_R32G32B32_SFLOAT,
      VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32_SFLOAT};
  const std::uint32_t stream_strides[3] = {sizeof(Float3), sizeof(Float3),
      sizeof(Float2)};
  VkVertexInputBindingDescription bindings[3]{};
  VkVertexInputAttributeDescription attributes[3]{};
  for (std::uint32_t stream = 0; stream < description.vertex_streams;
       ++stream) {
    bindings[stream].binding = stream;
    bindings[stream].stride = stream_strides[stream];
    bindings[stream].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    attributes[stream].location = stream;
    attributes[stream].binding = stream;
    attributes[stream].format = stream_formats[stream];
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
  if (description.blend) {
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
  }
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_attachment;
  // Cull mode, front face, depth writes and vertex strides are core dynamic
  // state in Vulkan 1.3; a stride of 0 stands in for a mesh's missing UVs.
  const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_CULL_MODE,
      VK_DYNAMIC_STATE_FRONT_FACE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
      VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE};
  VkPipelineDynamicStateCreateInfo dynamic{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = description.dynamic_draw_state ? 6U : 2U;
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

// Writes the top three rows of `matrix`, its 3x3 in xyz and translation in
// w, and returns whether the 3x3 mirrors, which turns a counter-clockwise
// triangle clockwise: whether its determinant is negative.
bool ViewRows(const Matrix4& matrix, float rows[12]) {
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 4; ++column) {
      rows[row * 4 + column] = matrix.m[column * 4 + row];
    }
  }
  const float* m = matrix.m.data();
  const float determinant = m[0] * (m[5] * m[10] - m[9] * m[6]) -
                            m[4] * (m[1] * m[10] - m[9] * m[2]) +
                            m[8] * (m[1] * m[6] - m[5] * m[2]);
  return determinant < 0.0F;
}

// KHR_texture_transform's translate * rotate * scale, as the two rows of a
// 2x3 affine map in glTF's UV space, each padded to four floats.
void WriteUvRows(const ToonTextureRef& texture, float rows[8]) {
  const float c = std::cos(texture.rotation);
  const float s = std::sin(texture.rotation);
  const float values[8] = {texture.scale.x * c, texture.scale.y * s,
      texture.offset.x, 0.0F, -texture.scale.x * s, texture.scale.y * c,
      texture.offset.y, 0.0F};
  std::copy(values, values + 8, rows);
}

std::uint32_t SamplerIndex(const ToonTextureRef& texture) {
  return static_cast<std::uint32_t>(texture.wrap_s) * 3U +
         static_cast<std::uint32_t>(texture.wrap_t);
}

VkSamplerAddressMode AddressMode(std::uint32_t wrap) {
  switch (static_cast<ToonWrap>(wrap)) {
  case ToonWrap::ClampToEdge:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  case ToonWrap::MirroredRepeat:
    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  case ToonWrap::Repeat:
    break;
  }
  return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

std::uint32_t OutlineMode(const ToonMaterial& material) {
  switch (material.mtoon.outline_width_mode) {
  case ToonOutlineWidthMode::World:
    return kOutlineWorld;
  case ToonOutlineWidthMode::Screen:
    return kOutlineScreen;
  case ToonOutlineWidthMode::None:
    break;
  }
  return 0U;
}

MaterialEntries EntriesOf(const ToonMaterial& material,
    const TextureCache& textures) {
  return {textures.Entry(material.base_texture.texture),
      textures.Entry(material.mtoon.shade_texture.texture),
      textures.Entry(material.mtoon.outline_width_texture.texture),
      textures.Entry(material.mtoon.matcap_texture.texture),
      textures.Entry(material.mtoon.rim_multiply_texture.texture)};
}

void WriteParameters(const ToonMaterial& material,
    const MaterialEntries& entries, MToonParameters& slot) {
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
  slot.textures[0] = entries.base;
  slot.textures[1] = SamplerIndex(material.base_texture);
  slot.textures[2] = entries.shade;
  slot.textures[3] = SamplerIndex(material.mtoon.shade_texture);
  WriteUvRows(material.base_texture, slot.base_uv);
  WriteUvRows(material.mtoon.shade_texture, slot.shade_uv);
  slot.outline_color[0] = material.outline_color.x;
  slot.outline_color[1] = material.outline_color.y;
  slot.outline_color[2] = material.outline_color.z;
  slot.outline_color[3] = material.mtoon.outline_lighting_mix;
  slot.outline[0] = material.outline_width;
  slot.outline_texture[0] = entries.outline;
  slot.outline_texture[1] = SamplerIndex(material.mtoon.outline_width_texture);
  slot.outline_texture[2] = OutlineMode(material);
  WriteUvRows(material.mtoon.outline_width_texture, slot.outline_uv);
  slot.rim_color[0] = material.mtoon.rim_color.x;
  slot.rim_color[1] = material.mtoon.rim_color.y;
  slot.rim_color[2] = material.mtoon.rim_color.z;
  slot.rim_color[3] = material.mtoon.rim_lighting_mix;
  slot.rim[0] = material.mtoon.rim_fresnel_power;
  slot.rim[1] = material.mtoon.rim_lift;
  slot.matcap[0] = material.mtoon.matcap.x;
  slot.matcap[1] = material.mtoon.matcap.y;
  slot.matcap[2] = material.mtoon.matcap.z;
  slot.rim_textures[0] = entries.matcap;
  slot.rim_textures[1] = SamplerIndex(material.mtoon.matcap_texture);
  slot.rim_textures[2] = entries.rim;
  slot.rim_textures[3] = SamplerIndex(material.mtoon.rim_multiply_texture);
  WriteUvRows(material.mtoon.matcap_texture, slot.matcap_uv);
  WriteUvRows(material.mtoon.rim_multiply_texture, slot.rim_uv);
}

// Every level down to 1x1.
std::uint32_t MipLevels(std::uint32_t width, std::uint32_t height) {
  std::uint32_t levels = 1;
  for (std::uint32_t size = std::max(width, height); size > 1U; size /= 2U) {
    ++levels;
  }
  return levels;
}

bool CanMakeMipmaps(VkPhysicalDevice device, VkFormat format) {
  VkFormatProperties properties{};
  vkGetPhysicalDeviceFormatProperties(device, format, &properties);
  const VkFormatFeatureFlags required =
      VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  return (properties.optimalTilingFeatures & required) == required;
}

VkImageMemoryBarrier2 MipBarrier(VkImage image, std::uint32_t base_level,
    std::uint32_t level_count, VkPipelineStageFlags2 source_stage,
    VkAccessFlags2 source_access, VkPipelineStageFlags2 destination_stage,
    VkAccessFlags2 destination_access, VkImageLayout old_layout,
    VkImageLayout new_layout) {
  VkImageMemoryBarrier2 barrier = ImageBarrier(image,
      VK_IMAGE_ASPECT_COLOR_BIT, source_stage, source_access,
      destination_stage, destination_access, old_layout, new_layout);
  barrier.subresourceRange.baseMipLevel = base_level;
  barrier.subresourceRange.levelCount = level_count;
  return barrier;
}

void Barrier(VkCommandBuffer command, const VkImageMemoryBarrier2& barrier) {
  VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(command, &dependency);
}

} // namespace

bool LoadSceneShaders(const SceneShaders& shaders, SceneShaderWords& words,
    std::string& detail) {
  return LoadSpirv(shaders.mesh_vertex, words.mesh_vertex, detail) &&
         LoadSpirv(shaders.mesh_fragment, words.mesh_fragment, detail) &&
         LoadSpirv(shaders.mtoon_vertex, words.mtoon_vertex, detail) &&
         LoadSpirv(shaders.mtoon_fragment, words.mtoon_fragment, detail) &&
         LoadSpirv(shaders.mtoon_transparent_vertex,
             words.mtoon_transparent_vertex, detail) &&
         LoadSpirv(shaders.mtoon_transparent_fragment,
             words.mtoon_transparent_fragment, detail) &&
         LoadSpirv(shaders.mtoon_outline_vertex, words.mtoon_outline_vertex,
             detail) &&
         LoadSpirv(shaders.mtoon_outline_fragment,
             words.mtoon_outline_fragment, detail);
}

bool CreateScenePipelines(VkDevice device, const SceneShaderWords& words,
    VkFormat color_format, VkFormat depth_format, ScenePipelines& pipelines,
    std::string& detail) {
  // Trilinear, since glTF's filters are not on the stage; one sampler per
  // wrap pair, so a material's wrap is an index, not a descriptor.
  for (std::uint32_t index = 0; index < kSamplerCount; ++index) {
    VkSamplerCreateInfo sampler_create{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_create.magFilter = VK_FILTER_LINEAR;
    sampler_create.minFilter = VK_FILTER_LINEAR;
    sampler_create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_create.addressModeU = AddressMode(index / 3U);
    sampler_create.addressModeV = AddressMode(index % 3U);
    sampler_create.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_create.maxLod = VK_LOD_CLAMP_NONE;
    if (!VulkanOk(vkCreateSampler(device, &sampler_create, nullptr,
                      &pipelines.samplers[index]),
            "vkCreateSampler", detail)) {
      return false;
    }
  }

  // mtoon_outline's vertex stage reads the width, its mode and its texture
  // from the same slot and table the fragment stages do.
  const VkShaderStageFlags material_stages =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutBinding material_bindings[3]{};
  material_bindings[0].binding = 0;
  material_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  material_bindings[0].descriptorCount = 1;
  material_bindings[0].stageFlags = material_stages;
  material_bindings[1].binding = 1;
  material_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  material_bindings[1].descriptorCount = kTextureCapacity;
  material_bindings[1].stageFlags = material_stages;
  material_bindings[2].binding = 2;
  material_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  material_bindings[2].descriptorCount = kSamplerCount;
  material_bindings[2].stageFlags = material_stages;
  material_bindings[2].pImmutableSamplers = pipelines.samplers;
  VkDescriptorSetLayoutCreateInfo set_layout_create{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  set_layout_create.bindingCount = 3;
  set_layout_create.pBindings = material_bindings;
  if (!VulkanOk(vkCreateDescriptorSetLayout(device, &set_layout_create,
                    nullptr, &pipelines.material_layout),
          "vkCreateDescriptorSetLayout", detail)) {
    return false;
  }
  // A mesh's influences and joint buffer, read by the vertex stage alone.
  VkDescriptorSetLayoutBinding skin_bindings[2]{};
  for (std::uint32_t binding = 0; binding < 2U; ++binding) {
    skin_bindings[binding].binding = binding;
    skin_bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    skin_bindings[binding].descriptorCount = 1;
    skin_bindings[binding].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  }
  set_layout_create.bindingCount = 2;
  set_layout_create.pBindings = skin_bindings;
  if (!VulkanOk(vkCreateDescriptorSetLayout(device, &set_layout_create,
                    nullptr, &pipelines.skin_layout),
          "vkCreateDescriptorSetLayout(skin)", detail)) {
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
  mtoon.vertex_streams = 3;
  mtoon.dynamic_draw_state = true;
  // The same streams, constants and dynamic state; only the shaders and
  // blending differ. The hull blends, so a transparent material's outline
  // takes its surface's alpha while an opaque one's returns 1.
  PipelineDescription transparent = mtoon;
  transparent.vertex_words = &words.mtoon_transparent_vertex;
  transparent.fragment_words = &words.mtoon_transparent_fragment;
  transparent.blend = true;
  PipelineDescription outline = mtoon;
  outline.vertex_words = &words.mtoon_outline_vertex;
  outline.fragment_words = &words.mtoon_outline_fragment;
  outline.blend = true;
  return CreateScenePipeline(device, mesh, pipelines, color_format,
             depth_format, pipelines.mesh, detail) &&
         CreateScenePipeline(device, mtoon, pipelines, color_format,
             depth_format, pipelines.mtoon, detail) &&
         CreateScenePipeline(device, transparent, pipelines, color_format,
             depth_format, pipelines.mtoon_transparent, detail) &&
         CreateScenePipeline(device, outline, pipelines, color_format,
             depth_format, pipelines.mtoon_outline, detail);
}

void DestroyScenePipelines(VkDevice device, ScenePipelines& pipelines) {
  DestroyScenePipeline(device, pipelines.mtoon_outline);
  DestroyScenePipeline(device, pipelines.mtoon_transparent);
  DestroyScenePipeline(device, pipelines.mtoon);
  DestroyScenePipeline(device, pipelines.mesh);
  vkDestroyDescriptorSetLayout(device, pipelines.material_layout, nullptr);
  vkDestroyDescriptorSetLayout(device, pipelines.skin_layout, nullptr);
  for (VkSampler sampler : pipelines.samplers) {
    vkDestroySampler(device, sampler, nullptr);
  }
  pipelines = {};
}

bool CreateDeviceImage(VkPhysicalDevice physical_device, VkDevice device,
    VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
    std::uint32_t width, std::uint32_t height, DeviceImage& image,
    std::string& detail, std::uint32_t mip_levels) {
  VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  create.imageType = VK_IMAGE_TYPE_2D;
  create.format = format;
  create.extent = {width, height, 1};
  create.mipLevels = mip_levels;
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
  view_create.subresourceRange.levelCount = mip_levels;
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

bool TextureCache::Initialize(VkPhysicalDevice physical_device,
    VkDevice device, VkDescriptorSet set, std::string& detail) {
  physical_device_ = physical_device;
  device_ = device;
  set_ = set;
  srgb_mipmaps_ = CanMakeMipmaps(physical_device, VK_FORMAT_R8G8B8A8_SRGB);
  linear_mipmaps_ = CanMakeMipmaps(physical_device, VK_FORMAT_R8G8B8A8_UNORM);
  // White is white in either encoding. Every entry points at it until a
  // texture takes the entry, so the whole table is always valid.
  if (!CreateDeviceImage(physical_device_, device_, VK_FORMAT_R8G8B8A8_UNORM,
          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
          VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, placeholder_, detail) ||
      !Stage({255, 255, 255, 255}, placeholder_.image, 1, 1, 1, detail)) {
    return false;
  }
  for (std::uint32_t entry = 0; entry < kTextureCapacity; ++entry) {
    Point(entry, placeholder_.view);
  }
  return true;
}

bool TextureCache::Update(const DrawList& draws, std::string& detail) {
  // The frame that copied from these has completed.
  for (HostBuffer& staging : recorded_) {
    DestroyHostBuffer(device_, staging);
  }
  recorded_.clear();
  ++generation_;
  for (const TextureSnapshot& snapshot : draws.textures) {
    Resident& resident = textures_[snapshot.id];
    resident.generation = generation_;
    const ToonTexture& texture = snapshot.texture;
    if (texture.pixels == nullptr || texture.pixels->empty()) {
      Release(resident);
      resident.revision = snapshot.revision;
      continue;
    }
    if (resident.entry != 0 && resident.revision == snapshot.revision) {
      continue;
    }
    // A texture that finds the table full keeps sampling the placeholder
    // and tries again on a later frame.
    if (resident.entry == 0) {
      if (!free_entries_.empty()) {
        resident.entry = free_entries_.back();
        free_entries_.pop_back();
      } else if (next_entry_ < kTextureCapacity) {
        resident.entry = next_entry_++;
      } else {
        continue;
      }
    }
    const bool srgb = texture.encoding == ToonTextureEncoding::Srgb;
    const VkFormat format =
        srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    const std::uint32_t mip_levels =
        (srgb ? srgb_mipmaps_ : linear_mipmaps_)
            ? MipLevels(texture.width, texture.height)
            : 1U;
    if (resident.image.image == VK_NULL_HANDLE ||
        resident.width != texture.width || resident.height != texture.height ||
        resident.format != format || resident.mip_levels != mip_levels) {
      DestroyDeviceImage(device_, resident.image);
      if (!CreateDeviceImage(physical_device_, device_, format,
              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              VK_IMAGE_ASPECT_COLOR_BIT, texture.width, texture.height,
              resident.image, detail, mip_levels)) {
        return false;
      }
      resident.width = texture.width;
      resident.height = texture.height;
      resident.format = format;
      resident.mip_levels = mip_levels;
    }
    if (!Stage(*texture.pixels, resident.image.image, texture.width,
            texture.height, mip_levels, detail)) {
      return false;
    }
    Point(resident.entry, resident.image.view);
    resident.revision = snapshot.revision;
    ++uploads_;
  }
  for (auto texture = textures_.begin(); texture != textures_.end();) {
    if (texture->second.generation != generation_) {
      Release(texture->second);
      texture = textures_.erase(texture);
    } else {
      ++texture;
    }
  }
  return true;
}

void TextureCache::RecordUploads(VkCommandBuffer command) {
  for (const Upload& upload : pending_) {
    Barrier(command, MipBarrier(upload.image, 0, upload.mip_levels,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {upload.width, upload.height, 1};
    vkCmdCopyBufferToImage(command, upload.staging.buffer, upload.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    // Each level is blitted from the one above it, which then waits,
    // readable, for the final transition.
    std::int32_t width = static_cast<std::int32_t>(upload.width);
    std::int32_t height = static_cast<std::int32_t>(upload.height);
    for (std::uint32_t level = 1; level < upload.mip_levels; ++level) {
      Barrier(command, MipBarrier(upload.image, level - 1U, 1,
          VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
          VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT,
          VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL));
      const std::int32_t next_width = std::max(width / 2, 1);
      const std::int32_t next_height = std::max(height / 2, 1);
      VkImageBlit blit{};
      blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.srcSubresource.mipLevel = level - 1U;
      blit.srcSubresource.layerCount = 1;
      blit.srcOffsets[1] = {width, height, 1};
      blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.dstSubresource.mipLevel = level;
      blit.dstSubresource.layerCount = 1;
      blit.dstOffsets[1] = {next_width, next_height, 1};
      vkCmdBlitImage(command, upload.image,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, upload.image,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
      width = next_width;
      height = next_height;
    }
    const std::uint32_t last = upload.mip_levels - 1U;
    if (last > 0U) {
      Barrier(command, MipBarrier(upload.image, 0, last,
          VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_NONE,
          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    }
    Barrier(command, MipBarrier(upload.image, last, 1,
        VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    recorded_.push_back(upload.staging);
  }
  pending_.clear();
}

std::uint32_t TextureCache::Entry(TextureId texture) const {
  const auto found = textures_.find(texture);
  return found == textures_.end() ? 0U : found->second.entry;
}

void TextureCache::Destroy() {
  for (auto& texture : textures_) {
    DestroyDeviceImage(device_, texture.second.image);
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
  free_entries_.clear();
  next_entry_ = 1;
  DestroyDeviceImage(device_, placeholder_);
}

bool TextureCache::Stage(const std::vector<std::uint8_t>& pixels,
    VkImage image, std::uint32_t width, std::uint32_t height,
    std::uint32_t mip_levels, std::string& detail) {
  HostBuffer staging;
  if (!CreateHostBuffer(physical_device_, device_, pixels.size(),
          VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, detail)) {
    DestroyHostBuffer(device_, staging);
    return false;
  }
  std::memcpy(staging.mapped, pixels.data(), pixels.size());
  pending_.push_back({image, width, height, mip_levels, staging});
  return FlushIfNeeded(device_, staging, detail);
}

void TextureCache::Point(std::uint32_t entry, VkImageView view) {
  VkDescriptorImageInfo image_info{};
  image_info.imageView = view;
  image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = set_;
  write.dstBinding = 1;
  write.dstArrayElement = entry;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  write.pImageInfo = &image_info;
  vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

// The entry samples the placeholder again, and is free for another texture.
void TextureCache::Release(Resident& resident) {
  if (resident.entry != 0) {
    Point(resident.entry, placeholder_.view);
    free_entries_.push_back(resident.entry);
    resident.entry = 0;
  }
  DestroyDeviceImage(device_, resident.image);
  resident.width = 0;
  resident.height = 0;
  resident.format = VK_FORMAT_UNDEFINED;
}

bool MaterialCache::Initialize(VkPhysicalDevice physical_device,
    VkDevice device, VkDescriptorSetLayout layout, std::string& detail) {
  physical_device_ = physical_device;
  device_ = device;
  VkDescriptorPoolSize pool_sizes[3]{};
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  pool_sizes[0].descriptorCount = 1;
  pool_sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  pool_sizes[1].descriptorCount = kTextureCapacity;
  pool_sizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;
  pool_sizes[2].descriptorCount = kSamplerCount;
  VkDescriptorPoolCreateInfo pool_create{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_create.maxSets = 1;
  pool_create.poolSizeCount = 3;
  pool_create.pPoolSizes = pool_sizes;
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

bool MaterialCache::Update(const DrawList& draws,
    const TextureCache& textures, std::string& detail) {
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
    const MaterialEntries entries = EntriesOf(material.material, textures);
    if (entry.parameters_revision != material.parameters_revision ||
        entry.entries != entries) {
      auto* slots = static_cast<MToonParameters*>(buffer_.mapped);
      WriteParameters(material.material, entries, slots[entry.slot]);
      entry.parameters_revision = material.parameters_revision;
      entry.entries = entries;
      entry.model = material.material.model;
      entry.double_sided = material.material.double_sided;
      entry.outline = HasOutline(material.material);
      entry.transparent = IsTransparent(material.material);
      entry.queue = RenderQueue(material.material);
      entry.depth_write = WritesDepth(material.material);
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

bool MeshCache::Initialize(VkPhysicalDevice physical_device, VkDevice device,
    VkDescriptorSetLayout skin_layout, std::string& detail) {
  physical_device_ = physical_device;
  device_ = device;
  skin_layout_ = skin_layout;
  const Float2 zero;
  const Matrix4 identity;
  std::size_t pool = 0;
  if (!Upload(zero_uv_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &zero,
          sizeof(zero), detail) ||
      !Upload(zero_skin_, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &identity,
          sizeof(identity), detail) ||
      !AllocateSkinSet(unskinned_set_, pool, detail)) {
    return false;
  }
  PointSkinSet(unskinned_set_, zero_skin_.buffer, zero_skin_.buffer);
  return true;
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
    // UVs are counted with the points they follow, like the normals.
    const bool uvs_present = mesh.uvs != nullptr && !mesh.uvs->empty();
    if (entry.uvs_revision != mesh.uvs_revision) {
      if (uvs_present &&
          !Upload(entry.uvs, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
              mesh.uvs->data(), mesh.uvs->size() * sizeof(Float2), detail)) {
        return false;
      }
      entry.uvs_revision = mesh.uvs_revision;
    }
    // Against this frame's topology, which can reach past UVs that were
    // long enough before, or come back within them.
    entry.has_uvs = uvs_present && mesh.uvs->size() >= mesh.index_bound;
    if (!UpdateSkin(mesh, entry, detail)) {
      return false;
    }
    entry.generation = generation_;
  }
  for (auto entry = entries_.begin(); entry != entries_.end();) {
    if (entry->second.generation != generation_) {
      Release(entry->second);
      entry = entries_.erase(entry);
    } else {
      ++entry;
    }
  }
  return true;
}

// A pose change writes the joint buffer and nothing else. A skin change
// uploads the influences and rewrites the joints too, since each joint's
// entry carries the geometry bind transform. A mesh whose skin does not
// cover this frame's topology, or whose pose lacks a joint it names, draws
// its points unskinned and keeps its buffers for when it is whole again.
bool MeshCache::UpdateSkin(const MeshSnapshot& mesh, Entry& entry,
    std::string& detail) {
  entry.skinned = IsSkinned(mesh);
  if (!entry.skinned) {
    entry.skin_flags = 0;
    return true;
  }
  bool rebind = false;
  if (entry.skin_set == VK_NULL_HANDLE) {
    if (!AllocateSkinSet(entry.skin_set, entry.skin_pool, detail)) {
      return false;
    }
    rebind = true;
  }
  if (entry.skin_revision != mesh.skin_revision) {
    const VkBuffer before = entry.influences.buffer;
    if (!Upload(entry.influences, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            mesh.influences->data(),
            mesh.influences->size() * sizeof(ToonJointInfluence), detail)) {
      return false;
    }
    rebind = rebind || entry.influences.buffer != before;
    entry.skin_revision = mesh.skin_revision;
    entry.pose_revision = 0;
    ++skin_uploads_;
  }
  if (entry.pose_revision != mesh.pose_revision) {
    joint_scratch_.clear();
    joint_scratch_.push_back(mesh.skeleton_to_mesh);
    for (const Matrix4& joint : *mesh.joints) {
      joint_scratch_.push_back(Multiply(joint, mesh.geom_bind));
    }
    const VkBuffer before = entry.joints.buffer;
    if (!Upload(entry.joints, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            joint_scratch_.data(), joint_scratch_.size() * sizeof(Matrix4),
            detail)) {
      return false;
    }
    rebind = rebind || entry.joints.buffer != before;
    entry.pose_revision = mesh.pose_revision;
    ++pose_writes_;
  }
  if (rebind) {
    PointSkinSet(entry.skin_set, entry.influences.buffer, entry.joints.buffer);
  }
  entry.skin_flags = kDrawSkinned |
                     (mesh.constant_influences ? kDrawConstantInfluences : 0U) |
                     (mesh.influences_per_point << kInfluenceCountShift);
  return true;
}

bool MeshCache::AllocateSkinSet(VkDescriptorSet& set, std::size_t& pool,
    std::string& detail) {
  constexpr std::uint32_t kSetsPerPool = 64;
  VkDescriptorSetAllocateInfo allocate{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate.descriptorSetCount = 1;
  allocate.pSetLayouts = &skin_layout_;
  if (!skin_pools_.empty()) {
    allocate.descriptorPool = skin_pools_.back();
    const VkResult result = vkAllocateDescriptorSets(device_, &allocate, &set);
    if (result == VK_SUCCESS) {
      pool = skin_pools_.size() - 1U;
      return true;
    }
    if (result != VK_ERROR_OUT_OF_POOL_MEMORY &&
        result != VK_ERROR_FRAGMENTED_POOL) {
      return VulkanOk(result, "vkAllocateDescriptorSets(skin)", detail);
    }
  }
  VkDescriptorPoolSize size{};
  size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  size.descriptorCount = kSetsPerPool * 2U;
  VkDescriptorPoolCreateInfo pool_create{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_create.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_create.maxSets = kSetsPerPool;
  pool_create.poolSizeCount = 1;
  pool_create.pPoolSizes = &size;
  VkDescriptorPool created = VK_NULL_HANDLE;
  if (!VulkanOk(vkCreateDescriptorPool(device_, &pool_create, nullptr,
                    &created),
          "vkCreateDescriptorPool(skin)", detail)) {
    return false;
  }
  skin_pools_.push_back(created);
  allocate.descriptorPool = created;
  pool = skin_pools_.size() - 1U;
  return VulkanOk(vkAllocateDescriptorSets(device_, &allocate, &set),
      "vkAllocateDescriptorSets(skin)", detail);
}

void MeshCache::PointSkinSet(VkDescriptorSet set, VkBuffer influences,
    VkBuffer joints) {
  VkDescriptorBufferInfo buffers[2]{};
  buffers[0].buffer = influences;
  buffers[0].range = VK_WHOLE_SIZE;
  buffers[1].buffer = joints;
  buffers[1].range = VK_WHOLE_SIZE;
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = set;
  write.dstBinding = 0;
  write.descriptorCount = 2;
  write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  write.pBufferInfo = buffers;
  vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void MeshCache::Release(Entry& entry) {
  DestroyHostBuffer(device_, entry.vertices);
  DestroyHostBuffer(device_, entry.normals);
  DestroyHostBuffer(device_, entry.uvs);
  DestroyHostBuffer(device_, entry.indices);
  DestroyHostBuffer(device_, entry.influences);
  DestroyHostBuffer(device_, entry.joints);
  if (entry.skin_set != VK_NULL_HANDLE) {
    vkFreeDescriptorSets(device_, skin_pools_[entry.skin_pool], 1,
        &entry.skin_set);
    entry.skin_set = VK_NULL_HANDLE;
  }
}

void MeshCache::Record(VkCommandBuffer command,
    const ScenePipelines& pipelines, const MaterialCache& materials,
    const DrawList& draws) {
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
    const VkDescriptorSet skin_set =
        entry.skinned ? entry.skin_set : unskinned_set_;
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelines.mesh.layout, 1, 1, &skin_set, 0, nullptr);
    DrawConstants constants{};
    const Matrix4 clip_from_object = Multiply(clip_from_world, mesh.transform);
    std::memcpy(constants.clip_from_object, clip_from_object.m.data(),
        sizeof(constants.clip_from_object));
    constants.color[0] = mesh.color.x;
    constants.color[1] = mesh.color.y;
    constants.color[2] = mesh.color.z;
    constants.color[3] = 1.0F;
    constants.flags = entry.skin_flags;
    vkCmdPushConstants(command, pipelines.mesh.layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        sizeof(constants), &constants);
    vkCmdDrawIndexed(command, entry.index_count, 1, 0, 0, 0);
  }

  // Every MToon pipeline shares the layout, the streams and the constants;
  // they differ in shaders, blending and which faces they cull. Their
  // layouts are identical, so the material set stays bound across them.
  const VkDescriptorSet material_set = materials.descriptor_set();
  const float projection_scale = std::fabs(draws.view.projection.m[5]);
  // A perspective projection puts -z into w; an orthographic one keeps w 1.
  const bool orthographic = draws.view.projection.m[11] == 0.0F;
  const ScenePipeline* bound = nullptr;
  const auto bind = [&](const ScenePipeline& pipeline) {
    if (bound == &pipeline) {
      return;
    }
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline.pipeline);
    if (bound == nullptr) {
      vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
          pipeline.layout, 0, 1, &material_set, 0, nullptr);
    }
    bound = &pipeline;
  };
  const auto draw_mtoon = [&](const ScenePipeline& pipeline,
                              const MeshSnapshot& mesh, const Entry& entry,
                              const MaterialCache::Entry& material,
                              VkCullModeFlags cull) {
    bind(pipeline);
    const VkBuffer streams[] = {entry.vertices.buffer, entry.normals.buffer,
        entry.has_uvs ? entry.uvs.buffer : zero_uv_.buffer};
    const VkDeviceSize offsets[] = {0, 0, 0};
    const VkDeviceSize strides[] = {sizeof(Float3), sizeof(Float3),
        entry.has_uvs ? sizeof(Float2) : 0U};
    vkCmdBindVertexBuffers2(command, 0, 3, streams, offsets, nullptr,
        strides);
    vkCmdBindIndexBuffer(command, entry.indices.buffer, 0,
        VK_INDEX_TYPE_UINT32);
    const VkDescriptorSet skin_set =
        entry.skinned ? entry.skin_set : unskinned_set_;
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline.layout, 1, 1, &skin_set, 0, nullptr);
    MToonDrawConstants constants{};
    const Matrix4 clip_from_object = Multiply(clip_from_world, mesh.transform);
    std::memcpy(constants.clip_from_object, clip_from_object.m.data(),
        sizeof(constants.clip_from_object));
    const bool mirrored = ViewRows(Multiply(draws.view.view, mesh.transform),
        constants.view_from_object_rows);
    constants.material_slot = material.slot;
    constants.flags = (entry.has_uvs ? kDrawHasUVs : 0U) |
                      (orthographic ? kDrawOrthographic : 0U) |
                      (material.transparent ? kDrawBlend : 0U) |
                      entry.skin_flags;
    constants.projection_scale = projection_scale;
    vkCmdSetCullMode(command, cull);
    vkCmdSetFrontFace(command, mirrored ? VK_FRONT_FACE_CLOCKWISE
                                        : VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetDepthWriteEnable(command,
        material.depth_write ? VK_TRUE : VK_FALSE);
    vkCmdPushConstants(command, pipeline.layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        sizeof(constants), &constants);
    vkCmdDrawIndexed(command, entry.index_count, 1, 0, 0, 0);
  };

  // The opaque pass, Mask included: every hull, then every surface. The
  // hull culls its front faces whether or not the material is double-sided,
  // as MToon states.
  transparent_scratch_.clear();
  for (const bool outline : {true, false}) {
    for (std::size_t index = 0; index < draws.draws.size(); ++index) {
      const MeshSnapshot& mesh = draws.draws[index];
      const auto found = entries_.find(mesh.id);
      const MaterialCache::Entry* material = mtoon_material(mesh);
      if (found == entries_.end() || material == nullptr) {
        continue;
      }
      if (material->transparent) {
        if (!outline) {
          transparent_scratch_.push_back(index);
        }
        continue;
      }
      if (outline && !material->outline) {
        continue;
      }
      draw_mtoon(outline ? pipelines.mtoon_outline : pipelines.mtoon, mesh,
          found->second, *material,
          outline                  ? VK_CULL_MODE_FRONT_BIT
          : material->double_sided ? VK_CULL_MODE_NONE
                                   : VK_CULL_MODE_BACK_BIT);
    }
  }

  // The transparent pass, back to front by render queue and, within one
  // queue, in the order `draws` lists them. A double-sided surface draws
  // its back faces before its front ones, so its near side blends over its
  // far side. Its hull follows it, as UniVRM and three-vrm draw MToon's
  // outline, so a surface that writes depth hides the hull's far side.
  std::stable_sort(transparent_scratch_.begin(), transparent_scratch_.end(),
      [&](std::size_t left, std::size_t right) {
        return mtoon_material(draws.draws[left])->queue <
               mtoon_material(draws.draws[right])->queue;
      });
  for (const std::size_t index : transparent_scratch_) {
    const MeshSnapshot& mesh = draws.draws[index];
    const Entry& entry = entries_.find(mesh.id)->second;
    const MaterialCache::Entry& material = *mtoon_material(mesh);
    if (material.double_sided) {
      draw_mtoon(pipelines.mtoon_transparent, mesh, entry, material,
          VK_CULL_MODE_FRONT_BIT);
    }
    draw_mtoon(pipelines.mtoon_transparent, mesh, entry, material,
        VK_CULL_MODE_BACK_BIT);
    if (material.outline) {
      draw_mtoon(pipelines.mtoon_outline, mesh, entry, material,
          VK_CULL_MODE_FRONT_BIT);
    }
  }
}

void MeshCache::Destroy() {
  for (auto& entry : entries_) {
    Release(entry.second);
  }
  entries_.clear();
  DestroyHostBuffer(device_, zero_uv_);
  DestroyHostBuffer(device_, zero_skin_);
  // Frees every set with its pool.
  for (VkDescriptorPool pool : skin_pools_) {
    vkDestroyDescriptorPool(device_, pool, nullptr);
  }
  skin_pools_.clear();
  unskinned_set_ = VK_NULL_HANDLE;
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
