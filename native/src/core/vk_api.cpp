// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#include "core/vk_api.h"

#include <dlfcn.h>

#include <cstdlib>
#include <cstring>
#include <mutex>

#include "core/log.h"

namespace lxr {
namespace {
VkApi g_vk;
void* g_vkHandle = nullptr;
std::mutex g_vkMutex;
}  // namespace

VkApi& vk() { return g_vk; }

bool loadVulkanLoader() {
  std::lock_guard<std::mutex> lock(g_vkMutex);
  if (g_vk.vkGetInstanceProcAddr) return true;
  g_vkHandle = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!g_vkHandle) g_vkHandle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
  if (!g_vkHandle) {
    LXR_ERROR("libvulkan.so.1 not found: %s", dlerror());
    return false;
  }
  g_vk.vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(g_vkHandle, "vkGetInstanceProcAddr"));
  if (!g_vk.vkGetInstanceProcAddr) {
    LXR_ERROR("vkGetInstanceProcAddr missing");
    return false;
  }
#define LXR_LOAD(name) g_vk.name = reinterpret_cast<PFN_##name>(g_vk.vkGetInstanceProcAddr(VK_NULL_HANDLE, #name));
  LXR_VK_GLOBAL_FUNCS(LXR_LOAD)
#undef LXR_LOAD
  return g_vk.vkEnumerateInstanceExtensionProperties && g_vk.vkCreateInstance;
}

bool loadVulkanInstanceFunctions(VkInstance instance) {
  if (!loadVulkanLoader()) return false;
#define LXR_LOAD(name) g_vk.name = reinterpret_cast<PFN_##name>(g_vk.vkGetInstanceProcAddr(instance, #name));
  LXR_VK_INSTANCE_FUNCS(LXR_LOAD)
#undef LXR_LOAD
  if (!g_vk.vkGetPhysicalDeviceProperties2) {
    g_vk.vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
        g_vk.vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2KHR"));
  }
  return g_vk.vkEnumeratePhysicalDevices && g_vk.vkGetPhysicalDeviceProperties && g_vk.vkGetDeviceProcAddr;
}

bool loadVulkanDeviceFunctions(VkDevice device) {
  if (!g_vk.vkGetDeviceProcAddr) return false;
  bool ok = true;
#define LXR_LOAD(name)                                                                \
  g_vk.name = reinterpret_cast<PFN_##name>(g_vk.vkGetDeviceProcAddr(device, #name));  \
  if (!g_vk.name) {                                                                   \
    LXR_WARN("Vulkan device function missing: %s", #name);                            \
    ok = false;                                                                       \
  }
  LXR_VK_DEVICE_FUNCS(LXR_LOAD)
#undef LXR_LOAD
  return ok;
}

std::vector<std::string> vkAvailableInstanceExtensions() {
  std::vector<std::string> out;
  if (!loadVulkanLoader()) return out;
  uint32_t count = 0;
  g_vk.vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> props(count);
  g_vk.vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data());
  for (auto& p : props) out.emplace_back(p.extensionName);
  return out;
}

std::vector<std::string> vkAvailableDeviceExtensions(VkPhysicalDevice device) {
  std::vector<std::string> out;
  if (!g_vk.vkEnumerateDeviceExtensionProperties || device == VK_NULL_HANDLE) return out;
  uint32_t count = 0;
  g_vk.vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> props(count);
  g_vk.vkEnumerateDeviceExtensionProperties(device, nullptr, &count, props.data());
  for (auto& p : props) out.emplace_back(p.extensionName);
  return out;
}

PhysicalDeviceInfo vkDescribePhysicalDevice(VkPhysicalDevice device) {
  PhysicalDeviceInfo info;
  info.handle = device;
  if (!g_vk.vkGetPhysicalDeviceProperties || device == VK_NULL_HANDLE) return info;
  VkPhysicalDeviceProperties props{};
  g_vk.vkGetPhysicalDeviceProperties(device, &props);
  info.name = props.deviceName;
  info.type = props.deviceType;
  info.apiVersion = props.apiVersion;
  info.vendorId = props.vendorID;
  info.deviceId = props.deviceID;
  if (g_vk.vkGetPhysicalDeviceProperties2 && props.apiVersion >= VK_API_VERSION_1_1) {
    VkPhysicalDeviceIDProperties idProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props2.pNext = &idProps;
    g_vk.vkGetPhysicalDeviceProperties2(device, &props2);
    std::memcpy(info.uuid.data(), idProps.deviceUUID, VK_UUID_SIZE);
    info.hasUuid = true;
  }
  return info;
}

std::vector<PhysicalDeviceInfo> vkListPhysicalDevices(VkInstance instance) {
  std::vector<PhysicalDeviceInfo> out;
  if (!g_vk.vkEnumeratePhysicalDevices) return out;
  uint32_t count = 0;
  if (g_vk.vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS) return out;
  std::vector<VkPhysicalDevice> devices(count);
  g_vk.vkEnumeratePhysicalDevices(instance, &count, devices.data());
  for (auto d : devices) out.push_back(vkDescribePhysicalDevice(d));
  return out;
}

int vkChooseDefaultDevice(const std::vector<PhysicalDeviceInfo>& devices, const char* overrideSpec) {
  if (devices.empty()) return -1;
  if (overrideSpec && *overrideSpec) {
    char* end = nullptr;
    long idx = std::strtol(overrideSpec, &end, 10);
    if (end && *end == '\0' && idx >= 0 && idx < static_cast<long>(devices.size())) return static_cast<int>(idx);
    for (size_t i = 0; i < devices.size(); ++i) {
      if (devices[i].name.find(overrideSpec) != std::string::npos) return static_cast<int>(i);
    }
    LXR_WARN("GPU override '%s' matches no device - using default selection", overrideSpec);
  }
  auto rank = [](VkPhysicalDeviceType t) {
    switch (t) {
      case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 4;
      case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3;
      case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2;
      case VK_PHYSICAL_DEVICE_TYPE_OTHER: return 1;
      default: return 0;  // CPU (llvmpipe)
    }
  };
  int best = -1, bestRank = -1;
  for (size_t i = 0; i < devices.size(); ++i) {
    int r = rank(devices[i].type);
    if (r > bestRank) {
      best = static_cast<int>(i);
      bestRank = r;
    }
  }
  return best;
}

const char* vkDeviceTypeName(VkPhysicalDeviceType type) {
  switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
    default: return "other";
  }
}

std::string vkVersionString(uint32_t v) {
  return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." +
         std::to_string(VK_API_VERSION_PATCH(v));
}

const char* vkResultName(VkResult r) {
  switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    default: return "VK_ERROR_?";
  }
}

namespace detail {
bool checkVk(VkResult result, const char* expr, const char* file, int line) {
  if (result == VK_SUCCESS) return true;
  const char* base = std::strrchr(file, '/');
  LXR_ERROR("%s failed: %s (%d) (%s:%d)", expr, vkResultName(result), static_cast<int>(result),
            base ? base + 1 : file, line);
  return false;
}
}  // namespace detail

}  // namespace lxr
