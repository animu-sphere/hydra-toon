// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <toon/vulkan_present.hpp>
#include <toon/webgpu_backend.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace Toon::viewport {

struct Arguments {
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
  std::uint64_t frame_limit = 0;
  bool visible = true;
  bool vsync = true;
  // Whether the overlay is laid out and drawn; O shows and hides it.
  bool overlay = true;
  bool outlines = true;
  bool shadows = true;
  Toon::ShadowSettings shadow_settings;
  bool mmd_self_shadow = true;
  bool mmd_morph_tints = false;
  bool outline_culling = true;
  Toon::LightingDebug lighting;
  std::optional<double> time;
  std::optional<double> time_step;
  bool play = false;
  double playback_speed = 1.0;
  bool loop = true;
  std::optional<std::uint64_t> expect_hulls;
  float camera_pan_x = 0.0F;
  float camera_pan_y = 0.0F;
  float camera_dolly = 0.0F;
  Toon::RenderOptions options;
  // Which backend presents: Vulkan, or WebGPU on Dawn's adapter of
  // `webgpu_adapter` (WebGPU policy §8), a build with TOON_ENABLE_WEBGPU.
  bool webgpu = false;
  Toon::WebGpuAdapter webgpu_adapter = Toon::WebGpuAdapter::Direct3D12;
  Toon::CompositeBackground background;
  // A USD stage to draw through Hydra instead of the bootstrap scene.
  std::string usd;
  // Fail unless the last frame drew this many meshes.
  std::optional<std::uint64_t> expect_draws;
  // Where the last of `frame_limit` frames is written, as a binary PPM.
  std::string screenshot;
  // The independent foreground product, exported as straight sRGB RGBA PNG.
  std::string foreground_output;
  // Bounded all-frame evidence; readbacks perturb timing, so benchmark separately.
  std::string capture_sequence;
  std::string camera_output;
  std::string telemetry_output;
  // The sample count asked for halfway through `frame_limit` frames, as the
  // number keys ask for one: the presentation test of a live change.
  std::optional<std::uint32_t> switch_samples;
  // An in-session file open halfway through a bounded run, for regression.
  std::string switch_file;
  // The stage's avatar driven by usd-avatar-runtime's real providers
  // (toon-viewport-avatar only).
  bool avatar_runtime = false;
  bool mmd_physics = false;
  std::string avatar_look_at;
  double avatar_rate = 120.0;
  std::uint64_t avatar_reset_every = 0;
  std::string avatar_motion;
  std::string avatar_model;
};

Arguments ParseArguments(int argc, char** argv);

} // namespace Toon::viewport
