// SPDX-License-Identifier: Apache-2.0
#include "webgpu_internal.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <span>
#include <tuple>

namespace Toon::webgpu_internal {
namespace {

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
    WGPUBufferBindingType type, bool dynamic = false) {
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
    std::uint64_t size = WGPU_WHOLE_SIZE) {
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

WGPUAddressMode AddressMode(std::uint32_t wrap) {
  switch (static_cast<ToonWrap>(wrap)) {
  case ToonWrap::ClampToEdge:
    return WGPUAddressMode_ClampToEdge;
  case ToonWrap::MirroredRepeat:
    return WGPUAddressMode_MirrorRepeat;
  case ToonWrap::Repeat:
    break;
  }
  return WGPUAddressMode_Repeat;
}

// The Vulkan backend's helpers, for the same records and draw order.
bool ViewRows(const Matrix4& matrix, float rows[12]) {
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 4; ++column) {
      rows[row * 4 + column] = matrix.m[column * 4 + row];
    }
  }
  const float* m = matrix.m.data();
  const float determinant = m[0] * (m[5] * m[10] - m[9] * m[6]) -
                            m[4] * (m[1] * m[10] - m[9] * m[2]) +
                            m[8] * (m[1] * m[6] - m[5] * m[2]);
  return determinant < 0.0F;
}

void WriteUvRows(const ToonTextureRef& texture, float rows[8]) {
  const float c = std::cos(texture.rotation);
  const float s = std::sin(texture.rotation);
  const float values[8] = {texture.scale.x * c, texture.scale.y * s,
      texture.offset.x, 0.0F, -texture.scale.x * s, texture.scale.y * c,
      texture.offset.y, 0.0F};
  std::copy(values, values + 8, rows);
}

std::uint32_t SamplerIndex(const ToonTextureRef& texture) {
  return static_cast<std::uint32_t>(texture.wrap_s) * 3U +
         static_cast<std::uint32_t>(texture.wrap_t);
}

std::span<const MeshPart> PartsOf(const MeshSnapshot& mesh,
    std::uint32_t index_count, MeshPart& whole) {
  if (mesh.parts != nullptr && !mesh.parts->empty()) {
    return *mesh.parts;
  }
  whole = {0, index_count, mesh.material};
  return {&whole, 1};
}

bool Fits(const MeshPart& part, std::uint32_t index_count) {
  return part.index_count != 0 && part.first_index <= index_count &&
         part.index_count <= index_count - part.first_index;
}

std::uint32_t MipLevels(std::uint32_t width, std::uint32_t height) {
  std::uint32_t levels = 1;
  for (std::uint32_t size = std::max(width, height); size > 1U; size /= 2U) {
    ++levels;
  }
  return levels;
}

constexpr WGPUShaderStage kBothStages = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;

} // namespace

bool ScenePipelines::Initialize(WGPUDevice device, const ShaderSources& sources,
    std::uint32_t samples, std::string& detail) {
  device_ = device;
  samples_ = samples;
  mesh_vertex_ = CreateShader(device, sources.mesh_vertex, "mesh.vert");
  mesh_fragment_ = CreateShader(device, sources.mesh_fragment, "mesh.frag");
  preview_vertex_ = CreateShader(device, sources.preview_vertex, "preview_surface.vert");
  preview_fragment_ = CreateShader(device, sources.preview_fragment, "preview_surface.frag");

  // Group 0, the frame: its buffer, the shadow views' image, their
  // parameters and the gather sampler (WebGPU policy §4). The image is
  // depth32float, which WGSL reads as an unfilterable float.
  frame_layout_ = CreateLayout(device,
      {BufferEntry(0, kBothStages, WGPUBufferBindingType_ReadOnlyStorage),
          TextureEntry(1, WGPUShaderStage_Fragment, WGPUTextureSampleType_UnfilterableFloat,
              WGPUTextureViewDimension_2DArray),
          BufferEntry(2, kBothStages, WGPUBufferBindingType_ReadOnlyStorage),
          SamplerEntry(3, WGPUShaderStage_Fragment, WGPUSamplerBindingType_NonFiltering)},
      "frame");
  // Group 1, a PreviewSurface material: its model's buffer and a texture
  // and sampler per role.
  std::vector<WGPUBindGroupLayoutEntry> material{BufferEntry(
      kPreviewMaterialsBinding, WGPUShaderStage_Fragment,
      WGPUBufferBindingType_ReadOnlyStorage)};
  for (std::uint32_t role = 0; role < kTextureRoles; ++role) {
    material.push_back(TextureEntry(kFirstRoleTexture + role, WGPUShaderStage_Fragment,
        WGPUTextureSampleType_Float, WGPUTextureViewDimension_2D));
    material.push_back(SamplerEntry(kFirstRoleSampler + role, WGPUShaderStage_Fragment,
        WGPUSamplerBindingType_Filtering));
  }
  material_layout_ = CreateLayout(device, material, "preview material");
  empty_layout_ = CreateLayout(device, {}, "empty");
  // Group 2, a mesh's deformation: influences, joints, morph offsets,
  // ranges and weights, read by the vertex stage.
  std::vector<WGPUBindGroupLayoutEntry> deformation;
  for (std::uint32_t binding = 0; binding < 5; ++binding) {
    deformation.push_back(BufferEntry(binding, WGPUShaderStage_Vertex,
        WGPUBufferBindingType_ReadOnlyStorage));
  }
  deformation_layout_ = CreateLayout(device, deformation, "deformation");
  // Group 3, the draw: its constants at a dynamic offset in the ring.
  draw_layout_ = CreateLayout(device,
      {BufferEntry(0, kBothStages, WGPUBufferBindingType_Uniform, true)}, "draw");
  mesh_layout_ = CreatePipelineLayout(device,
      {empty_layout_.get(), empty_layout_.get(), deformation_layout_.get(), draw_layout_.get()},
      "mesh");
  preview_layout_ = CreatePipelineLayout(device,
      {frame_layout_.get(), material_layout_.get(), deformation_layout_.get(),
          draw_layout_.get()},
      "preview_surface");

  // Trilinear, one sampler per wrap pair, as the Vulkan backend's.
  for (std::uint32_t index = 0; index < kSamplerCount; ++index) {
    WGPUSamplerDescriptor descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
    descriptor.addressModeU = AddressMode(index / 3U);
    descriptor.addressModeV = AddressMode(index % 3U);
    descriptor.addressModeW = WGPUAddressMode_Repeat;
    descriptor.magFilter = WGPUFilterMode_Linear;
    descriptor.minFilter = WGPUFilterMode_Linear;
    descriptor.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplers_[index].reset(wgpuDeviceCreateSampler(device, &descriptor));
  }
  WGPUSamplerDescriptor shadow = WGPU_SAMPLER_DESCRIPTOR_INIT;
  shadow.addressModeU = WGPUAddressMode_ClampToEdge;
  shadow.addressModeV = WGPUAddressMode_ClampToEdge;
  shadow_sampler_.reset(wgpuDeviceCreateSampler(device, &shadow));

  if (!mesh_vertex_ || !mesh_fragment_ || !preview_vertex_ || !preview_fragment_ ||
      !frame_layout_ || !material_layout_ || !deformation_layout_ || !draw_layout_ ||
      !mesh_layout_ || !preview_layout_ || !shadow_sampler_) {
    detail = "creating the WebGPU scene layouts or shader modules failed";
    return false;
  }
  return true;
}

