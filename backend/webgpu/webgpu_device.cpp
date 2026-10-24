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

FrameStatus CreateGpu(WebGpuAdapter kind, Gpu& gpu, std::string& detail,
    const GpuRequest& request) {
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
  if (request.hwnd != nullptr) {
    WGPUSurfaceSourceWindowsHWND window = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
    window.hwnd = request.hwnd;
    window.hinstance = request.hinstance;
    WGPUSurfaceDescriptor surface = WGPU_SURFACE_DESCRIPTOR_INIT;
    surface.nextInChain = &window.chain;
    surface.label = View("window");
    gpu.surface.reset(wgpuInstanceCreateSurface(gpu.instance.get(), &surface));
    if (!gpu.surface) {
      detail = "Dawn could not create a surface for the window";
      return FrameStatus::Skip;
    }
  }

  // A high-performance adapter on Direct3D 12 by default; Dawn's Vulkan
  // adapter only when asked, for diagnosis (WebGPU policy §2).
  WGPURequestAdapterOptions options{};
  options.featureLevel = WGPUFeatureLevel_Core;
  options.powerPreference = WGPUPowerPreference_HighPerformance;
  options.backendType = kind == WebGpuAdapter::Vulkan ? WGPUBackendType_Vulkan
                                                      : WGPUBackendType_D3D12;
  options.compatibleSurface = gpu.surface.get();
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
  // required (WebGPU policy §4) but timestamps, where a host asks and the
  // adapter offers them (§6). Errors and the device's loss reach `events`,
  // which outlives the device.
  std::vector<WGPUFeatureName> optional;
  if (request.timestamps && wgpuAdapterHasFeature(gpu.adapter.get(), WGPUFeatureName_TimestampQuery)) {
    optional.push_back(WGPUFeatureName_TimestampQuery);
    if (wgpuAdapterHasFeature(gpu.adapter.get(),
            WGPUFeatureName_ChromiumExperimentalTimestampQueryInsidePasses)) {
      optional.push_back(WGPUFeatureName_ChromiumExperimentalTimestampQueryInsidePasses);
    }
  }
  WGPUDeviceDescriptor device_descriptor{};
  device_descriptor.label = View("toon-webgpu");
  device_descriptor.requiredFeatureCount = optional.size();
  device_descriptor.requiredFeatures = optional.data();
  // Dawn rounds timestamps to 2^16 ns by default, a browser's guard against
  // timing attacks, which reads a pass shorter than 0.066 ms as 0 or 0.066;
  // a native renderer's own passes are not that (renderer report 93).
  const char* const unquantized[] = {"timestamp_quantization"};
  WGPUDawnTogglesDescriptor toggles = WGPU_DAWN_TOGGLES_DESCRIPTOR_INIT;
  toggles.disabledToggleCount = 1;
  toggles.disabledToggles = unquantized;
  if (!optional.empty()) device_descriptor.nextInChain = &toggles.chain;
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
  gpu.timestamps = wgpuDeviceHasFeature(gpu.device.get(), WGPUFeatureName_TimestampQuery);
  gpu.timestamps_inside_passes = wgpuDeviceHasFeature(gpu.device.get(),
      WGPUFeatureName_ChromiumExperimentalTimestampQueryInsidePasses);
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
  const std::pair<const std::string*, std::string*> stages[] = {
      {&shaders.mesh_vertex, &sources.mesh_vertex},
      {&shaders.mesh_fragment, &sources.mesh_fragment},
      {&shaders.preview_vertex, &sources.preview_vertex},
      {&shaders.preview_fragment, &sources.preview_fragment},
      {&shaders.mtoon_vertex, &sources.mtoon_vertex},
      {&shaders.mtoon_fragment, &sources.mtoon_fragment},
      {&shaders.mtoon_transparent_vertex, &sources.mtoon_transparent_vertex},
      {&shaders.mtoon_transparent_fragment, &sources.mtoon_transparent_fragment},
      {&shaders.mtoon_outline_vertex, &sources.mtoon_outline_vertex},
      {&shaders.mtoon_outline_fragment, &sources.mtoon_outline_fragment},
      {&shaders.mtoon_coverage_vertex, &sources.mtoon_coverage_vertex},
      {&shaders.mtoon_coverage_fragment, &sources.mtoon_coverage_fragment},
      {&shaders.mmd_vertex, &sources.mmd_vertex},
      {&shaders.mmd_fragment, &sources.mmd_fragment},
      {&shaders.mmd_transparent_vertex, &sources.mmd_transparent_vertex},
      {&shaders.mmd_transparent_fragment, &sources.mmd_transparent_fragment},
      {&shaders.mmd_outline_vertex, &sources.mmd_outline_vertex},
      {&shaders.mmd_outline_fragment, &sources.mmd_outline_fragment},
      {&shaders.shadow_vertex, &sources.shadow_vertex},
      {&shaders.shadow_fragment, &sources.shadow_fragment},
      {&shaders.composite_vertex, &sources.composite_vertex},
      {&shaders.composite_fragment, &sources.composite_fragment},
      {&shaders.overlay_vertex, &sources.overlay_vertex},
      {&shaders.overlay_fragment, &sources.overlay_fragment},
      {&shaders.mipmap_vertex, &sources.mipmap_vertex},
      {&shaders.mipmap_fragment, &sources.mipmap_fragment},
      {&shaders.depth_resolve_vertex, &sources.depth_resolve_vertex},
      {&shaders.depth_resolve_fragment, &sources.depth_resolve_fragment},
  };
  for (const auto& [path, source] : stages) {
    if (!LoadWgsl(*path, *source, detail)) return false;
  }
  return true;
}

