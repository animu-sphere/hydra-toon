// SPDX-License-Identifier: Apache-2.0
// Presentation of a scene's DrawList through a WebGPU surface (WebGPU policy
// §8): the scene into a reusable linear premultiplied RGBA8 image, drawn as
// the offscreen renderer draws it, composited over the host's background
// into the surface's sRGB view, then the host's overlay. The backend owns
// the surface; the window layer supplies only the native window. One frame
// in flight, waited for on its completion future (§6). GPU times come from
// the timestamps each pass writes as it begins and ends, read once their
// frame has completed.
#include <toon/webgpu_present.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <unordered_map>

#include "webgpu_internal.hpp"

namespace Toon {
namespace {

using namespace webgpu_internal;

// Rows of a texture-to-buffer copy are a multiple of 256 bytes.
std::uint32_t CopyPitch(std::uint32_t width) {
  return (width * 4U + 255U) & ~255U;
}

// The timestamps a frame writes, as each run of passes begins and ends; a
// part of PresentGpuTimes lies between two of them. A run the frame does
// not record writes none, which resolve as 0.
enum Timestamp : std::uint32_t {
  kUploadsBegin,
  kUploadsEnd,
  kShadowBegin,
  kShadowEnd,
  kSceneBegin,
  kSceneEnd,
  kCompositeBegin,
  kCompositeEnd,
  kOverlayBegin,
  kOverlayEnd,
  kTimestampCount,
};

// Must match CompositeConstants in shaders/present/composite.slang.
struct CompositeConstants {
  float color[4];
  float alternate[4];
};

// Must match OverlayConstants in shaders/present/overlay.slang.
struct OverlayConstants {
  float scale[2];
  float translate[2];
  std::uint32_t flags;
  std::uint32_t padding[3];
};
constexpr std::uint32_t kOverlayLinearize = 1;
constexpr std::uint32_t kOverlayDepth = 2;
// A frame's two constant blocks, an overlay texture's and a shadow view's,
// at dynamic offsets this far apart.
constexpr std::uint32_t kOverlayConstantsStride = 256;

bool IsSrgb(WGPUTextureFormat format) {
  return format == WGPUTextureFormat_BGRA8UnormSrgb || format == WGPUTextureFormat_RGBA8UnormSrgb;
}

// The fullscreen source-over of the foreground on the host's background
// into the surface's view, which encodes it to sRGB as it is written.
class Compositor {
public:
  bool Initialize(WGPUDevice device, WGPUQueue queue, const ShaderSources& sources,
      WGPUTextureFormat format, std::string& detail) {
    device_ = device;
    queue_ = queue;
    vertex_ = CreateShader(device, sources.composite_vertex, "composite.vert");
    fragment_ = CreateShader(device, sources.composite_fragment, "composite.frag");
    layout_ = CreateLayout(device,
        {TextureEntry(0, WGPUShaderStage_Fragment, WGPUTextureSampleType_Float,
             WGPUTextureViewDimension_2D),
            BufferEntry(1, WGPUShaderStage_Fragment, WGPUBufferBindingType_Uniform)},
        "composite");
    pipeline_layout_ = CreatePipelineLayout(device, {layout_.get()}, "composite");
    constants_ = CreateBuffer(device, sizeof(CompositeConstants),
        WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "composite");
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = format;
    target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragment_.get();
    fragment.entryPoint = View("fragment_main");
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = View("composite");
    descriptor.layout = pipeline_layout_.get();
    descriptor.vertex.module = vertex_.get();
    descriptor.vertex.entryPoint = View("vertex_main");
    descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    descriptor.fragment = &fragment;
    if (vertex_ && fragment_ && layout_ && pipeline_layout_ && constants_) {
      pipeline_.reset(wgpuDeviceCreateRenderPipeline(device, &descriptor));
    }
    if (!pipeline_) {
      detail = "creating the WebGPU composite pass failed";
      return false;
    }
    return true;
  }

  // The foreground it reads from the next frame on.
  bool SetForeground(WGPUTextureView view, std::string& detail) {
    group_ = CreateGroup(device_, layout_.get(),
        {ViewBinding(0, view), BufferBinding(1, constants_.get())}, "composite");
    if (!group_) {
      detail = "creating the WebGPU composite bind group failed";
      return false;
    }
    return true;
  }

