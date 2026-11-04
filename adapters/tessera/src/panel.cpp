// SPDX-License-Identifier: Apache-2.0
#include "panel.hpp"

#include <tessera/input/scroll.hpp>
#include <tessera/render/paint.hpp>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace toon_tessera {
namespace {

namespace ui = tessera::authoring;

constexpr tessera::Color Rgb(int r, int g, int b, float a = 1) {
  return {r / 255.0F, g / 255.0F, b / 255.0F, a};
}

template <class T> T Take(tessera::Result<T> result, const char* operation) {
  if (!result) {
    for (const auto& d : result.diagnostics) {
      std::cerr << d.code << " at '" << d.path << "': " << d.message << '\n';
    }
    throw std::runtime_error(operation);
  }
  return std::move(*result.value);
}

std::string Format(const char* format, double value) {
  char text[64];
  std::snprintf(text, sizeof text, format, value);
  return text;
}

bool Contains(tessera::Rect rect, tessera::Point point) {
  return point.x >= rect.origin.x && point.y >= rect.origin.y &&
         point.x < rect.origin.x + rect.size.width &&
         point.y < rect.origin.y + rect.size.height;
}

ui::Element Workspace(const PanelState& state, float sidebar, tessera::Size size) {
  using namespace tessera;
  std::vector<ui::Element> properties;
  const auto row = [&](const char* name, std::string value) {
    properties.push_back(ui::Text(name, {.classes = {"muted"}}));
    properties.push_back(ui::Text(std::move(value)));
  };
  row("Render", std::to_string(state.width) + " x " + std::to_string(state.height));
  row("MSAA", std::to_string(state.samples) + "x");
  row("Frame", std::to_string(state.frame));
  row("Ring", std::to_string(state.generation));
  row("Host", Format("%.2f ms", state.frame_ms));
  row("Render", Format("%.2f ms", state.render_ms));
  row("GPU", Format("%.2f ms", state.gpu_ms));
  row("Readback", Format("%.0f bytes", static_cast<double>(state.readback_bytes)));

  auto inspector = ui::ScrollView(
      {ui::Text("HYDRA TOON", {.classes = {"title"}}),
          ui::Text("Lookdev viewport", {.classes = {"muted"}}),
          ui::Text(state.handoff ? "GPU handoff" : "CPU readback", {.id = "path"}),
          ui::Button("Switch path", "switch-path",
              {.node = {.id = "switch", .classes = {"button"}}}),
          ui::Text("Frame statistics", {.classes = {"heading"}}),
          ui::Grid(std::move(properties),
              {.id = "statistics",
                  .style = {.grid_columns = std::vector<GridTrack>{
                                GridTrack::points(64), GridTrack::fraction(1)},
                      .grid_rows = std::vector<GridTrack>(8),
                      .column_gap = 8.0F,
                      .row_gap = 6.0F}}),
          ui::Text("Right drag: orbit\nMiddle drag: pan\nWheel: zoom\nF: reset view\nSpace: switch path", {.classes = {"muted"}})},
      {.id = "inspector", .classes = {"inspector"}});
  auto scene = ui::Stack(
      {ui::Column({}, {.id = "scene"}),
          ui::Row(
              {ui::Button("Reset", "reset-camera",
                   {.node = {.id = "reset", .classes = {"button"}}}),
                  ui::Button("+", "zoom-in",
                      {.node = {.id = "zoom-in", .classes = {"button"}}}),
                  ui::Button("-", "zoom-out",
                      {.node = {.id = "zoom-out", .classes = {"button"}}})},
              {.id = "toolbar",
                  .style = {.margin = Edges{8, 8, 8, 8}, .padding = Edges{4, 4, 4, 4}, .gap = 4.0F, .background = Rgb(24, 28, 38, 0.9F), .corner_radius = 6.0F, .stack_horizontal = Align::start, .stack_vertical = Align::start}})},
      {.id = "viewport", .style = {.overflow = Overflow::clip}});
  const float handle = std::min(8.0F, size.width * 0.05F);
  const float available = std::max(0.0F, size.width - handle);
  return ui::HorizontalSplitPane(std::move(inspector), std::move(scene), sidebar,
      "resize-sidebar",
      {.node = {.id = "workspace",
           .style = {.overflow = Overflow::clip, .text = {.size = 13.0F}, .color = Rgb(225, 230, 240)}},
          .limits = {std::min(176.0F, available * 0.55F),
              std::min(96.0F, available * 0.45F), handle},
          .label = "Resize inspector"});
}

} // namespace