ShaderModule CreateShader(WGPUDevice device, const std::string& source,
    const char* label) {
  WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
  wgsl.code = {source.data(), source.size()};
  WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  descriptor.nextInChain = &wgsl.chain;
  descriptor.label = View(label);
  return ShaderModule(wgpuDeviceCreateShaderModule(device, &descriptor));
}

BindGroupLayout CreateLayout(WGPUDevice device,
    const std::vector<WGPUBindGroupLayoutEntry>& entries, const char* label) {
  WGPUBindGroupLayoutDescriptor descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
  descriptor.label = View(label);
  descriptor.entryCount = entries.size();
  descriptor.entries = entries.data();
  return BindGroupLayout(wgpuDeviceCreateBindGroupLayout(device, &descriptor));
}

WGPUBindGroupLayoutEntry BufferEntry(std::uint32_t binding, WGPUShaderStage stages,
    WGPUBufferBindingType type, bool dynamic) {
  WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
  entry.binding = binding;
  entry.visibility = stages;
  entry.buffer.type = type;
  entry.buffer.hasDynamicOffset = dynamic;
  return entry;
}

WGPUBindGroupLayoutEntry TextureEntry(std::uint32_t binding, WGPUShaderStage stages,
    WGPUTextureSampleType type, WGPUTextureViewDimension dimension) {
  WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
  entry.binding = binding;
  entry.visibility = stages;
  entry.texture.sampleType = type;
  entry.texture.viewDimension = dimension;
  return entry;
}

WGPUBindGroupLayoutEntry SamplerEntry(std::uint32_t binding, WGPUShaderStage stages,
    WGPUSamplerBindingType type) {
  WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
  entry.binding = binding;
  entry.visibility = stages;
  entry.sampler.type = type;
  return entry;
}

BindGroup CreateGroup(WGPUDevice device, WGPUBindGroupLayout layout,
    const std::vector<WGPUBindGroupEntry>& entries, const char* label) {
  WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
  descriptor.label = View(label);
  descriptor.layout = layout;
  descriptor.entryCount = entries.size();
  descriptor.entries = entries.data();
  return BindGroup(wgpuDeviceCreateBindGroup(device, &descriptor));
}

WGPUBindGroupEntry BufferBinding(std::uint32_t binding, WGPUBuffer buffer,
    std::uint64_t size) {
  WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
  entry.binding = binding;
  entry.buffer = buffer;
  entry.size = size;
  return entry;
}

WGPUBindGroupEntry ViewBinding(std::uint32_t binding, WGPUTextureView view) {
  WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
  entry.binding = binding;
  entry.textureView = view;
  return entry;
}

WGPUBindGroupEntry SamplerBinding(std::uint32_t binding, WGPUSampler sampler) {
  WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
  entry.binding = binding;
  entry.sampler = sampler;
  return entry;
}

PipelineLayout CreatePipelineLayout(WGPUDevice device,
    const std::vector<WGPUBindGroupLayout>& layouts, const char* label) {
  WGPUPipelineLayoutDescriptor descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
  descriptor.label = View(label);
  descriptor.bindGroupLayoutCount = layouts.size();
  descriptor.bindGroupLayouts = layouts.data();
  return PipelineLayout(wgpuDeviceCreatePipelineLayout(device, &descriptor));
}

const WGPUPassTimestampWrites* PassTimestamps::Of(std::size_t index, std::size_t count,
    WGPUPassTimestampWrites& writes) const {
  if (set == nullptr || (index != 0 && index + 1U != count)) return nullptr;
  writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
  writes.querySet = set;
  writes.beginningOfPassWriteIndex = index == 0 ? begin : WGPU_QUERY_SET_INDEX_UNDEFINED;
  writes.endOfPassWriteIndex = index + 1U == count ? end : WGPU_QUERY_SET_INDEX_UNDEFINED;
  return &writes;
}

Matrix4 WebGpuClipFromWorld(const ToonView& view) {
  Matrix4 depth;
  depth.m[10] = 0.5F;
  depth.m[14] = 0.5F;
  return Multiply(depth, Multiply(view.projection, view.view));
}

Matrix4 WebGpuShadowClipFromWorld(const ShadowView& view) {
  Matrix4 flip;
  flip.m[5] = -1.0F;
  return Multiply(flip, view.clip_from_world);
}

} // namespace Toon::webgpu_internal
