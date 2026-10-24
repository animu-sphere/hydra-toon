// SPDX-License-Identifier: Apache-2.0
#include "webgpu_internal.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Toon::webgpu_internal {
namespace {

constexpr std::uint64_t kWaitTimeoutNs = 10'000'000'000ULL;

const char* BackendName(WGPUBackendType type) {
  switch (type) {
  case WGPUBackendType_D3D12:
    return "Direct3D 12";
  case WGPUBackendType_Vulkan:
    return "Vulkan";
  default:
    return "other";
  }
}

} // namespace

WGPUStringView View(const char* text) {
  return {text, WGPU_STRLEN};
}

std::string Text(WGPUStringView view) {
  if (view.data == nullptr) return {};
  return view.length == WGPU_STRLEN ? std::string(view.data)
                                    : std::string(view.data, view.length);
}

bool Wait(WGPUInstance instance, WGPUFuture future, const char* what,
    std::string& detail) {
  WGPUFutureWaitInfo info{future, false};
  const WGPUWaitStatus status = wgpuInstanceWaitAny(instance, 1, &info, kWaitTimeoutNs);
  if (status != WGPUWaitStatus_Success || !info.completed) {
    detail = std::string(what) + " did not complete";
    return false;
  }
  return true;
}

FrameStatus CreateGpu(WebGpuAdapter kind, Gpu& gpu, std::string& detail) {
  // Waiting with a timeout is an instance feature.
  const WGPUInstanceFeatureName features[] = {WGPUInstanceFeatureName_TimedWaitAny};
  WGPUInstanceDescriptor instance_descriptor{};
  instance_descriptor.requiredFeatureCount = 1;
  instance_descriptor.requiredFeatures = features;
  gpu.instance.reset(wgpuCreateInstance(&instance_descriptor));
  if (!gpu.instance) {
    detail = "Dawn could not create a WebGPU instance";
    return FrameStatus::Skip;
  }

  // A high-performance adapter on Direct3D 12 by default; Dawn's Vulkan
  // adapter only when asked, for diagnosis (WebGPU policy §2).
  WGPURequestAdapterOptions options{};
  options.featureLevel = WGPUFeatureLevel_Core;
  options.powerPreference = WGPUPowerPreference_HighPerformance;
  options.backendType = kind == WebGpuAdapter::Vulkan ? WGPUBackendType_Vulkan
                                                      : WGPUBackendType_D3D12;
  struct AdapterResult {
    WGPURequestAdapterStatus status = WGPURequestAdapterStatus_Error;
    WGPUAdapter adapter = nullptr;
    std::string message;
  } adapter_result;
  WGPURequestAdapterCallbackInfo adapter_callback{};
  adapter_callback.mode = WGPUCallbackMode_WaitAnyOnly;
  adapter_callback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
                                  WGPUStringView message, void* result, void*) {
    auto& out = *static_cast<AdapterResult*>(result);
    out.status = status;
    out.adapter = adapter;
    out.message = Text(message);
  };
  adapter_callback.userdata1 = &adapter_result;
  if (!Wait(gpu.instance.get(),
          wgpuInstanceRequestAdapter(gpu.instance.get(), &options, adapter_callback),
          "the adapter request", detail)) {
    return FrameStatus::Fail;
  }
  if (adapter_result.status != WGPURequestAdapterStatus_Success ||
      adapter_result.adapter == nullptr) {
    detail = std::string("Dawn offers no ") + BackendName(options.backendType) +
             " adapter" +
             (adapter_result.message.empty() ? "" : ": " + adapter_result.message);
    return FrameStatus::Skip;
  }
  gpu.adapter.reset(adapter_result.adapter);

  WGPUAdapterInfo info{};
  if (wgpuAdapterGetInfo(gpu.adapter.get(), &info) == WGPUStatus_Success) {
    gpu.device_name = Text(info.device);
    if (gpu.device_name.empty()) gpu.device_name = Text(info.description);
    gpu.driver = Text(info.description);
    gpu.backend_name = BackendName(info.backendType);
    gpu.vendor_id = info.vendorID;
    gpu.device_id = info.deviceID;
    wgpuAdapterInfoFreeMembers(info);
  }
  if (gpu.backend_name != BackendName(options.backendType)) {
    detail = std::string("Dawn returned a ") + gpu.backend_name + " adapter, not " +
             BackendName(options.backendType);
    return FrameStatus::Skip;
  }

  // The WebGPU defaults: no limit is raised and no optional feature is
  // required (WebGPU policy §4). Errors and the device's loss reach
  // `events`, which outlives the device.
  WGPUDeviceDescriptor device_descriptor{};
  device_descriptor.label = View("toon-webgpu");
  device_descriptor.uncapturedErrorCallbackInfo.callback =
      [](WGPUDevice const*, WGPUErrorType, WGPUStringView message, void* events, void*) {
        auto& out = *static_cast<DeviceEvents*>(events);
        if (out.errors++ == 0) out.first_error = Text(message);
      };
  device_descriptor.uncapturedErrorCallbackInfo.userdata1 = gpu.events.get();
  device_descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
  device_descriptor.deviceLostCallbackInfo.callback =
      [](WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message,
          void* events, void*) {
        if (reason == WGPUDeviceLostReason_Destroyed ||
            reason == WGPUDeviceLostReason_CallbackCancelled) {
          return;
        }
        auto& out = *static_cast<DeviceEvents*>(events);
        out.lost = true;
        out.lost_reason = Text(message);
      };
  device_descriptor.deviceLostCallbackInfo.userdata1 = gpu.events.get();
  struct DeviceResult {
    WGPURequestDeviceStatus status = WGPURequestDeviceStatus_Error;
    WGPUDevice device = nullptr;
    std::string message;
  } device_result;
  WGPURequestDeviceCallbackInfo device_callback{};
  device_callback.mode = WGPUCallbackMode_WaitAnyOnly;
  device_callback.callback = [](WGPURequestDeviceStatus status, WGPUDevice device,
                                 WGPUStringView message, void* result, void*) {
    auto& out = *static_cast<DeviceResult*>(result);
    out.status = status;
    out.device = device;
    out.message = Text(message);
  };
  device_callback.userdata1 = &device_result;
  if (!Wait(gpu.instance.get(),
          wgpuAdapterRequestDevice(gpu.adapter.get(), &device_descriptor, device_callback),
          "the device request", detail)) {
    return FrameStatus::Fail;
  }
  if (device_result.status != WGPURequestDeviceStatus_Success ||
      device_result.device == nullptr) {
    detail = "Dawn could not create a device on " + gpu.device_name + ": " +
             device_result.message;
    return FrameStatus::Fail;
  }
  gpu.device.reset(device_result.device);
  gpu.queue.reset(wgpuDeviceGetQueue(gpu.device.get()));
  return FrameStatus::Pass;
}

