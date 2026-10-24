// SPDX-License-Identifier: Apache-2.0
#include "webgpu_internal.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <span>
#include <tuple>
#include <type_traits>

namespace Toon::webgpu_internal {
namespace {

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

std::uint32_t MipLevels(std::uint32_t width, std::uint32_t height) {
  std::uint32_t levels = 1;
  for (std::uint32_t size = std::max(width, height); size > 1U; size /= 2U) {
    ++levels;
  }
  return levels;
}

// What a material's bind group holds per role of its model (WebGPU policy
// §4): each texture's view, the serial that tells a rebuilt view from an
// old one, and its sampler; and the slot's entries, 1 for a texture the
// role has. A role its model does not declare binds the white.
struct Roles {
  MaterialEntries entries;
  std::array<WGPUTextureView, kTextureRoles> views{};
  std::array<std::uint64_t, kTextureRoles> serials{};
  std::array<std::uint32_t, kTextureRoles> samplers{};
};

Roles RolesOf(const ToonMaterial& values, const TextureCache& textures,
    WGPUTextureView white) {
  Roles roles;
  roles.views.fill(white);
  MaterialEntries& entries = roles.entries;
  const auto bind = [&](std::uint32_t role, const ToonTextureRef& texture,
                        std::uint32_t& entry) {
    entry = textures.Entry(texture.texture);
    roles.views[role] = textures.ViewOf(texture.texture);
    roles.serials[role] = textures.SerialOf(texture.texture);
    roles.samplers[role] = SamplerIndex(texture);
  };
  switch (values.model) {
  case ToonShadingModel::MToon:
    bind(kMToonBase, values.base_texture, entries.base);
    bind(kMToonShade, values.mtoon.shade_texture, entries.shade);
    bind(kMToonNormal, values.normal_texture, entries.normal);
    bind(kMToonEmissive, values.emissive_texture, entries.emissive);
    bind(kMToonRim, values.mtoon.rim_multiply_texture, entries.rim);
    bind(kMToonMatcap, values.mtoon.matcap_texture, entries.matcap);
    bind(kMToonOutlineWidth, values.mtoon.outline_width_texture, entries.outline);
    bind(kMToonMask, values.mtoon.uv_animation_mask_texture, entries.mask);
    bind(kMToonShift, values.mtoon.shading_shift_texture, entries.shift);
    break;
  case ToonShadingModel::MMD:
    bind(kMmdBase, values.base_texture, entries.base);
    bind(kMmdSphere, values.mmd.sphere_texture, entries.sphere);
    // The ramp is its own texture or the renderer's shared one, read
    // through the clamp sampler whatever the texture's wrap; a material
    // without one samples white.
    if (values.mmd.toon_source == ToonRampSource::Individual) {
      bind(kMmdToon, values.mmd.toon_texture, entries.toon);
    } else if (values.mmd.toon_source == ToonRampSource::Shared) {
      roles.views[kMmdToon] = textures.RampView(values.mmd.shared_toon_index);
      roles.serials[kMmdToon] = textures.RampSerial(values.mmd.shared_toon_index);
      entries.toon = roles.serials[kMmdToon] != 0 ? 1U : 0U;
    }
    roles.samplers[kMmdToon] = kClampSampler;
    break;
  case ToonShadingModel::PreviewSurface: {
    const ToonTextureRef* inputs[kPreviewRoles] = {&values.base_texture,
        &values.emissive_texture, &values.preview.metallic_texture,
        &values.preview.roughness_texture, &values.preview.opacity_texture,
        &values.normal_texture};
    std::uint32_t* input_entries[kPreviewRoles] = {&entries.base, &entries.emissive,
        &entries.metallic, &entries.roughness, &entries.opacity, &entries.normal};
    for (std::uint32_t role = 0; role < kPreviewRoles; ++role) {
      bind(role, *inputs[role], *input_entries[role]);
    }
    break;
  }
  }
  return roles;
}

} // namespace

bool ScenePipelines::Initialize(WGPUDevice device, const ShaderSources& sources,
    std::uint32_t samples, std::string& detail) {
  device_ = device;
  samples_ = samples;
  const struct {
    Kind kind;
    const std::string* vertex;
    const std::string* fragment;
    const char* label;
  } modules[] = {
      {Kind::Mesh, &sources.mesh_vertex, &sources.mesh_fragment, "mesh"},
      {Kind::Preview, &sources.preview_vertex, &sources.preview_fragment, "preview_surface"},
      {Kind::MToon, &sources.mtoon_vertex, &sources.mtoon_fragment, "mtoon"},
      {Kind::MToonCoverage, &sources.mtoon_coverage_vertex, &sources.mtoon_coverage_fragment,
          "mtoon_coverage"},
      {Kind::MToonTransparent, &sources.mtoon_transparent_vertex,
          &sources.mtoon_transparent_fragment, "mtoon_transparent"},
      {Kind::MToonOutline, &sources.mtoon_outline_vertex, &sources.mtoon_outline_fragment,
          "mtoon_outline"},
      {Kind::Mmd, &sources.mmd_vertex, &sources.mmd_fragment, "mmd"},
      {Kind::MmdTransparent, &sources.mmd_transparent_vertex,
          &sources.mmd_transparent_fragment, "mmd_transparent"},
      {Kind::MmdOutline, &sources.mmd_outline_vertex, &sources.mmd_outline_fragment,
          "mmd_outline"},
      {Kind::Shadow, &sources.shadow_vertex, &sources.shadow_fragment, "shadow"},
  };
  bool shaders = true;
  for (const auto& module : modules) {
    Stages& stages = stages_[static_cast<std::size_t>(module.kind)];
    stages.vertex = CreateShader(device, *module.vertex, module.label);
    stages.fragment = CreateShader(device, *module.fragment, module.label);
    shaders = shaders && stages.vertex && stages.fragment;
  }

  // Group 0, the frame: its buffer, the shadow views' image, their
  // parameters, the gather sampler and MMD's reading of the views (WebGPU
  // policy §4). The image is depth32float, which WGSL reads as an
  // unfilterable float. Only the frame buffer is the vertex stage's, for
  // mmd_outline's metres per unit: core WebGPU gives a stage eight storage
  // buffers, and the frame's, two models' and five deformation buffers are
  // the vertex stage's eight.
  frame_layout_ = CreateLayout(device,
      {BufferEntry(0, kBothStages, WGPUBufferBindingType_ReadOnlyStorage),
          TextureEntry(1, WGPUShaderStage_Fragment, WGPUTextureSampleType_UnfilterableFloat,
              WGPUTextureViewDimension_2DArray),
          BufferEntry(2, WGPUShaderStage_Fragment, WGPUBufferBindingType_ReadOnlyStorage),
          SamplerEntry(3, WGPUShaderStage_Fragment, WGPUSamplerBindingType_NonFiltering),
          BufferEntry(4, WGPUShaderStage_Fragment, WGPUBufferBindingType_ReadOnlyStorage)},
      "frame");
  // The caster pass's group 0: the frame buffer alone, for a Mask cut's
  // UV animation time, since the pass draws into the views' image.
  caster_frame_layout_ = CreateLayout(device,
      {BufferEntry(0, kBothStages, WGPUBufferBindingType_ReadOnlyStorage)}, "caster frame");
  // Group 1, a material: every model's buffer and a texture and sampler
  // per role. mtoon_outline's vertex stage reads its MToon slot and two of
  // its roles, and mmd_outline's its MMD slot; PreviewSurface is read by
  // fragments alone.
  std::vector<WGPUBindGroupLayoutEntry> material{
      BufferEntry(kMToonMaterialsBinding, kBothStages, WGPUBufferBindingType_ReadOnlyStorage),
      BufferEntry(kMmdMaterialsBinding, kBothStages, WGPUBufferBindingType_ReadOnlyStorage),
      BufferEntry(kPreviewMaterialsBinding, WGPUShaderStage_Fragment,
          WGPUBufferBindingType_ReadOnlyStorage)};
  for (std::uint32_t role = 0; role < kTextureRoles; ++role) {
    material.push_back(TextureEntry(kFirstRoleTexture + role, kBothStages,
        WGPUTextureSampleType_Float, WGPUTextureViewDimension_2D));
    material.push_back(SamplerEntry(kFirstRoleSampler + role, kBothStages,
        WGPUSamplerBindingType_Filtering));
  }
  material_layout_ = CreateLayout(device, material, "material");
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
  surface_layout_ = CreatePipelineLayout(device,
      {frame_layout_.get(), material_layout_.get(), deformation_layout_.get(),
          draw_layout_.get()},
      "surface");
  caster_layout_ = CreatePipelineLayout(device,
      {caster_frame_layout_.get(), material_layout_.get(), deformation_layout_.get(),
          draw_layout_.get()},
      "caster");

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

  if (!shaders || !frame_layout_ || !caster_frame_layout_ || !material_layout_ ||
      !deformation_layout_ || !draw_layout_ || !mesh_layout_ || !surface_layout_ ||
      !caster_layout_ || !shadow_sampler_) {
    detail = "creating the WebGPU scene layouts or shader modules failed";
    return false;
  }
  return true;
}

WGPURenderPipeline ScenePipelines::Get(const Key& key, std::string& detail) {
  for (const auto& [existing, pipeline] : pipelines_) {
    if (existing == key) return pipeline.get();
  }
  const Kind kind = key.kind;
  if (kind == Kind::Shadow) return CreateCaster(key, detail);
  const bool mesh = kind == Kind::Mesh;
  const bool outline = kind == Kind::MToonOutline || kind == Kind::MmdOutline;
  // Position, normal, UV and a fourth stream, as the Vulkan pipelines'
  // streams: the tangent frame, MMD's sub-texture UV or its edge scale. The
  // unlit mesh reads the position alone.
  WGPUVertexFormat fourth_format = WGPUVertexFormat_Float32x4;
  std::uint64_t fourth_stride = sizeof(Float4);
  if (kind == Kind::Mmd || kind == Kind::MmdTransparent) {
    fourth_format = WGPUVertexFormat_Float32x2;
    fourth_stride = sizeof(Float2);
  } else if (kind == Kind::MmdOutline) {
    fourth_format = WGPUVertexFormat_Float32;
    fourth_stride = sizeof(float);
  }
  const WGPUVertexAttribute attributes[4] = {
      {nullptr, WGPUVertexFormat_Float32x3, 0, 0},
      {nullptr, WGPUVertexFormat_Float32x3, 0, 1},
      {nullptr, WGPUVertexFormat_Float32x2, 0, 2},
      {nullptr, fourth_format, 0, 3},
  };
  WGPUVertexBufferLayout buffers[4]{};
  const std::uint64_t strides[4] = {sizeof(Float3), sizeof(Float3),
      key.uvs ? sizeof(Float2) : 0U, key.fourth ? fourth_stride : 0U};
  for (std::uint32_t stream = 0; stream < 4; ++stream) {
    buffers[stream].stepMode = WGPUVertexStepMode_Vertex;
    buffers[stream].arrayStride = strides[stream];
    buffers[stream].attributeCount = 1;
    buffers[stream].attributes = &attributes[stream];
  }

  // Source over: the fragment's alpha blends it over the target, so one
  // that returns 1 draws as an opaque one would. One fixed PreviewSurface
  // pipeline blends, its Opaque and Mask returning 1; MToon's and MMD's
  // opaque surfaces blend nothing, as the Vulkan backend's.
  WGPUBlendState blend{};
  blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha,
      WGPUBlendFactor_OneMinusSrcAlpha};
  blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One,
      WGPUBlendFactor_OneMinusSrcAlpha};
  const bool blends = kind == Kind::Preview || kind == Kind::MToonTransparent || outline ||
                      kind == Kind::MmdTransparent;
  WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
  target.format = kColorFormat;
  target.blend = blends ? &blend : nullptr;
  target.writeMask = WGPUColorWriteMask_All;
  const Stages& stages = stages_[static_cast<std::size_t>(kind)];
  WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
  fragment.module = stages.fragment.get();
  fragment.entryPoint = View("fragment_main");
  fragment.targetCount = 1;
  fragment.targets = &target;

  WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
  depth.format = kDepthFormat;
  depth.depthWriteEnabled = key.depth_write ? WGPUOptionalBool_True : WGPUOptionalBool_False;
  // MMD's surfaces pass a fragment at the depth already written, as
  // Direct3D 9's LESSEQUAL, which MMD draws with, does: a model layers its
  // decals on its face's own vertices.
  depth.depthCompare = kind == Kind::Mmd || kind == Kind::MmdTransparent
                           ? WGPUCompareFunction_LessEqual
                           : WGPUCompareFunction_Less;
  // A hull is pushed away from the camera by a depth resolution step and
  // the slope's worth of one, so its surface wins a tie, as on Vulkan.
  if (outline) {
    depth.depthBias = 1;
    depth.depthBiasSlopeScale = 1.0F;
  }

  WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
  descriptor.label = View("scene");
  descriptor.layout = mesh ? mesh_layout_.get() : surface_layout_.get();
  descriptor.vertex.module = stages.vertex.get();
  descriptor.vertex.entryPoint = View("vertex_main");
  descriptor.vertex.bufferCount = mesh ? 1U : 4U;
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

