// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#pragma once
// Own Vulkan instance/device for linuxxr-probe (simulates what Unity does in the Editor).

#include <string>
#include <vector>

#include "core/vk_api.h"

namespace probe {

struct ProbeVulkan {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  VkQueue queue = VK_NULL_HANDLE;
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  std::string deviceName;

  // instanceExts/deviceExts: space-separated (as delivered by PreInit)
  bool createInstance(const std::string& instanceExts, uint32_t apiVersion);
  // pick: preselected device (from OpenXR) or VK_NULL_HANDLE -> default selection
  bool createDevice(VkPhysicalDevice pick, const std::string& deviceExts);
  void destroy();

  // Clears each array layer of a swapchain image to a color
  // (layout: COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_DST -> COLOR_ATTACHMENT_OPTIMAL).
  bool clearImageLayers(VkImage image, const float colors[][4], uint32_t layerCount);
};

}  // namespace probe