WGPURenderPipeline ScenePipelines::Get(const Key& key, std::string& detail) {
  for (const auto& [existing, pipeline] : pipelines_) {
    if (existing == key) return pipeline.get();
  }
  const bool preview = key.kind == Kind::Preview;
  // Position, normal, UV and tangent, as the Vulkan pipelines' streams; the
  // unlit mesh reads the position alone.
  const WGPUVertexAttribute attributes[4] = {
      {nullptr, WGPUVertexFormat_Float32x3, 0, 0},
      {nullptr, WGPUVertexFormat_Float32x3, 0, 1},
      {nullptr, WGPUVertexFormat_Float32x2, 0, 2},
      {nullptr, WGPUVertexFormat_Float32x4, 0, 3},
  };
  WGPUVertexBufferLayout buffers[4]{};
  const std::uint64_t strides[4] = {sizeof(Float3), sizeof(Float3),
      key.uvs ? sizeof(Float2) : 0U, key.tangents ? sizeof(Float4) : 0U};
  for (std::uint32_t stream = 0; stream < 4; ++stream) {
    buffers[stream].stepMode = WGPUVertexStepMode_Vertex;
    buffers[stream].arrayStride = strides[stream];
    buffers[stream].attributeCount = 1;
    buffers[stream].attributes = &attributes[stream];
  }

  WGPUBlendState blend{};
  blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha,
      WGPUBlendFactor_OneMinusSrcAlpha};
  blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One,
      WGPUBlendFactor_OneMinusSrcAlpha};
  WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
  target.format = kColorFormat;
  // One fixed PreviewSurface pipeline blends: Opaque and Mask return alpha
  // one, as the Vulkan backend's.
  target.blend = preview ? &blend : nullptr;
  target.writeMask = WGPUColorWriteMask_All;
  WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
  fragment.module = preview ? preview_fragment_.get() : mesh_fragment_.get();
  fragment.entryPoint = View("fragment_main");
  fragment.targetCount = 1;
  fragment.targets = &target;

  WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
  depth.format = kDepthFormat;
  depth.depthWriteEnabled = key.depth_write ? WGPUOptionalBool_True : WGPUOptionalBool_False;
  depth.depthCompare = WGPUCompareFunction_Less;

  WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
  descriptor.label = View(preview ? "preview_surface" : "mesh");
  descriptor.layout = preview ? preview_layout_.get() : mesh_layout_.get();
  descriptor.vertex.module = preview ? preview_vertex_.get() : mesh_vertex_.get();
  descriptor.vertex.entryPoint = View("vertex_main");
  descriptor.vertex.bufferCount = preview ? 4U : 1U;
  descriptor.vertex.buffers = buffers;
  descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
  descriptor.primitive.frontFace = key.front;
  descriptor.primitive.cullMode = key.cull;
  descriptor.depthStencil = &depth;
  descriptor.multisample.count = samples_;
  descriptor.fragment = &fragment;
  RenderPipeline pipeline(wgpuDeviceCreateRenderPipeline(device_, &descriptor));
  if (!pipeline) {
    detail = "creating a WebGPU scene pipeline failed";
    return nullptr;
  }
  ++created_;
  pipelines_.emplace_back(key, std::move(pipeline));
  return pipelines_.back().second.get();
}

bool TextureCache::Initialize(const Gpu& gpu, const ShaderSources& sources,
    std::string& detail) {
  device_ = gpu.device.get();
  queue_ = gpu.queue.get();
  // The white a role without a texture samples, as the Vulkan table's
  // entry 0.
  WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
  descriptor.label = View("white");
  descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
  descriptor.size = {1, 1, 1};
  descriptor.format = WGPUTextureFormat_RGBA8Unorm;
  white_.reset(wgpuDeviceCreateTexture(device_, &descriptor));
  if (!white_) {
    detail = "creating the white texture failed";
    return false;
  }
  white_view_.reset(wgpuTextureCreateView(white_.get(), nullptr));
  const std::uint8_t white[4] = {255, 255, 255, 255};
  WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  destination.texture = white_.get();
  WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
  layout.bytesPerRow = 4;
  layout.rowsPerImage = 1;
  const WGPUExtent3D extent{1, 1, 1};
  wgpuQueueWriteTexture(queue_, &destination, white, sizeof(white), &layout, &extent);

  mip_vertex_ = CreateShader(device_, sources.mipmap_vertex, "mipmap.vert");
  mip_fragment_ = CreateShader(device_, sources.mipmap_fragment, "mipmap.frag");
  mip_layout_ = CreateLayout(device_,
      {TextureEntry(0, WGPUShaderStage_Fragment, WGPUTextureSampleType_Float,
           WGPUTextureViewDimension_2D),
          SamplerEntry(1, WGPUShaderStage_Fragment, WGPUSamplerBindingType_Filtering)},
      "mipmap");
  mip_pipeline_layout_ = CreatePipelineLayout(device_, {mip_layout_.get()}, "mipmap");
  WGPUSamplerDescriptor sampler = WGPU_SAMPLER_DESCRIPTOR_INIT;
  sampler.magFilter = WGPUFilterMode_Linear;
  sampler.minFilter = WGPUFilterMode_Linear;
  sampler.addressModeU = WGPUAddressMode_ClampToEdge;
  sampler.addressModeV = WGPUAddressMode_ClampToEdge;
  mip_sampler_.reset(wgpuDeviceCreateSampler(device_, &sampler));
  for (const bool srgb : {true, false}) {
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = srgb ? WGPUTextureFormat_RGBA8UnormSrgb : WGPUTextureFormat_RGBA8Unorm;
    target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = mip_fragment_.get();
    fragment.entryPoint = View("fragment_main");
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor pipeline = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeline.label = View("mipmap");
    pipeline.layout = mip_pipeline_layout_.get();
    pipeline.vertex.module = mip_vertex_.get();
    pipeline.vertex.entryPoint = View("vertex_main");
    pipeline.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeline.fragment = &fragment;
    (srgb ? mip_srgb_ : mip_linear_)
        .reset(wgpuDeviceCreateRenderPipeline(device_, &pipeline));
  }
  if (!white_view_ || !mip_vertex_ || !mip_fragment_ || !mip_srgb_ || !mip_linear_ ||
      !mip_sampler_) {
    detail = "creating the WebGPU texture resources failed";
    return false;
  }
  return true;
}