Panel::Panel()
    : actions_{{"switch-path", "reset-camera", "zoom-in", "zoom-out", "resize-sidebar"}},
      text_(std::make_unique<tessera::PlaceholderTextShaper>()) {
  using S = tessera::StyleSelector;
  const auto rule = [&](S selector, auto declare) {
    tessera::StyleDeclarations values;
    declare(values);
    sheet_.rules.push_back({std::move(selector), values});
  };
  rule(S::of_class("inspector"), [](auto& s) {
    s.padding = tessera::Edges{12, 12, 12, 12};
    s.gap = 10.0F;
    s.background = Rgb(24, 28, 38);
  });
  rule(S::of_id("workspace-handle"), [](auto& s) {
    s.border = tessera::Edges{1, 1, 1, 1};
    s.border_color = Rgb(62, 72, 90);
  });
  rule(S::of_class("title"), [](auto& s) {
    s.text.size = 18.0F;
    s.color = Rgb(255, 255, 255);
  });
  rule(S::of_class("heading"), [](auto& s) { s.color = Rgb(160, 190, 240); });
  rule(S::of_class("muted"), [](auto& s) { s.color = Rgb(155, 168, 190); });
  rule(S::of_class("button"), [](auto& s) {
    s.padding = tessera::Edges{6, 10, 6, 10};
    s.border = tessera::Edges{1, 1, 1, 1};
    s.border_color = Rgb(90, 120, 180);
    s.corner_radius = 4.0F;
    s.background = Rgb(47, 74, 128);
    s.align = tessera::Align::center;
  });
  rule(S::of_class("button", {.hover = true}), [](auto& s) {
    s.background = Rgb(62, 94, 155);
  });
  rule(S::of_class("button", {.active = true}), [](auto& s) {
    s.background = Rgb(32, 54, 100);
  });
  rule(S::of_class("button", {.focus = true}), [](auto& s) {
    s.border_color = Rgb(225, 235, 255);
  });
  rule(S::of_id("workspace-handle", {.focus = true}), [](auto& s) {
    s.border_color = Rgb(225, 235, 255);
  });
}

tessera::HitTestInput Panel::hit() const {
  return {tree_.get(), styles_, &layout_};
}

void Panel::Update(const PanelState& state, tessera::Size viewport) {
  state_ = state;
  // PointerDispatcher capture is tree-identity scoped. Keep the accepted snapshot
  // until release; SplitPaneDispatcher independently supports replacement mid-drag.
  if (pointer_down_ && viewport == size_)
    return;
  if (pointer_down_) {
    pointer_.reset();
    pointer_down_ = false;
  }
  auto authored = Take(session_.lower(Workspace(state, sidebar_size_, viewport), actions_),
      "workspace composition rejected");
  auto tree = Take(tessera::UiTree::create(authored.document, actions_),
      "workspace tree rejected");
  auto styles = Take(tessera::resolve_styles({tree.get(), &sheet_, {}, authored.styles}),
      "workspace styles rejected");
  std::vector<tessera::Point> offsets(tree->size());
  offsets[tree->find("inspector")->index] = scroll_;
  auto layout = Take(tessera::compute_layout(
                         {tree.get(), styles, viewport, text_.get(), offsets}),
      "workspace layout rejected");
  session_.publish();
  tree_ = std::move(tree);
  overrides_ = std::move(authored.styles);
  styles_ = std::move(styles);
  layout_ = std::move(layout);
  size_ = viewport;
  const auto split = Take(splitter_.refresh(hit()), "workspace splitter refresh failed");
  if (!split.size)
    throw std::runtime_error("workspace splitter has no size");
  const bool clamped = sidebar_size_ != *split.size;
  sidebar_size_ = *split.size;
  for (const auto& box : layout_.boxes) {
    if (box.node == *tree_->find("inspector") && box.scroll)
      scroll_ = box.scroll->offset;
  }
  interaction_ = {};
  interaction_.focused = Take(focus_.refresh(hit()), "workspace focus refresh failed").focused;
  auto pointers = Take(pointer_.refresh(hit()), "workspace pointer refresh failed");
  if (pointer_position_) {
    pointers = Take(pointer_.dispatch(hit(), {pointer_time_, *pointer_position_}),
        "workspace pointer retarget failed");
  }
  for (const auto& pointer : pointers.pointers) {
    if (pointer.hovered)
      interaction_.hovered.push_back(*pointer.hovered);
    if (pointer.active)
      interaction_.active.push_back(*pointer.active);
  }
  Paint();
  if (clamped)
    Update(state_, size_);
}

void Panel::Paint() {
  styles_ = Take(tessera::resolve_styles(
                     {tree_.get(), &sheet_, interaction_, overrides_}),
      "workspace interaction styles rejected");
  paint_ = Take(tessera::build_paint_list({tree_.get(), styles_, &layout_, text_.get()}),
      "workspace interaction paint rejected");
}

