// SPDX-License-Identifier: Apache-2.0
// The GPU handoff's product ring (GPU handoff policy §4–§7): the slot images
// frames render into and consumers sample, who holds them, and the waits
// their releases leave for a slot's next use. Private to backend/vulkan;
// vulkan_handoff.cpp implements it.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include <toon/vulkan_handoff.hpp>

#include "vulkan_scene.hpp"

namespace Toon::vulkan_internal {

struct ProductSlot {
  DeviceImage image;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t generation = 0;
  std::uint32_t index = 0;
  // Acquisitions not yet released.
  std::uint32_t holders = 0;
  // The frame last drawn into it, 0 before any.
  std::uint64_t frame = 0;
  // What released consumers signal once their reads have executed; the
  // slot's next frame, or its destruction, waits for each.
  std::vector<VkSemaphoreSubmitInfo> waits;
  // The last frame drawn into it.
  HandoffImage descriptor;
};

class ProductRing {
public:
  void Initialize(VkPhysicalDevice physical_device, VkDevice device,
      std::uint32_t slots);
  // Before a frame at this extent: a new generation of slots when the
  // extent changed, the old ones retired until released.
  bool Ensure(std::uint32_t width, std::uint32_t height, std::string& error);
  // A slot of the current generation no consumer holds, preferring one
  // that is not the latest frame's and was drawn longest ago; null when
  // every slot is held.
  [[nodiscard]] ProductSlot* Free();
  // `slot` now holds the latest frame, `descriptor`.
  void Publish(ProductSlot& slot, const HandoffImage& descriptor);
  [[nodiscard]] bool Acquire(HandoffImage& image);
  [[nodiscard]] bool Release(const HandoffImage& image, VkSemaphore semaphore,
      std::uint64_t value, std::string& error);
  // Forgets every release value already reached, and destroys each retired
  // slot that is released and has none left. Called once the renderer's own
  // frames have completed.
  void Collect();
  // Teardown: waits for every release value, then destroys every slot.
  void Destroy();

  [[nodiscard]] std::uint32_t slots() const { return slots_; }
  [[nodiscard]] std::uint64_t generation() const { return generation_; }
  [[nodiscard]] std::uint32_t held() const;
  [[nodiscard]] std::uint32_t retired() const {
    return static_cast<std::uint32_t>(retired_.size());
  }
  [[nodiscard]] std::uint64_t created() const { return created_; }
  [[nodiscard]] std::uint64_t destroyed() const { return destroyed_; }

private:
  ProductSlot* Find(const HandoffImage& image);
  // Drops the slot's reached waits; true when none is left.
  bool Prune(ProductSlot& slot) const;
  void DestroySlot(ProductSlot& slot);

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  std::uint32_t slots_ = 0;
  std::uint64_t generation_ = 0;
  std::vector<std::unique_ptr<ProductSlot>> current_;
  std::vector<std::unique_ptr<ProductSlot>> retired_;
  ProductSlot* latest_ = nullptr;
  std::uint64_t created_ = 0;
  std::uint64_t destroyed_ = 0;
};

// The renderer on a device a host lent and the caller has checked against
// the requirements, with a ring of `slots` slots. It destroys what it
// creates on the device and never the device itself; a lent device's
// validation messages go to its host's instance.
std::unique_ptr<HandoffRenderer> CreateLentRenderer(
    const OffscreenDevice& device, const SceneShaders& shaders,
    const RenderOptions& options, std::uint32_t slots, FrameStatus& status,
    std::string& detail);

} // namespace Toon::vulkan_internal