  void Record(WGPUCommandEncoder encoder, WGPUTextureView target,
      const CompositeBackground& background, const PassTimestamps& timestamps) {
    const CompositeConstants constants{{background.color.x, background.color.y,
                                           background.color.z,
                                           static_cast<float>(background.checker_size)},
        {background.alternate.x, background.alternate.y, background.alternate.z,
            background.checkerboard ? 1.0F : 0.0F}};
    wgpuQueueWriteBuffer(queue_, constants_.get(), 0, &constants, sizeof(constants));
    // Every pixel is written, so nothing is loaded.
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = target;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    descriptor.label = View("composite");
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    descriptor.timestampWrites = timestamps.Of(0, 1, writes);
    RenderPassEncoder pass(wgpuCommandEncoderBeginRenderPass(encoder, &descriptor));
    wgpuRenderPassEncoderSetPipeline(pass.get(), pipeline_.get());
    wgpuRenderPassEncoderSetBindGroup(pass.get(), 0, group_.get(), 0, nullptr);
    wgpuRenderPassEncoderDraw(pass.get(), 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass.get());
  }

private:
  WGPUDevice device_ = nullptr;
  WGPUQueue queue_ = nullptr;
  ShaderModule vertex_;
  ShaderModule fragment_;
  BindGroupLayout layout_;
  PipelineLayout pipeline_layout_;
  RenderPipeline pipeline_;
  Buffer constants_;
  BindGroup group_;
};

// The host's OverlayDrawList, as the Vulkan backend's OverlayRenderer draws
// it: textured, vertex-coloured triangles in framebuffer pixels, blended
// over the presented image, each cut to its clip rectangle. An overlay
// texture is filtered; a shadow view's depth, which WGSL reads only as an
// unfilterable float, binds through a layout and pipeline of its own.
class OverlayRenderer {
public:
  bool Initialize(WGPUDevice device, WGPUQueue queue, const ShaderSources& sources,
      WGPUTextureFormat format, bool linearize, std::string& detail) {
    device_ = device;
    queue_ = queue;
    linearize_ = linearize;
    vertex_ = CreateShader(device, sources.overlay_vertex, "overlay.vert");
    fragment_ = CreateShader(device, sources.overlay_fragment, "overlay.frag");
    texture_layout_ = CreateLayout(device,
        {TextureEntry(0, WGPUShaderStage_Fragment, WGPUTextureSampleType_Float,
             WGPUTextureViewDimension_2D),
            SamplerEntry(1, WGPUShaderStage_Fragment, WGPUSamplerBindingType_Filtering)},
        "overlay texture");
    depth_layout_ = CreateLayout(device,
        {TextureEntry(0, WGPUShaderStage_Fragment, WGPUTextureSampleType_UnfilterableFloat,
             WGPUTextureViewDimension_2D),
            SamplerEntry(1, WGPUShaderStage_Fragment, WGPUSamplerBindingType_NonFiltering)},
        "overlay depth");
    constants_layout_ = CreateLayout(device,
        {BufferEntry(0, kBothStages, WGPUBufferBindingType_Uniform, true)}, "overlay constants");
    WGPUSamplerDescriptor sampler = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler.addressModeU = WGPUAddressMode_ClampToEdge;
    sampler.addressModeV = WGPUAddressMode_ClampToEdge;
    nearest_.reset(wgpuDeviceCreateSampler(device, &sampler));
    sampler.magFilter = WGPUFilterMode_Linear;
    sampler.minFilter = WGPUFilterMode_Linear;
    linear_.reset(wgpuDeviceCreateSampler(device, &sampler));
    constants_ = CreateBuffer(device, 2U * kOverlayConstantsStride,
        WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "overlay constants");
    if (!vertex_ || !fragment_ || !texture_layout_ || !depth_layout_ || !constants_layout_ ||
        !nearest_ || !linear_ || !constants_) {
      detail = "creating the WebGPU overlay resources failed";
      return false;
    }
    constants_group_ = CreateGroup(device, constants_layout_.get(),
        {BufferBinding(0, constants_.get(), sizeof(OverlayConstants))}, "overlay constants");
    const WGPUVertexAttribute attributes[3] = {
        {nullptr, WGPUVertexFormat_Float32x2, offsetof(OverlayVertex, x), 0},
        {nullptr, WGPUVertexFormat_Float32x2, offsetof(OverlayVertex, u), 1},
        {nullptr, WGPUVertexFormat_Unorm8x4, offsetof(OverlayVertex, color), 2},
    };
    WGPUVertexBufferLayout buffer{};
    buffer.stepMode = WGPUVertexStepMode_Vertex;
    buffer.arrayStride = sizeof(OverlayVertex);
    buffer.attributeCount = 3;
    buffer.attributes = attributes;
    WGPUBlendState blend{};
    blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha,
        WGPUBlendFactor_OneMinusSrcAlpha};
    blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One,
        WGPUBlendFactor_OneMinusSrcAlpha};
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = format;
    target.blend = &blend;
    target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragment_.get();
    fragment.entryPoint = View("fragment_main");
    fragment.targetCount = 1;
    fragment.targets = &target;
    for (const bool depth : {false, true}) {
      PipelineLayout& layout = depth ? depth_pipeline_layout_ : texture_pipeline_layout_;
      layout = CreatePipelineLayout(device,
          {depth ? depth_layout_.get() : texture_layout_.get(), constants_layout_.get()},
          "overlay");
      WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
      descriptor.label = View("overlay");
      descriptor.layout = layout.get();
      descriptor.vertex.module = vertex_.get();
      descriptor.vertex.entryPoint = View("vertex_main");
      descriptor.vertex.bufferCount = 1;
      descriptor.vertex.buffers = &buffer;
      descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
      descriptor.fragment = &fragment;
      (depth ? depth_pipeline_ : texture_pipeline_)
          .reset(layout ? wgpuDeviceCreateRenderPipeline(device, &descriptor) : nullptr);
    }
    if (!constants_group_ || !texture_pipeline_ || !depth_pipeline_) {
      detail = "creating the WebGPU overlay pipelines failed";
      return false;
    }
    return true;
  }

  // Upload the textures `overlay` changed, release those it no longer has,
  // and write its vertices and indices.
  bool Update(const OverlayDrawList& overlay, std::string& detail) {
    ++generation_;
    for (const OverlayTexture& texture : overlay.textures) {
      const std::size_t size = static_cast<std::size_t>(texture.width) * texture.height * 4U;
      if (texture.width == 0 || texture.height == 0 || texture.pixels.size() != size) {
        detail = "overlay texture " + std::to_string(texture.id) + " holds " +
                 std::to_string(texture.pixels.size()) + " bytes for " +
                 std::to_string(texture.width) + "x" + std::to_string(texture.height) +
                 " RGBA8 pixels";
        return false;
      }
      Resident& resident = textures_[texture.id];
      resident.generation = generation_;
      if (resident.group && resident.revision == texture.revision) continue;
      if (!resident.texture || resident.width != texture.width ||
          resident.height != texture.height) {
        WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
        descriptor.label = View("overlay texture");
        descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        descriptor.size = {texture.width, texture.height, 1};
        descriptor.format = WGPUTextureFormat_RGBA8Unorm;
        resident.texture.reset(wgpuDeviceCreateTexture(device_, &descriptor));
        resident.view.reset(
            resident.texture ? wgpuTextureCreateView(resident.texture.get(), nullptr) : nullptr);
        resident.group = resident.view
            ? CreateGroup(device_, texture_layout_.get(),
                  {ViewBinding(0, resident.view.get()), SamplerBinding(1, linear_.get())},
                  "overlay texture")
            : BindGroup();
        if (!resident.group) {
          detail = "creating a WebGPU overlay texture failed";
          return false;
        }
        resident.width = texture.width;
        resident.height = texture.height;
      }
      WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
      destination.texture = resident.texture.get();
      WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
      layout.bytesPerRow = texture.width * 4U;
      layout.rowsPerImage = texture.height;
      const WGPUExtent3D extent{texture.width, texture.height, 1};
      wgpuQueueWriteTexture(queue_, &destination, texture.pixels.data(), size, &layout, &extent);
      resident.revision = texture.revision;
      ++texture_uploads_;
    }
    for (auto texture = textures_.begin(); texture != textures_.end();) {
      if (texture->second.generation != generation_) {
        texture = textures_.erase(texture);
      } else {
        ++texture;
      }
    }
    return Write(vertices_, vertex_capacity_, WGPUBufferUsage_Vertex, overlay.vertices.data(),
               overlay.vertices.size() * sizeof(OverlayVertex), detail) &&
           Write(indices_, index_capacity_, WGPUBufferUsage_Index, overlay.indices.data(),
               overlay.indices.size() * sizeof(std::uint16_t), detail);
  }