bool TextureCache::Update(const DrawList& draws, std::string& detail) {
  ++generation_;
  for (const TextureSnapshot& snapshot : draws.textures) {
    Resident& resident = textures_[snapshot.id];
    resident.generation = generation_;
    const ToonTexture& texture = snapshot.texture;
    if (texture.pixels == nullptr || texture.pixels->empty() ||
        texture.pixels->size() < static_cast<std::size_t>(texture.width) * texture.height * 4U) {
      resident.view.reset();
      resident.texture.reset();
      resident.revision = snapshot.revision;
      continue;
    }
    if (resident.texture && resident.revision == snapshot.revision) continue;
    const WGPUTextureFormat format = texture.encoding == ToonTextureEncoding::Srgb
                                         ? WGPUTextureFormat_RGBA8UnormSrgb
                                         : WGPUTextureFormat_RGBA8Unorm;
    const std::uint32_t levels = MipLevels(texture.width, texture.height);
    if (!resident.texture || resident.width != texture.width ||
        resident.height != texture.height || resident.format != format) {
      WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
      descriptor.label = View("texture");
      descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
                         WGPUTextureUsage_RenderAttachment;
      descriptor.size = {texture.width, texture.height, 1};
      descriptor.format = format;
      descriptor.mipLevelCount = levels;
      resident.texture.reset(wgpuDeviceCreateTexture(device_, &descriptor));
      if (!resident.texture) {
        detail = "creating a WebGPU texture failed";
        return false;
      }
      resident.view.reset(wgpuTextureCreateView(resident.texture.get(), nullptr));
      resident.width = texture.width;
      resident.height = texture.height;
      resident.format = format;
      resident.mip_levels = levels;
    }
    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = resident.texture.get();
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = texture.width * 4U;
    layout.rowsPerImage = texture.height;
    const WGPUExtent3D extent{texture.width, texture.height, 1};
    wgpuQueueWriteTexture(queue_, &destination, texture.pixels->data(),
        static_cast<std::size_t>(texture.width) * texture.height * 4U, &layout, &extent);
    if (levels > 1) pending_.push_back({resident.texture.get(), format, levels});
    resident.revision = snapshot.revision;
    ++uploads_;
  }
  for (auto texture = textures_.begin(); texture != textures_.end();) {
    if (texture->second.generation != generation_) {
      texture = textures_.erase(texture);
    } else {
      ++texture;
    }
  }
  return true;
}

void TextureCache::RecordMipmaps(WGPUCommandEncoder encoder) {
  for (const Mipmaps& mipmaps : pending_) {
    for (std::uint32_t level = 1; level < mipmaps.levels; ++level) {
      WGPUTextureViewDescriptor source_view = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
      source_view.baseMipLevel = level - 1;
      source_view.mipLevelCount = 1;
      TextureView source(wgpuTextureCreateView(mipmaps.texture, &source_view));
      WGPUTextureViewDescriptor target_view = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
      target_view.baseMipLevel = level;
      target_view.mipLevelCount = 1;
      TextureView target(wgpuTextureCreateView(mipmaps.texture, &target_view));
      BindGroup group = CreateGroup(device_, mip_layout_.get(),
          {ViewBinding(0, source.get()), SamplerBinding(1, mip_sampler_.get())}, "mipmap");
      WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
      color.view = target.get();
      color.loadOp = WGPULoadOp_Clear;
      color.storeOp = WGPUStoreOp_Store;
      WGPURenderPassDescriptor pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
      pass_descriptor.colorAttachmentCount = 1;
      pass_descriptor.colorAttachments = &color;
      RenderPassEncoder pass(wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor));
      wgpuRenderPassEncoderSetPipeline(pass.get(),
          mipmaps.format == WGPUTextureFormat_RGBA8UnormSrgb ? mip_srgb_.get()
                                                             : mip_linear_.get());
      wgpuRenderPassEncoderSetBindGroup(pass.get(), 0, group.get(), 0, nullptr);
      wgpuRenderPassEncoderDraw(pass.get(), 3, 1, 0, 0);
      wgpuRenderPassEncoderEnd(pass.get());
    }
  }
  pending_.clear();
}

WGPUTextureView TextureCache::ViewOf(TextureId texture) const {
  const auto found = textures_.find(texture);
  return found == textures_.end() || !found->second.view ? white_view_.get()
                                                         : found->second.view.get();
}

std::uint32_t TextureCache::Entry(TextureId texture) const {
  const auto found = textures_.find(texture);
  return found != textures_.end() && found->second.view ? 1U : 0U;
}

