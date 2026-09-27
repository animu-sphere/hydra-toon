// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <toon/extraction.hpp>
#include <toon/vulkan_backend.hpp>

namespace Toon {

// The window layer supplies the native pieces the backend needs to own a
// VkSurfaceKHR, without Vulkan or windowing types crossing this header.
// Handles travel encoded in std::uintptr_t: `create_surface` receives the
// encoded VkInstance, writes the encoded VkSurfaceKHR, and returns the raw
// VkResult as an integer.
struct PresentSurfaceProvider {
  std::vector<std::string> instance_extensions;
  std::int32_t (*create_surface)(void* user_data, std::uintptr_t instance,
      std::uintptr_t* surface) = nullptr;
  void* user_data = nullptr;
};

// Why session creation returned no session: `Unavailable` means this
// environment cannot present (no loader/device/present queue) and callers
// should report an explicit skip; `Error` is a real failure.
enum class PresentSetupStatus {
  Ready,
  Unavailable,
  Error,
};

struct PresentStatistics {
  std::uint64_t frames_presented = 0;
  std::uint32_t swapchain_recreates = 0;
  // Samples per pixel: RenderOptions' count, as far as the device offers it.
  std::uint32_t samples = 1;
  bool validation_available = false;
  std::uint32_t validation_message_count = 0;
  std::string validation_detail;
  std::string device_name;
};

// One swapchain presentation session drawing a scene's DrawList, through the
// same mesh pipeline as the offscreen renderer. One frame in flight, FIFO
// present mode when vsync is on, IMMEDIATE (when available) otherwise.
class PresentSession {
public:
  virtual ~PresentSession() = default;

  // Render and present one frame at the window's current framebuffer extent.
  // A zero extent (minimized window) is not an error: the frame is skipped
  // and `presented` reports false. Swapchain recreation on resize or
  // out-of-date presentation is handled internally.
  [[nodiscard]] virtual bool RenderFrame(const DrawList& draws,
      std::uint32_t width,
      std::uint32_t height, bool& presented,
      std::string& error) = 0;

  [[nodiscard]] virtual const PresentStatistics& statistics() const = 0;
};

// Creates the presentation session, enabling Vulkan validation capture
// whenever the loader offers it (same policy as RenderOffscreen). Returns
// nullptr with `status`/`error` describing why: the core-only configuration
// and missing device capability report Unavailable, real failures Error.
// Shader paths are explicit, as in RenderOffscreen.
[[nodiscard]] std::unique_ptr<PresentSession> CreatePresentSession(
    const PresentSurfaceProvider& surface, const SceneShaders& shaders,
    bool vsync, PresentSetupStatus& status, std::string& error,
    const RenderOptions& options = {});

} // namespace Toon