  // A bind group per layer of the shadow views' image, made again when the
  // image is.
  bool UpdateShadowViews(const ShadowMap& shadow, std::string& detail) {
    if (shadow.rebuilds() == shadow_rebuilds_) return true;
    shadow_groups_.clear();
    for (std::uint32_t layer = 0; layer < shadow.layers(); ++layer) {
      shadow_groups_.push_back(CreateGroup(device_, depth_layout_.get(),
          {ViewBinding(0, shadow.layer_view(layer)), SamplerBinding(1, nearest_.get())},
          "overlay shadow view"));
      if (!shadow_groups_.back()) {
        detail = "creating a WebGPU overlay shadow view failed";
        return false;
      }
    }
    shadow_rebuilds_ = shadow.rebuilds();
    return true;
  }

  // Draw `overlay` inside `pass`, whose target is `width` x `height`; the
  // shadow views the frame drew are `shadow_views`. Returns the draws made.
  std::uint32_t Record(WGPURenderPassEncoder pass, const OverlayDrawList& overlay,
      std::uint32_t width, std::uint32_t height, std::uint32_t shadow_views) {
    if (overlay.commands.empty() || overlay.vertices.empty() || overlay.indices.empty()) {
      return 0;
    }
    // Pixels from the top left to clip space, whose y points up in WebGPU.
    OverlayConstants constants[2]{};
    for (std::uint32_t depth = 0; depth < 2; ++depth) {
      constants[depth].scale[0] = 2.0F / static_cast<float>(width);
      constants[depth].scale[1] = -2.0F / static_cast<float>(height);
      constants[depth].translate[0] = -1.0F;
      constants[depth].translate[1] = 1.0F;
      constants[depth].flags = (linearize_ ? kOverlayLinearize : 0U) |
                               (depth != 0 ? kOverlayDepth : 0U);
      wgpuQueueWriteBuffer(queue_, constants_.get(), depth * kOverlayConstantsStride,
          &constants[depth], sizeof(OverlayConstants));
    }
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertices_.get(), 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, indices_.get(), WGPUIndexFormat_Uint16, 0,
        WGPU_WHOLE_SIZE);
    const auto right_edge = static_cast<float>(width);
    const auto bottom_edge = static_cast<float>(height);
    WGPUBindGroup bound = nullptr;
    std::uint32_t recorded = 0;
    for (const OverlayCommand& draw : overlay.commands) {
      WGPUBindGroup group = nullptr;
      const bool depth = draw.texture >= kOverlayShadowView;
      if (depth) {
        const std::uint64_t view = draw.texture - kOverlayShadowView;
        if (view < shadow_views && view < shadow_groups_.size()) {
          group = shadow_groups_[static_cast<std::size_t>(view)].get();
        }
      } else if (const auto texture = textures_.find(draw.texture); texture != textures_.end()) {
        group = texture->second.group.get();
      }
      if (group == nullptr || draw.index_count == 0 ||
          static_cast<std::size_t>(draw.first_index) + draw.index_count >
              overlay.indices.size()) {
        continue;
      }
      const float left = std::clamp(draw.clip[0], 0.0F, right_edge);
      const float top = std::clamp(draw.clip[1], 0.0F, bottom_edge);
      const float right = std::clamp(draw.clip[2], 0.0F, right_edge);
      const float bottom = std::clamp(draw.clip[3], 0.0F, bottom_edge);
      if (right <= left || bottom <= top) continue;
      const auto x = static_cast<std::uint32_t>(std::floor(left));
      const auto y = static_cast<std::uint32_t>(std::floor(top));
      wgpuRenderPassEncoderSetScissorRect(pass, x, y,
          std::min(static_cast<std::uint32_t>(std::ceil(right)), width) - x,
          std::min(static_cast<std::uint32_t>(std::ceil(bottom)), height) - y);
      if (group != bound) {
        bound = group;
        wgpuRenderPassEncoderSetPipeline(pass,
            depth ? depth_pipeline_.get() : texture_pipeline_.get());
        wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        const std::uint32_t offset = depth ? kOverlayConstantsStride : 0U;
        wgpuRenderPassEncoderSetBindGroup(pass, 1, constants_group_.get(), 1, &offset);
      }
      wgpuRenderPassEncoderDrawIndexed(pass, draw.index_count, 1, draw.first_index,
          static_cast<std::int32_t>(draw.vertex_offset), 0);
      ++recorded;
    }
    return recorded;
  }

  [[nodiscard]] std::uint64_t texture_uploads() const { return texture_uploads_; }

private:
  struct Resident {
    Texture texture;
    TextureView view;
    BindGroup group;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t revision = 0;
    std::uint64_t generation = 0;
  };

  // A queue write is a multiple of four bytes; doubling keeps a growing
  // overlay from reallocating every frame.
  bool Write(Buffer& buffer, std::uint64_t& capacity, WGPUBufferUsage usage, const void* data,
      std::uint64_t size, std::string& detail) {
    if (size == 0) return true;
    const std::uint64_t padded = (size + 3U) & ~std::uint64_t{3};
    if (!buffer || capacity < padded) {
      capacity = std::max(padded, capacity * 2U);
      buffer = CreateBuffer(device_, capacity, usage | WGPUBufferUsage_CopyDst, "overlay");
      if (!buffer) {
        detail = "allocating the WebGPU overlay buffers failed";
        return false;
      }
    }
    if (padded == size) {
      wgpuQueueWriteBuffer(queue_, buffer.get(), 0, data, static_cast<std::size_t>(size));
    } else {
      scratch_.assign(static_cast<std::size_t>(padded), 0);
      std::memcpy(scratch_.data(), data, static_cast<std::size_t>(size));
      wgpuQueueWriteBuffer(queue_, buffer.get(), 0, scratch_.data(), scratch_.size());
    }
    return true;
  }

  WGPUDevice device_ = nullptr;
  WGPUQueue queue_ = nullptr;
  bool linearize_ = false;
  ShaderModule vertex_;
  ShaderModule fragment_;
  BindGroupLayout texture_layout_;
  BindGroupLayout depth_layout_;
  BindGroupLayout constants_layout_;
  PipelineLayout texture_pipeline_layout_;
  PipelineLayout depth_pipeline_layout_;
  RenderPipeline texture_pipeline_;
  RenderPipeline depth_pipeline_;
  Sampler linear_;
  Sampler nearest_;
  Buffer constants_;
  BindGroup constants_group_;
  Buffer vertices_;
  std::uint64_t vertex_capacity_ = 0;
  Buffer indices_;
  std::uint64_t index_capacity_ = 0;
  std::vector<std::uint8_t> scratch_;
  std::unordered_map<std::uint64_t, Resident> textures_;
  std::uint64_t generation_ = 0;
  std::vector<BindGroup> shadow_groups_;
  std::uint64_t shadow_rebuilds_ = 0;
  std::uint64_t texture_uploads_ = 0;
};

class WebGpuPresentSession final : public PresentSession {
public:
  ~WebGpuPresentSession() override {
    std::string ignored;
    WaitForCompletion(ignored);
    // The surface outlives the device in Gpu; it lets go of the device's
    // textures first.
    if (configured_once_) wgpuSurfaceUnconfigure(gpu_.surface.get());
  }