bool MaterialCache::Initialize(const Gpu& gpu, const ScenePipelines& pipelines,
    const TextureCache& textures, std::string& detail) {
  device_ = gpu.device.get();
  queue_ = gpu.queue.get();
  pipelines_ = &pipelines;
  white_ = textures.ViewOf(0);
  frame_buffer_ = CreateBuffer(device_, kFrameBytes,
      WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "frame");
  // No shared shadow view is drawn yet on this backend, so every light's
  // slot names none and receivers never read these; they are bound because
  // the receivers' code is there.
  WGPUTextureDescriptor shadow = WGPU_TEXTURE_DESCRIPTOR_INIT;
  shadow.label = View("shadow views");
  shadow.usage = WGPUTextureUsage_TextureBinding;
  shadow.size = {1, 1, 1};
  shadow.format = kDepthFormat;
  shadow_map_.reset(wgpuDeviceCreateTexture(device_, &shadow));
  WGPUTextureViewDescriptor shadow_view = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
  shadow_view.dimension = WGPUTextureViewDimension_2DArray;
  shadow_view.aspect = WGPUTextureAspect_DepthOnly;
  shadow_view_.reset(shadow_map_ ? wgpuTextureCreateView(shadow_map_.get(), &shadow_view)
                                 : nullptr);
  shadow_views_ = CreateBuffer(device_, kShadowViewBytes,
      WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "shadow views");
  if (!frame_buffer_ || !shadow_view_ || !shadow_views_) {
    detail = "creating the WebGPU frame resources failed";
    return false;
  }
  frame_group_ = CreateGroup(device_, pipelines.frame_layout(),
      {BufferBinding(0, frame_buffer_.get()), ViewBinding(1, shadow_view_.get()),
          BufferBinding(2, shadow_views_.get()),
          SamplerBinding(3, pipelines.shadow_sampler())},
      "frame");
  return frame_group_ && Reserve(64, detail);
}

BindGroup MaterialCache::MaterialGroup(
    const std::array<WGPUTextureView, kTextureRoles>& views,
    const std::array<std::uint32_t, kTextureRoles>& samplers) const {
  std::vector<WGPUBindGroupEntry> entries{
      BufferBinding(kPreviewMaterialsBinding, preview_buffer_.get())};
  for (std::uint32_t role = 0; role < kTextureRoles; ++role) {
    entries.push_back(ViewBinding(kFirstRoleTexture + role, views[role]));
    entries.push_back(
        SamplerBinding(kFirstRoleSampler + role, pipelines_->sampler(samplers[role])));
  }
  return CreateGroup(device_, pipelines_->material_layout(), entries, "material");
}