WGPURenderPipeline ScenePipelines::CreateCaster(const Key& key, std::string& detail) {
  // The points and, for a Mask cut, the UVs, the second stream, as the
  // Vulkan backend's shadow pipeline reads them.
  const WGPUVertexAttribute attributes[2] = {
      {nullptr, WGPUVertexFormat_Float32x3, 0, 0},
      {nullptr, WGPUVertexFormat_Float32x2, 0, 1},
  };
  WGPUVertexBufferLayout buffers[2]{};
  const std::uint64_t strides[2] = {sizeof(Float3), key.uvs ? sizeof(Float2) : 0U};
  for (std::uint32_t stream = 0; stream < 2; ++stream) {
    buffers[stream].stepMode = WGPUVertexStepMode_Vertex;
    buffers[stream].arrayStride = strides[stream];
    buffers[stream].attributeCount = 1;
    buffers[stream].attributes = &attributes[stream];
  }
  const Stages& stages = stages_[static_cast<std::size_t>(Kind::Shadow)];
  // Depth alone: no colour target.
  WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
  fragment.module = stages.fragment.get();
  fragment.entryPoint = View("fragment_main");
  // Pushed back by a depth resolution step and the slope's worth of one,
  // so a lit surface does not shadow itself, as on Vulkan.
  WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
  depth.format = kShadowFormat;
  depth.depthWriteEnabled = WGPUOptionalBool_True;
  depth.depthCompare = WGPUCompareFunction_Less;
  depth.depthBias = 1;
  depth.depthBiasSlopeScale = 1.0F;
  WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
  descriptor.label = View("shadow");
  descriptor.layout = caster_layout_.get();
  descriptor.vertex.module = stages.vertex.get();
  descriptor.vertex.entryPoint = View("vertex_main");
  descriptor.vertex.bufferCount = 2;
  descriptor.vertex.buffers = buffers;
  descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
  descriptor.primitive.frontFace = key.front;
  descriptor.primitive.cullMode = key.cull;
  descriptor.depthStencil = &depth;
  descriptor.multisample.count = 1;
  descriptor.fragment = &fragment;
  RenderPipeline pipeline(wgpuDeviceCreateRenderPipeline(device_, &descriptor));
  if (!pipeline) {
    detail = "creating the WebGPU shadow pipeline failed";
    return nullptr;
  }
  ++created_;
  pipelines_.emplace_back(key, std::move(pipeline));
  return pipelines_.back().second.get();
}

