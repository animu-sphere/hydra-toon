// SPDX-License-Identifier: Apache-2.0
#include <toon/webgpu_backend.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <sstream>

#include "webgpu_internal.hpp"

namespace Toon {

WebGpuShaders WebGpuShadersIn(const std::string& directory) {
  const std::filesystem::path root(directory);
  return {(root / "mesh.vert.wgsl").string(), (root / "mesh.frag.wgsl").string(),
      (root / "preview_surface.vert.wgsl").string(),
      (root / "preview_surface.frag.wgsl").string(),
      (root / "mipmap.vert.wgsl").string(), (root / "mipmap.frag.wgsl").string()};
}

BackendCapability ProbeWebGpuBackend(WebGpuAdapter adapter) {
  webgpu_internal::Gpu gpu;
  std::string detail;
  const FrameStatus status = webgpu_internal::CreateGpu(adapter, gpu, detail);
  if (status != FrameStatus::Pass) return {false, detail};
  return {true, "Dawn " + gpu.backend_name + " adapter: " + gpu.device_name};
}

namespace {

using namespace webgpu_internal;

// Rows of a texture-to-buffer copy are a multiple of 256 bytes.
std::uint32_t CopyPitch(std::uint32_t width) {
  return (width * 4U + 255U) & ~255U;
}

class WebGpuOffscreenRenderer final : public OffscreenRenderer {
public:
  FrameStatus Initialize(const WebGpuShaders& shaders, const RenderOptions& options,
      WebGpuAdapter adapter, std::string& detail);

  [[nodiscard]] bool Render(const DrawList& draws, std::uint32_t width,
      std::uint32_t height, ColorProduct& color, DepthProduct& depth,
      std::string& error) override;
  [[nodiscard]] const OffscreenStatistics& statistics() const override {
    return statistics_;
  }
  void SetLateFrameSource(LateFrameSource source) override { late_.source = source; }
  bool SetBackground(const BackgroundSettings& settings, std::string& error) override;

private:
  bool EnsureTargets(std::uint32_t width, std::uint32_t height, std::string& error);
  bool EnsureRing(std::size_t draws, std::string& error);
  bool DeviceHealthy(std::string& error) const;

