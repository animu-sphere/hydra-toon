// SPDX-License-Identifier: Apache-2.0
// The host that lends its device (GPU handoff policy §2, §9). It creates its
// instance and device with Vulkan itself, from the published requirements,
// with validation and synchronization validation on its own instance; checks
// that a device lacking a requirement is refused with the item named; and
// renders the RGBA fixtures on the lent device and on a device the renderer
// owns, at 1x and 4x, comparing both products byte for byte. Its reference
// consumer samples handed-off frames on the GPU and must equal the CPU
// composite of the readback product exactly, through capture, several
// consumers, held slots and resize.
#include <toon/vulkan_handoff.hpp>
#include "lending_host.hpp"
#include "rgba_fixture_scenes.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kSkip = 77;

using toon_lending::Host;
using toon_lending::Require;
using toon_lending::Skip;

// A device lacking a requirement is refused, the missing item named.
void Refusals(const Host& host, const Toon::SceneShaders& shaders) {
  const auto refused = [&](Toon::LentVulkanDevice lent, std::string_view named) {
    Toon::FrameStatus status = Toon::FrameStatus::Pass;
    std::string detail;
    auto renderer = Toon::CreateOffscreenRenderer(lent, shaders, status, detail);
    Require(renderer == nullptr && status == Toon::FrameStatus::Fail &&
                detail.find(named) != std::string::npos,
        "a lent device without " + std::string(named) + " was not refused by "
        "name: " + detail);
  };
  auto lent = host.Lend();
  lent.enabled.synchronization2 = false;
  refused(lent, "synchronization2");
  lent = host.Lend();
  lent.enabled.timeline_semaphore = false;
  lent.enabled.shader_sampled_image_array_dynamic_indexing = false;
  refused(lent, "shaderSampledImageArrayDynamicIndexing");
  lent = host.Lend();
  lent.api_version = VK_API_VERSION_1_2;
  refused(lent, "Vulkan 1.3");
  lent = host.Lend();
  lent.queue_family = host.family_count();
  refused(lent, "family");
  lent = host.Lend();
  lent.queue = 0;
  refused(lent, "queue");
}

struct Frame {
  Toon::ColorProduct color;
  Toon::DepthProduct depth;
};

void Render(Toon::OffscreenRenderer& renderer, const Toon::DrawList& draws,
    const Toon::BackgroundSettings& background, std::uint32_t width,
    std::uint32_t height, Frame& frame) {
  std::string error;
  Require(renderer.SetBackground(background, error), error);
  Require(renderer.Render(draws, width, height, frame.color, frame.depth, error),
      error);
}

