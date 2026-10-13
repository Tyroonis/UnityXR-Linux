// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#pragma once
// Logic behind IUnityXRPreInit - without Unity dependency so linuxxr-probe can test it.
//
// Flow in Unity (Editor start, before Vulkan initialization):
//   1. prepare()                       - ask the runtime (if reachable) / cache / base list
//   2. instanceExtensionString()       - Unity creates its VkInstance with this
//   3. choosePhysicalDevice(instance)  - Unity uses this device
//   4. deviceExtensionString()         - Unity creates its VkDevice with this
//   5. finish()                        - release the temporary OpenXR instance

#include <memory>
#include <string>
#include <vector>

#include "core/runtime_select.h"
#include "core/vk_api.h"
#include "core/vk_requirements.h"

namespace lxr {

class XrContext;

class VulkanPreInit {
 public:
  VulkanPreInit();
  ~VulkanPreInit();

  // runtimePreference: same as XrContextConfig::runtimePreference
  void prepare(const std::string& runtimePreference);

  std::string instanceExtensionString();
  VkPhysicalDevice choosePhysicalDevice(VkInstance instance);
  std::string deviceExtensionString();
  void finish();

  const VulkanRequirements& requirements() const { return req_; }
  const RuntimeCandidate& runtime() const { return runtime_; }
  bool runtimeWasQueried() const { return runtimeQueried_; }
  VkPhysicalDevice chosenDevice() const { return chosen_; }
  const PhysicalDeviceInfo& chosenDeviceInfo() const { return chosenInfo_; }
  const std::vector<std::string>& droppedInstanceExtensions() const { return droppedInstance_; }
  const std::vector<std::string>& droppedDeviceExtensions() const { return droppedDevice_; }

  // Stable address for IUnityXRPreInit::GetGraphicsAdapterId (adapterId = pointer to VkPhysicalDevice)
  VkPhysicalDevice* chosenDeviceStorage() { return &chosen_; }

 private:
  std::unique_ptr<XrContext> temp_;
  VulkanRequirements req_;
  RuntimeCandidate runtime_;
  bool prepared_ = false;
  bool runtimeQueried_ = false;
  VkPhysicalDevice chosen_ = VK_NULL_HANDLE;
  PhysicalDeviceInfo chosenInfo_;
  std::vector<std::string> droppedInstance_, droppedDevice_;
};

}  // namespace lxr
