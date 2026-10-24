// SPDX-License-Identifier: Apache-2.0
// The WebGPU backend's present session (WebGPU policy §8). The window crosses
// as an opaque native window, as PresentSurfaceProvider carries Vulkan's
// surface, so no webgpu.h or windowing type reaches this header.
#pragma once

#include <memory>
#include <string>

#include <toon/backend.hpp>
#include <toon/webgpu_backend.hpp>

namespace Toon {

// The Win32 window the session presents to: its HWND and its module's
// HINSTANCE. The backend owns the surface it makes of them.
struct WebGpuWindow {
  void* hwnd = nullptr;
  void* hinstance = nullptr;
};

// Creates the presentation session on Dawn's adapter of the requested kind,
// one that can present to `window`. Returns nullptr with `status` and
// `error` describing why: a build without the backend, no adapter or no
// surface report Unavailable, real failures Error. The session's frames,
// captures and statistics mean what the Vulkan session's do; its GPU times
// are whole passes (PresentStatistics::gpu_parts_timed).
[[nodiscard]] std::unique_ptr<PresentSession> CreateWebGpuPresentSession(
    const WebGpuWindow& window, const WebGpuShaders& shaders, bool vsync,
    PresentSetupStatus& status, std::string& error, const RenderOptions& options = {},
    WebGpuAdapter adapter = WebGpuAdapter::Direct3D12);

} // namespace Toon
