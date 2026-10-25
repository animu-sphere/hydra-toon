// SPDX-License-Identifier: Apache-2.0
// The Tessera host of GPU handoff policy §9: a Win32 window whose swapchain
// image shows an avatar drawn through the GPU handoff and Tessera UI drawn
// through tessera::vulkan, in one render pass. The host lends its device to
// the renderer, binds each handed-off slot to Tessera as a premultiplied
// image, waits for the handoff's timeline value at the fragment stage, and
// releases the slot with its own timeline value. A readback mode draws the
// same frames from ColorProducts uploaded to a host image. --smoke checks the
// presented frames against the CPU composite of their capture under the
// validation layer; --measure writes both paths' timings as JSON.
#include "camera.hpp"
#include "compare.hpp"
#include "lending_host.hpp"
#include "panel.hpp"
#include "stage_scene.hpp"

#include <pxr/usd/usd/stage.h>

#include <tessera/vulkan/renderer.hpp>

#include <toon/vulkan_handoff.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include <vulkan/vulkan_win32.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

using toon_lending::Require;

constexpr int kSkip = 77;
// The clear colour behind the avatar, linear RGB; the renderer's own
// background is transparent, so its product is the foreground alone.
constexpr Toon::Float3 kBackground{0.05F, 0.10F, 0.15F};

void Check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(operation) + " failed: VkResult " +
                             std::to_string(result));
  }
}

void Check(const std::vector<tessera::Diagnostic>& diagnostics, const char* operation) {
  if (diagnostics.empty()) return;
  for (const auto& d : diagnostics) {
    std::cerr << d.code << " at '" << d.path << "': " << d.message << '\n';
  }
  throw std::runtime_error(operation);
}

struct Arguments {
  std::string shaders;
  std::string tessera_shaders;
  std::string stage;
  std::uint32_t samples = 4;
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
  bool readback = false;
  std::string smoke;    // Output directory of a scripted smoke run.
  std::string measure;  // Output JSON of a scripted measurement run.
  std::uint32_t frames = 240;
  std::uint32_t warmup = 60;
};

Arguments Parse(int argc, char** argv) {
  Arguments arguments;
  for (int at = 1; at < argc; ++at) {
    const std::string argument = argv[at];
    const auto value = [&]() -> std::string {
      Require(at + 1 < argc, argument + " needs a value");
      return argv[++at];
    };
    const auto number = [&] { return static_cast<std::uint32_t>(std::stoul(value())); };
    if (argument == "--shaders") arguments.shaders = value();
    else if (argument == "--tessera-shaders") arguments.tessera_shaders = value();
    else if (argument == "--stage") arguments.stage = value();
    else if (argument == "--samples") arguments.samples = number();
    else if (argument == "--width") arguments.width = number();
    else if (argument == "--height") arguments.height = number();
    else if (argument == "--readback") arguments.readback = true;
    else if (argument == "--smoke") arguments.smoke = value();
    else if (argument == "--measure") arguments.measure = value();
    else if (argument == "--frames") arguments.frames = number();
    else if (argument == "--warmup") arguments.warmup = number();
    else Require(false, "unknown argument " + argument);
  }
  Require(!arguments.shaders.empty() && !arguments.tessera_shaders.empty() &&
              !arguments.stage.empty(),
      "usage: toon-tessera-host --shaders DIR --tessera-shaders DIR --stage FILE "
      "[--samples N] [--width W] [--height H] [--readback] [--smoke DIR | "
      "--measure FILE [--frames N] [--warmup N]]");
  Require(arguments.smoke.empty() || arguments.measure.empty(),
      "--smoke and --measure are separate runs");
  Require(arguments.width > 0 && arguments.height > 0 && arguments.frames > 0,
      "the extent and --frames must be non-zero");
  return arguments;
}

std::vector<std::uint32_t> ReadSpirv(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  Require(file.good(), "cannot open " + path.string());
  const auto bytes = static_cast<std::size_t>(file.tellg());
  Require(bytes > 0 && bytes % 4 == 0, "malformed SPIR-V " + path.string());
  std::vector<std::uint32_t> words(bytes / 4);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(bytes));
  return words;
}

// Largest float logical length whose ceil(length * scale) equals the
// physical extent, as Tessera computes it.
float LogicalLength(std::uint32_t pixels, float scale) {
  float value = static_cast<float>(static_cast<double>(pixels) / scale);
  while (std::ceil(static_cast<double>(value) * scale) > pixels) {
    value = std::nextafter(value, 0.0F);
  }
  while (std::ceil(static_cast<double>(value) * scale) < pixels) {
    value = std::nextafter(value, std::numeric_limits<float>::infinity());
  }
  return value;
}

struct Series {
  std::vector<double> values;

  // Nearest rank.
  [[nodiscard]] double Percentile(double rank) const {
    if (values.empty()) return 0;
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const auto at = static_cast<std::size_t>(
        std::ceil(rank / 100.0 * static_cast<double>(sorted.size())));
    return sorted[std::clamp<std::size_t>(at, 1, sorted.size()) - 1];
  }
  void Write(std::ostream& out, const char* name) const {
    double sum = 0;
    double max = 0;
    for (const double value : values) {
      sum += value;
      max = std::max(max, value);
    }
    out << '"' << name << "\": {\"p50\": " << Percentile(50)
        << ", \"p95\": " << Percentile(95) << ", \"p99\": " << Percentile(99)
        << ", \"mean\": " << (values.empty() ? 0 : sum / values.size())
        << ", \"max\": " << max << '}';
  }
};

