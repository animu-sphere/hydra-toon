// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <toon/backend.hpp>
#include <toon/extraction.hpp>

namespace Toon {

// The WGSL of every pipeline the WebGPU backend has so far (WebGPU policy
// §10): generated from the shared Slang modules and validated by Tint at
// build time. Paths are explicit, as SceneShaders' are.
struct WebGpuShaders {
  // Unlit, one colour per draw: bound diagnostic materials.
  std::string mesh_vertex;
  std::string mesh_fragment;
  // Basic metallic/roughness PreviewSurface, and unbound mesh display
  // colour as an opaque rough dielectric.
  std::string preview_vertex;
  std::string preview_fragment;
  // MToon's Opaque and Mask surfaces, its Blend surfaces and its outline
  // hull, as the Vulkan backend's mtoon_opaque, mtoon_transparent and
  // mtoon_outline; and mtoon_opaque multisampled, which writes Mask's
  // coverage through the sample mask.
  std::string mtoon_vertex;
  std::string mtoon_fragment;
  std::string mtoon_transparent_vertex;
  std::string mtoon_transparent_fragment;
  std::string mtoon_outline_vertex;
  std::string mtoon_outline_fragment;
  std::string mtoon_coverage_vertex;
  std::string mtoon_coverage_fragment;
  // MMD's surfaces that blend nothing, those that can blend, and its edge.
  std::string mmd_vertex;
  std::string mmd_fragment;
  std::string mmd_transparent_vertex;
  std::string mmd_transparent_fragment;
  std::string mmd_outline_vertex;
  std::string mmd_outline_fragment;
  // The shared shadow caster pass, depth alone.
  std::string shadow_vertex;
  std::string shadow_fragment;
  // The present session's composite and overlay passes.
  std::string composite_vertex;
  std::string composite_fragment;
  std::string overlay_vertex;
  std::string overlay_fragment;
  // Each texture level drawn from the one above; WebGPU has no blit.
  std::string mipmap_vertex;
  std::string mipmap_fragment;
  // A multisampled frame's depth, sample 0 of each pixel; WebGPU resolves
  // colour alone.
  std::string depth_resolve_vertex;
  std::string depth_resolve_fragment;
};

// The shaders as the build and install trees lay them out:
// `<module>.vert.wgsl` and `<module>.frag.wgsl` in one directory.
[[nodiscard]] WebGpuShaders WebGpuShadersIn(const std::string& directory);

// Which of Dawn's adapters renders. Direct3D 12 is the default; Dawn's
// Vulkan adapter is for diagnosis, and evidence names which one ran
// (WebGPU policy §2).
enum class WebGpuAdapter { Direct3D12, Vulkan };

[[nodiscard]] BackendCapability ProbeWebGpuBackend(
    WebGpuAdapter adapter = WebGpuAdapter::Direct3D12);

// Returns nullptr with `status` Skip when this environment cannot render (no
// WebGPU build, or no adapter of the requested kind) and Fail on a real
// error; `detail` says which. Products, statistics and the late-frame source
// mean what the Vulkan backend's do.
[[nodiscard]] std::unique_ptr<OffscreenRenderer> CreateWebGpuOffscreenRenderer(
    const WebGpuShaders& shaders, FrameStatus& status, std::string& detail,
    const RenderOptions& options = {},
    WebGpuAdapter adapter = WebGpuAdapter::Direct3D12);

// Render `frame_count` frames of `draws` at 64 x 64 on one persistent
// renderer and return the last frame's products: the headless evidence run.
[[nodiscard]] GpuFrameEvidence RenderWebGpuOffscreen(const DrawList& draws,
    const WebGpuShaders& shaders, std::uint32_t frame_count,
    WebGpuAdapter adapter = WebGpuAdapter::Direct3D12);

} // namespace Toon