  PresentSetupStatus Initialize(const WebGpuWindow& window, const WebGpuShaders& shaders,
      bool vsync, const RenderOptions& options, WebGpuAdapter adapter, std::string& error);

  [[nodiscard]] bool RenderFrame(const DrawList& draws, const OverlayDrawList& overlay,
      std::uint32_t width, std::uint32_t height, bool& presented,
      std::string& error) override;

  void SetSamples(std::uint32_t requested) override {
    requested_samples_ = std::max(requested, 1U);
  }

  bool SetCompositeBackground(const CompositeBackground& settings, std::string& error) override {
    error.clear();
    const auto valid = [](Float3 c) {
      return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z) && c.x >= 0 &&
             c.x <= 1 && c.y >= 0 && c.y <= 1 && c.z >= 0 && c.z <= 1;
    };
    if (!valid(settings.color) || !valid(settings.alternate) || settings.checker_size == 0) {
      error = "compositor requires finite colours in [0,1] and a positive checker size";
      return false;
    }
    background_ = settings;
    return true;
  }
  void RequestForegroundCapture() override { foreground_capture_.requested = true; }
  bool TakeForegroundCapture(ColorProduct& color, std::string& error) override {
    return ReadCapture(foreground_capture_, true, color, error);
  }
  void RequestCapture() override { capture_.requested = true; }
  bool TakeCapture(ColorProduct& color, std::string& error) override {
    return ReadCapture(capture_, false, color, error);
  }
  void SetLateFrameSource(LateFrameSource source) override { late_.source = source; }
  void ResetScene() override { reset_scene_ = true; }
  [[nodiscard]] const PresentStatistics& statistics() const override { return statistics_; }

private:
  // A capture's buffer, which a frame's copy fills and TakeCapture maps
  // once that frame has completed.
  struct Capture {
    bool requested = false;
    Buffer buffer;
    std::uint64_t capacity = 0;
    // The frame whose copy it holds, 0 when none waits to be taken.
    std::uint64_t frame = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
  };

  // Configures the surface at `width` x `height` and makes the scene's
  // targets at that extent.
  bool Configure(std::uint32_t width, std::uint32_t height, std::string& error);
  // The depth target and, multisampled, the colour samples, at the extent
  // and the pipelines' sample count.
  bool CreateSampleTargets(std::string& error);
  // Draws at the count `requested_samples_` takes, when that differs.
  bool ApplySamples(std::string& error);
  bool WaitForCompletion(std::string& error);
  // Reads the GPU times of the last frame that wrote timestamps, which has
  // completed.
  void ReadTimestamps();
  bool EnsureRing(std::size_t draws, std::string& error);
  bool EnsureCapture(Capture& capture, std::string& error);
  bool ReadCapture(Capture& capture, bool foreground, ColorProduct& color, std::string& error);

  Gpu gpu_;
  WGPUTextureFormat surface_format_ = WGPUTextureFormat_Undefined;
  // The format the composite and overlay passes write the surface through,
  // its sRGB form where the surface offers one.
  WGPUTextureFormat view_format_ = WGPUTextureFormat_Undefined;
  WGPUPresentMode present_mode_ = WGPUPresentMode_Fifo;
  WGPUCompositeAlphaMode alpha_mode_ = WGPUCompositeAlphaMode_Auto;
  bool capture_supported_ = false;
  bool configured_once_ = false;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  ScenePipelines pipelines_;
  std::uint32_t requested_samples_ = 1;
  TextureCache textures_;
  ShadowMap shadow_;
  MaterialCache materials_;
  MeshCache meshes_;
  Compositor compositor_;
  OverlayRenderer overlay_;
  CompositeBackground background_;
  LateFrameState late_;
  bool reset_scene_ = false;
  // The scene's linear premultiplied RGBA, independent of the surface;
  // multisampled, the pass draws `color_samples_` and resolves into it.
  Texture foreground_;
  TextureView foreground_view_;
  Texture color_samples_;
  TextureView color_samples_view_;
  Texture depth_;
  TextureView depth_view_;
  Buffer ring_;
  std::uint64_t ring_capacity_ = 0;
  BindGroup ring_group_;
  std::vector<std::uint8_t> constants_;
  Capture capture_;
  Capture foreground_capture_;
  // Null when the device writes no timestamps.
  QuerySet timestamps_;
  Buffer timestamp_resolve_;
  Buffer timestamp_readback_;
  // The frame whose timestamps wait to be read, 0 when none do, and the
  // first it wrote.
  std::uint64_t timed_frame_ = 0;
  Timestamp timed_first_ = kSceneBegin;
  // The frame in flight's completion.
  WGPUFuture in_flight_{};
  bool waiting_ = false;
  WGPUQueueWorkDoneStatus in_flight_status_ = WGPUQueueWorkDoneStatus_Success;
  std::uint64_t submitted_ = 0;
  PresentStatistics statistics_;
};