std::string Escape(const std::string& text) {
  std::string escaped;
  for (const char c : text) {
    if (c == '"' || c == '\\') escaped += '\\';
    escaped += c;
  }
  return escaped;
}

double Milliseconds(std::int64_t from, std::int64_t to) {
  return static_cast<double>(to - from) * 1e-6;
}

class App;
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

// A host-owned image the readback path uploads each frame's product into.
struct Upload {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory staging_memory = VK_NULL_HANDLE;
  void* mapped = nullptr;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t handle = 0;
  bool initialized = false;
};

// A captured swapchain image, read back for the smoke.
struct Readback {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  void* mapped = nullptr;
  VkDeviceSize size = 0;
};

class App {
public:
  explicit App(const Arguments& arguments) : arguments_(arguments) {
    handoff_ = !arguments.readback;
  }

  // Separate from construction, so the destructor also cleans up after a
  // setup that failed half way.
  void Initialize() {
    CreateWindowAndDevice();
    LoadScene();
    CreateRenderers();
  }

  ~App() {
    if (window_) SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    if (host_ == nullptr) {
      if (window_) DestroyWindow(window_);
      return;
    }
    vkDeviceWaitIdle(device_);
    if (tessera_) {
      tessera_->retire(tessera_frame_);
      for (const auto& [key, handle] : slot_handles_) {
        (void)tessera_->unbind_image({handle.handle});
      }
      if (upload_.handle != 0) (void)tessera_->unbind_image({upload_.handle});
      tessera_.reset();
    }
    // The handoff renderer's teardown waits for release values not yet
    // reached, so it goes before the host's timeline semaphore.
    renderer_.reset();
    DestroyUpload();
    DestroyReadback();
    DestroySwapchain();
    if (pass_) vkDestroyRenderPass(device_, pass_, nullptr);
    if (sampler_) vkDestroySampler(device_, sampler_, nullptr);
    for (const VkSemaphore semaphore : finished_) vkDestroySemaphore(device_, semaphore, nullptr);
    if (acquired_) vkDestroySemaphore(device_, acquired_, nullptr);
    if (timeline_) vkDestroySemaphore(device_, timeline_, nullptr);
    if (fence_) vkDestroyFence(device_, fence_, nullptr);
    if (pool_) vkDestroyCommandPool(device_, pool_, nullptr);
    scene_.reset();
    if (surface_) vkDestroySurfaceKHR(host_->instance(), surface_, nullptr);
    host_.reset();
    if (window_) DestroyWindow(window_);
  }
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  int Run() {
    if (!arguments_.smoke.empty()) return Smoke();
    if (!arguments_.measure.empty()) return Measure();
    ShowWindow(window_, SW_SHOW);
    while (!closed_) {
      if (!Pump()) break;
      if (!minimized_) Frame();
    }
    return 0;
  }

  LRESULT Message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CLOSE:
      closed_ = true;
      return 0;
    case WM_SIZE:
      minimized_ = wparam == SIZE_MINIMIZED || LOWORD(lparam) == 0 || HIWORD(lparam) == 0;
      resized_ = true;
      return 0;
    case WM_DPICHANGED: {
      const auto* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(window_, nullptr, suggested->left, suggested->top,
          suggested->right - suggested->left, suggested->bottom - suggested->top,
          SWP_NOZORDER | SWP_NOACTIVATE);
      resized_ = true;
      return 0;
    }
    case WM_KEYDOWN:
      if (wparam == VK_SPACE) handoff_ = !handoff_;
      if (wparam == VK_ESCAPE) closed_ = true;
      return 0;
    case WM_LBUTTONUP: {
      // Client pixel centres, as Tessera's pointer mapping takes them.
      const float x = (static_cast<float>(GET_X_LPARAM(lparam)) + 0.5F) / scale_;
      const float y = (static_cast<float>(GET_Y_LPARAM(lparam)) + 0.5F) / scale_;
      if (panel_.HitsSwitch({x, y})) {
        handoff_ = !handoff_;
        ++switch_clicks_;
      }
      return 0;
    }
    case WM_RBUTTONDOWN:
      dragging_ = true;
      drag_ = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      SetCapture(window_);
      return 0;
    case WM_RBUTTONUP:
      dragging_ = false;
      ReleaseCapture();
      return 0;
    case WM_MOUSEMOVE:
      if (dragging_ && camera_) {
        const POINT now{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        camera_->Orbit(static_cast<float>(now.x - drag_.x), static_cast<float>(now.y - drag_.y));
        drag_ = now;
      }
      return 0;
    case WM_MOUSEWHEEL:
      if (camera_) camera_->Dolly(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA);
      return 0;
    default:
      return DefWindowProcW(window_, message, wparam, lparam);
    }
  }

