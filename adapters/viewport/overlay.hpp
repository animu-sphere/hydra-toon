// SPDX-License-Identifier: Apache-2.0
// The viewport's overlay: its measurements and controls, laid out by Dear
// ImGui and handed to the present session as an OverlayDrawList, so ImGui
// never reaches the backend. ImGui's types stay in overlay.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include <toon/overlay.hpp>
#include <toon/vulkan_present.hpp>

#include "telemetry.hpp"
#include "window.hpp"

namespace Toon::viewport {

// What the last frame drew, as the Hydra host evidence counts it.
struct SceneCounts {
  std::size_t draws = 0;
  std::size_t mtoon = 0;
  std::size_t transparent = 0;
  std::size_t outline = 0;
  std::size_t skinned = 0;
  std::size_t authored_normals = 0;
  std::size_t preview_materials = 0;
  std::size_t mtoon_materials = 0;
  std::size_t textures = 0;
};

// What reached the GPU, of each kind, as PresentStatistics counts it.
struct UploadCounts {
  std::uint64_t topology = 0;
  std::uint64_t points = 0;
  std::uint64_t materials = 0;
  std::uint64_t textures = 0;
  std::uint64_t skins = 0;
  std::uint64_t poses = 0;
  std::uint64_t overlay_textures = 0;

  [[nodiscard]] static UploadCounts Of(const PresentStatistics& statistics);
  // Every scene upload or write of any kind; the overlay's are not the
  // scene's.
  [[nodiscard]] std::uint64_t scene() const {
    return topology + points + materials + textures + skins + poses;
  }
  friend UploadCounts operator-(const UploadCounts& after,
      const UploadCounts& before);
};

// What the overlay shows of one frame.
struct OverlayFrame {
  std::string_view scene;
  bool can_open_file = false;
  std::string_view open_error;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool vsync = true;
  bool outlines = true;
  LightingDebug lighting;
  std::size_t lights = 0;
  const PresentStatistics* statistics = nullptr;
  const FrameTelemetry* telemetry = nullptr;
  SceneCounts counts;
  // The last frame's uploads, and the session's.
  UploadCounts frame_uploads;
  UploadCounts uploads;
};

// What the overlay's controls asked for this frame.
struct OverlayControls {
  bool open_file = false;
  // MSAA samples per pixel; 0 when unchanged.
  std::uint32_t samples = 0;
  bool outlines_changed = false;
  bool outlines = true;
  LightingDebug lighting;
};

class Overlay {
public:
  // Text and spacing scale with the monitor's `content_scale`.
  explicit Overlay(float content_scale);
  ~Overlay();
  Overlay(const Overlay&) = delete;
  Overlay& operator=(const Overlay&) = delete;

  // Every window event goes through here, whether or not the overlay then
  // keeps it from the camera.
  void HandleEvent(const Event& event);
  // Whether the pointer is over the overlay, so the camera leaves a press or
  // the wheel to it.
  [[nodiscard]] bool WantsPointer() const;

  // Lays out this frame's overlay into `list`, reusing its storage, `delta`
  // seconds after the last.
  OverlayControls Build(const OverlayFrame& frame, double delta,
      OverlayDrawList& list);
  // Draws nothing this frame, but keeps the textures resident.
  static void Hide(OverlayDrawList& list);

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace Toon::viewport