bool MaterialCache::Update(const DrawList& draws, const TextureCache& textures,
    std::string& detail, bool fast_values) {
  if (fast_values) {
    // The Vulkan backend's frame rows: time, direct-light count, material
    // debug mode, metres per unit; ambient; then each direct light in view
    // space (shaders/common/surface_common.slang).
    std::array<float, kFrameBytes / sizeof(float)> frame{};
    frame[0] = static_cast<float>(draws.time_seconds);
    frame[2] = static_cast<float>(draws.lighting.material);
    frame[3] = draws.meters_per_unit;
    const auto transform = [&](Float3 value, float w) {
      const auto& m = draws.view.view.m;
      return Float3{m[0] * value.x + m[4] * value.y + m[8] * value.z + m[12] * w,
          m[1] * value.x + m[5] * value.y + m[9] * value.z + m[13] * w,
          m[2] * value.x + m[6] * value.y + m[10] * value.z + m[14] * w};
    };
    std::uint32_t count = 0;
    const auto append = [&](const ToonLight& light, bool view_space) {
      if (!light.visible) return;
      if (light.type == ToonLightType::Ambient) {
        frame[4] += light.color.x * draws.lighting.ambient_scale;
        frame[5] += light.color.y * draws.lighting.ambient_scale;
        frame[6] += light.color.z * draws.lighting.ambient_scale;
        return;
      }
      if (count == kLightCapacity) return;
      float* slot = frame.data() + 8 + count++ * 16;
      slot[0] = light.color.x * draws.lighting.direct_scale;
      slot[1] = light.color.y * draws.lighting.direct_scale;
      slot[2] = light.color.z * draws.lighting.direct_scale;
      slot[3] = static_cast<float>(light.type);
      const Float3 position = view_space ? light.position : transform(light.position, 1.0F);
      slot[4] = position.x;
      slot[5] = position.y;
      slot[6] = position.z;
      slot[7] = light.radius;
      const Float3 direction = view_space ? light.direction : transform(light.direction, 0.0F);
      slot[8] = direction.x;
      slot[9] = direction.y;
      slot[10] = direction.z;
      constexpr float radians = 0.017453292519943295F;
      slot[11] = std::cos(light.cone_angle * radians);
      slot[12] = std::cos(light.cone_angle * (1.0F - light.cone_softness) * radians);
      // slot[13], the shared view it casts into plus one, stays 0: this
      // backend draws no shadow views yet.
    };
    if (!draws.lighting.scene_lights || draws.lights.empty()) {
      ToonLight key;
      key.direction = {-draws.lighting.key_direction.x,
          -draws.lighting.key_direction.y, -draws.lighting.key_direction.z};
      if (key.direction == Float3{}) {
        key.direction = {-0.25F, -0.5F, -1.0F};
      }
      append(key, true);
      ToonLight ambient;
      ambient.type = ToonLightType::Ambient;
      ambient.color = {0.25F, 0.25F, 0.25F};
      append(ambient, true);
    } else {
      for (const auto& light : draws.lights) {
        append(light.light, false);
      }
    }
    frame[1] = static_cast<float>(count);
    wgpuQueueWriteBuffer(queue_, frame_buffer_.get(), 0, frame.data(), kFrameBytes);
  }

  ++generation_;
  bool written = false;
  for (const MaterialSnapshot& material : draws.materials) {
    auto found = entries_.find(material.id);
    if (found == entries_.end()) {
      std::uint32_t slot = 0;
      if (!free_slots_.empty()) {
        slot = free_slots_.back();
        free_slots_.pop_back();
      } else {
        if (next_slot_ == capacity_ && !Reserve(capacity_ * 2U, detail)) return false;
        slot = next_slot_++;
      }
      found = entries_.emplace(material.id, Entry{slot}).first;
    }
    Entry& entry = found->second;
    const ToonMaterial& values = material.material;
    const ToonTextureRef* refs[kPreviewRoles] = {&values.base_texture,
        &values.emissive_texture, &values.preview.metallic_texture,
        &values.preview.roughness_texture, &values.preview.opacity_texture,
        &values.normal_texture};
    std::array<std::uint32_t, kPreviewRoles> entries{};
    std::array<WGPUTextureView, kTextureRoles> views;
    views.fill(white_);
    std::array<std::uint32_t, kTextureRoles> samplers{};
    if (values.model == ToonShadingModel::PreviewSurface) {
      for (std::uint32_t role = 0; role < kPreviewRoles; ++role) {
        entries[role] = textures.Entry(refs[role]->texture);
        views[role] = textures.ViewOf(refs[role]->texture);
        samplers[role] = SamplerIndex(*refs[role]);
      }
    }
    if (fast_values && (entry.parameters_revision != material.parameters_revision ||
                           entry.entries != entries)) {
      const auto write_start = SteadyNanoseconds();
      PreviewParameters& slot = preview_slots_[entry.slot];
      slot = {};
      if (values.model == ToonShadingModel::PreviewSurface) {
        slot.base_color[0] = values.base_color.x;
        slot.base_color[1] = values.base_color.y;
        slot.base_color[2] = values.base_color.z;
        slot.base_color[3] = values.alpha;
        slot.emissive[0] = values.emissive.x;
        slot.emissive[1] = values.emissive.y;
        slot.emissive[2] = values.emissive.z;
        slot.surface[0] = values.preview.metallic;
        slot.surface[1] = values.preview.roughness;
        slot.surface[2] = values.alpha_mode == ToonAlphaMode::Mask ? values.alpha_cutoff : -1.0F;
        slot.normal[0] = values.preview.normal.x;
        slot.normal[1] = values.preview.normal.y;
        slot.normal[2] = values.preview.normal.z;
        for (std::size_t i = 0; i < values.preview.inputs.size() && i < kPreviewRoles; ++i) {
          const auto& input = values.preview.inputs[i];
          auto& target = slot.inputs[i];
          WriteUvRows(*refs[i], target.uv_rows);
          target.uv_rows[3] = input.uv_fallback.x;
          target.uv_rows[7] = input.uv_fallback.y;
          const auto copy = [](const Float4& value, float* out) {
            out[0] = value.x; out[1] = value.y; out[2] = value.z; out[3] = value.w;
          };
          copy(input.scale, target.scale);
          copy(input.bias, target.bias);
          copy(input.fallback, target.fallback);
          target.info[0] = entries[i];
          target.info[1] = SamplerIndex(*refs[i]);
          target.info[2] = input.channel;
          target.info[3] = input.connected ? 1U : 0U;
        }
      }
      wgpuQueueWriteBuffer(queue_, preview_buffer_.get(),
          static_cast<std::uint64_t>(entry.slot) * sizeof(PreviewParameters), &slot,
          sizeof(PreviewParameters));
      entry.parameters_revision = material.parameters_revision;
      entry.entries = entries;
      entry.model = values.model;
      entry.preview_enabled =
          values.model == ToonShadingModel::PreviewSurface && values.preview.enabled;
      entry.unlit_color = values.base_color;
      entry.double_sided = values.double_sided;
      entry.transparent = IsTransparent(values);
      entry.queue = RenderQueue(values);
      entry.depth_write = WritesDepth(values);
      ++writes_;
      written = true;
      parameter_write_ms_ += static_cast<double>(SteadyNanoseconds() - write_start) / 1e6;
    }
    // A bind group holds views and samplers, so a texture that becomes
    // resident, leaves or is reallocated, a wrap edit, or a grown buffer
    // rebuilds it; a value edit does not.
    if (!entry.group || entry.views != views || entry.samplers != samplers ||
        entry.buffer_generation != buffer_generation_) {
      entry.group = MaterialGroup(views, samplers);
      entry.views = views;
      entry.samplers = samplers;
      entry.buffer_generation = buffer_generation_;
      if (!entry.group) {
        detail = "creating a WebGPU material bind group failed";
        return false;
      }
    }
    entry.generation = generation_;
  }
  for (auto entry = entries_.begin(); entry != entries_.end();) {
    if (entry->second.generation != generation_) {
      free_slots_.push_back(entry->second.slot);
      entry = entries_.erase(entry);
    } else {
      ++entry;
    }
  }
  (void)written;
  return true;
}

const MaterialCache::Entry* MaterialCache::Find(MaterialId material) const {
  const auto found = entries_.find(material);
  return found == entries_.end() ? nullptr : &found->second;
}

bool MaterialCache::Reserve(std::uint32_t slots, std::string& detail) {
  // A grown buffer is a new one with every slot copied, so growing
  // rewrites no material but rebuilds every material's bind group.
  Buffer grown = CreateBuffer(device_,
      static_cast<std::uint64_t>(slots) * sizeof(PreviewParameters),
      WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "preview materials");
  if (!grown) {
    detail = "creating the WebGPU material buffer failed";
    return false;
  }
  preview_slots_.resize(slots);
  wgpuQueueWriteBuffer(queue_, grown.get(), 0, preview_slots_.data(),
      preview_slots_.size() * sizeof(PreviewParameters));
  preview_buffer_ = std::move(grown);
  ++buffer_generation_;
  capacity_ = slots;
  std::array<WGPUTextureView, kTextureRoles> views;
  views.fill(white_);
  display_group_ = MaterialGroup(views, {});
  return static_cast<bool>(display_group_);
}

bool MeshCache::Initialize(const Gpu& gpu, const ScenePipelines& pipelines,
    std::string& detail) {
  device_ = gpu.device.get();
  queue_ = gpu.queue.get();
  pipelines_ = &pipelines;
  const Float2 zero_uv;
  const Float4 zero_tangent;
  const Matrix4 identity;
  zero_uv_ = CreateBuffer(device_, sizeof(zero_uv),
      WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, "zero uv");
  zero_tangent_ = CreateBuffer(device_, sizeof(zero_tangent),
      WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, "zero tangent");
  zero_skin_ = CreateBuffer(device_, sizeof(identity),
      WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "zero skin");
  if (!zero_uv_ || !zero_tangent_ || !zero_skin_) {
    detail = "creating the WebGPU mesh fallbacks failed";
    return false;
  }
  wgpuQueueWriteBuffer(queue_, zero_uv_.get(), 0, &zero_uv, sizeof(zero_uv));
  wgpuQueueWriteBuffer(queue_, zero_tangent_.get(), 0, &zero_tangent, sizeof(zero_tangent));
  wgpuQueueWriteBuffer(queue_, zero_skin_.get(), 0, &identity, sizeof(identity));
  std::vector<WGPUBindGroupEntry> entries;
  for (std::uint32_t binding = 0; binding < 5; ++binding) {
    entries.push_back(BufferBinding(binding, zero_skin_.get()));
  }
  undeformed_ = CreateGroup(device_, pipelines.deformation_layout(), entries, "undeformed");
  empty_group_ = CreateGroup(device_, pipelines.empty_layout(), {}, "empty");
  if (!undeformed_ || !empty_group_) {
    detail = "creating the WebGPU deformation fallback failed";
    return false;
  }
  return true;
}