private:
  struct SlotHandle {
    std::uint64_t handle = 0;
    std::uint64_t generation = 0;
  };

  // --- Setup ---------------------------------------------------------------

  void CreateWindowAndDevice() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc = WindowProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    type.lpszClassName = L"toon-tessera-host";
    RegisterClassW(&type);
    RECT rect{0, 0, static_cast<LONG>(arguments_.width), static_cast<LONG>(arguments_.height)};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    window_ = CreateWindowExW(0, type.lpszClassName, L"hydra-toon on Tessera", style,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr,
        instance, this);
    Require(window_ != nullptr, "CreateWindowExW failed");
    SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    // The client area, not the frame, has the requested extent.
    const UINT dpi = GetDpiForWindow(window_);
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
    SetWindowPos(window_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (!arguments_.smoke.empty() || !arguments_.measure.empty()) {
      ShowWindow(window_, SW_SHOWNOACTIVATE);
    }

    toon_lending::Presenting presenting;
    presenting.instance_extensions = {VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    presenting.device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    presenting.created = [this, instance](VkInstance vulkan) {
      VkWin32SurfaceCreateInfoKHR create{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
      create.hinstance = instance;
      create.hwnd = window_;
      Check(vkCreateWin32SurfaceKHR(vulkan, &create, nullptr, &surface_),
          "vkCreateWin32SurfaceKHR");
    };
    presenting.presents = [this](VkPhysicalDevice physical, std::uint32_t family) {
      VkBool32 supported = VK_FALSE;
      return vkGetPhysicalDeviceSurfaceSupportKHR(physical, family, surface_, &supported) ==
                 VK_SUCCESS &&
             supported == VK_TRUE;
    };
    // The smoke runs under validation and synchronization validation; the
    // measurement does not, so the layer's cost is not counted.
    host_ = std::make_unique<toon_lending::Host>(Toon::VulkanHandoffRequirements(),
        !arguments_.smoke.empty(), std::move(presenting));
    device_ = host_->device();
    physical_ = host_->physical_device();
    queue_ = host_->queue();

    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = host_->queue_family();
    Check(vkCreateCommandPool(device_, &pool, nullptr, &pool_), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = pool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    Check(vkAllocateCommandBuffers(device_, &allocate, &commands_), "vkAllocateCommandBuffers");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    Check(vkCreateFence(device_, &fence, nullptr, &fence_), "vkCreateFence");
    VkSemaphoreCreateInfo binary{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    Check(vkCreateSemaphore(device_, &binary, nullptr, &acquired_), "vkCreateSemaphore");
    VkSemaphoreTypeCreateInfo timeline_type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    timeline_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo timeline{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timeline_type};
    Check(vkCreateSemaphore(device_, &timeline, nullptr, &timeline_), "vkCreateSemaphore");
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    // The product is drawn at its own extent: nearest sampling at pixel
    // centres reads each texel exactly.
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Check(vkCreateSampler(device_, &sampler, nullptr, &sampler_), "vkCreateSampler");
    CreateSwapchain();
  }

  void LoadScene() {
    const auto stage = UsdStage::Open(arguments_.stage);
    Require(stage != nullptr, "cannot open " + arguments_.stage);
    scene_ = std::make_unique<toon_stage::Scene>(stage);
    const Toon::FrameSnapshot snapshot = scene_->Sync();
    camera_ = std::make_unique<Toon::viewport::OrbitCamera>(scene_->up);
    camera_->Frame(Toon::viewport::SceneBounds(snapshot));
    draws_ = Toon::ExtractDrawList(snapshot);
  }

  void CreateRenderers() {
    Toon::FrameStatus status = Toon::FrameStatus::Fail;
    std::string error;
    renderer_ = Toon::CreateHandoffRenderer(host_->Lend(),
        Toon::SceneShadersIn(arguments_.shaders), status, error,
        {arguments_.samples});
    Require(renderer_ != nullptr, error);
    Require(renderer_->SetBackground({Toon::BackgroundMode::Transparent, {}}, error), error);

    const std::filesystem::path directory = arguments_.tessera_shaders;
    tessera_vertex_ = ReadSpirv(directory / "primitive.vertex.spv");
    tessera_fragment_ = ReadSpirv(directory / "primitive.fragment.spv");
    tessera_image_ = ReadSpirv(directory / "primitive.image.spv");
    tessera_batch_ = ReadSpirv(directory / "primitive.batch.spv");
    tessera::VulkanContext context;
    context.physical_device = physical_;
    context.device = device_;
    context.render_pass = pass_;
    context.format = format_;
    context.vertex_spirv = tessera_vertex_;
    context.fragment_spirv = tessera_fragment_;
    context.image_spirv = tessera_image_;
    context.batch_vertex_spirv = tessera_batch_;
    context.max_images = 32;
    context.placeholder_text = true;
    tessera_ = std::make_unique<tessera::VulkanRenderer>(context);
  }

  void CreateSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities{};
    Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &capabilities),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    if (format_ == VK_FORMAT_UNDEFINED) {
      std::uint32_t count = 0;
      vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr);
      std::vector<VkSurfaceFormatKHR> formats(count);
      vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data());
      for (const VkFormat wanted : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB}) {
        for (const auto& format : formats) {
          if (format_ == VK_FORMAT_UNDEFINED && format.format == wanted &&
              format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format_ = wanted;
          }
        }
      }
      Require(format_ != VK_FORMAT_UNDEFINED, "the surface has no 8-bit sRGB format");
      CreateRenderPass();
    }
    Require((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0 ||
                arguments_.smoke.empty(),
        "the smoke reads the swapchain back, which needs transfer-source usage");
    extent_ = capabilities.currentExtent;
    if (extent_.width == UINT32_MAX) {
      RECT client{};
      GetClientRect(window_, &client);
      extent_ = {static_cast<std::uint32_t>(client.right), static_cast<std::uint32_t>(client.bottom)};
    }
    if (extent_.width == 0 || extent_.height == 0) return;
    VkSwapchainCreateInfoKHR create{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    create.surface = surface_;
    create.minImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0) {
      create.minImageCount = std::min(create.minImageCount, capabilities.maxImageCount);
    }
    create.imageFormat = format_;
    create.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    create.imageExtent = extent_;
    create.imageArrayLayers = 1;
    create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.preTransform = capabilities.currentTransform;
    create.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create.presentMode = PresentMode();
    create.clipped = VK_TRUE;
    Check(vkCreateSwapchainKHR(device_, &create, nullptr, &swapchain_), "vkCreateSwapchainKHR");
    std::uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data());
    for (const VkImage image : images_) {
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view.image = image;
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = format_;
      view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      VkImageView created = VK_NULL_HANDLE;
      Check(vkCreateImageView(device_, &view, nullptr, &created), "vkCreateImageView");
      views_.push_back(created);
      VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      framebuffer.renderPass = pass_;
      framebuffer.attachmentCount = 1;
      framebuffer.pAttachments = &created;
      framebuffer.width = extent_.width;
      framebuffer.height = extent_.height;
      framebuffer.layers = 1;
      VkFramebuffer made = VK_NULL_HANDLE;
      Check(vkCreateFramebuffer(device_, &framebuffer, nullptr, &made), "vkCreateFramebuffer");
      framebuffers_.push_back(made);
    }
    // One render-finished semaphore per swapchain image: presentation may
    // still hold an image's semaphore when the next frame acquires another.
    while (finished_.size() < images_.size()) {
      VkSemaphoreCreateInfo binary{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
      VkSemaphore semaphore = VK_NULL_HANDLE;
      Check(vkCreateSemaphore(device_, &binary, nullptr, &semaphore), "vkCreateSemaphore");
      finished_.push_back(semaphore);
    }
    const UINT dpi = GetDpiForWindow(window_);
    scale_ = static_cast<float>(dpi == 0 ? 96 : dpi) / 96.0F;
  }

  // FIFO, except in a measurement: there the frame loop is not paced by the
  // display, so frame time and GPU clocks follow the work.
  VkPresentModeKHR PresentMode() {
    if (arguments_.measure.empty()) return present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &count, modes.data());
    for (const VkPresentModeKHR wanted :
        {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR}) {
      if (std::find(modes.begin(), modes.end(), wanted) != modes.end()) {
        return present_mode_ = wanted;
      }
    }
    return present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
  }

  void DestroySwapchain() {
    for (const VkFramebuffer framebuffer : framebuffers_) {
      vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    for (const VkImageView view : views_) vkDestroyImageView(device_, view, nullptr);
    framebuffers_.clear();
    views_.clear();
    images_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }

  void RecreateSwapchain() {
    WaitFrame();
    Check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    DestroySwapchain();
    CreateSwapchain();
    resized_ = false;
  }

  void CreateRenderPass() {
    VkAttachmentDescription attachment{};
    attachment.format = format_;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    // In: the acquire semaphore's wait at colour output. Out: the smoke's
    // copy of the presented image.
    VkSubpassDependency dependencies[2]{};
    dependencies[0] = {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, 0};
    dependencies[1] = {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, 0};
    VkRenderPassCreateInfo create{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    create.attachmentCount = 1;
    create.pAttachments = &attachment;
    create.subpassCount = 1;
    create.pSubpasses = &subpass;
    create.dependencyCount = 2;
    create.pDependencies = dependencies;
    Check(vkCreateRenderPass(device_, &create, nullptr, &pass_), "vkCreateRenderPass");
  }

  [[nodiscard]] std::uint32_t MemoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory);
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
      if ((bits & (1U << i)) != 0 && (memory.memoryTypes[i].propertyFlags & flags) == flags) {
        return i;
      }
    }
    throw std::runtime_error("no suitable memory type");
  }

  void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags,
      VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped) {
    VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    create.size = size;
    create.usage = usage;
    Check(vkCreateBuffer(device_, &create, nullptr, &buffer), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, flags);
    Check(vkAllocateMemory(device_, &allocate, nullptr, &memory), "vkAllocateMemory");
    Check(vkBindBufferMemory(device_, buffer, memory, 0), "vkBindBufferMemory");
    Check(vkMapMemory(device_, memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
  }

  // --- The readback path's upload image -------------------------------------

  void DestroyUpload() {
    if (upload_.view) vkDestroyImageView(device_, upload_.view, nullptr);
    if (upload_.image) vkDestroyImage(device_, upload_.image, nullptr);
    if (upload_.memory) vkFreeMemory(device_, upload_.memory, nullptr);
    if (upload_.staging) vkDestroyBuffer(device_, upload_.staging, nullptr);
    if (upload_.staging_memory) vkFreeMemory(device_, upload_.staging_memory, nullptr);
    upload_ = {};
  }

  // After WaitFrame: the old image's last Tessera frame has been retired.
  void EnsureUpload(std::uint32_t width, std::uint32_t height) {
    if (upload_.image && upload_.width == width && upload_.height == height) return;
    if (upload_.handle != 0) Check(tessera_->unbind_image({upload_.handle}), "unbind upload");
    DestroyUpload();
    upload_.width = width;
    upload_.height = height;
    VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    create.imageType = VK_IMAGE_TYPE_2D;
    // The product's own format: linear values, as a handed-off slot.
    create.format = VK_FORMAT_R8G8B8A8_UNORM;
    create.extent = {width, height, 1};
    create.mipLevels = create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    Check(vkCreateImage(device_, &create, nullptr, &upload_.image), "vkCreateImage");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, upload_.image, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex =
        MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(device_, &allocate, nullptr, &upload_.memory), "vkAllocateMemory");
    Check(vkBindImageMemory(device_, upload_.image, upload_.memory, 0), "vkBindImageMemory");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = upload_.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = create.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    Check(vkCreateImageView(device_, &view, nullptr, &upload_.view), "vkCreateImageView");
    CreateBuffer(static_cast<VkDeviceSize>(width) * height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        upload_.staging, upload_.staging_memory, upload_.mapped);
    upload_.handle = next_handle_++;
    Check(tessera_->bind_image({upload_.handle}, upload_.view, sampler_,
              tessera::ImageAlpha::premultiplied),
        "bind upload");
  }

  void RecordUpload(const Toon::ColorProduct& color) {
    auto* destination = static_cast<std::uint8_t*>(upload_.mapped);
    for (std::uint32_t row = 0; row < color.height; ++row) {
      std::memcpy(destination + static_cast<std::size_t>(row) * color.width * 4,
          color.payload.data() + static_cast<std::size_t>(row) * color.row_pitch,
          static_cast<std::size_t>(color.width) * 4);
    }
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = upload_.initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = upload_.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                            : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = upload_.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commands_, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {color.width, color.height, 1};
    vkCmdCopyBufferToImage(commands_, upload_.staging, upload_.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(commands_, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    upload_.initialized = true;
  }

  // --- The smoke's swapchain readback ---------------------------------------

  void DestroyReadback() {
    if (readback_.buffer) vkDestroyBuffer(device_, readback_.buffer, nullptr);
    if (readback_.memory) vkFreeMemory(device_, readback_.memory, nullptr);
    readback_ = {};
  }

  void RecordCapture(VkImage image) {
    const VkDeviceSize size = static_cast<VkDeviceSize>(extent_.width) * extent_.height * 4;
    if (readback_.size != size) {
      DestroyReadback();
      CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
              VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
          readback_.buffer, readback_.memory, readback_.mapped);
      readback_.size = size;
    }
    // The render pass's dependency into transfer made its writes available
    // there; this barrier chains after it and only changes the layout.
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commands_, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {extent_.width, extent_.height, 1};
    vkCmdCopyImageToBuffer(commands_, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        readback_.buffer, 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(commands_, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
  }

  // --- Frames ---------------------------------------------------------------

  bool Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) return false;
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    return !closed_;
  }

  // Waits for the host's previous frame, retires it in Tessera, and unbinds
  // handles of older ring generations, whose slots the renderer may destroy
  // once their releases are reached.
  void WaitFrame() {
    if (!submitted_) return;
    Check(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    Check(vkResetFences(device_, 1, &fence_), "vkResetFences");
    submitted_ = false;
    tessera_->retire(tessera_frame_);
    for (auto it = slot_handles_.begin(); it != slot_handles_.end();) {
      if (it->second.generation != generation_) {
        Check(tessera_->unbind_image({it->second.handle}), "unbind old slot");
        it = slot_handles_.erase(it);
      } else {
        ++it;
      }
    }
  }

  std::uint64_t HandleFor(const Toon::HandoffImage& image) {
    const auto key = std::make_pair(image.generation, image.slot);
    auto found = slot_handles_.find(key);
    if (found != slot_handles_.end()) return found->second.handle;
    const std::uint64_t handle = next_handle_++;
    Check(tessera_->bind_image({handle}, reinterpret_cast<VkImageView>(image.view), sampler_,
              tessera::ImageAlpha::premultiplied),
        "bind handed-off slot");
    slot_handles_[key] = {handle, image.generation};
    return handle;
  }

  struct FrameResult {
    bool drawn = false;
    double frame_ms = 0;
    double render_ms = 0;
    Toon::ColorProduct product;  // The frame's product, when captured.
    std::vector<std::uint8_t> presented;
  };

  // One frame. With `capture`, the product of this frame and the presented
  // image are returned for comparison.
  FrameResult Frame(bool capture = false) {
    FrameResult result;
    const std::int64_t start = Toon::SteadyNanoseconds();
    if (resized_) RecreateSwapchain();
    if (swapchain_ == VK_NULL_HANDLE) return result;
    WaitFrame();
    std::uint32_t index = 0;
    const VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
        acquired_, VK_NULL_HANDLE, &index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
      resized_ = true;
      return result;
    }
    if (acquired != VK_SUBOPTIMAL_KHR) Check(acquired, "vkAcquireNextImageKHR");

    const std::uint32_t width = extent_.width;
    const std::uint32_t height = extent_.height;
    draws_.view = camera_->View(static_cast<float>(width) / static_cast<float>(height));
    std::string error;
    Toon::HandoffImage held;
    std::uint64_t handle = 0;
    const bool handoff = handoff_;
    const std::int64_t render_start = Toon::SteadyNanoseconds();
    if (handoff) {
      // A new extent starts a new ring generation, whose RenderHandoff may
      // destroy the old slots: their Tessera handles go first. WaitFrame
      // retired every frame that drew them.
      if (width != handoff_width_ || height != handoff_height_) {
        for (const auto& [key, slot] : slot_handles_) {
          Check(tessera_->unbind_image({slot.handle}), "unbind old slot");
        }
        slot_handles_.clear();
        handoff_width_ = width;
        handoff_height_ = height;
      }
      if (capture) renderer_->RequestCapture();
      Toon::HandoffImage image;
      const auto status = renderer_->RenderHandoff(draws_, width, height, image, error);
      Require(status == Toon::HandoffStatus::Rendered, "RenderHandoff: " + error);
      Require(renderer_->Acquire(held), "no handed-off frame to acquire");
      generation_ = held.generation;
      handle = HandleFor(held);
    } else {
      Require(renderer_->Render(draws_, width, height, color_, depth_, error), error);
      EnsureUpload(width, height);
      handle = upload_.handle;
    }
    result.render_ms = Milliseconds(render_start, Toon::SteadyNanoseconds());

    const tessera::Size logical{LogicalLength(width, scale_), LogicalLength(height, scale_)};
    const auto& statistics = renderer_->statistics();
    panel_state_.handoff = handoff;
    panel_state_.frame = statistics.latency.frame;
    panel_state_.width = width;
    panel_state_.height = height;
    panel_state_.samples = statistics.samples;
    panel_state_.generation = handoff ? generation_ : 0;
    panel_state_.readback_bytes = statistics.readback_bytes;
    if (statistics.gpu_timing) panel_state_.gpu_ms = statistics.gpu_frame_ms;
    panel_.Update(panel_state_, logical);

    Check(vkResetCommandBuffer(commands_, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(commands_, &begin), "vkBeginCommandBuffer");
    if (!handoff) RecordUpload(color_);
    VkClearValue clear{};
    clear.color = {{kBackground.x, kBackground.y, kBackground.z, 1.0F}};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = pass_;
    pass.framebuffer = framebuffers_[index];
    pass.renderArea = {{0, 0}, extent_};
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    vkCmdBeginRenderPass(commands_, &pass, VK_SUBPASS_CONTENTS_INLINE);
    tessera_->set_target({commands_, extent_});
    tessera::UiDrawList list;
    // The avatar first, as it is: a premultiplied image over the clear colour.
    list.commands.push_back(tessera::DrawImage{{{0, 0}, logical}, {handle}});
    const auto& ui = panel_.paint().commands;
    list.commands.insert(list.commands.end(), ui.begin(), ui.end());
    Check(tessera_->submit({tessera_frame_ + 1, logical, scale_}, list), "Tessera submit");
    ++tessera_frame_;
    vkCmdEndRenderPass(commands_);
    if (capture) RecordCapture(images_[index]);
    Check(vkEndCommandBuffer(commands_), "vkEndCommandBuffer");

    // Waits: the swapchain image at colour output, and on the handoff path
    // the frame's timeline value where Tessera samples it.
    std::vector<VkSemaphore> waits{acquired_};
    std::vector<VkPipelineStageFlags> stages{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    std::vector<std::uint64_t> wait_values{0};
    if (handoff) {
      waits.push_back(reinterpret_cast<VkSemaphore>(held.semaphore));
      stages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
      wait_values.push_back(held.value);
    }
    const VkSemaphore signals[] = {finished_[index], timeline_};
    const std::uint64_t signal_values[] = {0, ++timeline_value_};
    VkTimelineSemaphoreSubmitInfo values{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    values.waitSemaphoreValueCount = static_cast<std::uint32_t>(wait_values.size());
    values.pWaitSemaphoreValues = wait_values.data();
    values.signalSemaphoreValueCount = 2;
    values.pSignalSemaphoreValues = signal_values;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO, &values};
    submit.waitSemaphoreCount = static_cast<std::uint32_t>(waits.size());
    submit.pWaitSemaphores = waits.data();
    submit.pWaitDstStageMask = stages.data();
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commands_;
    submit.signalSemaphoreCount = 2;
    submit.pSignalSemaphores = signals;
    Check(vkQueueSubmit(queue_, 1, &submit, fence_), "vkQueueSubmit");
    submitted_ = true;
    if (handoff) {
      // The slot returns to the ring once this frame's read has executed.
      Require(renderer_->Release(held, reinterpret_cast<std::uintptr_t>(timeline_),
                  timeline_value_, error),
          "Release: " + error);
    }

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &finished_[index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &index;
    const VkResult presented = vkQueuePresentKHR(queue_, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
      resized_ = true;
    } else {
      Check(presented, "vkQueuePresentKHR");
    }

    if (capture) {
      WaitFrame();
      result.presented.assign(static_cast<const std::uint8_t*>(readback_.mapped),
          static_cast<const std::uint8_t*>(readback_.mapped) + readback_.size);
      if (handoff) {
        Require(renderer_->TakeCapture(result.product, error), "TakeCapture: " + error);
      } else {
        result.product = color_;
      }
    }
    result.drawn = true;
    result.frame_ms = Milliseconds(start, Toon::SteadyNanoseconds());
    panel_state_.frame_ms = result.frame_ms;
    panel_state_.render_ms = result.render_ms;
    return result;
  }

  // Frames until one is drawn, pumping messages between them.
  FrameResult Drawn(bool capture) {
    for (int attempt = 0; attempt < 64; ++attempt) {
      Require(Pump(), "the window closed during a scripted run");
      FrameResult result = Frame(capture);
      if (result.drawn) return result;
    }
    throw std::runtime_error("no frame could be drawn");
  }

  void ResizeClient(std::uint32_t width, std::uint32_t height) {
    RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow(window_));
    SetWindowPos(window_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    resized_ = true;
  }

  // --- Scripted runs --------------------------------------------------------

  int Smoke() {
    std::filesystem::create_directories(arguments_.smoke);
    std::ostringstream checks;
    bool passed = true;
    int number = 0;
    const auto compare = [&](const char* name) {
      // Frames in flight settle before the captured frame.
      for (int i = 0; i < 4; ++i) (void)Drawn(false);
      FrameResult frame = Drawn(true);
      const tessera::Rect bounds = panel_.bounds();
      // The panel's pixels, one wider on each side for its edge coverage.
      const auto pixel = [&](float logical, std::uint32_t limit, float offset) {
        const double value = std::floor(static_cast<double>(logical) * scale_ + offset);
        return static_cast<std::uint32_t>(std::clamp(value, 0.0, static_cast<double>(limit)));
      };
      toon_tessera::PixelRect excluded;
      excluded.x0 = pixel(bounds.origin.x, extent_.width, -1);
      excluded.y0 = pixel(bounds.origin.y, extent_.height, -1);
      excluded.x1 = pixel(bounds.origin.x + bounds.size.width, extent_.width, 2);
      excluded.y1 = pixel(bounds.origin.y + bounds.size.height, extent_.height, 2);
      const bool bgra = format_ == VK_FORMAT_B8G8R8A8_SRGB;
      const auto comparison = toon_tessera::CompareComposite(frame.presented, extent_.width,
          extent_.height, bgra, frame.product, kBackground, excluded);
      const std::string file = (std::filesystem::path(arguments_.smoke) /
          (std::to_string(++number) + "-" + name + ".ppm")).string();
      toon_tessera::WritePpm(file, frame.presented, extent_.width, extent_.height, bgra);
      // One 8-bit step: the GPU blends in float and rounds its sRGB encode.
      const bool ok = comparison.max_difference <= 1 && comparison.pixels > 0;
      passed = passed && ok;
      std::cout << name << ' ' << extent_.width << 'x' << extent_.height
                << (handoff_ ? " handoff" : " readback") << ": " << comparison.pixels
                << " pixels, " << comparison.exact << " exact, max difference "
                << comparison.max_difference << " at (" << comparison.x << ','
                << comparison.y << ") channel " << comparison.channel
                << (ok ? "" : "  FAIL") << '\n';
      checks << (number == 1 ? "" : ",\n") << "    {\"name\": \"" << name
             << "\", \"path\": \"" << (handoff_ ? "handoff" : "readback")
             << "\", \"width\": " << extent_.width << ", \"height\": " << extent_.height
             << ", \"generation\": " << generation_ << ", \"pixels\": " << comparison.pixels
             << ", \"exact\": " << comparison.exact
             << ", \"max_difference\": " << comparison.max_difference
             << ", \"excluded\": [" << excluded.x0 << ", " << excluded.y0 << ", "
             << excluded.x1 << ", " << excluded.y1 << "], \"image\": \""
             << Escape(file) << "\"}";
    };
    const auto click_switch = [&] {
      // Finds a client pixel on the panel's switch and sends a click there
      // through the window procedure, as input arrives.
      const auto clicks = switch_clicks_;
      for (std::uint32_t y = 0; y < extent_.height && switch_clicks_ == clicks; y += 2) {
        for (std::uint32_t x = 0; x < std::min<std::uint32_t>(extent_.width, 600) &&
                                  switch_clicks_ == clicks;
             x += 2) {
          const float lx = (static_cast<float>(x) + 0.5F) / scale_;
          const float ly = (static_cast<float>(y) + 0.5F) / scale_;
          if (panel_.HitsSwitch({lx, ly})) {
            SendMessageW(window_, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
          }
        }
      }
      Require(switch_clicks_ == clicks + 1, "the panel's switch did not take a click");
    };

    handoff_ = true;
    compare("handoff");
    ResizeClient(arguments_.width * 3 / 4, arguments_.height * 3 / 4);
    const std::uint64_t before = generation_;
    compare("handoff-resized");
    passed = passed && generation_ != before;
    click_switch();
    Require(!handoff_, "the switch did not select the readback path");
    compare("readback");
    click_switch();
    compare("handoff-again");

    const auto& handoff = renderer_->handoff_statistics();
    const std::uint32_t validation = host_->messages().count;
    passed = passed && validation == 0 && host_->validation();
    std::ofstream out(std::filesystem::path(arguments_.smoke) / "smoke.json");
    out << "{\n  \"tool\": \"toon-tessera-host\",\n  \"stage\": \"" << Escape(arguments_.stage)
        << "\",\n  \"device\": \"" << Escape(renderer_->statistics().device_name)
        << "\",\n  \"format\": " << format_ << ",\n  \"scale\": " << scale_
        << ",\n  \"validation\": " << (host_->validation() ? "true" : "false")
        << ",\n  \"validation_messages\": " << validation
        << ",\n  \"frames_handed_off\": " << handoff.frames_handed_off
        << ",\n  \"release_waits\": " << handoff.release_waits
        << ",\n  \"slots_created\": " << handoff.slots_created
        << ",\n  \"slots_destroyed\": " << handoff.slots_destroyed
        << ",\n  \"captures\": " << handoff.captures
        << ",\n  \"switch_clicks\": " << switch_clicks_
        << ",\n  \"checks\": [\n" << checks.str() << "\n  ],\n  \"passed\": "
        << (passed ? "true" : "false") << "\n}\n";
    std::cout << "validation messages: " << validation
              << (host_->validation() ? "" : " (validation layer absent)") << '\n';
    if (!host_->messages().first.empty()) std::cout << host_->messages().first << '\n';
    return passed ? 0 : 1;
  }

  int Measure() {
    std::ostringstream runs;
    runs << std::setprecision(6);
    bool first = true;
    for (const bool handoff : {false, true}) {
      handoff_ = handoff;
      Series frame;
      Series render;
      Series gpu;
      std::uint64_t previous = 0;
      std::uint64_t bytes = 0;
      for (std::uint32_t i = 0; i < arguments_.warmup + arguments_.frames; ++i) {
        const FrameResult result = Drawn(false);
        const auto& statistics = renderer_->statistics();
        // A handed-off frame's GPU time is read by the next call.
        const bool timed = statistics.gpu_timing &&
                           statistics.gpu_frame == (handoff ? previous : statistics.latency.frame);
        previous = statistics.latency.frame;
        if (i < arguments_.warmup) continue;
        frame.values.push_back(result.frame_ms);
        render.values.push_back(result.render_ms);
        if (timed) gpu.values.push_back(statistics.gpu_frame_ms);
        bytes = statistics.readback_bytes;
      }
      const auto& statistics = renderer_->statistics();
      runs << (first ? "" : ",\n") << "    {\"path\": \"" << (handoff ? "handoff" : "readback")
           << "\", \"width\": " << extent_.width << ", \"height\": " << extent_.height
           << ", \"samples\": " << statistics.samples << ", \"frames\": " << arguments_.frames
           << ", \"readback_bytes_per_frame\": " << bytes << ",\n      ";
      frame.Write(runs, "host_frame_ms");
      runs << ",\n      ";
      render.Write(runs, "render_call_ms");
      runs << ",\n      ";
      gpu.Write(runs, "gpu_frame_ms");
      runs << '}';
      first = false;
      std::cout << (handoff ? "handoff" : "readback") << ' ' << extent_.width << 'x'
                << extent_.height << ' ' << statistics.samples << "x: frame p50 "
                << frame.Percentile(50) << " ms, render p50 " << render.Percentile(50)
                << " ms, gpu p50 " << gpu.Percentile(50) << " ms, " << bytes << " bytes\n";
    }
    std::ofstream out(arguments_.measure);
    out << std::setprecision(6) << "{\n  \"tool\": \"toon-tessera-host\",\n"
        << "  \"stage\": \"" << Escape(arguments_.stage) << "\",\n"
        << "  \"device\": \"" << Escape(renderer_->statistics().device_name) << "\",\n"
        << "  \"driver_version\": \"" << Escape(renderer_->statistics().driver_version)
        << "\",\n  \"validation\": false,\n  \"present_mode\": \""
        << (present_mode_ == VK_PRESENT_MODE_IMMEDIATE_KHR ? "immediate"
               : present_mode_ == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : "fifo")
        << "\",\n"
        << "  \"warmup\": " << arguments_.warmup << ",\n"
        << "  \"runs\": [\n" << runs.str() << "\n  ]\n}\n";
    Require(out.good(), "cannot write " + arguments_.measure);
    return 0;
  }

  Arguments arguments_;
  HWND window_ = nullptr;
  std::unique_ptr<toon_lending::Host> host_;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  VkCommandBuffer commands_ = VK_NULL_HANDLE;
  VkFence fence_ = VK_NULL_HANDLE;
  VkSemaphore acquired_ = VK_NULL_HANDLE;
  VkSemaphore timeline_ = VK_NULL_HANDLE;
  std::uint64_t timeline_value_ = 0;
  std::vector<VkSemaphore> finished_;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkFormat format_ = VK_FORMAT_UNDEFINED;
  VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
  VkRenderPass pass_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkExtent2D extent_{};
  float scale_ = 1.0F;
  std::vector<VkImage> images_;
  std::vector<VkImageView> views_;
  std::vector<VkFramebuffer> framebuffers_;
  bool submitted_ = false;
  bool resized_ = false;
  bool minimized_ = false;
  bool closed_ = false;
  bool handoff_ = true;
  bool dragging_ = false;
  POINT drag_{};
  std::uint64_t switch_clicks_ = 0;

  std::unique_ptr<toon_stage::Scene> scene_;
  std::unique_ptr<Toon::viewport::OrbitCamera> camera_;
  Toon::DrawList draws_;
  std::unique_ptr<Toon::HandoffRenderer> renderer_;
  Toon::ColorProduct color_;
  Toon::DepthProduct depth_;
  std::uint64_t generation_ = 0;
  std::uint32_t handoff_width_ = 0;
  std::uint32_t handoff_height_ = 0;
  std::map<std::pair<std::uint64_t, std::uint32_t>, SlotHandle> slot_handles_;

  std::vector<std::uint32_t> tessera_vertex_;
  std::vector<std::uint32_t> tessera_fragment_;
  std::vector<std::uint32_t> tessera_image_;
  std::vector<std::uint32_t> tessera_batch_;
  std::unique_ptr<tessera::VulkanRenderer> tessera_;
  std::uint64_t tessera_frame_ = 0;
  std::uint64_t next_handle_ = 1;
  Upload upload_;
  Readback readback_;
  toon_tessera::Panel panel_;
  toon_tessera::PanelState panel_state_;
};

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (app == nullptr) return DefWindowProcW(window, message, wparam, lparam);
  return app->Message(message, wparam, lparam);
}

} // namespace

int main(int argc, char** argv) {
  try {
    const Arguments arguments = Parse(argc, argv);
    App app(arguments);
    app.Initialize();
    return app.Run();
  } catch (const toon_lending::Skip& skip) {
    std::cout << "SKIP: " << skip.what() << '\n';
    return kSkip;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
