// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#include "core/vk_preinit.h"

#include <cstdlib>

#include "core/log.h"
#include "core/xr_context.h"

namespace lxr {

VulkanPreInit::VulkanPreInit() = default;
VulkanPreInit::~VulkanPreInit() { finish(); }

void VulkanPreInit::prepare(const std::string& runtimePreference) {
  if (prepared_) return;
  prepared_ = true;

  runtime_ = selectRuntime(runtimePreference);
  const std::string cachePath = defaultRequirementsCachePath();

  VulkanRequirements fromRuntime;
  if (!runtime_.manifestPath.empty() && isRuntimeReachable(runtime_)) {
    temp_ = std::make_unique<XrContext>();
    XrContextConfig cfg;
    cfg.appName = "LinuxXR PreInit";
    cfg.runtimePreference = runtime_.manifestPath;
    cfg.requestRefreshRate = false;
    if (temp_->createInstance(cfg) && temp_->queryVulkanRequirements(fromRuntime)) {
      runtimeQueried_ = true;
      saveRequirementsCache(cachePath, fromRuntime);
      LXR_INFO("PreInit: Vulkan requirements read from the runtime and cached");
    } else {
      temp_.reset();
    }
  } else if (!runtime_.manifestPath.empty()) {
    LXR_INFO("PreInit: %s - using cache/base list", runtimeReachabilityHint(runtime_).c_str());
  }

  VulkanRequirements base;
  if (runtimeQueried_) {
    base = fromRuntime;
  } else if (loadRequirementsCache(cachePath, runtime_.manifestPath, base)) {
    LXR_INFO("PreInit: Vulkan requirements from cache (%s)", cachePath.c_str());
  } else {
    base.source = "fallback";
    base.runtimeManifest = runtime_.manifestPath;
  }

  req_.source = base.source;
  req_.runtimeManifest = base.runtimeManifest;
  req_.instanceExtensions = mergeUnique(base.instanceExtensions, fallbackInstanceExtensions());
  req_.deviceExtensions = mergeUnique(base.deviceExtensions, fallbackDeviceExtensions());
}

std::string VulkanPreInit::instanceExtensionString() {
  if (!prepared_) prepare({});
  droppedInstance_.clear();
  auto filtered = filterSupported(req_.instanceExtensions, vkAvailableInstanceExtensions(), &droppedInstance_);
  if (!droppedInstance_.empty())
    LXR_DEBUG("PreInit: ignored unsupported instance extensions: %s", joinExtensions(droppedInstance_).c_str());
  std::string s = joinExtensions(filtered);
  LXR_INFO("PreInit: Vulkan instance extensions [%s]: %s", req_.source.c_str(), s.c_str());
  return s;
}

VkPhysicalDevice VulkanPreInit::choosePhysicalDevice(VkInstance instance) {
  if (!prepared_) prepare({});
  if (!loadVulkanInstanceFunctions(instance)) return VK_NULL_HANDLE;

  VkPhysicalDevice fromRuntime = VK_NULL_HANDLE;
  if (temp_ && temp_->hasInstance()) fromRuntime = temp_->vulkanPhysicalDeviceFor(instance);

  if (fromRuntime != VK_NULL_HANDLE) {
    chosen_ = fromRuntime;
    chosenInfo_ = vkDescribePhysicalDevice(chosen_);
    LXR_INFO("PreInit: GPU according to the runtime: %s", chosenInfo_.name.c_str());
    return chosen_;
  }

  auto devices = vkListPhysicalDevices(instance);
  int idx = vkChooseDefaultDevice(devices, std::getenv("LINUXXR_GPU"));
  if (idx < 0) {
    LXR_WARN("PreInit: no Vulkan GPU found");
    return VK_NULL_HANDLE;
  }
  chosen_ = devices[idx].handle;
  chosenInfo_ = devices[idx];
  LXR_INFO("PreInit: GPU selected: %s (%s)", chosenInfo_.name.c_str(), vkDeviceTypeName(chosenInfo_.type));
  return chosen_;
}

std::string VulkanPreInit::deviceExtensionString() {
  if (!prepared_) prepare({});
  std::vector<std::string> available;
  if (chosen_ != VK_NULL_HANDLE) {
    available = vkAvailableDeviceExtensions(chosen_);
  } else {
    // Unity has not (yet) asked for the adapter -> check with our own throwaway instance.
    if (loadVulkanLoader()) {
      VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
      VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
      ai.apiVersion = VK_API_VERSION_1_1;
      ci.pApplicationInfo = &ai;
      VkInstance tmp = VK_NULL_HANDLE;
      if (vk().vkCreateInstance(&ci, nullptr, &tmp) == VK_SUCCESS) {
        loadVulkanInstanceFunctions(tmp);
        auto devices = vkListPhysicalDevices(tmp);
        int idx = vkChooseDefaultDevice(devices, std::getenv("LINUXXR_GPU"));
        if (idx >= 0) available = vkAvailableDeviceExtensions(devices[idx].handle);
        vk().vkDestroyInstance(tmp, nullptr);
      }
    }
  }
  droppedDevice_.clear();
  auto filtered = available.empty() ? req_.deviceExtensions
                                    : filterSupported(req_.deviceExtensions, available, &droppedDevice_);
  if (!droppedDevice_.empty())
    LXR_INFO("PreInit: GPU does not support: %s", joinExtensions(droppedDevice_).c_str());
  std::string s = joinExtensions(filtered);
  LXR_INFO("PreInit: Vulkan device extensions: %s", s.c_str());
  return s;
}

void VulkanPreInit::finish() {
  if (temp_) {
    temp_->destroy();
    temp_.reset();
  }
}

}  // namespace lxr
