// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#include "probe_vulkan.h"

#include <cstdlib>

#include "core/log.h"
#include "core/vk_requirements.h"

using namespace lxr;

namespace probe {

bool ProbeVulkan::createInstance(const std::string& instanceExts, uint32_t apiVersion) {
  if (!loadVulkanLoader()) return false;
  auto exts = splitExtensions(instanceExts);
  std::vector<const char*> ptrs;
  for (auto& e : exts) ptrs.push_back(e.c_str());

  VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  ai.pApplicationName = "linuxxr-probe";
  ai.pEngineName = "LinuxXR";
  ai.apiVersion = apiVersion;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.pApplicationInfo = &ai;
  ci.enabledExtensionCount = static_cast<uint32_t>(ptrs.size());
  ci.ppEnabledExtensionNames = ptrs.data();
  if (!LXR_VK_OK(vk().vkCreateInstance(&ci, nullptr, &instance))) return false;
  return loadVulkanInstanceFunctions(instance);
}

bool ProbeVulkan::createDevice(VkPhysicalDevice pick, const std::string& deviceExts) {
  if (pick == VK_NULL_HANDLE) {
    auto devices = vkListPhysicalDevices(instance);
    int idx = vkChooseDefaultDevice(devices, std::getenv("LINUXXR_GPU"));
    if (idx < 0) return false;
    pick = devices[idx].handle;
  }
  physicalDevice = pick;
  deviceName = vkDescribePhysicalDevice(pick).name;

  uint32_t qfCount = 0;
  vk().vkGetPhysicalDeviceQueueFamilyProperties(pick, &qfCount, nullptr);
  std::vector<VkQueueFamilyProperties> qf(qfCount);
  vk().vkGetPhysicalDeviceQueueFamilyProperties(pick, &qfCount, qf.data());
  bool found = false;
  for (uint32_t i = 0; i < qfCount; ++i) {
    if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      queueFamily = i;
      found = true;
      break;
    }
  }
  if (!found) {
    LXR_ERROR("No graphics queue");
    return false;
  }

  auto requested = splitExtensions(deviceExts);
  std::vector<std::string> dropped;
  auto exts = filterSupported(requested, vkAvailableDeviceExtensions(pick), &dropped);
  if (!dropped.empty()) LXR_WARN("Device does not support: %s", joinExtensions(dropped).c_str());
  std::vector<const char*> ptrs;
  for (auto& e : exts) ptrs.push_back(e.c_str());

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = queueFamily;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;

  // Monado/WiVRn use timeline semaphores if available.
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
  timeline.timelineSemaphore = containsExtension(exts, "VK_KHR_timeline_semaphore") ? VK_TRUE : VK_FALSE;

  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.pNext = timeline.timelineSemaphore ? &timeline : nullptr;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = static_cast<uint32_t>(ptrs.size());
  dci.ppEnabledExtensionNames = ptrs.data();
  if (!LXR_VK_OK(vk().vkCreateDevice(pick, &dci, nullptr, &device))) return false;
  if (!loadVulkanDeviceFunctions(device)) return false;
  vk().vkGetDeviceQueue(device, queueFamily, 0, &queue);

  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pci.queueFamilyIndex = queueFamily;
  if (!LXR_VK_OK(vk().vkCreateCommandPool(device, &pci, nullptr, &pool))) return false;
  VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cai.commandPool = pool;
  cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cai.commandBufferCount = 1;
  if (!LXR_VK_OK(vk().vkAllocateCommandBuffers(device, &cai, &cmd))) return false;
  VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  return LXR_VK_OK(vk().vkCreateFence(device, &fci, nullptr, &fence));
}

bool ProbeVulkan::clearImageLayers(VkImage image, const float colors[][4], uint32_t layerCount) {
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vk().vkResetCommandBuffer(cmd, 0);
  vk().vkBeginCommandBuffer(cmd, &bi);

  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layerCount};
  b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vk().vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                            nullptr, 0, nullptr, 1, &b);
  for (uint32_t l = 0; l < layerCount; ++l) {
    VkClearColorValue c{};
    for (int i = 0; i < 4; ++i) c.float32[i] = colors[l][i];
    VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, l, 1};
    vk().vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &r);
  }
  b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  vk().vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                            nullptr, 0, nullptr, 1, &b);
  vk().vkEndCommandBuffer(cmd);

  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  if (!LXR_VK_OK(vk().vkQueueSubmit(queue, 1, &si, fence))) return false;
  vk().vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
  vk().vkResetFences(device, 1, &fence);
  return true;
}

void ProbeVulkan::destroy() {
  if (device) {
    vk().vkDeviceWaitIdle(device);
    if (fence) vk().vkDestroyFence(device, fence, nullptr);
    if (pool) vk().vkDestroyCommandPool(device, pool, nullptr);
    vk().vkDestroyDevice(device, nullptr);
  }
  if (instance) vk().vkDestroyInstance(instance, nullptr);
  *this = ProbeVulkan{};
}

}  // namespace probe
