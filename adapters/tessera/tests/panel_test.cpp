// SPDX-License-Identifier: Apache-2.0
#include "compare.hpp"
#include "panel.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void Require(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

tessera::Point Center(tessera::Rect rect) {
  return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
}

void PanelContract() {
  using namespace tessera;
  using toon_tessera::PanelAction;
  toon_tessera::Panel panel;
  toon_tessera::PanelState state;
  Size size{1024, 640};
  std::int64_t time = 0;
  const auto event = [&](auto data) {
    return panel.Dispatch({std::chrono::microseconds(++time), std::move(data)});
  };
  const auto click = [&](const char* id, PanelAction action) {
    const Point position = Center(panel.bounds(id));
    Require(event(PointerDown{{1}, position}).empty(), "press must not activate");
    // Telemetry changes every frame while native clicks span several frames.
    for (int i = 0; i < 3; ++i) {
      ++state.frame;
      panel.Update(state, size);
    }
    const auto actions = event(PointerUp{{1}, position});
    Require(actions == std::vector{action}, "click across frames lost or duplicated its action");
  };
  panel.Update(state, size);
  Require(panel.viewport().size.width > 600 && panel.viewport().size.height == size.height,
      "scene must fill the remaining split pane");
  Require(!panel.paint().commands.empty(), "workspace must paint");
  Require(panel.IsScenePoint({800, 400}), "scene input must reach the camera");
  Require(!panel.IsScenePoint(Center(panel.bounds("switch"))),
      "inspector input must not reach the camera");
  Require(!panel.IsScenePoint(Center(panel.bounds("reset"))),
      "toolbar input must not reach the camera");
  (void)event(PointerMove{{1}, Center(panel.bounds("switch"))});
  const auto hovered = panel.paint();
  panel.Update(state, size);
  Require(panel.paint() == hovered, "stationary hover must survive telemetry snapshots");
  Require(event(PointerUp{{1}, Center(panel.bounds("switch"))}).empty(),
      "release without press must not activate");
  click("switch", PanelAction::SwitchPath);
  click("reset", PanelAction::ResetCamera);
  click("zoom-in", PanelAction::ZoomIn);
  click("zoom-out", PanelAction::ZoomOut);

  auto point = Center(panel.bounds("switch"));
  (void)event(PointerDown{{1}, point});
  Require(event(PointerUp{{1}, {800, 400}}).empty(), "drag-out must cancel activation");
  (void)event(PointerDown{{1}, point});
  (void)event(PointerCancel{{1}});
  Require(event(PointerUp{{1}, point}).empty(), "capture loss must cancel activation");

  const float before = panel.sidebar_size();
  point = Center(panel.bounds("workspace-handle"));
  Require(panel.HitsSplitter(point), "separator must have a resize hit region");
  (void)event(PointerDown{{1}, point});
  Require(panel.resizing(), "separator must capture a primary press");
  (void)event(PointerMove{{1}, {point.x + 48, point.y}});
  Require(std::abs(panel.sidebar_size() - before - 48) < 0.01F,
      "separator must resize the inspector in logical units");
  panel.Update(state, size);
  (void)event(PointerMove{{1}, {point.x + 64, point.y}});
  Require(std::abs(panel.sidebar_size() - before - 64) < 0.01F,
      "separator capture must survive a new snapshot");
  (void)event(PointerUp{{1}, {point.x + 64, point.y}});
  Require(!panel.resizing(), "separator must release capture");

  const float dragged = panel.sidebar_size();
  (void)event(Navigate{Direction::left});
  Require(panel.sidebar_size() == dragged - 8, "focused separator must support arrow keys");
  (void)event(KeyDown{Key::home});
  Require(panel.sidebar_size() == 176, "Home must reach the inspector minimum");
  (void)event(FocusPrevious{});
  Require(event(Activate{}) == std::vector{PanelAction::SwitchPath},
      "keyboard and pointer must request the same path command");
  (void)event(FocusNext{});
  (void)event(FocusNext{});
  Require(event(Activate{}) == std::vector{PanelAction::ResetCamera},
      "Tab must reach the viewport toolbar");

  for (const float scale : {1.0F, 1.25F, 1.5F, 2.0F}) {
    size = {640 / scale, 360 / scale};
    panel.Update(state, size);
    const auto viewport = panel.viewport();
    Require(viewport.origin.x >= 0 && viewport.origin.x + viewport.size.width <= size.width + 0.01F &&
                viewport.size.width > 0 && viewport.size.height == size.height,
        "scaled layout must retain a nonempty scene inside the window");
    const auto previous = panel.bounds("statistics").origin.y;
    (void)event(Scroll{{20, 100}, {0, 1000}});
    Require(panel.bounds("statistics").origin.y < previous,
        "wheel over inspector must scroll its clipped contents");
    (void)event(Scroll{{20, 100}, {0, -1000}});
    (void)event(FocusPrevious{});
    (void)event(FocusPrevious{});
    Require(event(Activate{}) == std::vector{PanelAction::SwitchPath},
        "focus navigation must reveal the scrolled inspector control");
    (void)event(FocusNext{});
    (void)event(FocusNext{});
  }
  size = {1024, 640};
  panel.Update(state, size);
  Require(panel.sidebar_size() <= 176, "growing must not restore an obsolete sidebar request");

  point = Center(panel.bounds("switch"));
  (void)event(PointerDown{{1}, point});
  size = {800, 500};
  panel.Update(state, size);
  Require(event(PointerUp{{1}, point}).empty(), "resize must cancel an old button press");
  size = {120, 80};
  panel.Update(state, size);
  Require(panel.viewport().size.width > 0, "tiny windows must retain a scene region");
}

void CompositeContract() {
  using namespace toon_tessera;
  Toon::ColorProduct product;
  product.width = 2;
  product.height = 2;
  product.row_pitch = 12;
  product.payload.resize(24, 0);
  std::vector<std::uint8_t> presented(6 * 4 * 4, 255);
  const PixelRect region{3, 1, 5, 3};
  for (std::uint32_t y = 1; y < 3; ++y) {
    for (std::uint32_t x = 3; x < 5; ++x) {
      for (std::uint32_t c = 0; c < 3; ++c)
        presented[(y * 6 + x) * 4 + c] = 0;
    }
  }
  for (const bool bgra : {false, true}) {
    auto result = CompareComposite(presented, 6, 4, bgra, product, {}, region, {});
    Require(result.pixels == 4 && result.exact == 4 && result.max_difference == 0,
        "comparison must map a padded product into its offset scene region");
    presented[(1 * 6 + 3) * 4] = 255;
    result = CompareComposite(presented, 6, 4, bgra, product, {}, region, {3, 1, 4, 2});
    Require(result.pixels == 3 && result.exact == 3,
        "comparison must exclude only toolbar pixels inside the scene");
    presented[(1 * 6 + 3) * 4] = 0;
  }
  bool rejected = false;
  try {
    (void)CompareComposite(presented, 6, 4, false, product, {}, {5, 1, 7, 3}, {});
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  Require(rejected, "comparison must reject a region outside the swapchain");
}

} // namespace

int main() {
  try {
    PanelContract();
    CompositeContract();
    std::cout << "Tessera workspace layout, input and composite contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