PresentSetupStatus WebGpuPresentSession::Initialize(const WebGpuWindow& window,
    const WebGpuShaders& shaders, bool vsync, const RenderOptions& options,
    WebGpuAdapter adapter, std::string& error) {
  if (window.hwnd == nullptr) {
    error = "the WebGPU present session needs a native window";
    return PresentSetupStatus::Error;
  }
  ShaderSources sources;
  if (!LoadShaders(shaders, sources, error)) return PresentSetupStatus::Error;
  GpuRequest request;
  request.hwnd = window.hwnd;
  request.hinstance = window.hinstance;
  request.timestamps = true;
  const FrameStatus status = CreateGpu(adapter, gpu_, error, request);
  if (status == FrameStatus::Skip) return PresentSetupStatus::Unavailable;
  if (status != FrameStatus::Pass) return PresentSetupStatus::Error;

  WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
  if (wgpuSurfaceGetCapabilities(gpu_.surface.get(), gpu_.adapter.get(), &capabilities) !=
          WGPUStatus_Success ||
      capabilities.formatCount == 0) {
    error = "the window's surface reports no formats";
    return PresentSetupStatus::Unavailable;
  }
  const auto offers = [&](WGPUTextureFormat format) {
    return std::find(capabilities.formats, capabilities.formats + capabilities.formatCount,
               format) != capabilities.formats + capabilities.formatCount;
  };
  // The scene's colour is linear. An sRGB view encodes it as it is written,
  // as the Vulkan session's sRGB swapchain does; a surface that offers only
  // the UNORM form takes the sRGB one as a view format.
  surface_format_ = capabilities.formats[0];
  view_format_ = surface_format_;
  bool found = false;
  for (const WGPUTextureFormat preferred :
      {WGPUTextureFormat_BGRA8UnormSrgb, WGPUTextureFormat_RGBA8UnormSrgb}) {
    if (!found && offers(preferred)) {
      surface_format_ = view_format_ = preferred;
      found = true;
    }
  }
  const struct {
    WGPUTextureFormat unorm;
    WGPUTextureFormat srgb;
  } pairs[] = {{WGPUTextureFormat_BGRA8Unorm, WGPUTextureFormat_BGRA8UnormSrgb},
      {WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_RGBA8UnormSrgb}};
  for (const auto& pair : pairs) {
    if (!found && offers(pair.unorm)) {
      surface_format_ = pair.unorm;
      view_format_ = pair.srgb;
      found = true;
    }
  }
  statistics_.srgb_encoded = IsSrgb(view_format_);
  capture_supported_ = (capabilities.usages & WGPUTextureUsage_CopySrc) != 0 &&
                       (surface_format_ == WGPUTextureFormat_BGRA8Unorm ||
                           surface_format_ == WGPUTextureFormat_BGRA8UnormSrgb ||
                           surface_format_ == WGPUTextureFormat_RGBA8Unorm ||
                           surface_format_ == WGPUTextureFormat_RGBA8UnormSrgb);
  // FIFO under vsync; otherwise immediate, then mailbox, where the surface
  // offers them (WebGPU policy §8).
  present_mode_ = WGPUPresentMode_Fifo;
  if (!vsync) {
    for (const WGPUPresentMode preferred : {WGPUPresentMode_Immediate, WGPUPresentMode_Mailbox}) {
      if (present_mode_ == WGPUPresentMode_Fifo &&
          std::find(capabilities.presentModes,
              capabilities.presentModes + capabilities.presentModeCount,
              preferred) != capabilities.presentModes + capabilities.presentModeCount) {
        present_mode_ = preferred;
      }
    }
  }
  alpha_mode_ = WGPUCompositeAlphaMode_Auto;
  if (std::find(capabilities.alphaModes, capabilities.alphaModes + capabilities.alphaModeCount,
          WGPUCompositeAlphaMode_Opaque) != capabilities.alphaModes + capabilities.alphaModeCount) {
    alpha_mode_ = WGPUCompositeAlphaMode_Opaque;
  }
  wgpuSurfaceCapabilitiesFreeMembers(capabilities);

  requested_samples_ = std::max(options.samples, 1U);
  const std::uint32_t samples = requested_samples_ >= kMultisampleCount ? kMultisampleCount : 1U;
  if (!pipelines_.Initialize(gpu_.device.get(), sources, samples, error) ||
      !textures_.Initialize(gpu_, sources, error) || !shadow_.Initialize(gpu_, error) ||
      !materials_.Initialize(gpu_, pipelines_, textures_, shadow_, error) ||
      !meshes_.Initialize(gpu_, pipelines_, error) ||
      !compositor_.Initialize(gpu_.device.get(), gpu_.queue.get(), sources, view_format_, error) ||
      !overlay_.Initialize(gpu_.device.get(), gpu_.queue.get(), sources, view_format_,
          statistics_.srgb_encoded, error)) {
    return PresentSetupStatus::Error;
  }
  statistics_.samples = samples;

  // Timestamps where the adapter offers them; the passes' own, so the
  // scene's parts are not timed (WebGPU policy §6).
  if (gpu_.timestamps) {
    WGPUQuerySetDescriptor descriptor = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    descriptor.label = View("frame timestamps");
    descriptor.type = WGPUQueryType_Timestamp;
    descriptor.count = kTimestampCount;
    timestamps_.reset(wgpuDeviceCreateQuerySet(gpu_.device.get(), &descriptor));
    timestamp_resolve_ = CreateBuffer(gpu_.device.get(), kTimestampCount * sizeof(std::uint64_t),
        WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc, "timestamp resolve");
    timestamp_readback_ = CreateBuffer(gpu_.device.get(), kTimestampCount * sizeof(std::uint64_t),
        WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst, "timestamp readback");
    if (!timestamps_ || !timestamp_resolve_ || !timestamp_readback_) {
      error = "creating the WebGPU timestamp queries failed";
      return PresentSetupStatus::Error;
    }
    statistics_.gpu_timing = true;
  }
  statistics_.device_name = gpu_.device_name;
  // WebGPU validates every call; Dawn's errors are this backend's
  // validation messages.
  statistics_.validation_available = true;
  return PresentSetupStatus::Ready;
}

bool WebGpuPresentSession::Configure(std::uint32_t width, std::uint32_t height,
    std::string& error) {
  if (!WaitForCompletion(error)) return false;
  WGPUSurfaceConfiguration configuration = WGPU_SURFACE_CONFIGURATION_INIT;
  configuration.device = gpu_.device.get();
  configuration.format = surface_format_;
  configuration.usage =
      WGPUTextureUsage_RenderAttachment | (capture_supported_ ? WGPUTextureUsage_CopySrc : 0U);
  configuration.width = width;
  configuration.height = height;
  configuration.viewFormatCount = view_format_ != surface_format_ ? 1U : 0U;
  configuration.viewFormats = &view_format_;
  configuration.alphaMode = alpha_mode_;
  configuration.presentMode = present_mode_;
  wgpuSurfaceConfigure(gpu_.surface.get(), &configuration);

  WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
  descriptor.label = View("foreground");
  descriptor.size = {width, height, 1};
  descriptor.format = kColorFormat;
  descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
                     WGPUTextureUsage_CopySrc;
  foreground_.reset(wgpuDeviceCreateTexture(gpu_.device.get(), &descriptor));
  foreground_view_.reset(
      foreground_ ? wgpuTextureCreateView(foreground_.get(), nullptr) : nullptr);
  width_ = width;
  height_ = height;
  if (!foreground_view_ || !compositor_.SetForeground(foreground_view_.get(), error) ||
      !CreateSampleTargets(error)) {
    if (error.empty()) error = "allocating the WebGPU present targets failed";
    width_ = height_ = 0;
    return false;
  }
  if (configured_once_) ++statistics_.swapchain_recreates;
  configured_once_ = true;
  return true;
}

