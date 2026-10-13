// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#pragma once
// Minimal Vulkan function table, loaded via dlopen("libvulkan.so.1").
// In the Unity process libvulkan is already loaded - dlopen returns the same instance,
// so the functions also work with Unity's VkInstance/VkDevice.

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lxr {

#define LXR_VK_GLOBAL_FUNCS(X)              \
  X(vkEnumerateInstanceExtensionProperties) \
  X(vkEnumerateInstanceLayerProperties)     \
  X(vkEnumerateInstanceVersion)             \
  X(vkCreateInstance)

#define LXR_VK_INSTANCE_FUNCS(X)                \
  X(vkDestroyInstance)                          \
  X(vkEnumeratePhysicalDevices)                 \
  X(vkGetPhysicalDeviceProperties)              \
  X(vkGetPhysicalDeviceProperties2)             \
  X(vkGetPhysicalDeviceQueueFamilyProperties)   \
  X(vkGetPhysicalDeviceMemoryProperties)        \
  X(vkGetPhysicalDeviceFormatProperties)        \
  X(vkEnumerateDeviceExtensionProperties)       \
  X(vkCreateDevice)                             \
  X(vkGetDeviceProcAddr)

#define LXR_VK_DEVICE_FUNCS(X)  \
  X(vkDestroyDevice)            \
  X(vkGetDeviceQueue)           \
  X(vkDeviceWaitIdle)           \
  X(vkQueueWaitIdle)            \
  X(vkQueueSubmit)              \
  X(vkCreateCommandPool)        \
  X(vkDestroyCommandPool)       \
  X(vkAllocateCommandBuffers)   \
  X(vkFreeCommandBuffers)       \
  X(vkResetCommandBuffer)       \
  X(vkBeginCommandBuffer)       \
  X(vkEndCommandBuffer)         \
  X(vkCmdPipelineBarrier)       \
  X(vkCmdClearColorImage)       \
  X(vkCmdCopyImage)             \
  X(vkCmdBlitImage)             \
  X(vkCreateFence)              \
  X(vkDestroyFence)             \
  X(vkWaitForFences)            \
  X(vkResetFences)

struct VkApi {
#define LXR_DECLARE(name) PFN_##name name = nullptr;
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
  LXR_VK_GLOBAL_FUNCS(LXR_DECLARE)
  LXR_VK_INSTANCE_FUNCS(LXR_DECLARE)
  LXR_VK_DEVICE_FUNCS(LXR_DECLARE)
#undef LXR_DECLARE
};

VkApi& vk();

// Loads libvulkan.so.1 and the global functions (idempotent).
bool loadVulkanLoader();
// Load instance functions for a (foreign or own) VkInstance.
bool loadVulkanInstanceFunctions(VkInstance instance);
// Load device functions.
bool loadVulkanDeviceFunctions(VkDevice device);

// --- Helper functions --------------------------------------------------------

std::vector<std::string> vkAvailableInstanceExtensions();
std::vector<std::string> vkAvailableDeviceExtensions(VkPhysicalDevice device);

struct PhysicalDeviceInfo {
  VkPhysicalDevice handle = VK_NULL_HANDLE;
  std::string name;
  VkPhysicalDeviceType type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
  uint32_t apiVersion = 0;
  uint32_t vendorId = 0;
  uint32_t deviceId = 0;
  std::array<uint8_t, VK_UUID_SIZE> uuid{};
  bool hasUuid = false;
};

std::vector<PhysicalDeviceInfo> vkListPhysicalDevices(VkInstance instance);
PhysicalDeviceInfo vkDescribePhysicalDevice(VkPhysicalDevice device);

// Picks a device: override by index/name (LINUXXR_GPU), otherwise discrete > integrated > virtual > CPU
// (CPU/llvmpipe only if nothing else is available).
// Returns the index into devices, or -1.
int vkChooseDefaultDevice(const std::vector<PhysicalDeviceInfo>& devices, const char* overrideSpec);

const char* vkDeviceTypeName(VkPhysicalDeviceType type);
std::string vkVersionString(uint32_t version);
const char* vkResultName(VkResult result);

}  // namespace lxr

#define LXR_VK_OK(expr) ::lxr::detail::checkVk((expr), #expr, __FILE__, __LINE__)
namespace lxr::detail {
bool checkVk(VkResult result, const char* expr, const char* file, int line);
}
