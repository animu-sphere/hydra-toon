// SPDX-License-Identifier: Apache-2.0
#include "window.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace Toon::viewport {
namespace {

std::runtime_error GlfwError(std::string_view operation) {
  const char* detail = nullptr;
  const int code = glfwGetError(&detail);
  return std::runtime_error(std::string(operation) + " failed (GLFW " +
                            std::to_string(code) + ")" +
                            (detail == nullptr ? ""
                                               : ": " + std::string(detail)));
}

// Owns process-wide GLFW initialization; create at most one instance.
class GlfwWindow final : public Window {
public:
  GlfwWindow(std::string_view title, std::uint32_t width, std::uint32_t height,
      bool visible) {
    if (glfwInit() != GLFW_TRUE) {
      throw GlfwError("initialize GLFW");
    }
    initialized_ = true;
    // A constructor exception skips the destructor, so release GLFW here.
    try {
      Initialize(title, width, height, visible);
    } catch (...) {
      if (window_ != nullptr) {
        glfwDestroyWindow(window_);
      }
      glfwTerminate();
      throw;
    }
  }

  ~GlfwWindow() override {
    if (window_ != nullptr) {
      glfwDestroyWindow(window_);
    }
    if (initialized_) {
      glfwTerminate();
    }
  }

  bool PollEvent(Event& event) override {
    glfwPollEvents();
    if (events_.empty()) {
      return false;
    }
    event = events_.front();
    events_.pop_front();
    return true;
  }

  void WaitForEvent() override {
    glfwWaitEvents();
  }

  void SetTitle(std::string_view title) override {
    const std::string owned(title);
    glfwSetWindowTitle(window_, owned.c_str());
  }

  std::uint32_t width() const noexcept override {
    return width_;
  }
  std::uint32_t height() const noexcept override {
    return height_;
  }

  [[nodiscard]] GLFWwindow* native() const noexcept {
    return window_;
  }

private:
  void Initialize(std::string_view title, std::uint32_t width,
      std::uint32_t height, bool visible) {
    if (glfwVulkanSupported() != GLFW_TRUE) {
      throw GlfwError("query GLFW Vulkan support");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    const std::string owned_title(title);
    window_ = glfwCreateWindow(static_cast<int>(width),
        static_cast<int>(height), owned_title.c_str(),
        nullptr, nullptr);
    if (window_ == nullptr) {
      throw GlfwError("create GLFW viewport window");
    }
    glfwSetWindowUserPointer(window_, this);
    glfwSetWindowCloseCallback(window_, [](GLFWwindow* window) {
      auto& self = Self(window);
      self.events_.push_back({EventType::Close});
      glfwSetWindowShouldClose(window, GLFW_FALSE);
    });
    glfwSetFramebufferSizeCallback(
        window_, [](GLFWwindow* window, int width, int height) {
          auto& self = Self(window);
          self.width_ = width < 0 ? 0U : static_cast<std::uint32_t>(width);
          self.height_ = height < 0 ? 0U : static_cast<std::uint32_t>(height);
          Event event{EventType::Resize};
          event.width = self.width_;
          event.height = self.height_;
          self.events_.push_back(event);
        });
    glfwSetKeyCallback(
        window_, [](GLFWwindow* window, int key, int, int action, int mods) {
          if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            Event event{EventType::KeyDown};
            event.key = ToKey(key);
            event.shift = (mods & GLFW_MOD_SHIFT) != 0;
            event.alt = (mods & GLFW_MOD_ALT) != 0;
            Self(window).events_.push_back(event);
          }
        });
    glfwSetMouseButtonCallback(
        window_, [](GLFWwindow* window, int button, int action, int mods) {
          Event event{action == GLFW_PRESS ? EventType::PointerDown
                                           : EventType::PointerUp};
          event.button = ToButton(button);
          event.shift = (mods & GLFW_MOD_SHIFT) != 0;
          event.alt = (mods & GLFW_MOD_ALT) != 0;
          glfwGetCursorPos(window, &event.x, &event.y);
          Self(window).events_.push_back(event);
        });
    glfwSetCursorPosCallback(
        window_, [](GLFWwindow* window, double x, double y) {
          Event event{EventType::PointerMove};
          event.x = x;
          event.y = y;
          Self(window).events_.push_back(event);
        });
    glfwSetScrollCallback(
        window_, [](GLFWwindow* window, double, double y) {
          Event event{EventType::Scroll};
          event.y = y;
          Self(window).events_.push_back(event);
        });
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    glfwGetFramebufferSize(window_, &framebuffer_width, &framebuffer_height);
    width_ = static_cast<std::uint32_t>(std::max(0, framebuffer_width));
    height_ = static_cast<std::uint32_t>(std::max(0, framebuffer_height));
  }