bool WebGpuPresentSession::CreateSampleTargets(std::string& error) {
  const std::uint32_t samples = pipelines_.samples();
  WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
  descriptor.size = {width_, height_, 1};
  descriptor.sampleCount = samples;
  descriptor.usage = WGPUTextureUsage_RenderAttachment;
  descriptor.label = View("depth");
  descriptor.format = kDepthFormat;
  depth_.reset(wgpuDeviceCreateTexture(gpu_.device.get(), &descriptor));
  depth_view_.reset(depth_ ? wgpuTextureCreateView(depth_.get(), nullptr) : nullptr);
  color_samples_.reset();
  color_samples_view_.reset();
  if (samples > 1) {
    descriptor.label = View("color samples");
    descriptor.format = kColorFormat;
    color_samples_.reset(wgpuDeviceCreateTexture(gpu_.device.get(), &descriptor));
    color_samples_view_.reset(
        color_samples_ ? wgpuTextureCreateView(color_samples_.get(), nullptr) : nullptr);
  }
  if (!depth_view_ || (samples > 1 && !color_samples_view_)) {
    error = "allocating the WebGPU sample targets failed";
    return false;
  }
  return true;
}

bool WebGpuPresentSession::ApplySamples(std::string& error) {
  const std::uint32_t samples = requested_samples_ >= kMultisampleCount ? kMultisampleCount : 1U;
  if (samples == pipelines_.samples()) return true;
  // Nothing in flight may use the targets; every cache and bind group
  // stays, and nothing is uploaded.
  if (!WaitForCompletion(error)) return false;
  pipelines_.SetSamples(samples);
  if (width_ != 0 && height_ != 0 && !CreateSampleTargets(error)) return false;
  statistics_.samples = samples;
  ++statistics_.sample_changes;
  return true;
}

bool WebGpuPresentSession::WaitForCompletion(std::string& error) {
  if (!waiting_) return true;
  waiting_ = false;
  if (!Wait(gpu_.instance.get(), in_flight_, "the frame in flight", error)) return false;
  if (in_flight_status_ != WGPUQueueWorkDoneStatus_Success) {
    error = gpu_.events->lost ? "the WebGPU device was lost: " + gpu_.events->lost_reason
                              : "the frame in flight did not complete";
    return false;
  }
  return true;
}

void WebGpuPresentSession::ReadTimestamps() {
  if (timed_frame_ == 0) return;
  const std::uint64_t frame = timed_frame_;
  timed_frame_ = 0;
  std::string ignored;
  const std::uint64_t bytes = kTimestampCount * sizeof(std::uint64_t);
  if (!MapForRead(gpu_, timestamp_readback_.get(), bytes, ignored)) {
    wgpuBufferUnmap(timestamp_readback_.get());
    return;
  }
  std::uint64_t ticks[kTimestampCount]{};
  const void* mapped = wgpuBufferGetConstMappedRange(timestamp_readback_.get(), 0, bytes);
  if (mapped != nullptr) std::memcpy(ticks, mapped, sizeof(ticks));
  wgpuBufferUnmap(timestamp_readback_.get());
  if (mapped == nullptr) return;
  // In nanoseconds; a run the frame did not record wrote 0 at both ends.
  const auto between = [&](Timestamp from, Timestamp to) {
    return ticks[to] > ticks[from] ? static_cast<double>(ticks[to] - ticks[from]) * 1e-6 : 0.0;
  };
  PresentGpuTimes& gpu = statistics_.gpu;
  gpu = {};
  gpu.uploads = between(kUploadsBegin, kUploadsEnd);
  gpu.shadow = between(kShadowBegin, kShadowEnd);
  gpu.scene = between(kSceneBegin, kSceneEnd);
  gpu.composite = between(kCompositeBegin, kCompositeEnd);
  // The copies a capture records lie between the composite and the overlay.
  gpu.capture = between(kCompositeEnd, kOverlayBegin);
  gpu.overlay = between(kOverlayBegin, kOverlayEnd);
  gpu.frame = between(timed_first_, kOverlayEnd);
  statistics_.gpu_frame = frame;
}

bool WebGpuPresentSession::EnsureRing(std::size_t draws, std::string& error) {
  const std::uint64_t needed = std::max<std::uint64_t>(draws, 1) * kDrawStride;
  if (ring_ && ring_capacity_ >= needed) return true;
  std::uint64_t capacity = std::max<std::uint64_t>(ring_capacity_, 64U * kDrawStride);
  while (capacity < needed) capacity *= 2U;
  ring_ = CreateBuffer(gpu_.device.get(), capacity,
      WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "draw constants");
  ring_group_ = ring_ ? CreateGroup(gpu_.device.get(), pipelines_.draw_layout(),
                            {BufferBinding(0, ring_.get(), kDrawBindingSize)}, "draw")
                      : BindGroup();
  ring_capacity_ = capacity;
  if (!ring_group_) {
    error = "allocating the WebGPU draw constants failed";
    return false;
  }
  return true;
}

bool WebGpuPresentSession::EnsureCapture(Capture& capture, std::string& error) {
  const std::uint64_t bytes = static_cast<std::uint64_t>(CopyPitch(width_)) * height_;
  if (capture.buffer && capture.capacity >= bytes) return true;
  capture.buffer = CreateBuffer(gpu_.device.get(), bytes,
      WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst, "capture");
  capture.capacity = bytes;
  if (!capture.buffer) {
    error = "allocating a WebGPU capture buffer failed";
    return false;
  }
  return true;
}

