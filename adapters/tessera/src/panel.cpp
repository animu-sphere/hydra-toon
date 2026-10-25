// SPDX-License-Identifier: Apache-2.0
#include "panel.hpp"

#include <tessera/render/paint.hpp>
#include <tessera/ui/serialization.hpp>

#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace toon_tessera {
namespace {

// The placeholder shaper draws ASCII letters, digits and . , : - _ + / ! ? ( );
// every label below keeps to them.
constexpr const char* kPanelJson = R"json({"version":1,"root":{"type":"Box","id":"screen","children":[
{"type":"Box","id":"panel","children":[
{"type":"Text","id":"title","properties":{"text":"hydra-toon on Tessera"}},
{"type":"Text","id":"path","properties":{"text":""}},
{"type":"Text","id":"extent","properties":{"text":""}},
{"type":"Text","id":"timing","properties":{"text":""}},
{"type":"Text","id":"bytes","properties":{"text":""}},
{"type":"Box","id":"switch","classes":["button"],"events":{"activate":"switch-path"},"children":[
{"type":"Text","id":"switch-label","properties":{"text":"Switch path (Space)"}}]}]}]}})json";

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

std::string Format(const char* format, double a, double b = 0, double c = 0) {
  char text[96];
  std::snprintf(text, sizeof text, format, a, b, c);
  return text;
}

tessera::UiNode& Child(tessera::UiNode& node, std::size_t index) {
  return node.children.at(index);
}

} // namespace

Panel::Panel()
    : actions_{{"switch-path"}},
      text_(std::make_unique<tessera::PlaceholderTextShaper>()) {
  document_ = Take(tessera::load_document(kPanelJson, actions_), "panel JSON rejected");
  using S = tessera::StyleSelector;
  const auto rule = [&](S selector, auto declare) {
    tessera::StyleDeclarations values;
    declare(values);
    sheet_.rules.push_back({std::move(selector), values});
  };
  // The screen is transparent so the avatar shows; only the panel paints.
  rule(S::of_id("screen"), [](auto& s) {
    s.justify = tessera::Justify::start;
    s.align = tessera::Align::start;
    s.padding = tessera::Edges{12, 12, 12, 12};
  });
  rule(S::of_id("panel"), [](auto& s) {
    s.padding = tessera::Edges{12, 14, 12, 14};
    s.gap = 6.0F;
    s.border = tessera::Edges{1, 1, 1, 1};
    s.border_color = Rgb(90, 104, 130);
    s.corner_radius = 8.0F;
    s.background = Rgb(24, 28, 38, 0.85F);
    s.text.size = 14.0F;
    s.color = Rgb(225, 230, 240);
  });
  rule(S::of_id("title"), [](auto& s) {
    s.text.size = 18.0F;
    s.color = Rgb(255, 255, 255);
  });
  rule(S::of_class("button"), [](auto& s) {
    s.padding = tessera::Edges{6, 10, 6, 10};
    s.border = tessera::Edges{1, 1, 1, 1};
    s.border_color = Rgb(90, 120, 180);
    s.corner_radius = 5.0F;
    s.background = Rgb(47, 74, 128);
    s.align = tessera::Align::center;
  });
}

void Panel::Update(const PanelState& state, tessera::Size viewport) {
  auto document = document_;
  auto& panel = Child(document.root, 0);
  const auto set = [&](std::size_t index, std::string text) {
    Child(panel, index).properties["text"] = std::move(text);
  };
  set(1, std::string("Path: ") + (state.handoff ? "GPU handoff" : "readback") +
             Format(" - frame %.0f", static_cast<double>(state.frame)));
  set(2, Format("%.0f x %.0f, ", state.width, state.height) +
             Format("%.0fx MSAA, generation %.0f", state.samples,
                 static_cast<double>(state.generation)));
  set(3, Format("Frame %.2f ms, render %.2f ms, GPU %.2f ms", state.frame_ms,
             state.render_ms, state.gpu_ms));
  set(4, Format("Read back: %.0f bytes per frame",
             static_cast<double>(state.readback_bytes)));
  tree_ = Take(tessera::UiTree::create(document, actions_), "panel tree rejected");
  styles_ = Take(tessera::resolve_styles({tree_.get(), &sheet_, {}, {}}),
      "panel styles rejected");
  layout_ = Take(tessera::compute_layout({tree_.get(), styles_, viewport, text_.get(), {}}),
      "panel layout rejected");
  paint_ = Take(tessera::build_paint_list({tree_.get(), styles_, &layout_, text_.get()}),
      "panel paint rejected");
}

tessera::Rect Panel::bounds() const {
  const auto node = tree_->find("panel");
  for (const auto& box : layout_.boxes) {
    if (node && box.node == *node) return box.border_box;
  }
  return {};
}

bool Panel::HitsSwitch(tessera::Point position) const {
  if (!tree_) return false;
  const auto hit = tessera::hit_test({tree_.get(), styles_, &layout_}, position);
  if (!hit || !hit.value->target) return false;
  const auto target = *hit.value->target;
  const auto button = tree_->find("switch");
  const auto label = tree_->find("switch-label");
  return (button && target == *button) || (label && target == *label);
}

} // namespace toon_tessera