bool MeshCache::Upload(Stream& stream, WGPUBufferUsage usage, const void* data,
    std::uint64_t size, bool* grown) {
  const std::uint64_t padded = (std::max<std::uint64_t>(size, 4) + 3U) & ~std::uint64_t{3};
  if (!stream.buffer || stream.capacity < padded) {
    stream.buffer = CreateBuffer(device_, padded, usage | WGPUBufferUsage_CopyDst, "mesh");
    if (!stream.buffer) return false;
    stream.capacity = padded;
    if (grown != nullptr) *grown = true;
  }
  if (size == padded) {
    wgpuQueueWriteBuffer(queue_, stream.buffer.get(), 0, data, static_cast<std::size_t>(size));
  } else {
    // A queue write is a multiple of four bytes.
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(padded), 0);
    if (size != 0) std::memcpy(bytes.data(), data, static_cast<std::size_t>(size));
    wgpuQueueWriteBuffer(queue_, stream.buffer.get(), 0, bytes.data(), bytes.size());
  }
  return true;
}

bool MeshCache::Update(const DrawList& draws, std::string& detail, bool fast_values) {
  ++generation_;
  const auto fail = [&] {
    detail = "uploading WebGPU mesh data failed";
    return false;
  };
  for (const MeshSnapshot& mesh : draws.draws) {
    Entry& entry = entries_[mesh.id];
    if (entry.topology_revision != mesh.topology_revision) {
      if (!Upload(entry.indices, WGPUBufferUsage_Index, mesh.indices->data(),
              mesh.indices->size() * sizeof(std::uint32_t))) {
        return fail();
      }
      entry.topology_revision = mesh.topology_revision;
      entry.index_count = static_cast<std::uint32_t>(mesh.indices->size());
      ++topology_uploads_;
    }
    if (entry.points_revision != mesh.points_revision) {
      if (!Upload(entry.vertices, WGPUBufferUsage_Vertex, mesh.points->data(),
              mesh.points->size() * sizeof(Float3))) {
        return fail();
      }
      entry.points_revision = mesh.points_revision;
      ++point_uploads_;
    }
    if (entry.normals_revision != mesh.normals_revision) {
      if (!Upload(entry.normals, WGPUBufferUsage_Vertex, mesh.normals->data(),
              mesh.normals->size() * sizeof(Float3))) {
        return fail();
      }
      entry.normals_revision = mesh.normals_revision;
    }
    const bool uvs_present = mesh.uvs != nullptr && !mesh.uvs->empty();
    if (entry.uvs_revision != mesh.uvs_revision) {
      if (uvs_present && !Upload(entry.uvs, WGPUBufferUsage_Vertex, mesh.uvs->data(),
                             mesh.uvs->size() * sizeof(Float2))) {
        return fail();
      }
      entry.uvs_revision = mesh.uvs_revision;
    }
    entry.has_uvs = uvs_present && mesh.uvs->size() >= mesh.index_bound;
    const bool tangents_present = mesh.tangents && !mesh.tangents->empty();
    if (entry.tangents_revision != mesh.tangents_revision) {
      if (tangents_present &&
          !Upload(entry.tangents, WGPUBufferUsage_Vertex, mesh.tangents->data(),
              mesh.tangents->size() * sizeof(Float4))) {
        return fail();
      }
      entry.tangents_revision = mesh.tangents_revision;
    }
    entry.has_tangents = tangents_present && mesh.tangents->size() >= mesh.index_bound;
    if (!UpdateDeformation(mesh, entry, fast_values)) return fail();
    entry.generation = generation_;
  }
  for (auto entry = entries_.begin(); entry != entries_.end();) {
    if (entry->second.generation != generation_) {
      entry = entries_.erase(entry);
    } else {
      ++entry;
    }
  }
  return true;
}

