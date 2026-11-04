// SPDX-License-Identifier: Apache-2.0
// The dedicated Tessera host's inspector, resizable scene pane and toolbar.
#pragma once

#include <tessera/input/focus.hpp>
#include <tessera/input/pointer.hpp>
#include <tessera/input/split_pane.hpp>
#include <tessera/layout/layout_box.hpp>
#include <tessera/render/draw_list.hpp>
#include <tessera/style/style_sheet.hpp>
#include <tessera/text/text.hpp>
#include <tessera/ui/document.hpp>
#include <tessera/ui/tree.hpp>
#include <tessera/ui/authoring.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
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

enum class PanelAction { SwitchPath,
  ResetCamera,
  ZoomIn,
  ZoomOut };

class Panel {
public:
  Panel();

  void Update(const PanelState& state, tessera::Size viewport);
  [[nodiscard]] std::vector<PanelAction> Dispatch(const tessera::InputEvent& event);

  [[nodiscard]] const tessera::UiDrawList& paint() const { return paint_; }
  [[nodiscard]] tessera::Rect bounds(const char* id) const;
  [[nodiscard]] tessera::Rect viewport() const {
    return bounds("viewport");
  }
  [[nodiscard]] bool IsScenePoint(tessera::Point position) const;
  [[nodiscard]] bool resizing() const {
    return splitter_.dragging();
  }
  [[nodiscard]] bool HitsSplitter(tessera::Point position) const;
  [[nodiscard]] float sidebar_size() const {
    return sidebar_size_;
  }

private:
  [[nodiscard]] tessera::HitTestInput hit() const;
  void Paint();
  void RevealFocus();

  tessera::authoring::Session session_;
  tessera::ValidationContext actions_;
  tessera::StyleSheet sheet_;
  std::unique_ptr<tessera::TextShaper> text_;
  std::unique_ptr<tessera::UiTree> tree_;
  std::vector<tessera::StyleDeclarations> overrides_;
  std::vector<tessera::ResolvedStyle> styles_;
  tessera::LayoutResult layout_;
  tessera::UiDrawList paint_;
  tessera::PointerDispatcher pointer_;
  tessera::FocusDispatcher focus_;
  tessera::SplitPaneDispatcher splitter_{"workspace"};
  tessera::InteractionState interaction_;
  std::optional<tessera::PointerMove> pointer_position_;
  std::chrono::microseconds pointer_time_{0};
  tessera::Point scroll_;
  float sidebar_size_ = 280;
  bool pointer_down_ = false;
  PanelState state_;
  tessera::Size size_{};
};

} // namespace toon_tessera