  static GlfwWindow& Self(GLFWwindow* window) {
    return *static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
  }

  static Key ToKey(int key) {
    switch (key) {
    case GLFW_KEY_ESCAPE:
      return Key::Escape;
    case GLFW_KEY_P:
      return Key::P;
    case GLFW_KEY_F:
      return Key::F;
    case GLFW_KEY_R:
      return Key::R;
    default:
      return Key::Unknown;
    }
  }

  static PointerButton ToButton(int button) {
    switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT:
      return PointerButton::Left;
    case GLFW_MOUSE_BUTTON_MIDDLE:
      return PointerButton::Middle;
    case GLFW_MOUSE_BUTTON_RIGHT:
      return PointerButton::Right;
    default:
      return PointerButton::None;
    }
  }

  bool initialized_ = false;
  GLFWwindow* window_ = nullptr;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  std::deque<Event> events_;
};

template <typename Handle>
std::uintptr_t EncodeHandle(Handle handle) noexcept {
  if constexpr (std::is_pointer_v<Handle>) {
    return reinterpret_cast<std::uintptr_t>(handle);
  } else {
    return static_cast<std::uintptr_t>(handle);
  }
}

template <typename Handle>
Handle DecodeHandle(std::uintptr_t handle) noexcept {
  if constexpr (std::is_pointer_v<Handle>) {
    return reinterpret_cast<Handle>(handle);
  } else {
    return static_cast<Handle>(handle);
  }
}

std::int32_t CreateSurface(void* user_data, std::uintptr_t encoded_instance,
    std::uintptr_t* encoded_surface) {
  if (user_data == nullptr || encoded_instance == 0 ||
      encoded_surface == nullptr) {
    return static_cast<std::int32_t>(VK_ERROR_INITIALIZATION_FAILED);
  }
  VkSurfaceKHR surface{};
  const VkResult result = glfwCreateWindowSurface(
      DecodeHandle<VkInstance>(encoded_instance),
      static_cast<GLFWwindow*>(user_data), nullptr, &surface);
  if (result == VK_SUCCESS) {
    *encoded_surface = EncodeHandle(surface);
  }
  return static_cast<std::int32_t>(result);
}

} // namespace

std::unique_ptr<Window> Window::Create(std::string_view title,
    std::uint32_t width,
    std::uint32_t height, bool visible) {
  return std::make_unique<GlfwWindow>(title, width, height, visible);
}

PresentSurfaceProvider MakeSurfaceProvider(Window& window) {
  std::uint32_t count = 0;
  const char** extensions = glfwGetRequiredInstanceExtensions(&count);
  if (extensions == nullptr || count == 0) {
    throw GlfwError("query GLFW Vulkan instance extensions");
  }
  PresentSurfaceProvider provider;
  provider.instance_extensions.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    provider.instance_extensions.emplace_back(extensions[index]);
  }
  provider.create_surface = CreateSurface;
  provider.user_data = static_cast<GlfwWindow&>(window).native();
  return provider;
}

} // namespace Toon::viewport