bool DepthResolve::Initialize(WGPUDevice device, const ShaderSources& sources,
    std::string& detail) {
  device_ = device;
  vertex_ = CreateShader(device, sources.depth_resolve_vertex, "depth_resolve.vert");
  fragment_ = CreateShader(device, sources.depth_resolve_fragment, "depth_resolve.frag");
  // Depth32float samples, read by texel as an unfilterable float.
  WGPUBindGroupLayoutEntry samples = TextureEntry(0, WGPUShaderStage_Fragment,
      WGPUTextureSampleType_UnfilterableFloat, WGPUTextureViewDimension_2D);
  samples.texture.multisampled = true;
  layout_ = CreateLayout(device, {samples}, "depth resolve");
  pipeline_layout_ = CreatePipelineLayout(device, {layout_.get()}, "depth resolve");
  WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
  depth.format = kDepthFormat;
  depth.depthWriteEnabled = WGPUOptionalBool_True;
  depth.depthCompare = WGPUCompareFunction_Always;
  WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
  fragment.module = fragment_.get();
  fragment.entryPoint = View("fragment_main");
  WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
  descriptor.label = View("depth resolve");
  descriptor.layout = pipeline_layout_.get();
  descriptor.vertex.module = vertex_.get();
  descriptor.vertex.entryPoint = View("vertex_main");
  descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
  descriptor.depthStencil = &depth;
  descriptor.fragment = &fragment;
  if (vertex_ && fragment_ && layout_ && pipeline_layout_) {
    pipeline_.reset(wgpuDeviceCreateRenderPipeline(device, &descriptor));
  }
  if (!pipeline_) {
    detail = "creating the WebGPU depth resolve failed";
    return false;
  }
  return true;
}

bool DepthResolve::Record(WGPUCommandEncoder encoder, WGPUTextureView samples,
    WGPUTextureView target, std::string& detail) const {
  BindGroup group = CreateGroup(device_, layout_.get(), {ViewBinding(0, samples)},
      "depth resolve");
  if (!group) {
    detail = "creating the WebGPU depth resolve bind group failed";
    return false;
  }
  WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
  depth.view = target;
  depth.depthLoadOp = WGPULoadOp_Clear;
  depth.depthStoreOp = WGPUStoreOp_Store;
  depth.depthClearValue = 1.0F;
  WGPURenderPassDescriptor pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
  pass_descriptor.depthStencilAttachment = &depth;
  RenderPassEncoder pass(wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor));
  wgpuRenderPassEncoderSetPipeline(pass.get(), pipeline_.get());
  wgpuRenderPassEncoderSetBindGroup(pass.get(), 0, group.get(), 0, nullptr);
  wgpuRenderPassEncoderDraw(pass.get(), 3, 1, 0, 0);
  wgpuRenderPassEncoderEnd(pass.get());
  return true;
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
  // MMD's shared ramps, sampled as stored, as every MMD texture is; their
  // rows grade in 32 steps, so they take no mipmaps.
  for (std::uint32_t index = 0; index < kSharedToonRamps; ++index) {
    const ToonTexture ramp = SharedToonRamp(static_cast<std::int32_t>(index));
    descriptor.label = View("toon ramp");
    descriptor.size = {ramp.width, ramp.height, 1};
    Ramp& resident = ramps_[index];
    resident.texture.reset(wgpuDeviceCreateTexture(device_, &descriptor));
    if (!resident.texture) {
      detail = "creating a shared toon ramp failed";
      return false;
    }
    resident.view.reset(wgpuTextureCreateView(resident.texture.get(), nullptr));
    resident.serial = ++serial_;
    destination.texture = resident.texture.get();
    layout.bytesPerRow = ramp.width * 4U;
    layout.rowsPerImage = ramp.height;
    const WGPUExtent3D ramp_extent{ramp.width, ramp.height, 1};
    wgpuQueueWriteTexture(queue_, &destination, ramp.pixels->data(), ramp.pixels->size(),
        &layout, &ramp_extent);
  }

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
      resident.serial = ++serial_;
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
    const std::vector<std::uint8_t>& pixels = *texture.pixels;
    resident.zero_green = true;
    for (std::size_t pixel = 1; pixel < pixels.size(); pixel += 4) {
      if (pixels[pixel] != 0) {
        resident.zero_green = false;
        break;
      }
    }
    resident.translucent = false;
    for (std::size_t pixel = 3; pixel < pixels.size(); pixel += 4) {
      if (pixels[pixel] != 255) {
        resident.translucent = true;
        break;
      }
    }
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