// Every fixture, background and extent, lent against owned.
void Compare(const Host& host, const Toon::SceneShaders& shaders,
    std::uint32_t samples) {
  Toon::FrameStatus status = Toon::FrameStatus::Fail;
  std::string detail;
  auto owned = Toon::CreateOffscreenRenderer(shaders, status, detail, {samples});
  if (status == Toon::FrameStatus::Skip) throw Skip(detail);
  Require(owned != nullptr, detail);
  auto lent = Toon::CreateOffscreenRenderer(host.Lend(), shaders, status, detail,
      {samples});
  Require(lent != nullptr, detail);
  const auto& own = owned->statistics();
  const auto& borrowed = lent->statistics();
  Require(own.vendor_id == borrowed.vendor_id && own.device_id == borrowed.device_id &&
              own.driver_version == borrowed.driver_version,
      "the host chose another device than the renderer: " + borrowed.device_name +
          " against " + own.device_name);
  Require(own.samples == samples && borrowed.samples == samples,
      "requested samples unavailable");
  Require(own.pipelines_created == borrowed.pipelines_created,
      "a lent device built other pipelines");
  std::cout << "device=" << borrowed.device_name << " samples=" << samples
            << " gpu_timing=" << borrowed.gpu_timing << '\n';

  const Toon::BackgroundSettings backgrounds[] = {
      {Toon::BackgroundMode::Transparent, {0, 0, 0}},
      {Toon::BackgroundMode::Opaque, {0, 0, 0}},
      {Toon::BackgroundMode::Opaque, {1, 1, 1}},
  };
  // The fixtures' extent, then a resize and back.
  const std::uint32_t extents[][2] = {{256, 256}, {320, 180}, {256, 256}};
  for (const std::string name : {"avatar", "mmd-shadow"}) {
    Toon::RenderWorld world;
    if (name == "avatar") {
      toon_fixtures::Avatar(world);
    } else {
      toon_fixtures::Shadow(world);
    }
    const auto draws = Toon::ExtractDrawList(world.Commit());
    for (const auto& extent : extents) {
      for (const auto& background : backgrounds) {
        Frame a;
        Frame b;
        Render(*owned, draws, background, extent[0], extent[1], a);
        Render(*lent, draws, background, extent[0], extent[1], b);
        const std::string where = name + " " + std::to_string(samples) + "x " +
                                  std::to_string(extent[0]) + "x" +
                                  std::to_string(extent[1]);
        Require(!a.color.payload.empty() && a.color.payload == b.color.payload,
            where + ": the lent device's colour differs from the owned one's");
        Require(a.depth.payload == b.depth.payload,
            where + ": the lent device's depth differs from the owned one's");
        Require(a.color.alpha_convention == b.color.alpha_convention &&
                    a.color.color_space == b.color.color_space &&
                    a.color.origin == b.color.origin,
            where + ": the products' interpretation differs");
        Require(borrowed.readback_bytes ==
                    std::uint64_t{extent[0]} * extent[1] * 8U,
            where + ": unexpected readback bytes");
        Require(!borrowed.gpu_timing || borrowed.gpu_frame_ms > 0,
            where + ": the GPU frame time was not measured");
      }
    }
    std::cout << name << ' ' << samples << "x: lent equals owned\n";
  }
  // A steady frame on the lent device rebuilds nothing, as on an owned one.
  Require(borrowed.pipelines_created == own.pipelines_created &&
              borrowed.texture_uploads == own.texture_uploads &&
              borrowed.topology_uploads == own.topology_uploads &&
              borrowed.target_allocations == own.target_allocations,
      "the lent device's uploads or allocations differ from the owned one's");
  Require(own.validation_message_count == 0,
      "Vulkan validation messages on the owned device: " + own.validation_detail);
  Require(host.messages().count == 0,
      "Vulkan validation messages on the lent device: " + host.messages().first);
}

// A consumer's background: a solid colour, or a checkerboard of it and an
// alternate. Black and white keep every composite an exact multiple of
// 1/255, so the GPU's result is comparable byte for byte.
struct Background {
  float color = 0;
  float alternate = 0;
  bool checker = false;
  std::uint32_t size = 16;
};

std::vector<std::uint32_t> Spirv(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  Require(file.good(), "cannot open " + path);
  const auto size = static_cast<std::size_t>(file.tellg());
  std::vector<std::uint32_t> words(size / 4U);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(words.data()),
      static_cast<std::streamsize>(size));
  return words;
}

std::uint32_t MemoryType(VkPhysicalDevice physical, std::uint32_t allowed,
    VkMemoryPropertyFlags wanted) {
  VkPhysicalDeviceMemoryProperties properties{};
  vkGetPhysicalDeviceMemoryProperties(physical, &properties);
  for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
    if ((allowed & (1U << index)) != 0 &&
        (properties.memoryTypes[index].propertyFlags & wanted) == wanted) {
      return index;
    }
  }
  throw std::runtime_error("no suitable memory type");
}

void Check(VkResult result, const char* operation) {
  Require(result == VK_SUCCESS, std::string(operation) + " failed with VkResult " +
                                    std::to_string(result));
}