bool WebGpuPresentSession::RenderFrame(const DrawList& extracted,
    const OverlayDrawList& overlay, std::uint32_t width, std::uint32_t height, bool& presented,
    std::string& error) {
  presented = false;
  if (width == 0 || height == 0) return true;
  using Clock = std::chrono::steady_clock;
  using Milliseconds = std::chrono::duration<double, std::milli>;
  const auto start = Clock::now();
  if (gpu_.events->lost) {
    error = "the WebGPU device was lost: " + gpu_.events->lost_reason;
    return false;
  }
  if (!ApplySamples(error) ||
      ((width != width_ || height != height_) && !Configure(width, height, error))) {
    return false;
  }

  // One frame in flight: the previous frame has completed before anything
  // it read is touched, and before the late latch (WebGPU policy §6).
  const auto wait_start = Clock::now();
  if (!WaitForCompletion(error)) return false;
  ReadTimestamps();
  if (reset_scene_) {
    const DrawList empty;
    if (!meshes_.Update(empty, error) || !textures_.Update(empty, error) ||
        !materials_.Update(empty, textures_, shadow_, error)) {
      return false;
    }
    reset_scene_ = false;
  }
  WGPUSurfaceTexture surface = WGPU_SURFACE_TEXTURE_INIT;
  wgpuSurfaceGetCurrentTexture(gpu_.surface.get(), &surface);
  const Milliseconds waited = Clock::now() - wait_start;
  Texture surface_texture(surface.texture);
  switch (surface.status) {
  case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
  case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
    break;
  case WGPUSurfaceGetCurrentTextureStatus_Timeout:
    return true;
  case WGPUSurfaceGetCurrentTextureStatus_Outdated:
  case WGPUSurfaceGetCurrentTextureStatus_Lost:
    // Configured again on the next frame.
    width_ = height_ = 0;
    return true;
  default:
    error = "the window's surface gave no texture to draw into";
    return false;
  }
  WGPUTextureViewDescriptor surface_view_descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
  surface_view_descriptor.format = view_format_;
  surface_view_descriptor.dimension = WGPUTextureViewDimension_2D;
  surface_view_descriptor.mipLevelCount = 1;
  surface_view_descriptor.arrayLayerCount = 1;
  TextureView surface_view(wgpuTextureCreateView(surface_texture.get(), &surface_view_descriptor));

  const double pose_before = meshes_.pose_write_ms();
  const double morph_before = meshes_.morph_write_ms();
  const double material_before = materials_.parameter_write_ms();
  if (!textures_.Update(extracted, error) ||
      (late_.enabled() && (!meshes_.Update(extracted, error, false) ||
                              !materials_.Update(extracted, textures_, shadow_, error, false))) ||
      !overlay_.Update(overlay, error)) {
    return false;
  }
  const DrawList* frame = nullptr;
  if (!late_.Read(extracted, frame, statistics_, error)) return false;
  const DrawList& draws = *frame;
  if (!meshes_.Update(draws, error) ||
      !shadow_.Update(draws, error, statistics_.latency.shadow_fit_ms) ||
      !overlay_.UpdateShadowViews(shadow_, error) ||
      !materials_.Update(draws, textures_, shadow_, error) ||
      !EnsureRing(MeshCache::DrawBound(draws, shadow_.fit().views().size()), error)) {
    return false;
  }
  statistics_.shadow_views = static_cast<std::uint32_t>(shadow_.fit().views().size());
  statistics_.shadow_unviewed = shadow_.fit().unviewed();
  statistics_.shadow_resolution = shadow_.resolution();
  statistics_.shadow_layers = shadow_.layers();
  statistics_.shadow_memory_bytes = shadow_.memory_bytes();
  statistics_.shadow_rebuilds = shadow_.rebuilds();
  statistics_.latency.buffers_written = SteadyNanoseconds();
  statistics_.latency.pose_write_ms = meshes_.pose_write_ms() - pose_before;
  statistics_.latency.morph_write_ms = meshes_.morph_write_ms() - morph_before;
  statistics_.latency.material_write_ms = materials_.parameter_write_ms() - material_before;
  statistics_.topology_uploads = meshes_.topology_uploads();
  statistics_.point_uploads = meshes_.point_uploads();
  statistics_.material_writes = materials_.writes();
  statistics_.texture_uploads = textures_.uploads();
  statistics_.skin_uploads = meshes_.skin_uploads();
  statistics_.pose_writes = meshes_.pose_writes();
  statistics_.morph_uploads = meshes_.morph_uploads();
  statistics_.morph_weight_writes = meshes_.morph_weight_writes();
  statistics_.overlay_texture_uploads = overlay_.texture_uploads();
  // A capture copies this frame's image into a buffer the CPU maps once
  // the frame has completed; the frame before it has, so the buffer is free.
  const bool capture = capture_.requested;
  if (capture && !capture_supported_) {
    error = "this surface's textures cannot be captured";
    return false;
  }
  const bool capture_foreground = foreground_capture_.requested;
  if ((capture && !EnsureCapture(capture_, error)) ||
      (capture_foreground && !EnsureCapture(foreground_capture_, error))) {
    return false;
  }

  WGPUCommandEncoderDescriptor encoder_descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
  CommandEncoder encoder(wgpuDeviceCreateCommandEncoder(gpu_.device.get(), &encoder_descriptor));
  const WGPUQuerySet timestamps = timestamps_.get();
  timed_first_ = kSceneBegin;
  if (textures_.has_pending_mipmaps()) {
    timed_first_ = kUploadsBegin;
  } else if (!shadow_.fit().views().empty()) {
    timed_first_ = kShadowBegin;
  }
  textures_.RecordMipmaps(encoder.get(), {timestamps, kUploadsBegin, kUploadsEnd});
  constants_.clear();
  if (!shadow_.Record(encoder.get(), pipelines_, materials_, meshes_, draws, ring_group_.get(),
          constants_, error, {timestamps, kShadowBegin, kShadowEnd})) {
    return false;
  }

  // The scene's colour is always linear premultiplied RGBA over a
  // transparent clear, independent of the surface's format; multisampled,
  // its samples resolve into the foreground as the pass ends.
  const bool multisampled = static_cast<bool>(color_samples_);
  WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
  color.view = multisampled ? color_samples_view_.get() : foreground_view_.get();
  color.resolveTarget = multisampled ? foreground_view_.get() : nullptr;
  color.loadOp = WGPULoadOp_Clear;
  color.storeOp = multisampled ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
  color.clearValue = {0.0, 0.0, 0.0, 0.0};
  WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
  depth.view = depth_view_.get();
  depth.depthLoadOp = WGPULoadOp_Clear;
  depth.depthStoreOp = WGPUStoreOp_Discard;
  depth.depthClearValue = 1.0F;
  WGPURenderPassDescriptor scene_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
  scene_descriptor.label = View("scene");
  scene_descriptor.colorAttachmentCount = 1;
  scene_descriptor.colorAttachments = &color;
  scene_descriptor.depthStencilAttachment = &depth;
  WGPUPassTimestampWrites scene_writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
  scene_descriptor.timestampWrites =
      PassTimestamps{timestamps, kSceneBegin, kSceneEnd}.Of(0, 1, scene_writes);
  RenderPassEncoder scene(wgpuCommandEncoderBeginRenderPass(encoder.get(), &scene_descriptor));
  SceneRecord record;
  const bool recorded = meshes_.Record(scene.get(), pipelines_, materials_, draws, width_,
      height_, ring_group_.get(), constants_, record, error);
  wgpuRenderPassEncoderEnd(scene.get());
  if (!recorded) return false;

  compositor_.Record(encoder.get(), surface_view.get(), background_,
      {timestamps, kCompositeBegin, kCompositeEnd});
  WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
  destination.layout.bytesPerRow = CopyPitch(width_);
  destination.layout.rowsPerImage = height_;
  const WGPUExtent3D extent{width_, height_, 1};
  if (capture_foreground) {
    source.texture = foreground_.get();
    destination.buffer = foreground_capture_.buffer.get();
    wgpuCommandEncoderCopyTextureToBuffer(encoder.get(), &source, &destination, &extent);
  }
  if (capture) {
    source.texture = surface_texture.get();
    destination.buffer = capture_.buffer.get();
    wgpuCommandEncoderCopyTextureToBuffer(encoder.get(), &source, &destination, &extent);
  }
  // The overlay loads what the composite left, after any copy, so a
  // capture never shows it. Its pass is recorded every frame, drawing
  // nothing without commands, so its timestamps end the frame.
  WGPURenderPassColorAttachment overlay_color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
  overlay_color.view = surface_view.get();
  overlay_color.loadOp = WGPULoadOp_Load;
  overlay_color.storeOp = WGPUStoreOp_Store;
  WGPURenderPassDescriptor overlay_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
  overlay_descriptor.label = View("overlay");
  overlay_descriptor.colorAttachmentCount = 1;
  overlay_descriptor.colorAttachments = &overlay_color;
  WGPUPassTimestampWrites overlay_writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
  overlay_descriptor.timestampWrites =
      PassTimestamps{timestamps, kOverlayBegin, kOverlayEnd}.Of(0, 1, overlay_writes);
  RenderPassEncoder overlay_pass(
      wgpuCommandEncoderBeginRenderPass(encoder.get(), &overlay_descriptor));
  const std::uint32_t overlay_draws = overlay_.Record(overlay_pass.get(), overlay, width_,
      height_, static_cast<std::uint32_t>(shadow_.fit().views().size()));
  wgpuRenderPassEncoderEnd(overlay_pass.get());
  if (timestamps != nullptr) {
    wgpuCommandEncoderResolveQuerySet(encoder.get(), timestamps, 0, kTimestampCount,
        timestamp_resolve_.get(), 0);
    wgpuCommandEncoderCopyBufferToBuffer(encoder.get(), timestamp_resolve_.get(), 0,
        timestamp_readback_.get(), 0, kTimestampCount * sizeof(std::uint64_t));
  }
  WGPUCommandBufferDescriptor command_descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
  CommandBuffer commands(wgpuCommandEncoderFinish(encoder.get(), &command_descriptor));

  // The draw constants are the frame's last value written, once, before it
  // is submitted (WebGPU policy §6).
  if (!constants_.empty()) {
    wgpuQueueWriteBuffer(gpu_.queue.get(), ring_.get(), 0, constants_.data(), constants_.size());
  }
  const WGPUCommandBuffer submitted = commands.get();
  wgpuQueueSubmit(gpu_.queue.get(), 1, &submitted);
  WGPUQueueWorkDoneCallbackInfo done{};
  done.mode = WGPUCallbackMode_WaitAnyOnly;
  done.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView, void* out, void*) {
    *static_cast<WGPUQueueWorkDoneStatus*>(out) = status;
  };
  done.userdata1 = &in_flight_status_;
  in_flight_ = wgpuQueueOnSubmittedWorkDone(gpu_.queue.get(), done);
  waiting_ = true;
  ++submitted_;
  statistics_.latency.frame = submitted_;
  statistics_.latency.submitted = SteadyNanoseconds();
  timed_frame_ = timestamps != nullptr ? submitted_ : 0U;
  statistics_.draws = {record.unlit, record.outline, record.opaque, record.transparent,
      record.triangles, record.pipeline_binds, overlay_draws,
      static_cast<std::uint32_t>(overlay.vertices.size()), record.hulls,
      shadow_.caster_draws()};
  for (const auto& [taken, requested] : {std::pair{&capture_, capture},
           std::pair{&foreground_capture_, capture_foreground}}) {
    if (requested) {
      taken->requested = false;
      taken->frame = submitted_;
      taken->width = width_;
      taken->height = height_;
    }
  }

  const WGPUStatus present = wgpuSurfacePresent(gpu_.surface.get());
  statistics_.latency.present_returned = SteadyNanoseconds();
  if (present != WGPUStatus_Success) {
    error = "presenting the window's surface failed";
    return false;
  }
  ++statistics_.frames_presented;
  presented = true;
  // A suboptimal texture is configured again on the next frame.
  if (surface.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
    width_ = height_ = 0;
  }
  statistics_.validation_message_count = gpu_.events->errors;
  if (!gpu_.events->first_error.empty()) {
    statistics_.validation_detail = gpu_.events->first_error;
  }
  statistics_.cpu_wait = waited.count();
  statistics_.cpu_submit = Milliseconds(Clock::now() - start).count() - statistics_.cpu_wait;
  return true;
}

