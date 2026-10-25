// SPDX-License-Identifier: Apache-2.0
// The Tessera UI of the Tessera host (GPU handoff policy §9): a small panel
// over the avatar that shows the frame's path and timings and switches the
// path. JSON v1 document -> tree -> styles -> layout -> paint, all Tessera's;
// the host owns the window, the device and the frame loop.
#pragma once

#include <tessera/input/pointer.hpp>
#include <tessera/layout/layout_box.hpp>
#include <tessera/render/draw_list.hpp>
#include <tessera/style/style_sheet.hpp>
#include <tessera/text/text.hpp>
#include <tessera/ui/document.hpp>
#include <tessera/ui/tree.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace toon_tessera {

// What the panel shows; the host fills it each frame.
struct PanelState {
  bool handoff = true;
  std::uint64_t frame = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t samples = 1;
  std::uint64_t generation = 0;
  double frame_ms = 0;
  double render_ms = 0;
  double gpu_ms = 0;
  std::uint64_t readback_bytes = 0;
};

class Panel {
public:
  Panel();

  // Rebuilds the panel's text, layout and paint for a logical viewport.
  void Update(const PanelState& state, tessera::Size viewport);

  [[nodiscard]] const tessera::UiDrawList& paint() const { return paint_; }
  // The panel's border box, in logical units: what the smoke excludes from
  // its comparison with the reference composite.
  [[nodiscard]] tessera::Rect bounds() const;
  // Whether a logical point lies on the path switch.
  [[nodiscard]] bool HitsSwitch(tessera::Point position) const;

private:
  tessera::UiDocument document_;
  tessera::ValidationContext actions_;
  tessera::StyleSheet sheet_;
  std::unique_ptr<tessera::TextShaper> text_;
  std::unique_ptr<tessera::UiTree> tree_;
  std::vector<tessera::ResolvedStyle> styles_;
  tessera::LayoutResult layout_;
  tessera::UiDrawList paint_;
};

} // namespace toon_tessera
