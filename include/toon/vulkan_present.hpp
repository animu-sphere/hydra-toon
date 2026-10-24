// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <toon/backend.hpp>
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
