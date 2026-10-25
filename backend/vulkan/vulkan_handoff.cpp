// SPDX-License-Identifier: Apache-2.0
// The GPU handoff of the Vulkan backend (GPU handoff policy): the device
// requirements a host enables; the renderer on the device it lends, which
// this file checks against them before the renderer draws on it; and the
// product ring the renderer hands its frames off through.
#include <toon/vulkan_handoff.hpp>

#include <algorithm>
#include <vector>

#if defined(TOON_HAS_VULKAN)
#include <vulkan/vulkan.h>

#include "handoff_ring.hpp"
#include "vulkan_scene.hpp"
#endif

namespace Toon {

VulkanDeviceRequirements VulkanHandoffRequirements() {
  VulkanDeviceRequirements requirements;
#if defined(TOON_HAS_VULKAN)
  requirements.api_version = VK_API_VERSION_1_3;
  requirements.queue_flags = VK_QUEUE_GRAPHICS_BIT;
  requirements.required.timeline_semaphore = true;
  requirements.required.dynamic_rendering = true;
  requirements.required.synchronization2 = true;
  requirements.required.shader_sampled_image_array_dynamic_indexing = true;
  requirements.optional.alpha_to_one = true;
  requirements.color_format = vulkan_internal::kOffscreenColorFormat;
  requirements.depth_format = vulkan_internal::kOffscreenDepthFormat;
  requirements.shadow_format = vulkan_internal::kShadowFormat;
#endif
  return requirements;
}

#if defined(TOON_HAS_VULKAN)
namespace vulkan_internal {
namespace {

// Teardown may wait this long for consumers' release values.
constexpr std::uint64_t kReleaseTimeoutNs = 10'000'000'000ULL;

} // namespace

void ProductRing::Initialize(VkPhysicalDevice physical_device, VkDevice device,
    std::uint32_t slots) {
  physical_device_ = physical_device;
  device_ = device;
  slots_ = slots;
}

bool ProductRing::Ensure(std::uint32_t width, std::uint32_t height,
    std::string& error) {
  if (!current_.empty() && current_.front()->width == width &&
      current_.front()->height == height) {
    return true;
  }
  std::vector<std::unique_ptr<ProductSlot>> slots;
  for (std::uint32_t index = 0; index < slots_; ++index) {
    auto slot = std::make_unique<ProductSlot>();
    slot->width = width;
    slot->height = height;
    slot->generation = generation_ + 1;
    slot->index = index;
    if (!CreateDeviceImage(physical_device_, device_, kOffscreenColorFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, width, height, slot->image, error)) {
      DestroyDeviceImage(device_, slot->image);
      for (auto& created : slots) {
        DestroyDeviceImage(device_, created->image);
      }
      return false;
    }
    slots.push_back(std::move(slot));
  }
  // The old generation's slots stay until every consumer has released them
  // and their release values are reached (GPU handoff policy §7).
  for (auto& slot : current_) {
    retired_.push_back(std::move(slot));
  }
  current_ = std::move(slots);
  ++generation_;
  created_ += slots_;
  return true;
}

ProductSlot* ProductRing::Free() {
  ProductSlot* choice = nullptr;
  for (auto& slot : current_) {
    if (slot->holders != 0 || slot.get() == latest_) {
      continue;
    }
    if (choice == nullptr || slot->frame < choice->frame) {
      choice = slot.get();
    }
  }
  if (choice == nullptr && latest_ != nullptr && latest_->holders == 0 &&
      latest_->generation == generation_) {
    choice = latest_;
  }
  return choice;
}

void ProductRing::Publish(ProductSlot& slot, const HandoffImage& descriptor) {
  slot.frame = descriptor.frame;
  slot.descriptor = descriptor;
  latest_ = &slot;
}

bool ProductRing::Acquire(HandoffImage& image) {
  if (latest_ == nullptr) {
    return false;
  }
  ++latest_->holders;
  image = latest_->descriptor;
  return true;
}

ProductSlot* ProductRing::Find(const HandoffImage& image) {
  for (auto* slots : {&current_, &retired_}) {
    for (auto& slot : *slots) {
      if (slot->generation == image.generation && slot->index == image.slot) {
        return slot.get();
      }
    }
  }
  return nullptr;
}

bool ProductRing::Release(const HandoffImage& image, VkSemaphore semaphore,
    std::uint64_t value, std::string& error) {
  ProductSlot* slot = Find(image);
  if (slot == nullptr || slot->holders == 0 || slot->frame != image.frame) {
    error = "the released image is not an acquired frame of this renderer";
    return false;
  }
  --slot->holders;
  if (semaphore == VK_NULL_HANDLE) {
    return true;
  }
  // One wait per semaphore, for the highest value released.
  for (VkSemaphoreSubmitInfo& wait : slot->waits) {
    if (wait.semaphore == semaphore) {
      wait.value = std::max(wait.value, value);
      return true;
    }
  }
  VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  wait.semaphore = semaphore;
  wait.value = value;
  // A frame's first use of the slot is its transition at the colour output
  // stage.
  wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  slot->waits.push_back(wait);
  return true;
}

bool ProductRing::Prune(ProductSlot& slot) const {
  std::erase_if(slot.waits, [this](const VkSemaphoreSubmitInfo& wait) {
    std::uint64_t value = 0;
    return vkGetSemaphoreCounterValue(device_, wait.semaphore, &value) ==
               VK_SUCCESS &&
           value >= wait.value;
  });
  return slot.waits.empty();
}

void ProductRing::Collect() {
  // Reached release values are forgotten, so a consumer's semaphore is not
  // touched after a render call has seen its value.
  for (auto& slot : current_) {
    (void)Prune(*slot);
  }
  std::erase_if(retired_, [this](std::unique_ptr<ProductSlot>& slot) {
    if (!Prune(*slot) || slot->holders != 0 || slot.get() == latest_) {
      return false;
    }
    DestroySlot(*slot);
    return true;
  });
}

void ProductRing::DestroySlot(ProductSlot& slot) {
  DestroyDeviceImage(device_, slot.image);
  ++destroyed_;
}

void ProductRing::Destroy() {
  if (device_ == VK_NULL_HANDLE) {
    return;
  }
  for (auto* slots : {&current_, &retired_}) {
    for (auto& slot : *slots) {
      // Consumers' reads of the slot have executed before it goes.
      for (const VkSemaphoreSubmitInfo& wait : slot->waits) {
        VkSemaphoreWaitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        info.semaphoreCount = 1;
        info.pSemaphores = &wait.semaphore;
        info.pValues = &wait.value;
        (void)vkWaitSemaphores(device_, &info, kReleaseTimeoutNs);
      }
      DestroySlot(*slot);
    }
    slots->clear();
  }
  latest_ = nullptr;
}

std::uint32_t ProductRing::held() const {
  std::uint32_t held = 0;
  for (const auto* slots : {&current_, &retired_}) {
    for (const auto& slot : *slots) {
      held += slot->holders != 0 ? 1U : 0U;
    }
  }
  return held;
}

} // namespace vulkan_internal

namespace {

// Names every required feature the host did not enable, or nothing.
std::string MissingFeatures(const VulkanDeviceFeatures& enabled) {
  std::string missing;
  const auto require = [&](bool on, const char* name) {
    if (!on) {
      missing += missing.empty() ? name : std::string(", ") + name;
    }
  };
  require(enabled.timeline_semaphore, "timelineSemaphore");
  require(enabled.dynamic_rendering, "dynamicRendering");
  require(enabled.synchronization2, "synchronization2");
  require(enabled.shader_sampled_image_array_dynamic_indexing,
      "shaderSampledImageArrayDynamicIndexing");
  return missing;
}

// Whether the lent device meets the requirements; otherwise `detail` names
// the first thing it lacks.
bool CheckLentDevice(const LentVulkanDevice& lent, std::string& detail) {
  if (lent.instance == 0 || lent.physical_device == 0 || lent.device == 0 ||
      lent.queue == 0) {
    detail = "the lent device needs its instance, physical device, device "
             "and queue";
    return false;
  }
  if (lent.api_version < VK_API_VERSION_1_3) {
    detail = "the lent device's instance was created below Vulkan 1.3";
    return false;
  }
  const auto physical = reinterpret_cast<VkPhysicalDevice>(lent.physical_device);
  if (!vulkan_internal::SupportsSceneFeatures(physical, detail)) {
    return false;
  }
  if (!vulkan_internal::SupportsOffscreenTargets(physical)) {
    detail = "the lent device cannot draw and copy RGBA8 and depth32 targets";
    return false;
  }
  std::uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
  if (lent.queue_family >= count ||
      (families[lent.queue_family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
    detail = "the lent queue's family " + std::to_string(lent.queue_family) +
             " has no graphics queue";
    return false;
  }
  const std::string missing = MissingFeatures(lent.enabled);
  if (!missing.empty()) {
    detail = "the lent device does not enable " + missing;
    return false;
  }
  return true;
}

} // namespace
#endif

std::unique_ptr<OffscreenRenderer> CreateOffscreenRenderer(
    const LentVulkanDevice& device, const SceneShaders& shaders,
    FrameStatus& status, std::string& detail, const RenderOptions& options) {
  return CreateHandoffRenderer(device, shaders, status, detail, options);
}

std::unique_ptr<HandoffRenderer> CreateHandoffRenderer(
    const LentVulkanDevice& device, const SceneShaders& shaders,
    FrameStatus& status, std::string& detail, const RenderOptions& options,
    const HandoffOptions& handoff) {
#if !defined(TOON_HAS_VULKAN)
  (void)device;
  (void)shaders;
  (void)options;
  (void)handoff;
  status = FrameStatus::Skip;
  detail = "Vulkan backend was not compiled for this configuration";
  return nullptr;
#else
  if (handoff.slots < 2 || handoff.slots > 4) {
    status = FrameStatus::Fail;
    detail = "a product ring has two to four slots";
    return nullptr;
  }
  if (!CheckLentDevice(device, detail)) {
    status = FrameStatus::Fail;
    return nullptr;
  }
  const auto physical = reinterpret_cast<VkPhysicalDevice>(device.physical_device);
  VkPhysicalDeviceFeatures supported{};
  vkGetPhysicalDeviceFeatures(physical, &supported);
  vulkan_internal::OffscreenDevice lent;
  lent.physical_device = physical;
  lent.device = reinterpret_cast<VkDevice>(device.device);
  lent.queue = reinterpret_cast<VkQueue>(device.queue);
  lent.queue_family = device.queue_family;
  lent.alpha_to_one = device.enabled.alpha_to_one && supported.alphaToOne == VK_TRUE;
  return vulkan_internal::CreateLentRenderer(lent, shaders, options,
      handoff.slots, status, detail);
#endif
}

} // namespace Toon
