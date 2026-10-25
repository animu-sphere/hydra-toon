// SPDX-License-Identifier: Apache-2.0
// A host that lends its Vulkan device (GPU handoff policy §2): it creates its
// instance and device with Vulkan itself, from the published requirements,
// for the lending test host, the measurement host and the Tessera host.
// Test and example code only.
#pragma once

#include <toon/vulkan_handoff.hpp>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toon_lending {

inline void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

// This environment cannot lend a device: no 1.3 loader or device.
struct Skip : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Validation {
  std::uint32_t count = 0;
  std::string first;
};

// Errors, and validation or performance warnings, as the renderer's own
// instance counts them.
inline VKAPI_ATTR VkBool32 VKAPI_CALL OnMessage(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
  const bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
  const bool warning =
      (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0 &&
      (type & (VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                  VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)) != 0;
  if (error || warning) {
    auto* validation = static_cast<Validation*>(user);
    if (data != nullptr && data->pMessage != nullptr) {
      if (validation->count == 0) validation->first = data->pMessage;
      // The first few in full, for diagnosis.
      if (validation->count < 4) std::cerr << data->pMessage << "\n\n";
    }
    ++validation->count;
  }
  return VK_FALSE;
}

inline bool HasLayer(std::string_view name) {
  std::uint32_t count = 0;
  vkEnumerateInstanceLayerProperties(&count, nullptr);
  std::vector<VkLayerProperties> layers(count);
  vkEnumerateInstanceLayerProperties(&count, layers.data());
  for (const auto& layer : layers) {
    if (name == layer.layerName) return true;
  }
  return false;
}

// What a presenting host adds to the device it lends: the instance and
// device extensions for its surface and swapchain, a call made once the
// instance exists (to create the surface), and which queue families can
// present to it. The renderer enables none of these itself.
struct Presenting {
  std::vector<const char*> instance_extensions;
  std::vector<const char*> device_extensions;
  std::function<void(VkInstance)> created;
  std::function<bool(VkPhysicalDevice, std::uint32_t)> presents;
};

// The lending host's own Vulkan objects, destroyed after every renderer.
class Host {
public:
  // With `validate`, the Khronos validation layer and synchronization
  // validation, when installed, report into messages().
  Host(const Toon::VulkanDeviceRequirements& requirements, bool validate,
      Presenting presenting = {})
      : presenting_(std::move(presenting)) {
    CreateInstance(requirements, validate);
    if (presenting_.created) presenting_.created(instance_);
    ChooseDevice(requirements);
    CreateDevice(requirements);
  }
  ~Host() {
    if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
    if (messenger_ != VK_NULL_HANDLE) {
      const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
      destroy(instance_, messenger_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
  }
  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;

  [[nodiscard]] Toon::LentVulkanDevice Lend() const {
    Toon::LentVulkanDevice lent;
    lent.instance = reinterpret_cast<std::uintptr_t>(instance_);
    lent.physical_device = reinterpret_cast<std::uintptr_t>(physical_);
    lent.device = reinterpret_cast<std::uintptr_t>(device_);
    lent.queue = reinterpret_cast<std::uintptr_t>(queue_);
    lent.queue_family = family_;
    lent.api_version = api_version_;
    lent.enabled = enabled_;
    return lent;
  }
  [[nodiscard]] bool validation() const { return messenger_ != VK_NULL_HANDLE; }
  [[nodiscard]] VkInstance instance() const { return instance_; }
  [[nodiscard]] const Validation& messages() const { return messages_; }
  [[nodiscard]] std::uint32_t family_count() const { return family_count_; }
  [[nodiscard]] VkPhysicalDevice physical_device() const { return physical_; }
  [[nodiscard]] VkDevice device() const { return device_; }
  [[nodiscard]] VkQueue queue() const { return queue_; }
  [[nodiscard]] std::uint32_t queue_family() const { return family_; }
  // Destroys the device: the validation layer reports any object a renderer
  // left on it.
  void DestroyDevice() {
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }

private:
  void CreateInstance(const Toon::VulkanDeviceRequirements& requirements,
      bool want_validation) {
    std::uint32_t loader = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&loader) != VK_SUCCESS ||
        loader < requirements.api_version) {
      throw Skip("the Vulkan loader is below the required version");
    }
    api_version_ = requirements.api_version;
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "toon-lending-host";
    application.apiVersion = api_version_;
    const char* layer = "VK_LAYER_KHRONOS_validation";
    const char* extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    const bool validate = want_validation && HasLayer(layer);
    // Synchronization validation too: a lent device shares its queue and
    // timeline with the host (GPU handoff policy §9).
    const VkValidationFeatureEnableEXT enables[] = {
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};
    VkValidationFeaturesEXT features{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    features.enabledValidationFeatureCount = 1;
    features.pEnabledValidationFeatures = enables;
    VkDebugUtilsMessengerCreateInfoEXT messenger{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messenger.pfnUserCallback = OnMessage;
    messenger.pUserData = &messages_;
    messenger.pNext = &features;
    std::vector<const char*> extensions = presenting_.instance_extensions;
    if (validate) extensions.push_back(extension);
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &application;
    create.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    create.ppEnabledExtensionNames = extensions.data();
    if (validate) {
      create.enabledLayerCount = 1;
      create.ppEnabledLayerNames = &layer;
      create.pNext = &messenger;
    }
    if (vkCreateInstance(&create, nullptr, &instance_) != VK_SUCCESS) {
      throw Skip("vkCreateInstance failed");
    }
    if (validate) {
      messenger.pNext = nullptr;
      const auto create_messenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
      Require(create_messenger != nullptr &&
                  create_messenger(instance_, &messenger, nullptr, &messenger_) ==
                      VK_SUCCESS,
          "the debug messenger could not be created");
    }
  }

  // The first device with a graphics queue and the required features, as
  // the renderer's own device choice takes the first.
  void ChooseDevice(const Toon::VulkanDeviceRequirements& requirements) {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    for (VkPhysicalDevice device : devices) {
      VkPhysicalDeviceProperties properties{};
      vkGetPhysicalDeviceProperties(device, &properties);
      VkPhysicalDeviceVulkan13Features vulkan13{
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
      VkPhysicalDeviceVulkan12Features vulkan12{
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &vulkan13};
      VkPhysicalDeviceFeatures2 core{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
          &vulkan12};
      vkGetPhysicalDeviceFeatures2(device, &core);
      if (properties.apiVersion < requirements.api_version ||
          !vulkan12.timelineSemaphore || !vulkan13.dynamicRendering ||
          !vulkan13.synchronization2 ||
          !core.features.shaderSampledImageArrayDynamicIndexing) {
        continue;
      }
      vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count_, nullptr);
      std::vector<VkQueueFamilyProperties> families(family_count_);
      vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count_,
          families.data());
      for (std::uint32_t family = 0; family < family_count_; ++family) {
        if ((families[family].queueFlags & requirements.queue_flags) ==
                requirements.queue_flags &&
            (!presenting_.presents || presenting_.presents(device, family))) {
          physical_ = device;
          family_ = family;
          enabled_.alpha_to_one = core.features.alphaToOne == VK_TRUE;
          return;
        }
      }
    }
    throw Skip("no Vulkan device meets the handoff requirements");
  }

  void CreateDevice(const Toon::VulkanDeviceRequirements& requirements) {
    const float priority = 1.0F;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = family_;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features vulkan13{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    vulkan13.dynamicRendering = requirements.required.dynamic_rendering;
    vulkan13.synchronization2 = requirements.required.synchronization2;
    VkPhysicalDeviceVulkan12Features vulkan12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &vulkan13};
    vulkan12.timelineSemaphore = requirements.required.timeline_semaphore;
    VkPhysicalDeviceFeatures2 core{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        &vulkan12};
    core.features.shaderSampledImageArrayDynamicIndexing =
        requirements.required.shader_sampled_image_array_dynamic_indexing;
    core.features.alphaToOne =
        requirements.optional.alpha_to_one && enabled_.alpha_to_one;
    VkDeviceCreateInfo create{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    create.pNext = &core;
    create.queueCreateInfoCount = 1;
    create.pQueueCreateInfos = &queue;
    Require(requirements.device_extensions.empty(),
        "the renderer asks for device extensions this host does not enable");
    create.enabledExtensionCount =
        static_cast<std::uint32_t>(presenting_.device_extensions.size());
    create.ppEnabledExtensionNames = presenting_.device_extensions.data();
    Require(vkCreateDevice(physical_, &create, nullptr, &device_) == VK_SUCCESS,
        "vkCreateDevice failed");
    vkGetDeviceQueue(device_, family_, 0, &queue_);
    enabled_.timeline_semaphore = vulkan12.timelineSemaphore == VK_TRUE;
    enabled_.dynamic_rendering = vulkan13.dynamicRendering == VK_TRUE;
    enabled_.synchronization2 = vulkan13.synchronization2 == VK_TRUE;
    enabled_.shader_sampled_image_array_dynamic_indexing =
        core.features.shaderSampledImageArrayDynamicIndexing == VK_TRUE;
    enabled_.alpha_to_one = core.features.alphaToOne == VK_TRUE;
  }

  VkInstance instance_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  std::uint32_t family_ = 0;
  std::uint32_t family_count_ = 0;
  std::uint32_t api_version_ = 0;
  Toon::VulkanDeviceFeatures enabled_;
  Validation messages_;
  Presenting presenting_;
};

} // namespace toon_lending