// As the Vulkan backend's UpdateSkin: a pose change writes the joint buffer
// and nothing else; a skin change uploads the influences and rewrites the
// joints; morph targets and weights have their own revisions.
bool MeshCache::UpdateDeformation(const MeshSnapshot& mesh, Entry& entry,
    bool fast_values) {
  const bool was_skinned = entry.skinned;
  const bool was_morphed = entry.morphed;
  entry.skinned = IsSkinned(mesh);
  entry.morphed = IsMorphed(mesh);
  if (!entry.skinned && !entry.morphed) {
    entry.skin_flags = 0;
    return true;
  }
  bool rebind = !entry.deformation || was_skinned != entry.skinned ||
                was_morphed != entry.morphed;
  if (entry.skinned && entry.skin_revision != mesh.skin_revision) {
    if (!Upload(entry.influences, WGPUBufferUsage_Storage, mesh.influences->data(),
            mesh.influences->size() * sizeof(ToonJointInfluence), &rebind)) {
      return false;
    }
    entry.skin_revision = mesh.skin_revision;
    entry.pose_revision = 0;
    ++skin_uploads_;
  }
  if (fast_values && entry.skinned && entry.pose_revision != mesh.pose_revision) {
    const auto write_start = SteadyNanoseconds();
    joint_scratch_.clear();
    joint_scratch_.push_back(mesh.skeleton_to_mesh);
    for (const Matrix4& joint : *mesh.joints) {
      joint_scratch_.push_back(Multiply(joint, mesh.geom_bind));
    }
    if (!Upload(entry.joints, WGPUBufferUsage_Storage, joint_scratch_.data(),
            joint_scratch_.size() * sizeof(Matrix4), &rebind)) {
      return false;
    }
    entry.pose_revision = mesh.pose_revision;
    ++pose_writes_;
    pose_write_ms_ += static_cast<double>(SteadyNanoseconds() - write_start) / 1e6;
  }
  if (entry.morphed && entry.morph_revision != mesh.morph_revision) {
    if (!Upload(entry.morph_offsets, WGPUBufferUsage_Storage, mesh.morph_offsets->data(),
            mesh.morph_offsets->size() * sizeof(ToonMorphOffset), &rebind) ||
        !Upload(entry.morph_ranges, WGPUBufferUsage_Storage, mesh.morph_ranges->data(),
            mesh.morph_ranges->size() * sizeof(ToonMorphRange), &rebind)) {
      return false;
    }
    entry.morph_revision = mesh.morph_revision;
    ++morph_uploads_;
  }
  if (fast_values && entry.morphed &&
      entry.morph_weights_revision != mesh.morph_weights_revision) {
    const auto write_start = SteadyNanoseconds();
    const std::uint64_t bytes = mesh.morph_weights->size() * sizeof(float);
    if (!Upload(entry.morph_weights, WGPUBufferUsage_Storage, mesh.morph_weights->data(),
            bytes, &rebind)) {
      return false;
    }
    // arrayLength sees the bound range, so a weight count change rebinds.
    rebind = rebind || bytes != entry.morph_weight_bytes;
    entry.morph_weight_bytes = bytes;
    entry.morph_weights_revision = mesh.morph_weights_revision;
    ++morph_weight_writes_;
    morph_write_ms_ += static_cast<double>(SteadyNanoseconds() - write_start) / 1e6;
  }
  if (rebind) BindDeformation(entry);
  entry.skin_flags = (entry.skinned ? kDrawSkinned |
                         (mesh.constant_influences ? kDrawConstantInfluences : 0U) |
                         (mesh.influences_per_point << kInfluenceCountShift)
                                    : 0U) |
                     (entry.morphed ? kDrawMorphed : 0U);
  return static_cast<bool>(entry.deformation);
}

void MeshCache::BindDeformation(Entry& entry) {
  std::vector<WGPUBindGroupEntry> bindings;
  for (std::uint32_t binding = 0; binding < 5; ++binding) {
    bindings.push_back(BufferBinding(binding, zero_skin_.get()));
  }
  if (entry.skinned) {
    bindings[0].buffer = entry.influences.buffer.get();
    if (entry.joints.buffer) bindings[1].buffer = entry.joints.buffer.get();
  }
  if (entry.morphed) {
    bindings[2].buffer = entry.morph_offsets.buffer.get();
    bindings[3].buffer = entry.morph_ranges.buffer.get();
    if (entry.morph_weights.buffer) {
      bindings[4].buffer = entry.morph_weights.buffer.get();
      bindings[4].size = std::max<std::uint64_t>(entry.morph_weight_bytes, 4);
    }
  }
  entry.deformation = CreateGroup(device_, pipelines_->deformation_layout(), bindings,
      "deformation");
}

std::size_t MeshCache::DrawBound(const DrawList& draws) {
  std::size_t bound = 0;
  for (const MeshSnapshot& mesh : draws.draws) {
    bound += mesh.parts != nullptr && !mesh.parts->empty() ? mesh.parts->size() : 1U;
  }
  return bound;
}

bool MeshCache::Supported(const DrawList& draws, const MaterialCache& materials,
    std::string& detail) {
  for (const MeshSnapshot& mesh : draws.draws) {
    MeshPart whole;
    const std::uint32_t count =
        mesh.indices != nullptr ? static_cast<std::uint32_t>(mesh.indices->size()) : 0U;
    for (const MeshPart& part : PartsOf(mesh, count, whole)) {
      const MaterialCache::Entry* material = materials.Find(part.material);
      if (material != nullptr && (material->model == ToonShadingModel::MToon ||
                                     material->model == ToonShadingModel::MMD)) {
        detail = "the WebGPU backend draws MToon and MMD materials from stage 2 "
                 "of v0.7.0 (WebGPU policy §10)";
        return false;
      }
    }
  }
  return true;
}

