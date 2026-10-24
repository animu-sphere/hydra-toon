// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <toon/backend.hpp>
#include <toon/extraction.hpp>

namespace Toon {

// The SPIR-V of every scene pipeline. Paths are explicit so build-tree and
// install-tree layouts exercise the same backend code without source-tree
// fallbacks.
struct SceneShaders {
  // Unlit, one colour per draw: bound diagnostic materials.
  std::string mesh_vertex;
  std::string mesh_fragment;
  // mtoon_opaque (material policy §7).
  std::string mtoon_vertex;
  std::string mtoon_fragment;
  // mtoon_transparent: MToon's Blend alpha mode, blended over what is
  // behind it (material policy §6).
  std::string mtoon_transparent_vertex;
  std::string mtoon_transparent_fragment;
  // mtoon_outline: the inverted hull of an MToon material that asks for an
  // outline (material policy §5).
  std::string mtoon_outline_vertex;
  std::string mtoon_outline_fragment;
  // mmd_opaque: an MMD material that blends nothing (material policy §7).
  std::string mmd_vertex;
  std::string mmd_fragment;
  // mmd_transparent: an MMD material that can blend, in MMD's material
  // order after every opaque draw (material policy §6).
  std::string mmd_transparent_vertex;
  std::string mmd_transparent_fragment;
  // mmd_outline: the inverted hull of an MMD material that draws its edge,
  // after every surface (material policy §5).
  std::string mmd_outline_vertex;
  std::string mmd_outline_fragment;
  // shadow: the shared caster pass, the depth of every draw that casts
  // into a shadowing light's view, seen from the light, before the scene
  // (shadow policy §4).
  std::string shadow_vertex;
  std::string shadow_fragment;
  // overlay: a present session's 2D overlay (OverlayDrawList). The offscreen
  // renderer draws none and does not read these.
  std::string overlay_vertex;
  std::string overlay_fragment;
  // Basic metallic/roughness PreviewSurface, including surface opacity,
  // and unbound mesh display colour as an opaque rough dielectric.
  std::string preview_vertex;
  std::string preview_fragment;
  // Fullscreen premultiplied foreground compositor; presentation only.
  std::string composite_vertex;
  std::string composite_fragment;
};

// The shaders as the build and install trees lay them out: `<module>.vert.spv`
// and `<module>.frag.spv` in one directory.
[[nodiscard]] SceneShaders SceneShadersIn(const std::string& directory);

[[nodiscard]] BackendCapability ProbeVulkanBackend();

// Returns nullptr with `status` Skip when this environment cannot render (no
// Vulkan build, loader, or a 1.3 device with the features the renderer
// needs) and Fail on a real error; `detail` says which.
[[nodiscard]] std::unique_ptr<OffscreenRenderer> CreateOffscreenRenderer(
    const SceneShaders& shaders, FrameStatus& status, std::string& detail,
    const RenderOptions& options = {});

// Render `frame_count` frames of `draws` at 64 x 64 on one persistent
// renderer and return the last frame's products: the headless evidence run.
[[nodiscard]] GpuFrameEvidence RenderOffscreen(const DrawList& draws,
    const SceneShaders& shaders, std::uint32_t frame_count);

} // namespace Toon