void TextureCache::RecordMipmaps(WGPUCommandEncoder encoder,
    const PassTimestamps& timestamps) {
  std::size_t passes = 0;
  for (const Mipmaps& mipmaps : pending_) passes += mipmaps.levels - 1U;
  std::size_t pass_index = 0;
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
      WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
      pass_descriptor.timestampWrites = timestamps.Of(pass_index++, passes, writes);
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

std::uint64_t TextureCache::SerialOf(TextureId texture) const {
  const auto found = textures_.find(texture);
  return found == textures_.end() || !found->second.view ? 0U : found->second.serial;
}

WGPUTextureView TextureCache::RampView(std::int32_t index) const {
  return index >= 0 && index < static_cast<std::int32_t>(kSharedToonRamps)
             ? ramps_[static_cast<std::size_t>(index)].view.get()
             : white_view_.get();
}

std::uint64_t TextureCache::RampSerial(std::int32_t index) const {
  return index >= 0 && index < static_cast<std::int32_t>(kSharedToonRamps)
             ? ramps_[static_cast<std::size_t>(index)].serial
             : 0U;
}

bool TextureCache::ZeroGreen(TextureId texture) const {
  const auto found = textures_.find(texture);
  return found != textures_.end() && found->second.view && found->second.zero_green;
}

bool TextureCache::Translucent(TextureId texture) const {
  const auto found = textures_.find(texture);
  return found != textures_.end() && found->second.view && found->second.translucent;
}

bool MaterialCache::Initialize(const Gpu& gpu, const ScenePipelines& pipelines,
    const TextureCache& textures, const ShadowMap& shadow, std::string& detail) {
  device_ = gpu.device.get();
  queue_ = gpu.queue.get();
  pipelines_ = &pipelines;
  white_ = textures.ViewOf(0);
  frame_buffer_ = CreateBuffer(device_, kFrameBytes,
      WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "frame");
  if (!frame_buffer_) {
    detail = "creating the WebGPU frame buffer failed";
    return false;
  }
  caster_frame_group_ = CreateGroup(device_, pipelines.caster_frame_layout(),
      {BufferBinding(0, frame_buffer_.get())}, "caster frame");
  if (!caster_frame_group_) {
    detail = "creating the WebGPU caster frame group failed";
    return false;
  }
  return BindFrame(shadow, detail) && Reserve(64, detail);
}

bool MaterialCache::BindFrame(const ShadowMap& shadow, std::string& detail) {
  frame_group_ = CreateGroup(device_, pipelines_->frame_layout(),
      {BufferBinding(0, frame_buffer_.get()), ViewBinding(1, shadow.array_view()),
          BufferBinding(2, shadow.views_buffer()),
          SamplerBinding(3, pipelines_->shadow_sampler()),
          BufferBinding(4, shadow.mmd_buffer())},
      "frame");
  frame_shadow_build_ = shadow.rebuilds();
  if (!frame_group_) {
    detail = "creating the WebGPU frame group failed";
    return false;
  }
  return true;
}

BindGroup MaterialCache::MaterialGroup(
    const std::array<WGPUTextureView, kTextureRoles>& views,
    const std::array<std::uint32_t, kTextureRoles>& samplers) const {
  std::vector<WGPUBindGroupEntry> entries{
      BufferBinding(kMToonMaterialsBinding, mtoon_buffer_.get()),
      BufferBinding(kMmdMaterialsBinding, mmd_buffer_.get()),
      BufferBinding(kPreviewMaterialsBinding, preview_buffer_.get())};
  for (std::uint32_t role = 0; role < kTextureRoles; ++role) {
    entries.push_back(ViewBinding(kFirstRoleTexture + role, views[role]));
    entries.push_back(
        SamplerBinding(kFirstRoleSampler + role, pipelines_->sampler(samplers[role])));
  }
  return CreateGroup(device_, pipelines_->material_layout(), entries, "material");
}

bool MaterialCache::Update(const DrawList& draws, const TextureCache& textures,
    const ShadowMap& shadow, std::string& detail, bool fast_values) {
  if (fast_values) {
    std::array<float, kFrameBytes / sizeof(float)> frame{};
    WriteFrame(draws, &shadow.fit().views(), frame);
    wgpuQueueWriteBuffer(queue_, frame_buffer_.get(), 0, frame.data(), kFrameBytes);
  }
  if (frame_shadow_build_ != shadow.rebuilds() && !BindFrame(shadow, detail)) return false;

  ++generation_;
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
    const Roles roles = RolesOf(values, textures, white_);
    // Pixel edits can change this without changing a material or its
    // entries; it only decides what is drawn, so no slot is written.
    entry.zero_width_texture = textures.ZeroGreen(values.mtoon.outline_width_texture.texture);
    if (fast_values && (entry.parameters_revision != material.parameters_revision ||
                           entry.entries != roles.entries)) {
      const auto write_start = SteadyNanoseconds();
      const auto offset = [&](std::size_t size) {
        return static_cast<std::uint64_t>(entry.slot) * size;
      };
      if (values.model == ToonShadingModel::MMD) {
        MmdParameters& slot = mmd_slots_[entry.slot];
        slot = {};
        WriteParameters(values, roles.entries, slot);
        wgpuQueueWriteBuffer(queue_, mmd_buffer_.get(), offset(sizeof(slot)), &slot,
            sizeof(slot));
      } else if (values.model == ToonShadingModel::PreviewSurface) {
        PreviewParameters& slot = preview_slots_[entry.slot];
        slot = {};
        WriteParameters(values, roles.entries, slot);
        wgpuQueueWriteBuffer(queue_, preview_buffer_.get(), offset(sizeof(slot)), &slot,
            sizeof(slot));
      } else {
        MToonParameters& slot = mtoon_slots_[entry.slot];
        slot = {};
        WriteParameters(values, roles.entries, slot);
        wgpuQueueWriteBuffer(queue_, mtoon_buffer_.get(), offset(sizeof(slot)), &slot,
            sizeof(slot));
      }
      entry.parameters_revision = material.parameters_revision;
      entry.entries = roles.entries;
      entry.model = values.model;
      entry.preview_enabled =
          values.model == ToonShadingModel::PreviewSurface && values.preview.enabled;
      entry.unlit_color = values.base_color;
      entry.double_sided = values.double_sided;
      entry.outline = HasOutline(values);
      entry.outline_width = values.outline_width;
      entry.outline_width_mode = values.mtoon.outline_width_mode;
      entry.transparent = IsTransparent(values);
      entry.queue = RenderQueue(values);
      entry.depth_write = WritesDepth(values);
      entry.draw_order = values.mmd.draw_order;
      entry.mmd_translucent = values.alpha < 1.0F;
      ++writes_;
      parameter_write_ms_ += static_cast<double>(SteadyNanoseconds() - write_start) / 1e6;
    }
    // Whether an MMD material can blend follows its textures' pixels as
    // well as its diffuse alpha, so it is decided every frame, as on
    // Vulkan; one that cannot blend draws with the opaque ones.
    if (entry.model == ToonShadingModel::MMD) {
      entry.transparent = entry.mmd_translucent ||
                          textures.Translucent(values.base_texture.texture) ||
                          (SphereMode(values) != kSphereNone &&
                              textures.Translucent(values.mmd.sphere_texture.texture));
      entry.queue = 3000;
      entry.depth_write = true;
    }
    // A bind group holds views and samplers, so a texture that becomes
    // resident, leaves or is reallocated, a wrap edit, or a grown buffer
    // rebuilds it; a value edit does not. Views are compared by serial: a
    // reallocated texture's new view may reuse the old one's address.
    if (!entry.group || entry.views != roles.serials || entry.samplers != roles.samplers ||
        entry.buffer_generation != buffer_generation_) {
      entry.group = MaterialGroup(roles.views, roles.samplers);
      entry.views = roles.serials;
      entry.samplers = roles.samplers;
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
  return true;
}

const MaterialCache::Entry* MaterialCache::Find(MaterialId material) const {
  const auto found = entries_.find(material);
  return found == entries_.end() ? nullptr : &found->second;
}

bool MaterialCache::Reserve(std::uint32_t slots, std::string& detail) {
  // A grown buffer is a new one with every slot copied, so growing
  // rewrites no material but rebuilds every material's bind group.
  const auto grow = [&](Buffer& buffer, auto& copies, const char* label) {
    using Slot = typename std::remove_reference_t<decltype(copies)>::value_type;
    Buffer grown = CreateBuffer(device_, static_cast<std::uint64_t>(slots) * sizeof(Slot),
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, label);
    if (!grown) {
      detail = "creating the WebGPU material buffers failed";
      return false;
    }
    copies.resize(slots);
    wgpuQueueWriteBuffer(queue_, grown.get(), 0, copies.data(), copies.size() * sizeof(Slot));
    buffer = std::move(grown);
    return true;
  };
  if (!grow(mtoon_buffer_, mtoon_slots_, "mtoon materials") ||
      !grow(mmd_buffer_, mmd_slots_, "mmd materials") ||
      !grow(preview_buffer_, preview_slots_, "preview materials")) {
    return false;
  }
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
  const float unit = 1.0F;
  unit_edge_scale_ = CreateBuffer(device_, sizeof(unit),
      WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, "unit edge scale");
  if (!zero_uv_ || !zero_tangent_ || !zero_skin_ || !unit_edge_scale_) {
    detail = "creating the WebGPU mesh fallbacks failed";
    return false;
  }
  wgpuQueueWriteBuffer(queue_, zero_uv_.get(), 0, &zero_uv, sizeof(zero_uv));
  wgpuQueueWriteBuffer(queue_, zero_tangent_.get(), 0, &zero_tangent, sizeof(zero_tangent));
  wgpuQueueWriteBuffer(queue_, unit_edge_scale_.get(), 0, &unit, sizeof(unit));
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
    entry.outline_bounds.Update(mesh);
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
    // MMD's edge scales and sub-texture UVs, counted with the points as
    // the normals and UVs are.
    const bool edge_scales_present = mesh.edge_scales != nullptr && !mesh.edge_scales->empty();
    if (entry.edge_scales_revision != mesh.edge_scales_revision) {
      if (edge_scales_present &&
          !Upload(entry.edge_scales, WGPUBufferUsage_Vertex, mesh.edge_scales->data(),
              mesh.edge_scales->size() * sizeof(float))) {
        return fail();
      }
      entry.edge_scales_revision = mesh.edge_scales_revision;
    }
    entry.has_edge_scales = edge_scales_present && mesh.edge_scales->size() >= mesh.index_bound;
    const bool sub_texture_uvs_present =
        mesh.sub_texture_uvs != nullptr && !mesh.sub_texture_uvs->empty();
    if (entry.sub_texture_uvs_revision != mesh.sub_texture_uvs_revision) {
      if (sub_texture_uvs_present &&
          !Upload(entry.sub_texture_uvs, WGPUBufferUsage_Vertex, mesh.sub_texture_uvs->data(),
              mesh.sub_texture_uvs->size() * sizeof(Float2))) {
        return fail();
      }
      entry.sub_texture_uvs_revision = mesh.sub_texture_uvs_revision;
    }
    entry.has_sub_texture_uvs =
        sub_texture_uvs_present && mesh.sub_texture_uvs->size() >= mesh.index_bound;
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

std::size_t MeshCache::DrawBound(const DrawList& draws, std::size_t shadow_views) {
  std::size_t bound = 0;
  for (const MeshSnapshot& mesh : draws.draws) {
    bound += mesh.parts != nullptr && !mesh.parts->empty() ? mesh.parts->size() : 1U;
  }
  return bound * (3U + shadow_views);
}

bool MeshCache::RecordShadowCasters(WGPURenderPassEncoder pass, ScenePipelines& pipelines,
    const MaterialCache& materials, const DrawList& draws, const Matrix4& clip_from_world,
    WGPUBindGroup ring, std::vector<std::uint8_t>& constants, std::uint32_t& casters,
    std::uint32_t& mmd_casters, std::string& detail) const {
  MeshPart whole;
  WGPURenderPipeline bound = nullptr;
  wgpuRenderPassEncoderSetBindGroup(pass, 0, materials.caster_frame_group(), 0, nullptr);
  for (const MeshSnapshot& mesh : draws.draws) {
    const auto found = entries_.find(mesh.id);
    if (found == entries_.end()) continue;
    const Entry& entry = found->second;
    bool streams = false;
    SurfaceDrawConstants draw{};
    WGPUFrontFace front = WGPUFrontFace_CW;
    for (const MeshPart& part : PartsOf(mesh, entry.index_count, whole)) {
      const ShadowRole role = ShadowRoleOf(FindMaterial(draws, part.material));
      if (!ShadowFit::Casts(role, draws) || !Fits(part, entry.index_count)) continue;
      if (!streams) {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, entry.vertices.buffer.get(), 0,
            WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 1,
            entry.has_uvs ? entry.uvs.buffer.get() : zero_uv_.get(), 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(pass, entry.indices.buffer.get(),
            WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetBindGroup(pass, 2,
            (entry.skinned || entry.morphed) ? entry.deformation.get() : undeformed_.get(), 0,
            nullptr);
        const Matrix4 clip_from_object = Multiply(clip_from_world, mesh.transform);
        std::memcpy(draw.clip_from_object, clip_from_object.m.data(),
            sizeof(draw.clip_from_object));
        // Drawn with y flipped (WebGpuShadowClipFromWorld), a face
        // counter-clockwise in the scene is clockwise in the view's image,
        // as in the Vulkan backend's.
        float rows[12];
        front = ViewRows(mesh.transform, rows) ? WGPUFrontFace_CCW : WGPUFrontFace_CW;
        streams = true;
      }
      const MaterialCache::Entry* material = materials.Find(part.material);
      const bool cut = role.cut && material != nullptr;
      draw.material_slot = cut ? material->slot : 0U;
      draw.flags = (entry.has_uvs ? kDrawHasUVs : 0U) | entry.skin_flags |
                   (cut ? (material->model == ToonShadingModel::MToon ? kDrawCutMToon
                                                                      : kDrawCutPreview)
                        : 0U);
      ScenePipelines::Key key;
      key.kind = ScenePipelines::Kind::Shadow;
      key.cull = role.both_faces ? WGPUCullMode_None : WGPUCullMode_Back;
      key.front = front;
      key.uvs = entry.has_uvs;
      key.fourth = false;
      WGPURenderPipeline pipeline = pipelines.Get(key, detail);
      if (pipeline == nullptr) return false;
      if (pipeline != bound) {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        bound = pipeline;
      }
      // A cut reads its material's slot and base or opacity texture; any
      // other caster reads none, and binds the display colour's group.
      wgpuRenderPassEncoderSetBindGroup(pass, 1,
          cut ? material->group.get() : materials.display_group(), 0, nullptr);
      const auto offset = static_cast<std::uint32_t>(constants.size());
      constants.resize(constants.size() + kDrawStride);
      std::memcpy(constants.data() + offset, &draw, sizeof(draw));
      wgpuRenderPassEncoderSetBindGroup(pass, 3, ring, 1, &offset);
      wgpuRenderPassEncoderDrawIndexed(pass, part.index_count, 1, part.first_index, 0, 0);
      ++casters;
      mmd_casters += role.mmd ? 1U : 0U;
    }
  }
  return true;
}

bool MeshCache::Record(WGPURenderPassEncoder pass, ScenePipelines& pipelines,
    const MaterialCache& materials, const DrawList& draws, std::uint32_t width,
    std::uint32_t height, WGPUBindGroup ring, std::vector<std::uint8_t>& constants,
    SceneRecord& record, std::string& detail) {
  using Kind = ScenePipelines::Kind;
  const Matrix4 clip_from_world = WebGpuClipFromWorld(draws.view);
  // Missing bindings use a rough dielectric with per-draw display colour.
  // This entry is local: no per-mesh material, slot or texture is allocated.
  MaterialCache::Entry display_material;
  display_material.preview_enabled = true;
  display_material.double_sided = true;
  // Readable materials use their model; bound diagnostic materials stay unlit.
  const auto surface_material =
      [&](const MeshPart& part) -> const MaterialCache::Entry* {
    const MaterialCache::Entry* material = materials.Find(part.material);
    if (material == nullptr) return &display_material;
    return (material->model == ToonShadingModel::MToon ||
               material->model == ToonShadingModel::MMD || material->preview_enabled)
               ? material
               : nullptr;
  };
  MeshPart whole;
  if (draws.outlines && draws.outline_occlusion_culling) {
    outline_occlusion_.Update(draws, width, height);
  }
  const auto hidden_outline = [&](const MeshSnapshot& mesh, const Entry& entry,
                                  const MaterialCache::Entry& material) {
    return (draws.outline_frustum_culling &&
               entry.outline_bounds.OutsideView(mesh, draws.view, material.outline_width,
                   material.outline_width_mode, draws.meters_per_unit)) ||
           (draws.outline_occlusion_culling &&
               outline_occlusion_.Occludes(mesh, entry.outline_bounds, draws.view,
                   material.outline_width, material.outline_width_mode, draws.meters_per_unit));
  };
  auto draw_count = static_cast<std::uint32_t>(constants.size() / kDrawStride);
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
      if (!bind({Kind::Mesh, WGPUCullMode_None, WGPUFrontFace_CCW, true, true, true})) {
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
  // A perspective projection puts -z into w; an orthographic one keeps w 1.
  const bool orthographic = draws.view.projection.m[11] == 0.0F;
  // Multisampled, mtoon_opaque turns its alpha into samples, through
  // MToonCoverage's sample mask.
  const bool multisampled = pipelines.samples() > 1;
  const auto draw_surface = [&](Kind kind, const MeshSnapshot& mesh, const Entry& entry,
                                const MeshPart& part, const MaterialCache::Entry& material,
                                WGPUCullMode cull) {
    if (kind == Kind::MToon && multisampled) kind = Kind::MToonCoverage;
    const bool edge = kind == Kind::MmdOutline;
    const bool mmd_surface = kind == Kind::Mmd || kind == Kind::MmdTransparent;
    // The fourth stream: MMD's edge scale or sub-texture UV, or a tangent
    // frame.
    WGPUBuffer fourth = entry.has_tangents ? entry.tangents.buffer.get() : zero_tangent_.get();
    bool has_fourth = entry.has_tangents;
    if (edge) {
      fourth = entry.has_edge_scales ? entry.edge_scales.buffer.get() : unit_edge_scale_.get();
      has_fourth = entry.has_edge_scales;
    } else if (mmd_surface) {
      fourth = entry.has_sub_texture_uvs ? entry.sub_texture_uvs.buffer.get() : zero_uv_.get();
      has_fourth = entry.has_sub_texture_uvs;
    }
    SurfaceDrawConstants draw{};
    const Matrix4 clip_from_object = Multiply(clip_from_world, mesh.transform);
    std::memcpy(draw.clip_from_object, clip_from_object.m.data(), sizeof(draw.clip_from_object));
    const bool mirrored =
        ViewRows(Multiply(draws.view.view, mesh.transform), draw.view_from_object_rows);
    draw.material_slot = material.slot;
    draw.flags = (entry.has_uvs ? kDrawHasUVs : 0U) | (orthographic ? kDrawOrthographic : 0U) |
                 (material.transparent ? kDrawBlend : 0U) |
                 (kind == Kind::MToonCoverage ? kDrawAlphaToCoverage : 0U) | entry.skin_flags;
    draw.projection_scale = projection_scale;
    draw.outline_scale = units_per_meter;
    const bool display = &material == &display_material;
    if (display) {
      draw.flags |= kDrawDisplayColor;
      draw.material_slot = std::bit_cast<std::uint32_t>(mesh.color.x);
      draw.projection_scale = mesh.color.y;
      draw.outline_scale = mesh.color.z;
    }
    if (!bind({kind, cull, mirrored ? WGPUFrontFace_CW : WGPUFrontFace_CCW,
            material.depth_write, entry.has_uvs, has_fourth})) {
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
    wgpuRenderPassEncoderSetVertexBuffer(pass, 3, fourth, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, entry.indices.buffer.get(),
        WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, part.index_count, 1, part.first_index, 0, 0);
    record.triangles += part.index_count / 3U;
    return true;
  };
  const auto faces = [](const MaterialCache::Entry& material) {
    return material.double_sided ? WGPUCullMode_None : WGPUCullMode_Back;
  };

  // The opaque pass, Mask included: every hull, then every surface. The
  // hull culls its front faces whether or not the material is double-sided,
  // as MToon states. An MMD surface that cannot blend draws here too.
  transparent_scratch_.clear();
  for (const bool outline : {true, false}) {
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
          if (!outline) transparent_scratch_.emplace_back(index, at);
          continue;
        }
        if (material->model == ToonShadingModel::MMD || material->preview_enabled) {
          if (!outline) {
            if (!draw_surface(material->preview_enabled ? Kind::Preview : Kind::Mmd, mesh,
                    entry, part, *material, faces(*material))) {
              return false;
            }
            ++record.opaque;
          }
          continue;
        }
        if (outline && (!draws.outlines || !material->outline ||
                           (entry.has_uvs && material->zero_width_texture) ||
                           hidden_outline(mesh, entry, *material))) {
          continue;
        }
        if (!draw_surface(outline ? Kind::MToonOutline : Kind::MToon, mesh, entry, part,
                *material, outline ? WGPUCullMode_Front : faces(*material))) {
          return false;
        }
        ++(outline ? record.outline : record.opaque);
        record.hulls += outline ? 1U : 0U;
      }
    }
  }

  // The transparent pass, back to front by render queue and, within one
  // queue, in the order `draws` lists them, an MMD mesh's parts in MMD's
  // material order. A double-sided MToon surface draws its back faces
  // before its front ones, and its hull follows it; an MMD surface draws
  // once, both sides together when double-sided, as on Vulkan. `whole`
  // stands in for a mesh without parts, so the part is copied before the
  // next lookup reuses it.
  const auto transparent_part = [&](const std::pair<std::size_t, std::size_t>& draw) {
    const MeshSnapshot& mesh = draws.draws[draw.first];
    return PartsOf(mesh, entries_.find(mesh.id)->second.index_count, whole)[draw.second];
  };
  const auto transparent_key = [&](const std::pair<std::size_t, std::size_t>& draw) {
    const MaterialCache::Entry& material = *surface_material(transparent_part(draw));
    const std::int64_t place = material.model == ToonShadingModel::MMD
                                   ? material.draw_order
                                   : static_cast<std::int64_t>(draw.second);
    return std::make_tuple(material.queue, draw.first, place);
  };
  std::stable_sort(transparent_scratch_.begin(), transparent_scratch_.end(),
      [&](const auto& left, const auto& right) {
        return transparent_key(left) < transparent_key(right);
      });
  for (const auto& draw : transparent_scratch_) {
    const MeshSnapshot& mesh = draws.draws[draw.first];
    const Entry& entry = entries_.find(mesh.id)->second;
    const MeshPart part = transparent_part(draw);
    const MaterialCache::Entry& material = *surface_material(part);
    if (material.model == ToonShadingModel::MMD || material.preview_enabled) {
      if (!draw_surface(material.preview_enabled ? Kind::Preview : Kind::MmdTransparent, mesh,
              entry, part, material, faces(material))) {
        return false;
      }
      ++record.transparent;
      continue;
    }
    if (material.double_sided) {
      if (!draw_surface(Kind::MToonTransparent, mesh, entry, part, material,
              WGPUCullMode_Front)) {
        return false;
      }
      ++record.transparent;
    }
    if (!draw_surface(Kind::MToonTransparent, mesh, entry, part, material, WGPUCullMode_Back)) {
      return false;
    }
    ++record.transparent;
    if (draws.outlines && material.outline &&
        (!entry.has_uvs || !material.zero_width_texture) &&
        !hidden_outline(mesh, entry, material)) {
      if (!draw_surface(Kind::MToonOutline, mesh, entry, part, material, WGPUCullMode_Front)) {
        return false;
      }
      ++record.transparent;
      ++record.hulls;
    }
  }

  // MMD's edges, after every surface, each mesh's in MMD's material order.
  // The hull culls its front faces, double-sided or not.
  if (!draws.outlines) return true;
  transparent_scratch_.clear();
  for (std::size_t index = 0; index < draws.draws.size(); ++index) {
    const MeshSnapshot& mesh = draws.draws[index];
    const auto found = entries_.find(mesh.id);
    if (found == entries_.end()) continue;
    const auto parts = PartsOf(mesh, found->second.index_count, whole);
    for (std::size_t at = 0; at < parts.size(); ++at) {
      const MaterialCache::Entry* material = surface_material(parts[at]);
      if (material != nullptr && material->model == ToonShadingModel::MMD &&
          material->outline && Fits(parts[at], found->second.index_count)) {
        transparent_scratch_.emplace_back(index, at);
      }
    }
  }
  const auto edge_key = [&](const std::pair<std::size_t, std::size_t>& draw) {
    return std::make_pair(draw.first, surface_material(transparent_part(draw))->draw_order);
  };
  std::stable_sort(transparent_scratch_.begin(), transparent_scratch_.end(),
      [&](const auto& left, const auto& right) { return edge_key(left) < edge_key(right); });
  for (const auto& draw : transparent_scratch_) {
    const MeshSnapshot& mesh = draws.draws[draw.first];
    const MeshPart part = transparent_part(draw);
    if (!draw_surface(Kind::MmdOutline, mesh, entries_.find(mesh.id)->second, part,
            *surface_material(part), WGPUCullMode_Front)) {
      return false;
    }
    ++record.transparent;
    ++record.hulls;
  }
  return true;
}

bool ShadowMap::Initialize(const Gpu& gpu, std::string& detail) {
  device_ = gpu.device.get();
  queue_ = gpu.queue.get();
  views_buffer_ = CreateBuffer(device_, sizeof(ShadowViewParameters) * kShadowViewCapacity,
      WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "shadow views");
  mmd_buffer_ = CreateBuffer(device_, sizeof(MmdShadowParameters),
      WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "mmd shadow");
  if (!views_buffer_ || !mmd_buffer_) {
    detail = "creating the WebGPU shadow parameters failed";
    return false;
  }
  // Off until a frame draws a view; a new buffer is zeroed.
  return Build(ShadowResolution(ShadowSettings{}.resolution), 1, detail);
}

// The image, its array view and a view of each layer.
bool ShadowMap::Build(std::uint32_t resolution, std::uint32_t layers, std::string& detail) {
  layer_views_.clear();
  array_view_.reset();
  WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
  descriptor.label = View("shadow views");
  descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
  descriptor.size = {resolution, resolution, layers};
  descriptor.format = kShadowFormat;
  image_.reset(wgpuDeviceCreateTexture(device_, &descriptor));
  if (!image_) {
    detail = "creating the WebGPU shadow views' image failed";
    resolution_ = 0;
    return false;
  }
  WGPUTextureViewDescriptor array = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
  array.dimension = WGPUTextureViewDimension_2DArray;
  array.aspect = WGPUTextureAspect_DepthOnly;
  array_view_.reset(wgpuTextureCreateView(image_.get(), &array));
  for (std::uint32_t layer = 0; layer < layers; ++layer) {
    WGPUTextureViewDescriptor one = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    one.dimension = WGPUTextureViewDimension_2D;
    one.baseArrayLayer = layer;
    one.arrayLayerCount = 1;
    layer_views_.emplace_back(wgpuTextureCreateView(image_.get(), &one));
  }
  resolution_ = resolution;
  ++rebuilds_;
  return static_cast<bool>(array_view_);
}

bool ShadowMap::Update(const DrawList& draws, std::string& detail, double& fit_ms) {
  const std::int64_t fit_start = SteadyNanoseconds();
  fit_.Update(draws);
  fit_ms = static_cast<double>(SteadyNanoseconds() - fit_start) / 1e6;
  const std::uint32_t resolution = ShadowResolution(draws.shadow_settings.resolution);
  const auto count = static_cast<std::uint32_t>(fit_.views().size());
  if ((resolution != resolution_ || count > layers()) &&
      !Build(resolution, std::max(count, layers()), detail)) {
    return false;
  }
  std::array<ShadowViewParameters, kShadowViewCapacity> views{};
  MmdShadowParameters mmd{};
  WriteShadowParameters(draws, fit_.views(), resolution_, views, mmd);
  wgpuQueueWriteBuffer(queue_, views_buffer_.get(), 0, views.data(), sizeof(views));
  wgpuQueueWriteBuffer(queue_, mmd_buffer_.get(), 0, &mmd, sizeof(mmd));
  return true;
}

bool ShadowMap::Record(WGPUCommandEncoder encoder, ScenePipelines& pipelines,
    const MaterialCache& materials, const MeshCache& meshes, const DrawList& draws,
    WGPUBindGroup ring, std::vector<std::uint8_t>& constants, std::string& detail,
    const PassTimestamps& timestamps) {
  caster_draws_ = 0;
  mmd_draws_ = 0;
  // Each view is drawn whole; a layer no view draws this frame is not
  // read, since no light's entry names it.
  for (std::uint32_t layer = 0; layer < fit_.views().size(); ++layer) {
    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth.view = layer_views_[layer].get();
    depth.depthLoadOp = WGPULoadOp_Clear;
    depth.depthStoreOp = WGPUStoreOp_Store;
    depth.depthClearValue = 1.0F;
    WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    descriptor.label = View("shadow view");
    descriptor.depthStencilAttachment = &depth;
    WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    descriptor.timestampWrites = timestamps.Of(layer, fit_.views().size(), writes);
    RenderPassEncoder pass(wgpuCommandEncoderBeginRenderPass(encoder, &descriptor));
    const bool recorded = meshes.RecordShadowCasters(pass.get(), pipelines, materials, draws,
        WebGpuShadowClipFromWorld(fit_.views()[layer]), ring, constants, caster_draws_,
        mmd_draws_, detail);
    wgpuRenderPassEncoderEnd(pass.get());
    if (!recorded) return false;
  }
  return true;
}

} // namespace Toon::webgpu_internal