bool WebGpuPresentSession::ReadCapture(Capture& capture, bool foreground, ColorProduct& color,
    std::string& error) {
  error.clear();
  if (capture.frame == 0) return false;
  capture.frame = 0;
  const std::uint32_t pitch = CopyPitch(capture.width);
  const std::uint64_t bytes = static_cast<std::uint64_t>(pitch) * capture.height;
  // Mapping waits for the frame that copied into it.
  if (!MapForRead(gpu_, capture.buffer.get(), bytes, error)) {
    wgpuBufferUnmap(capture.buffer.get());
    return false;
  }
  const auto* rows = static_cast<const std::uint8_t*>(
      wgpuBufferGetConstMappedRange(capture.buffer.get(), 0, static_cast<std::size_t>(bytes)));
  if (rows == nullptr) {
    wgpuBufferUnmap(capture.buffer.get());
    error = "reading a WebGPU capture failed";
    return false;
  }
  color.width = capture.width;
  color.height = capture.height;
  color.row_pitch = color.width * 4U;
  color.pixel_format = "rgba8-unorm";
  color.origin = "top-left";
  color.color_space = foreground ? "linear" : "as-presented";
  color.transfer_function = !foreground && statistics_.srgb_encoded ? "srgb" : "linear";
  color.alpha_convention = foreground ? "premultiplied" : "opaque";
  color.payload.resize(static_cast<std::size_t>(color.width) * color.height * 4U);
  for (std::uint32_t row = 0; row < color.height; ++row) {
    std::memcpy(color.payload.data() + static_cast<std::size_t>(row) * color.row_pitch,
        rows + static_cast<std::size_t>(row) * pitch, color.row_pitch);
  }
  wgpuBufferUnmap(capture.buffer.get());
  const bool bgra = surface_format_ == WGPUTextureFormat_BGRA8Unorm ||
                    surface_format_ == WGPUTextureFormat_BGRA8UnormSrgb;
  if (!foreground && bgra) {
    for (std::size_t pixel = 0; pixel < color.payload.size(); pixel += 4U) {
      std::swap(color.payload[pixel], color.payload[pixel + 2U]);
    }
  }
  ++statistics_.readbacks;
  return true;
}

} // namespace

std::unique_ptr<PresentSession> CreateWebGpuPresentSession(const WebGpuWindow& window,
    const WebGpuShaders& shaders, bool vsync, PresentSetupStatus& status, std::string& error,
    const RenderOptions& options, WebGpuAdapter adapter) {
  auto session = std::make_unique<WebGpuPresentSession>();
  status = session->Initialize(window, shaders, vsync, options, adapter, error);
  if (status != PresentSetupStatus::Ready) return nullptr;
  return session;
}

} // namespace Toon