bool WaitForQueue(const Gpu& gpu, std::string& detail) {
  WGPUQueueWorkDoneStatus result = WGPUQueueWorkDoneStatus_Error;
  WGPUQueueWorkDoneCallbackInfo callback{};
  callback.mode = WGPUCallbackMode_WaitAnyOnly;
  callback.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView, void* out, void*) {
    *static_cast<WGPUQueueWorkDoneStatus*>(out) = status;
  };
  callback.userdata1 = &result;
  if (!Wait(gpu.instance.get(), wgpuQueueOnSubmittedWorkDone(gpu.queue.get(), callback),
          "the queue's work", detail)) {
    return false;
  }
  if (result != WGPUQueueWorkDoneStatus_Success) {
    detail = gpu.events->lost ? "the WebGPU device was lost: " + gpu.events->lost_reason
                              : "the queue's work did not complete";
    return false;
  }
  return true;
}

bool MapForRead(const Gpu& gpu, WGPUBuffer buffer, std::uint64_t size,
    std::string& detail) {
  struct MapResult {
    WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
    std::string message;
  } result;
  WGPUBufferMapCallbackInfo callback{};
  callback.mode = WGPUCallbackMode_WaitAnyOnly;
  callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView message, void* out, void*) {
    auto& map = *static_cast<MapResult*>(out);
    map.status = status;
    map.message = Text(message);
  };
  callback.userdata1 = &result;
  if (!Wait(gpu.instance.get(),
          wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, static_cast<size_t>(size), callback),
          "the readback map", detail)) {
    return false;
  }
  if (result.status != WGPUMapAsyncStatus_Success) {
    detail = "mapping a readback buffer failed: " + result.message;
    return false;
  }
  return true;
}

Buffer CreateBuffer(WGPUDevice device, std::uint64_t size, WGPUBufferUsage usage,
    const char* label) {
  WGPUBufferDescriptor descriptor{};
  descriptor.label = View(label);
  descriptor.usage = usage;
  // Every buffer is a multiple of four bytes, as queue writes need.
  descriptor.size = (std::max<std::uint64_t>(size, 4) + 3U) & ~std::uint64_t{3};
  return Buffer(wgpuDeviceCreateBuffer(device, &descriptor));
}

bool LoadWgsl(const std::string& path, std::string& source, std::string& detail) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    detail = "cannot read WGSL: " + path;
    return false;
  }
  std::ostringstream text;
  text << input.rdbuf();
  source = text.str();
  if (source.empty()) {
    detail = "empty WGSL: " + path;
    return false;
  }
  return true;
}

bool LoadShaders(const WebGpuShaders& shaders, ShaderSources& sources,
    std::string& detail) {
  return LoadWgsl(shaders.mesh_vertex, sources.mesh_vertex, detail) &&
         LoadWgsl(shaders.mesh_fragment, sources.mesh_fragment, detail) &&
         LoadWgsl(shaders.preview_vertex, sources.preview_vertex, detail) &&
         LoadWgsl(shaders.preview_fragment, sources.preview_fragment, detail) &&
         LoadWgsl(shaders.mipmap_vertex, sources.mipmap_vertex, detail) &&
         LoadWgsl(shaders.mipmap_fragment, sources.mipmap_fragment, detail);
}

Matrix4 WebGpuClipFromWorld(const ToonView& view) {
  Matrix4 depth;
  depth.m[10] = 0.5F;
  depth.m[14] = 0.5F;
  return Multiply(depth, Multiply(view.projection, view.view));
}

} // namespace Toon::webgpu_internal