bool MeshCache::Record(WGPURenderPassEncoder pass, ScenePipelines& pipelines,
    const MaterialCache& materials, const DrawList& draws, WGPUBindGroup ring,
    std::vector<std::uint8_t>& constants, SceneRecord& record, std::string& detail) {
  const Matrix4 clip_from_world = WebGpuClipFromWorld(draws.view);
  MaterialCache::Entry display_material;
  display_material.preview_enabled = true;
  display_material.double_sided = true;
  const auto surface_material =
      [&](const MeshPart& part) -> const MaterialCache::Entry* {
    const MaterialCache::Entry* material = materials.Find(part.material);
    if (material == nullptr) return &display_material;
    return material->preview_enabled ? material : nullptr;
  };
  std::uint32_t draw_count = 0;
  const auto push = [&](const void* data, std::size_t size) {
    const std::size_t offset = static_cast<std::size_t>(draw_count++) * kDrawStride;
    constants.resize(offset + kDrawStride);
    std::memcpy(constants.data() + offset, data, size);
    return static_cast<std::uint32_t>(offset);
  };
  WGPURenderPipeline bound = nullptr;
  const auto bind = [&](const ScenePipelines::Key& key) {
    WGPURenderPipeline pipeline = pipelines.Get(key, detail);
    if (pipeline == nullptr) return false;
    if (pipeline != bound) {
      wgpuRenderPassEncoderSetPipeline(pass, pipeline);
      ++record.pipeline_binds;
      bound = pipeline;
    }
    return true;
  };
  MeshPart whole;

  // The unlit draws: bound diagnostic materials' own colour.
  for (const MeshSnapshot& mesh : draws.draws) {
    const auto found = entries_.find(mesh.id);
    if (found == entries_.end()) continue;
    const Entry& entry = found->second;
    const auto parts = PartsOf(mesh, entry.index_count, whole);
    DrawConstants draw{};
    const Matrix4 clip_from_object = Multiply(clip_from_world, mesh.transform);
    std::memcpy(draw.clip_from_object, clip_from_object.m.data(), sizeof(draw.clip_from_object));
    draw.color[3] = 1.0F;
    draw.flags = entry.skin_flags;
    for (const MeshPart& part : parts) {
      if (surface_material(part) != nullptr || !Fits(part, entry.index_count)) continue;
      if (!bind({ScenePipelines::Kind::Mesh, WGPUCullMode_None, WGPUFrontFace_CCW, true,
              true, true})) {
        return false;
      }
      const auto* material = materials.Find(part.material);
      const Float3 color = material ? material->unlit_color : mesh.color;
      draw.color[0] = color.x;
      draw.color[1] = color.y;
      draw.color[2] = color.z;
      const std::uint32_t offset = push(&draw, sizeof(draw));
      wgpuRenderPassEncoderSetBindGroup(pass, 0, empty_group_.get(), 0, nullptr);
      wgpuRenderPassEncoderSetBindGroup(pass, 1, empty_group_.get(), 0, nullptr);
      wgpuRenderPassEncoderSetBindGroup(pass, 2,
          (entry.skinned || entry.morphed) ? entry.deformation.get() : undeformed_.get(), 0,
          nullptr);
      wgpuRenderPassEncoderSetBindGroup(pass, 3, ring, 1, &offset);
      wgpuRenderPassEncoderSetVertexBuffer(pass, 0, entry.vertices.buffer.get(), 0,
          WGPU_WHOLE_SIZE);
      wgpuRenderPassEncoderSetIndexBuffer(pass, entry.indices.buffer.get(),
          WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
      wgpuRenderPassEncoderDrawIndexed(pass, part.index_count, 1, part.first_index, 0, 0);
      ++record.unlit;
      record.triangles += part.index_count / 3U;
    }
  }

  const float projection_scale = std::fabs(draws.view.projection.m[5]);
  const float units_per_meter = 1.0F / draws.meters_per_unit;
  const bool orthographic = draws.view.projection.m[11] == 0.0F;
  const auto draw_surface = [&](const MeshSnapshot& mesh, const Entry& entry,
                                const MeshPart& part, const MaterialCache::Entry& material) {
    SurfaceDrawConstants draw{};
    const Matrix4 clip_from_object = Multiply(clip_from_world, mesh.transform);
    std::memcpy(draw.clip_from_object, clip_from_object.m.data(), sizeof(draw.clip_from_object));
    const bool mirrored =
        ViewRows(Multiply(draws.view.view, mesh.transform), draw.view_from_object_rows);
    draw.material_slot = material.slot;
    draw.flags = (entry.has_uvs ? kDrawHasUVs : 0U) | (orthographic ? kDrawOrthographic : 0U) |
                 (material.transparent ? kDrawBlend : 0U) | entry.skin_flags;
    draw.projection_scale = projection_scale;
    draw.outline_scale = units_per_meter;
    const bool display = &material == &display_material;
    if (display) {
      draw.flags |= kDrawDisplayColor;
      draw.material_slot = std::bit_cast<std::uint32_t>(mesh.color.x);
      draw.projection_scale = mesh.color.y;
      draw.outline_scale = mesh.color.z;
    }
    if (!bind({ScenePipelines::Kind::Preview,
            material.double_sided ? WGPUCullMode_None : WGPUCullMode_Back,
            mirrored ? WGPUFrontFace_CW : WGPUFrontFace_CCW, material.depth_write,
            entry.has_uvs, entry.has_tangents})) {
      return false;
    }
    const std::uint32_t offset = push(&draw, sizeof(draw));
    wgpuRenderPassEncoderSetBindGroup(pass, 0, materials.frame_group(), 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1,
        display ? materials.display_group() : material.group.get(), 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 2,
        (entry.skinned || entry.morphed) ? entry.deformation.get() : undeformed_.get(), 0,
        nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 3, ring, 1, &offset);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, entry.vertices.buffer.get(), 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, entry.normals.buffer.get(), 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 2,
        entry.has_uvs ? entry.uvs.buffer.get() : zero_uv_.get(), 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 3,
        entry.has_tangents ? entry.tangents.buffer.get() : zero_tangent_.get(), 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, entry.indices.buffer.get(),
        WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, part.index_count, 1, part.first_index, 0, 0);
    record.triangles += part.index_count / 3U;
    return true;
  };

  // The opaque surfaces, then the transparent ones in the Vulkan backend's
  // order: by render queue, then mesh, then part.
  transparent_scratch_.clear();
  for (std::size_t index = 0; index < draws.draws.size(); ++index) {
    const MeshSnapshot& mesh = draws.draws[index];
    const auto found = entries_.find(mesh.id);
    if (found == entries_.end()) continue;
    const Entry& entry = found->second;
    const auto parts = PartsOf(mesh, entry.index_count, whole);
    for (std::size_t at = 0; at < parts.size(); ++at) {
      const MeshPart& part = parts[at];
      const MaterialCache::Entry* material = surface_material(part);
      if (material == nullptr || !Fits(part, entry.index_count)) continue;
      if (material->transparent) {
        transparent_scratch_.emplace_back(index, at);
        continue;
      }
      if (!draw_surface(mesh, entry, part, *material)) return false;
      ++record.opaque;
    }
  }
  const auto transparent_part = [&](const std::pair<std::size_t, std::size_t>& draw) {
    const MeshSnapshot& mesh = draws.draws[draw.first];
    return PartsOf(mesh, entries_.find(mesh.id)->second.index_count, whole)[draw.second];
  };
  const auto transparent_key = [&](const std::pair<std::size_t, std::size_t>& draw) {
    return std::make_tuple(surface_material(transparent_part(draw))->queue, draw.first,
        static_cast<std::int64_t>(draw.second));
  };
  std::stable_sort(transparent_scratch_.begin(), transparent_scratch_.end(),
      [&](const auto& left, const auto& right) {
        return transparent_key(left) < transparent_key(right);
      });
  for (const auto& draw : transparent_scratch_) {
    const MeshSnapshot& mesh = draws.draws[draw.first];
    const Entry& entry = entries_.find(mesh.id)->second;
    const MeshPart part = transparent_part(draw);
    if (!draw_surface(mesh, entry, part, *surface_material(part))) return false;
    ++record.transparent;
  }
  return true;
}

} // namespace Toon::webgpu_internal