  Gpu gpu_;
  ScenePipelines pipelines_;
  TextureCache textures_;
  MaterialCache materials_;
  MeshCache meshes_;
  LateFrameState late_;
  BackgroundSettings background_;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  Texture color_;
  TextureView color_view_;
  Texture depth_;
  TextureView depth_view_;
  Buffer color_readback_;
  Buffer depth_readback_;
  // The draw constants of a frame, a draw's at kDrawStride times its index,
  // written with one queue write before the frame is submitted.
  Buffer ring_;
  std::uint64_t ring_capacity_ = 0;
  BindGroup ring_group_;
  std::vector<std::uint8_t> constants_;
  std::uint64_t submitted_ = 0;
  OffscreenStatistics statistics_;
};

FrameStatus WebGpuOffscreenRenderer::Initialize(const WebGpuShaders& shaders,
    const RenderOptions& options, WebGpuAdapter adapter, std::string& detail) {
  ShaderSources sources;
  if (!LoadShaders(shaders, sources, detail)) return FrameStatus::Fail;
  const FrameStatus status = CreateGpu(adapter, gpu_, detail);
  if (status != FrameStatus::Pass) return status;
  // Multisampling, with the sample-mask Mask coverage it needs (WebGPU
  // policy §7), arrives with stage 2; until then every request renders at
  // one sample.
  (void)options;
  const std::uint32_t samples = 1;
  if (!pipelines_.Initialize(gpu_.device.get(), sources, samples, detail) ||
      !textures_.Initialize(gpu_, sources, detail) ||
      !materials_.Initialize(gpu_, pipelines_, textures_, detail) ||
      !meshes_.Initialize(gpu_, pipelines_, detail)) {
    return FrameStatus::Fail;
  }
  statistics_.samples = samples;
  statistics_.device_name = gpu_.device_name;
  statistics_.api_version = "WebGPU (Dawn, " + gpu_.backend_name + ")";
  statistics_.driver_version = gpu_.driver;
  statistics_.vendor_id = gpu_.vendor_id;
  statistics_.device_id = gpu_.device_id;
  // WebGPU validates every call; Dawn's errors are this backend's
  // validation messages.
  statistics_.validation_available = true;
  return DeviceHealthy(detail) ? FrameStatus::Pass : FrameStatus::Fail;
}

bool WebGpuOffscreenRenderer::DeviceHealthy(std::string& error) const {
  if (gpu_.events->lost) {
    error = "the WebGPU device was lost: " + gpu_.events->lost_reason;
    return false;
  }
  return true;
}

bool WebGpuOffscreenRenderer::SetBackground(const BackgroundSettings& settings,
    std::string& error) {
  const auto valid_channel = [](float value) {
    return std::isfinite(value) && value >= 0 && value <= 1;
  };
  if ((settings.mode != BackgroundMode::Opaque &&
          settings.mode != BackgroundMode::Transparent) ||
      !valid_channel(settings.color.x) || !valid_channel(settings.color.y) ||
      !valid_channel(settings.color.z)) {
    error = "background requires Opaque or Transparent and finite linear RGB in [0,1]";
    return false;
  }
  background_ = settings;
  error.clear();
  return true;
}

bool WebGpuOffscreenRenderer::EnsureTargets(std::uint32_t width, std::uint32_t height,
    std::string& error) {
  if (width == width_ && height == height_ && color_) return true;
  WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
  descriptor.size = {width, height, 1};
  descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
  descriptor.label = View("color");
  descriptor.format = kColorFormat;
  color_.reset(wgpuDeviceCreateTexture(gpu_.device.get(), &descriptor));
  descriptor.label = View("depth");
  descriptor.format = kDepthFormat;
  depth_.reset(wgpuDeviceCreateTexture(gpu_.device.get(), &descriptor));
  const std::uint64_t bytes = static_cast<std::uint64_t>(CopyPitch(width)) * height;
  color_readback_ = CreateBuffer(gpu_.device.get(), bytes,
      WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst, "color readback");
  depth_readback_ = CreateBuffer(gpu_.device.get(), bytes,
      WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst, "depth readback");
  if (!color_ || !depth_ || !color_readback_ || !depth_readback_) {
    error = "allocating the WebGPU render targets failed";
    width_ = height_ = 0;
    return false;
  }
  color_view_.reset(wgpuTextureCreateView(color_.get(), nullptr));
  depth_view_.reset(wgpuTextureCreateView(depth_.get(), nullptr));
  width_ = width;
  height_ = height;
  ++statistics_.target_allocations;
  return true;
}

bool WebGpuOffscreenRenderer::EnsureRing(std::size_t draws, std::string& error) {
  const std::uint64_t needed = std::max<std::uint64_t>(draws, 1) * kDrawStride;
  if (ring_ && ring_capacity_ >= needed) return true;
  std::uint64_t capacity = std::max<std::uint64_t>(ring_capacity_, 64U * kDrawStride);
  while (capacity < needed) capacity *= 2U;
  ring_ = CreateBuffer(gpu_.device.get(), capacity,
      WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "draw constants");
  if (!ring_) {
    error = "allocating the WebGPU draw constants failed";
    return false;
  }
  WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
  entry.binding = 0;
  entry.buffer = ring_.get();
  entry.size = kDrawBindingSize;
  WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
  descriptor.label = View("draw");
  descriptor.layout = pipelines_.draw_layout();
  descriptor.entryCount = 1;
  descriptor.entries = &entry;
  ring_group_.reset(wgpuDeviceCreateBindGroup(gpu_.device.get(), &descriptor));
  ring_capacity_ = capacity;
  if (!ring_group_) {
    error = "creating the WebGPU draw bind group failed";
    return false;
  }
  return true;
}

bool WebGpuOffscreenRenderer::Render(const DrawList& extracted, std::uint32_t width,
    std::uint32_t height, ColorProduct& color, DepthProduct& depth, std::string& error) {
  if (width == 0 || height == 0) {
    error = "the render extent must be non-zero";
    return false;
  }
  if (!DeviceHealthy(error)) return false;
  // One frame in flight: Render returns after its readback, so the previous
  // frame has completed before anything it read is touched.
  const double pose_before = meshes_.pose_write_ms();
  const double morph_before = meshes_.morph_write_ms();
  const double material_before = materials_.parameter_write_ms();
  if (!EnsureTargets(width, height, error) || !textures_.Update(extracted, error) ||
      (late_.enabled() && (!meshes_.Update(extracted, error, false) ||
                              !materials_.Update(extracted, textures_, error, false)))) {
    return false;
  }
  const DrawList* frame = nullptr;
  if (!late_.Read(extracted, frame, statistics_, error)) return false;
  const DrawList& draws = *frame;
  if (!meshes_.Update(draws, error) || !materials_.Update(draws, textures_, error) ||
      !MeshCache::Supported(draws, materials_, error) ||
      !EnsureRing(MeshCache::DrawBound(draws), error)) {
    return false;
  }
  statistics_.latency.buffers_written = SteadyNanoseconds();
  statistics_.latency.pose_write_ms = meshes_.pose_write_ms() - pose_before;
  statistics_.latency.morph_write_ms = meshes_.morph_write_ms() - morph_before;
  statistics_.latency.material_write_ms = materials_.parameter_write_ms() - material_before;

  WGPUCommandEncoderDescriptor encoder_descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
  CommandEncoder encoder(
      wgpuDeviceCreateCommandEncoder(gpu_.device.get(), &encoder_descriptor));
  textures_.RecordMipmaps(encoder.get());

  WGPURenderPassColorAttachment color_attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
  color_attachment.view = color_view_.get();
  color_attachment.loadOp = WGPULoadOp_Clear;
  color_attachment.storeOp = WGPUStoreOp_Store;
  if (background_.mode == BackgroundMode::Opaque) {
    color_attachment.clearValue = {background_.color.x, background_.color.y,
        background_.color.z, 1.0};
  } else {
    color_attachment.clearValue = {0.0, 0.0, 0.0, 0.0};
  }
  WGPURenderPassDepthStencilAttachment depth_attachment =
      WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
  depth_attachment.view = depth_view_.get();
  depth_attachment.depthLoadOp = WGPULoadOp_Clear;
  depth_attachment.depthStoreOp = WGPUStoreOp_Store;
  depth_attachment.depthClearValue = 1.0F;
  WGPURenderPassDescriptor pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
  pass_descriptor.colorAttachmentCount = 1;
  pass_descriptor.colorAttachments = &color_attachment;
  pass_descriptor.depthStencilAttachment = &depth_attachment;
  RenderPassEncoder pass(wgpuCommandEncoderBeginRenderPass(encoder.get(), &pass_descriptor));
  constants_.clear();
  SceneRecord record;
  const bool recorded = meshes_.Record(pass.get(), pipelines_, materials_, draws,
      ring_group_.get(), constants_, record, error);
  wgpuRenderPassEncoderEnd(pass.get());
  if (!recorded) return false;
  statistics_.outline_draws = 0;
  statistics_.pipelines_created = pipelines_.created();

  WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
  destination.layout.bytesPerRow = CopyPitch(width_);
  destination.layout.rowsPerImage = height_;
  const WGPUExtent3D extent{width_, height_, 1};
  source.texture = color_.get();
  destination.buffer = color_readback_.get();
  wgpuCommandEncoderCopyTextureToBuffer(encoder.get(), &source, &destination, &extent);
  source.texture = depth_.get();
  source.aspect = WGPUTextureAspect_DepthOnly;
  destination.buffer = depth_readback_.get();
  wgpuCommandEncoderCopyTextureToBuffer(encoder.get(), &source, &destination, &extent);
  WGPUCommandBufferDescriptor command_descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
  CommandBuffer commands(wgpuCommandEncoderFinish(encoder.get(), &command_descriptor));

  // The draw constants are the frame's last value written, once, before it
  // is submitted (WebGPU policy §6).
  if (!constants_.empty()) {
    wgpuQueueWriteBuffer(gpu_.queue.get(), ring_.get(), 0, constants_.data(),
        constants_.size());
  }
  const WGPUCommandBuffer submitted = commands.get();
  wgpuQueueSubmit(gpu_.queue.get(), 1, &submitted);
  ++submitted_;
  statistics_.latency.frame = submitted_;
  statistics_.latency.submitted = SteadyNanoseconds();

  // The caller wants this frame's pixels, so the readback waits here.
  const std::uint64_t bytes = static_cast<std::uint64_t>(CopyPitch(width_)) * height_;
  if (!WaitForQueue(gpu_, error) || !MapForRead(gpu_, color_readback_.get(), bytes, error) ||
      !MapForRead(gpu_, depth_readback_.get(), bytes, error)) {
    // Neither stays mapped for the next frame's copy.
    wgpuBufferUnmap(color_readback_.get());
    wgpuBufferUnmap(depth_readback_.get());
    if (gpu_.events->errors != 0) error += ": " + gpu_.events->first_error;
    return false;
  }
  const auto* color_rows = static_cast<const std::uint8_t*>(
      wgpuBufferGetConstMappedRange(color_readback_.get(), 0, static_cast<std::size_t>(bytes)));
  const auto* depth_rows = static_cast<const std::uint8_t*>(
      wgpuBufferGetConstMappedRange(depth_readback_.get(), 0, static_cast<std::size_t>(bytes)));
  color.width = width_;
  color.height = height_;
  color.row_pitch = width_ * 4U;
  color.pixel_format = "rgba8-unorm";
  color.origin = "top-left";
  color.color_space = "linear";
  color.transfer_function = "linear";
  color.alpha_convention = "premultiplied";
  color.payload.resize(static_cast<std::size_t>(width_) * height_ * 4U);
  depth.width = width_;
  depth.height = height_;
  depth.row_pitch = width_ * static_cast<std::uint32_t>(sizeof(float));
  depth.pixel_format = "d32-sfloat";
  depth.origin = "top-left";
  depth.payload.resize(static_cast<std::size_t>(width_) * height_);
  if (color_rows == nullptr || depth_rows == nullptr) {
    wgpuBufferUnmap(color_readback_.get());
    wgpuBufferUnmap(depth_readback_.get());
    error = "reading the WebGPU readback buffers failed";
    return false;
  }
  // Rows repacked from the copy's 256-byte pitch to the products' (WebGPU
  // policy §6); row 0 is the top in both.
  const std::size_t pitch = CopyPitch(width_);
  for (std::uint32_t row = 0; row < height_; ++row) {
    std::memcpy(color.payload.data() + static_cast<std::size_t>(row) * color.row_pitch,
        color_rows + row * pitch, color.row_pitch);
    std::memcpy(depth.payload.data() + static_cast<std::size_t>(row) * width_,
        depth_rows + row * pitch, depth.row_pitch);
  }
  wgpuBufferUnmap(color_readback_.get());
  wgpuBufferUnmap(depth_readback_.get());

  ++statistics_.frames_rendered;
  statistics_.completion = submitted_;
  statistics_.topology_uploads = meshes_.topology_uploads();
  statistics_.point_uploads = meshes_.point_uploads();
  statistics_.material_writes = materials_.writes();
  statistics_.texture_uploads = textures_.uploads();
  statistics_.skin_uploads = meshes_.skin_uploads();
  statistics_.pose_writes = meshes_.pose_writes();
  statistics_.morph_uploads = meshes_.morph_uploads();
  statistics_.morph_weight_writes = meshes_.morph_weight_writes();
  statistics_.validation_message_count = gpu_.events->errors;
  if (!gpu_.events->first_error.empty()) {
    statistics_.validation_detail = gpu_.events->first_error;
  }
  return DeviceHealthy(error);
}

} // namespace

std::unique_ptr<OffscreenRenderer> CreateWebGpuOffscreenRenderer(
    const WebGpuShaders& shaders, FrameStatus& status, std::string& detail,
    const RenderOptions& options, WebGpuAdapter adapter) {
  auto renderer = std::make_unique<WebGpuOffscreenRenderer>();
  status = renderer->Initialize(shaders, options, adapter, detail);
  if (status != FrameStatus::Pass) return nullptr;
  return renderer;
}

GpuFrameEvidence RenderWebGpuOffscreen(const DrawList& draws,
    const WebGpuShaders& shaders, std::uint32_t frame_count, WebGpuAdapter adapter) {
  GpuFrameEvidence evidence;
  if (frame_count == 0) {
    evidence.status = FrameStatus::Fail;
    evidence.detail = "frame_count must be at least 1";
    return evidence;
  }
  auto renderer = CreateWebGpuOffscreenRenderer(shaders, evidence.status,
      evidence.detail, {}, adapter);
  if (renderer == nullptr) return evidence;
  for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
    if (!renderer->Render(draws, 64, 64, evidence.color, evidence.depth, evidence.detail)) {
      evidence.status = FrameStatus::Fail;
      evidence.statistics = renderer->statistics();
      return evidence;
    }
  }
  evidence.statistics = renderer->statistics();
  std::ostringstream success;
  success << "rendered " << frame_count << " frames of " << draws.draws.size()
          << " draw(s) on " << evidence.statistics.device_name << " through "
          << evidence.statistics.api_version;
  evidence.detail = success.str();
  return evidence;
}

} // namespace Toon