// The reference consumer of GPU handoff policy §9. On the lent device and
// queue it waits for a handed-off frame, samples it with the renderer's own
// composite shaders, source-over onto a background, into its own RGBA8
// target, signals its timeline once that read has executed, and reads the
// target back.
class Consumer {
public:
  Consumer(const Host& host, const Toon::SceneShaders& shaders)
      : physical_(host.physical_device()), device_(host.device()),
        queue_(host.queue()) {
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    Check(vkCreateSemaphore(device_, &semaphore, nullptr, &timeline_),
        "vkCreateSemaphore");
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = host.queue_family();
    Check(vkCreateCommandPool(device_, &pool, nullptr, &pool_), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = pool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    Check(vkAllocateCommandBuffers(device_, &allocate, &command_),
        "vkAllocateCommandBuffers");

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    Check(vkCreateSampler(device_, &sampler, nullptr, &sampler_), "vkCreateSampler");
    VkDescriptorSetLayoutBinding binding{};
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = &sampler_;
    VkDescriptorSetLayoutCreateInfo set_layout{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_layout.bindingCount = 1;
    set_layout.pBindings = &binding;
    Check(vkCreateDescriptorSetLayout(device_, &set_layout, nullptr, &set_layout_),
        "vkCreateDescriptorSetLayout");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo descriptor_pool{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptor_pool.maxSets = 1;
    descriptor_pool.poolSizeCount = 1;
    descriptor_pool.pPoolSizes = &size;
    Check(vkCreateDescriptorPool(device_, &descriptor_pool, nullptr, &descriptors_),
        "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set.descriptorPool = descriptors_;
    set.descriptorSetCount = 1;
    set.pSetLayouts = &set_layout_;
    Check(vkAllocateDescriptorSets(device_, &set, &set_), "vkAllocateDescriptorSets");
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 8};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &set_layout_;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    Check(vkCreatePipelineLayout(device_, &layout, nullptr, &layout_),
        "vkCreatePipelineLayout");
    CreatePipeline(shaders);
  }

  ~Consumer() {
    if (value_ != 0) {
      VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
      wait.semaphoreCount = 1;
      wait.pSemaphores = &timeline_;
      wait.pValues = &value_;
      vkWaitSemaphores(device_, &wait, ~0ULL);
    }
    DestroyTarget();
    vkDestroyPipeline(device_, pipeline_, nullptr);
    vkDestroyPipelineLayout(device_, layout_, nullptr);
    vkDestroyDescriptorPool(device_, descriptors_, nullptr);
    vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
    vkDestroySampler(device_, sampler_, nullptr);
    vkDestroyCommandPool(device_, pool_, nullptr);
    vkDestroySemaphore(device_, timeline_, nullptr);
  }
  Consumer(const Consumer&) = delete;
  Consumer& operator=(const Consumer&) = delete;

  [[nodiscard]] std::uintptr_t semaphore() const {
    return reinterpret_cast<std::uintptr_t>(timeline_);
  }

  // Composites `image` over `background`; returns what the consumer's
  // timeline signals once its read has executed, the release value.
  std::uint64_t Submit(const Toon::HandoffImage& image, const Background& background) {
    Wait();
    EnsureTarget(image.width, image.height);
    VkDescriptorImageInfo foreground{};
    foreground.imageView = reinterpret_cast<VkImageView>(image.view);
    foreground.imageLayout = static_cast<VkImageLayout>(image.layout);
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set_;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &foreground;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    Check(vkResetCommandBuffer(command_, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(command_, &begin), "vkBeginCommandBuffer");
    Barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = target_view_;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = {width_, height_};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(command_, &rendering);
    vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0,
        1, &set_, 0, nullptr);
    const float constants[8] = {background.color, background.color,
        background.color, static_cast<float>(background.size),
        background.alternate, background.alternate, background.alternate,
        background.checker ? 1.0F : 0.0F};
    vkCmdPushConstants(command_, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        sizeof(constants), constants);
    const VkViewport viewport{0, 0, static_cast<float>(width_),
        static_cast<float>(height_), 0, 1};
    const VkRect2D scissor{{0, 0}, {width_, height_}};
    vkCmdSetViewport(command_, 0, 1, &viewport);
    vkCmdSetScissor(command_, 0, 1, &scissor);
    vkCmdDraw(command_, 3, 1, 0, 0);
    vkCmdEndRendering(command_);
    Barrier(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    // The previous composite's copy into the same buffer, earlier on the
    // queue, comes first.
    VkMemoryBarrier2 after_copy{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    after_copy.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    after_copy.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    after_copy.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    after_copy.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    VkDependencyInfo copy_dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    copy_dependency.memoryBarrierCount = 1;
    copy_dependency.pMemoryBarriers = &after_copy;
    vkCmdPipelineBarrier2(command_, &copy_dependency);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {width_, height_, 1};
    vkCmdCopyImageToBuffer(command_, target_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        buffer_, 1, &copy);
    VkMemoryBarrier2 to_host{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    to_host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    to_host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    to_host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &to_host;
    vkCmdPipelineBarrier2(command_, &dependency);
    Check(vkEndCommandBuffer(command_), "vkEndCommandBuffer");

    // The handed-off frame before the fragment stage samples it; this
    // consumer's previous frame before its target is reused.
    VkSemaphoreSubmitInfo waits[2]{};
    waits[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waits[0].semaphore = reinterpret_cast<VkSemaphore>(image.semaphore);
    waits[0].value = image.value;
    waits[0].stageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    waits[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waits[1].semaphore = timeline_;
    waits[1].value = value_;
    waits[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = timeline_;
    signal.value = value_ + 1;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = command_;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = value_ != 0 ? 2U : 1U;
    submit.pWaitSemaphoreInfos = waits;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    Check(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2");
    return ++value_;
  }

  // Waits for the last composite and returns its RGBA8, top row first.
  std::vector<std::uint8_t> Read() {
    Wait();
    std::vector<std::uint8_t> pixels(std::size_t{width_} * height_ * 4U);
    const VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr,
        buffer_memory_, 0, VK_WHOLE_SIZE};
    Check(vkInvalidateMappedMemoryRanges(device_, 1, &range),
        "vkInvalidateMappedMemoryRanges");
    std::memcpy(pixels.data(), mapped_, pixels.size());
    return pixels;
  }

private:
  void Wait() {
    if (value_ == 0) return;
    VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &timeline_;
    wait.pValues = &value_;
    Check(vkWaitSemaphores(device_, &wait, 10'000'000'000ULL), "vkWaitSemaphores");
  }

  void Barrier(VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
      VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access,
      VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = source_stage;
    barrier.srcAccessMask = source_access;
    barrier.dstStageMask = destination_stage;
    barrier.dstAccessMask = destination_access;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = target_;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command_, &dependency);
  }

  void CreatePipeline(const Toon::SceneShaders& shaders) {
    VkShaderModule modules[2]{};
    const std::string paths[2] = {shaders.composite_vertex, shaders.composite_fragment};
    for (int stage = 0; stage < 2; ++stage) {
      const auto words = Spirv(paths[stage]);
      VkShaderModuleCreateInfo create{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      create.codeSize = words.size() * sizeof(std::uint32_t);
      create.pCode = words.data();
      Check(vkCreateShaderModule(device_, &create, nullptr, &modules[stage]),
          "vkCreateShaderModule");
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int stage = 0; stage < 2; ++stage) {
      stages[stage].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stages[stage].stage =
          stage == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
      stages[stage].module = modules[stage];
      stages[stage].pName = "main";
    }
    VkPipelineVertexInputStateCreateInfo vertex_input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;
    const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = states;
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo rendering{
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo create{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    create.pNext = &rendering;
    create.stageCount = 2;
    create.pStages = stages;
    create.pVertexInputState = &vertex_input;
    create.pInputAssemblyState = &input_assembly;
    create.pViewportState = &viewport;
    create.pRasterizationState = &raster;
    create.pMultisampleState = &multisample;
    create.pColorBlendState = &blend;
    create.pDynamicState = &dynamic;
    create.layout = layout_;
    const VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1,
        &create, nullptr, &pipeline_);
    vkDestroyShaderModule(device_, modules[0], nullptr);
    vkDestroyShaderModule(device_, modules[1], nullptr);
    Check(result, "vkCreateGraphicsPipelines");
  }

  void EnsureTarget(std::uint32_t width, std::uint32_t height) {
    if (target_ != VK_NULL_HANDLE && width == width_ && height == height_) return;
    DestroyTarget();
    width_ = width;
    height_ = height;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_R8G8B8A8_UNORM;
    image.extent = {width, height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    Check(vkCreateImage(device_, &image, nullptr, &target_), "vkCreateImage");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, target_, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = MemoryType(physical_, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(device_, &allocate, nullptr, &target_memory_),
        "vkAllocateMemory");
    Check(vkBindImageMemory(device_, target_, target_memory_, 0), "vkBindImageMemory");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = target_;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R8G8B8A8_UNORM;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    Check(vkCreateImageView(device_, &view, nullptr, &target_view_),
        "vkCreateImageView");
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = VkDeviceSize{width} * height * 4U;
    buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Check(vkCreateBuffer(device_, &buffer, nullptr, &buffer_), "vkCreateBuffer");
    vkGetBufferMemoryRequirements(device_, buffer_, &requirements);
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = MemoryType(physical_, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    Check(vkAllocateMemory(device_, &allocate, nullptr, &buffer_memory_),
        "vkAllocateMemory");
    Check(vkBindBufferMemory(device_, buffer_, buffer_memory_, 0), "vkBindBufferMemory");
    Check(vkMapMemory(device_, buffer_memory_, 0, VK_WHOLE_SIZE, 0, &mapped_),
        "vkMapMemory");
  }

  void DestroyTarget() {
    vkDestroyImageView(device_, target_view_, nullptr);
    vkDestroyImage(device_, target_, nullptr);
    vkFreeMemory(device_, target_memory_, nullptr);
    vkDestroyBuffer(device_, buffer_, nullptr);
    vkFreeMemory(device_, buffer_memory_, nullptr);
    target_view_ = VK_NULL_HANDLE;
    target_ = VK_NULL_HANDLE;
    target_memory_ = VK_NULL_HANDLE;
    buffer_ = VK_NULL_HANDLE;
    buffer_memory_ = VK_NULL_HANDLE;
    mapped_ = nullptr;
  }

  VkPhysicalDevice physical_;
  VkDevice device_;
  VkQueue queue_;
  VkSemaphore timeline_ = VK_NULL_HANDLE;
  std::uint64_t value_ = 0;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  VkCommandBuffer command_ = VK_NULL_HANDLE;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptors_ = VK_NULL_HANDLE;
  VkDescriptorSet set_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  VkImage target_ = VK_NULL_HANDLE;
  VkDeviceMemory target_memory_ = VK_NULL_HANDLE;
  VkImageView target_view_ = VK_NULL_HANDLE;
  VkBuffer buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory buffer_memory_ = VK_NULL_HANDLE;
  void* mapped_ = nullptr;
};

// The CPU composite of a premultiplied product over `background`, as the
// composite shader computes it: colour plus background times one minus
// alpha, opaque. Over black or white each value is an exact integer.
std::vector<std::uint8_t> Composite(const Toon::ColorProduct& product,
    const Background& background) {
  std::vector<std::uint8_t> out(product.payload.size());
  for (std::uint32_t y = 0; y < product.height; ++y) {
    for (std::uint32_t x = 0; x < product.width; ++x) {
      const std::size_t at = (std::size_t{y} * product.width + x) * 4U;
      const bool alternate =
          background.checker && ((x / background.size + y / background.size) & 1U) != 0;
      const int under = static_cast<int>(alternate ? background.alternate : background.color);
      const int alpha = product.payload[at + 3];
      for (int channel = 0; channel < 3; ++channel) {
        out[at + channel] = static_cast<std::uint8_t>(
            std::min(255, product.payload[at + channel] + under * (255 - alpha)));
      }
      out[at + 3] = 255;
    }
  }
  return out;
}

Toon::DrawList Fixture(const std::string& name) {
  Toon::RenderWorld world;
  if (name == "avatar") {
    toon_fixtures::Avatar(world);
  } else {
    toon_fixtures::Shadow(world);
  }
  return Toon::ExtractDrawList(world.Commit());
}

const Background kBackgrounds[] = {
    {0, 0, false, 16},
    {1, 1, false, 16},
    {0, 1, true, 16},
};

// Renders `draws` handed off and acquires the frame for one consumer.
Toon::HandoffImage HandOff(Toon::HandoffRenderer& renderer,
    const Toon::DrawList& draws, std::uint32_t width, std::uint32_t height) {
  Toon::HandoffImage image;
  std::string error;
  Require(renderer.RenderHandoff(draws, width, height, image, error) ==
              Toon::HandoffStatus::Rendered,
      "a handoff frame was not rendered: " + error);
  Toon::HandoffImage acquired;
  Require(renderer.Acquire(acquired) && acquired.frame == image.frame &&
              acquired.view == image.view,
      "the latest frame was not acquired");
  return acquired;
}

void Release(Toon::HandoffRenderer& renderer, const Toon::HandoffImage& image,
    const Consumer* consumer, std::uint64_t value) {
  std::string error;
  Require(renderer.Release(image, consumer != nullptr ? consumer->semaphore() : 0,
              value, error),
      "release failed: " + error);
}

// The reference consumer's exact comparisons (GPU handoff policy §9).
void Handoff(const Host& host, const Toon::SceneShaders& shaders,
    std::uint32_t samples) {
  Toon::FrameStatus status = Toon::FrameStatus::Fail;
  std::string error;
  for (const std::uint32_t slots : {1U, 5U}) {
    Require(Toon::CreateHandoffRenderer(host.Lend(), shaders, status, error,
                {samples}, {slots}) == nullptr &&
                status == Toon::FrameStatus::Fail,
        "a ring of " + std::to_string(slots) + " slots was not refused");
  }
  // The consumers' semaphores outlive the renderer, whose teardown may wait
  // for them.
  Consumer first(host, shaders);
  Consumer second(host, shaders);
  auto renderer = Toon::CreateHandoffRenderer(host.Lend(), shaders, status, error,
      {samples}, {3});
  Require(renderer != nullptr, error);
  Require(renderer->SetBackground({Toon::BackgroundMode::Transparent, {}}, error),
      error);
  const auto readback = [&](const Toon::DrawList& draws, std::uint32_t width,
                            std::uint32_t height) {
    Toon::ColorProduct color;
    Toon::DepthProduct depth;
    Require(renderer->Render(draws, width, height, color, depth, error), error);
    return color;
  };
  const auto& stats = renderer->handoff_statistics();
  const std::string tag = std::to_string(samples) + "x: ";

  // Every fixture over every background: the consumer's image is the CPU
  // composite of the readback product, byte for byte.
  for (const std::string name : {"avatar", "mmd-shadow"}) {
    const auto draws = Fixture(name);
    const auto product = readback(draws, 256, 256);
    for (const auto& background : kBackgrounds) {
      const auto image = HandOff(*renderer, draws, 256, 256);
      Require(image.width == 256 && image.height == 256 &&
                  image.format == VK_FORMAT_R8G8B8A8_UNORM &&
                  image.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                  image.queue_family == host.queue_family() &&
                  image.value == image.frame && image.semaphore != 0 &&
                  image.generation == 1 && image.render_started != 0 &&
                  image.submitted >= image.render_started &&
                  image.scene_revision == draws.source_revision &&
                  std::string_view(image.alpha_convention) == "premultiplied" &&
                  std::string_view(image.color_space) == "linear" &&
                  std::string_view(image.origin) == "top-left",
          tag + "the handoff descriptor is incomplete");
      const auto value = first.Submit(image, background);
      Release(*renderer, image, &first, value);
      Require(first.Read() == Composite(product, background),
          tag + name + ": the consumer's composite differs from the readback "
                       "product's");
    }
    Require(renderer->statistics().readback_bytes == 0,
        tag + "a handoff frame read back");
    std::cout << tag << name << ": consumer composites equal readback\n";
  }
  // A handed-off frame's GPU time arrives once a later call has waited for it.
  {
    const auto draws = Fixture("avatar");
    const auto image = HandOff(*renderer, draws, 256, 256);
    Release(*renderer, image, nullptr, 0);
    const auto later = HandOff(*renderer, draws, 256, 256);
    Release(*renderer, later, nullptr, 0);
    const auto& timed = renderer->statistics();
    Require(!timed.gpu_timing ||
                (timed.gpu_frame == image.frame && timed.gpu_frame_ms > 0),
        tag + "a handed-off frame's GPU time was not measured");
  }

  // Capture: the slot's copy equals the readback product and what the
  // consumer sampled.
  {
    const auto draws = Fixture("avatar");
    const auto product = readback(draws, 256, 256);
    renderer->RequestCapture();
    const auto image = HandOff(*renderer, draws, 256, 256);
    const auto value = first.Submit(image, kBackgrounds[0]);
    Release(*renderer, image, &first, value);
    Toon::ColorProduct captured;
    Require(renderer->TakeCapture(captured, error), error);
    Require(captured.payload == product.payload &&
                captured.alpha_convention == "premultiplied",
        tag + "the capture differs from the readback product");
    Require(first.Read() == Composite(captured, kBackgrounds[0]),
        tag + "the consumer sampled other bytes than the capture");
    Require(!renderer->TakeCapture(captured, error), tag + "a capture was taken twice");
    Require(stats.captures == 1, tag + "the capture was not counted");
  }

  // Several consumers acquire one frame; it returns to the ring only when
  // both have released it, and its next use waits for both.
  {
    const auto draws = Fixture("mmd-shadow");
    const auto product = readback(draws, 256, 256);
    const auto image = HandOff(*renderer, draws, 256, 256);
    Toon::HandoffImage again;
    Require(renderer->Acquire(again) && again.frame == image.frame, tag + "second acquire");
    const auto a = first.Submit(image, kBackgrounds[1]);
    const auto b = second.Submit(again, kBackgrounds[2]);
    Release(*renderer, image, &first, a);
    Require(stats.held == 1, tag + "a frame two consumers hold was freed by one");
    Release(*renderer, again, &second, b);
    Require(stats.held == 0, tag + "a released frame is still held");
    Require(first.Read() == Composite(product, kBackgrounds[1]) &&
                second.Read() == Composite(product, kBackgrounds[2]),
        tag + "two consumers of one frame differ from the readback product");
    std::string ignored;
    Require(!renderer->Release(image, 0, 0, ignored),
        tag + "a frame was released more often than acquired");
  }

  // Held slots: a held frame is never drawn over, and with every slot held
  // a render call draws nothing.
  {
    const auto avatar = Fixture("avatar");
    const auto shadow = Fixture("mmd-shadow");
    const auto avatar_product = readback(avatar, 256, 256);
    const auto shadow_product = readback(shadow, 256, 256);
    const auto held = HandOff(*renderer, avatar, 256, 256);
    const auto b = HandOff(*renderer, shadow, 256, 256);
    const auto c = HandOff(*renderer, shadow, 256, 256);
    Require(stats.held == 3, tag + "three acquisitions do not hold three slots");
    const auto handed = stats.frames_handed_off;
    const auto frames = renderer->statistics().frames_rendered;
    Toon::HandoffImage none;
    Require(renderer->RenderHandoff(avatar, 256, 256, none, error) ==
                    Toon::HandoffStatus::NoFreeSlot &&
                stats.no_free_slot == 1 && stats.frames_handed_off == handed &&
                renderer->statistics().frames_rendered == frames,
        tag + "a render call with every slot held drew");
    Toon::HandoffImage latest;
    Require(renderer->Acquire(latest) && latest.frame == c.frame,
        tag + "the latest frame changed without a render");
    Release(*renderer, latest, nullptr, 0);
    const auto value = first.Submit(held, kBackgrounds[0]);
    Require(first.Read() == Composite(avatar_product, kBackgrounds[0]),
        tag + "a held frame was drawn over");
    Release(*renderer, held, &first, value);
    Release(*renderer, b, nullptr, 0);
    const auto next = HandOff(*renderer, shadow, 256, 256);
    Require(next.slot == held.slot || next.slot == b.slot,
        tag + "a frame went into a held slot");
    const auto check = second.Submit(next, kBackgrounds[0]);
    Release(*renderer, next, &second, check);
    Require(second.Read() == Composite(shadow_product, kBackgrounds[0]),
        tag + "the frame after a full ring differs");
    Release(*renderer, c, nullptr, 0);
  }

  // Resize: a new extent is a new generation; a held old slot stays valid
  // until released, and is destroyed after.
  {
    const auto draws = Fixture("avatar");
    const auto held = HandOff(*renderer, draws, 256, 256);
    const auto product = readback(draws, 320, 180);
    const auto resized = HandOff(*renderer, draws, 320, 180);
    Require(resized.generation == held.generation + 1 && stats.generation == 2 &&
                stats.retired == 3 && resized.width == 320,
        tag + "a resize did not start a new generation");
    const auto old_product = readback(draws, 256, 256);
    const auto value = first.Submit(held, kBackgrounds[2]);
    Require(first.Read() == Composite(old_product, kBackgrounds[2]),
        tag + "a held slot of the old generation changed");
    Release(*renderer, held, &first, value);
    const auto now = second.Submit(resized, kBackgrounds[1]);
    Release(*renderer, resized, &second, now);
    Require(second.Read() == Composite(product, kBackgrounds[1]),
        tag + "the resized frame differs from the readback product");
    const auto destroyed = stats.slots_destroyed;
    const auto after = HandOff(*renderer, draws, 320, 180);
    Release(*renderer, after, nullptr, 0);
    Require(stats.retired == 0 && stats.slots_destroyed == destroyed + 3 &&
                stats.generation == 2 && after.generation == 2,
        tag + "the old generation's slots were not destroyed after release");
  }
  // A release value not yet reached when the slot is next drawn into: the
  // frame waits for it on the GPU, here a value the host signals only after
  // that frame's submit.
  {
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo create{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    VkSemaphore gate = VK_NULL_HANDLE;
    Check(vkCreateSemaphore(host.device(), &create, nullptr, &gate), "vkCreateSemaphore");
    const auto draws = Fixture("mmd-shadow");
    const auto product = readback(draws, 256, 256);
    const auto gated = HandOff(*renderer, draws, 256, 256);
    Require(renderer->Release(gated, reinterpret_cast<std::uintptr_t>(gate), 1, error),
        error);
    const auto waits = stats.release_waits;
    Toon::HandoffImage next;
    for (std::uint32_t frame = 0; frame < stats.slots; ++frame) {
      next = HandOff(*renderer, draws, 256, 256);
      Release(*renderer, next, nullptr, 0);
      if (next.slot == gated.slot) break;
    }
    Require(next.slot == gated.slot && stats.release_waits == waits + 1,
        tag + "the frame drawn into a released slot did not wait for its value");
    VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
    signal.semaphore = gate;
    signal.value = 1;
    Check(vkSignalSemaphore(host.device(), &signal), "vkSignalSemaphore");
    Toon::HandoffImage again;
    Require(renderer->Acquire(again) && again.frame == next.frame, tag + "reacquire");
    const auto value = first.Submit(again, kBackgrounds[2]);
    Release(*renderer, again, &first, value);
    Require(first.Read() == Composite(product, kBackgrounds[2]),
        tag + "the frame that waited for a release differs");
    vkDestroySemaphore(host.device(), gate, nullptr);
  }
  Require(stats.release_waits > 0, tag + "no frame waited for a release");
  std::cout << tag << "capture, consumers, held slots and resize passed ("
            << stats.frames_handed_off << " frames, " << stats.release_waits
            << " release waits)\n";
  Require(host.messages().count == 0,
      "Vulkan validation messages on the lent device: " + host.messages().first);
}

// Device loss is not reproducible on demand; the host's recovery is: it
// destroys the renderer and its device, creates both again and acquires.
void Recreate(const Toon::SceneShaders& shaders) {
  Host host(Toon::VulkanHandoffRequirements(), true);
  Toon::FrameStatus status = Toon::FrameStatus::Fail;
  std::string error;
  {
    Consumer consumer(host, shaders);
    auto renderer = Toon::CreateHandoffRenderer(host.Lend(), shaders, status, error);
    Require(renderer != nullptr, error);
    Require(renderer->SetBackground({Toon::BackgroundMode::Transparent, {}}, error),
        error);
    const auto draws = Fixture("avatar");
    Toon::ColorProduct product;
    Toon::DepthProduct depth;
    Require(renderer->Render(draws, 256, 256, product, depth, error), error);
    const auto image = HandOff(*renderer, draws, 256, 256);
    const auto value = consumer.Submit(image, kBackgrounds[1]);
    Release(*renderer, image, &consumer, value);
    Require(consumer.Read() == Composite(product, kBackgrounds[1]),
        "a recreated device's handoff differs");
  }
  host.DestroyDevice();
  Require(host.messages().count == 0,
      "Vulkan validation messages on the recreated device: " + host.messages().first);
  std::cout << "a recreated device and renderer hand off again\n";
}

} // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 2, "expected the shader directory");
    const auto shaders = Toon::SceneShadersIn(argv[1]);
    const auto requirements = Toon::VulkanHandoffRequirements();
    Require(requirements.api_version != 0,
        "the Vulkan backend publishes no requirements");
    Host host(requirements, true);
    std::cout << "validation=" << host.validation() << '\n';
    Refusals(host, shaders);
    for (const std::uint32_t samples : {1U, 4U}) {
      Compare(host, shaders, samples);
      Handoff(host, shaders, samples);
    }
    // Every renderer and consumer has gone; the device and what the host
    // made remain the host's, and destroying the device reports nothing
    // left on it.
    host.DestroyDevice();
    Require(host.messages().count == 0,
        "a renderer left objects on the lent device: " + host.messages().first);
    Recreate(shaders);
    std::cout << "lent and owned devices render identical products; the "
                 "reference consumer matches readback\n";
    return 0;
  } catch (const Skip& skip) {
    std::cout << "SKIP: " << skip.what() << '\n';
    return kSkip;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
