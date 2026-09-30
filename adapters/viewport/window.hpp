// SPDX-License-Identifier: Apache-2.0
// The window abstraction owned by the viewport adapter. GLFW is the native
// host boundary: its types stay inside window_glfw.cpp and never reach the
// core or backend targets. Input handling beyond close/resize/escape is an
// intentional project extension point.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <toon/vulkan_present.hpp>

namespace Toon::viewport {

enum class EventType {
  Close,
  Resize,
  KeyDown,
  PointerDown,
  PointerUp,
  PointerMove,
  Scroll,
};

enum class Key {
  Unknown,
  Escape,
  F,
  O,
  P,
  R,
  // The number row's 1, 2, 4 and 8: MSAA samples per pixel.
  Digit1,
  Digit2,
  Digit4,
  Digit8,
};

enum class PointerButton {
  None,
  Left,
  Middle,
  Right,
};

struct Event {
  EventType type = EventType::Close;
  Key key = Key::Unknown;
  PointerButton button = PointerButton::None;
  // Held while a key or button went down.
  bool shift = false;
  bool alt = false;
  bool control = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  // The cursor in window pixels from the top left, for the pointer events;
  // the wheel's vertical offset in notches, positive away from the user,
  // for Scroll.
  double x = 0.0;
  double y = 0.0;
};

class Window {
public:
  // Throws std::runtime_error when the windowing environment is unavailable
  // (no display, no Vulkan-capable GLFW); callers report that as a skip.
  static std::unique_ptr<Window> Create(std::string_view title,
      std::uint32_t width,
      std::uint32_t height, bool visible);
  virtual ~Window() = default;

  [[nodiscard]] virtual bool PollEvent(Event& event) = 0;
  virtual void WaitForEvent() = 0;
  virtual void SetTitle(std::string_view title) = 0;
  [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
  [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;
  // The monitor's scale for content, 1 at 96 DPI on Windows: what the
  // overlay scales its text and spacing by.
  [[nodiscard]] virtual float content_scale() const noexcept = 0;
  // UTF-8 scene path selected by the native dialog. Empty on cancellation or
  // failure; only a failure sets `error`. Built with the Hydra adapter.
  [[nodiscard]] virtual std::string OpenFile(std::string& error) = 0;
};

// Bundles the GLFW-required instance extensions and the surface-creation
// callback for the backend-owned VkSurfaceKHR. The window must outlive every
// session created from the returned provider.
[[nodiscard]] PresentSurfaceProvider MakeSurfaceProvider(Window& window);

} // namespace Toon::viewport