void Panel::RevealFocus() {
  if (!interaction_.focused)
    return;
  for (const auto& update : Take(tessera::scroll_into_view(hit(), *interaction_.focused),
           "workspace focus reveal failed")) {
    if (update.container == *tree_->find("inspector"))
      scroll_ = update.offset;
  }
}

std::vector<PanelAction> Panel::Dispatch(const tessera::InputEvent& event) {
  if (!tree_)
    return {};
  const auto errors = tessera::validate(event);
  if (!errors.empty()) {
    (void)Take(tessera::Result<bool>{std::nullopt, errors}, "workspace input rejected");
  }
  std::visit([&](const auto& data) {
    using T = std::decay_t<decltype(data)>;
    if constexpr (std::is_same_v<T, tessera::PointerMove> ||
                  std::is_same_v<T, tessera::PointerDown> || std::is_same_v<T, tessera::PointerUp>) {
      pointer_position_ = tessera::PointerMove{data.pointer, data.position, data.modifiers};
      pointer_time_ = event.timestamp;
    } else if constexpr (std::is_same_v<T, tessera::PointerCancel>) {
      pointer_position_.reset();
      pointer_time_ = event.timestamp;
    }
  },
      event.data);
  std::vector<PanelAction> actions;
  const auto focused = Take(focus_.refresh(hit()), "workspace focus refresh failed").focused;
  const auto split = Take(splitter_.dispatch(hit(), event, focused),
      "workspace splitter input failed");
  if (split.handled) {
    if (split.focus) {
      interaction_.focused = Take(focus_.focus(hit(), *split.focus),
          "workspace splitter focus failed")
                                 .focused;
    }
    if (split.size)
      sidebar_size_ = *split.size;
    Update(state_, size_);
    return actions;
  }
  if (const auto* scroll = std::get_if<tessera::Scroll>(&event.data)) {
    for (const auto& update : Take(tessera::route_scroll(hit(), *scroll),
             "workspace scroll failed")
             .updates) {
      if (update.container == *tree_->find("inspector"))
        scroll_ = update.offset;
    }
    Update(state_, size_);
    return actions;
  }
  std::vector<tessera::ActionRequest> requests;
  const bool pointer_event = std::holds_alternative<tessera::PointerDown>(event.data) ||
                             std::holds_alternative<tessera::PointerMove>(event.data) ||
                             std::holds_alternative<tessera::PointerUp>(event.data) ||
                             std::holds_alternative<tessera::PointerCancel>(event.data);
  if (pointer_event) {
    const auto result = Take(pointer_.dispatch(hit(), event), "workspace pointer input failed");
    requests = result.actions;
    interaction_.hovered.clear();
    interaction_.active.clear();
    pointer_down_ = false;
    for (const auto& pointer : result.pointers) {
      if (pointer.hovered)
        interaction_.hovered.push_back(*pointer.hovered);
      if (pointer.active)
        interaction_.active.push_back(*pointer.active);
      pointer_down_ = pointer_down_ || pointer.primary_down;
      if (pointer.pressed && std::holds_alternative<tessera::PointerDown>(event.data)) {
        interaction_.focused = Take(focus_.focus(hit(), *pointer.pressed),
            "workspace pointer focus failed")
                                   .focused;
      }
    }
  } else {
    const auto result = Take(focus_.dispatch(hit(), event), "workspace focus input failed");
    interaction_.focused = result.focused;
    requests = result.actions;
    RevealFocus();
  }
  for (const auto& request : requests) {
    if (request.action == "switch-path")
      actions.push_back(PanelAction::SwitchPath);
    else if (request.action == "reset-camera")
      actions.push_back(PanelAction::ResetCamera);
    else if (request.action == "zoom-in")
      actions.push_back(PanelAction::ZoomIn);
    else if (request.action == "zoom-out")
      actions.push_back(PanelAction::ZoomOut);
    else
      throw std::runtime_error("unexpected workspace action: " + request.action);
  }
  if (pointer_down_)
    Paint();
  else
    Update(state_, size_);
  return actions;
}

tessera::Rect Panel::bounds(const char* id) const {
  if (tree_) {
    const auto node = tree_->find(id);
    for (const auto& box : layout_.boxes) {
      if (node && box.node == *node)
        return box.border_box;
    }
  }
  throw std::runtime_error(std::string("missing workspace geometry: ") + id);
}

bool Panel::IsScenePoint(tessera::Point position) const {
  if (!tree_) return false;
  const auto target = Take(tessera::hit_test(hit(), position), "workspace hit test failed").target;
  return target && (target == tree_->find("scene") || target == tree_->find("viewport"));
}

bool Panel::HitsSplitter(tessera::Point position) const {
  return tree_ && Contains(bounds("workspace-handle"), position);
}

} // namespace toon_tessera
