// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
// Shared OpenXR session for the display and input subsystems.
// Created when the first subsystem starts (Play), destroyed when the last one stops.

#include <chrono>

#include "IUnityGraphics.h"
#include "IUnityGraphicsVulkan.h"
#include "core/log.h"
#include "core/vk_api.h"
#include "core/vk_requirements.h"
#include "unity/provider_context.h"

namespace lxr::unity {
namespace {
void (*g_swapchainCleanup)() = nullptr;
}

void setSwapchainCleanup(void (*fn)()) { g_swapchainCleanup = fn; }

IUnityGraphicsVulkan* unityVulkan() {
  auto& c = context();
  if (!c.interfaces) return nullptr;
  auto* gfx = c.interfaces->Get<IUnityGraphics>();
  if (!gfx || gfx->GetRenderer() != kUnityGfxRendererVulkan) return nullptr;
  return c.interfaces->Get<IUnityGraphicsVulkan>();
}

// Does Unity's VkDevice have the extensions the OpenXR runtime needs?
// Vulkan has no query for the extensions a device was created with, but device-level functions of an
// extension that was not enabled resolve to nullptr via vkGetDeviceProcAddr. Extensions without
// functions (e.g. VK_KHR_dedicated_allocation) cannot be checked this way and are skipped.
// Unity's Editor device already enables everything WiVRn needs except the optional timeline semaphores.
static void checkUnityVulkanDevice(const UnityVulkanInstance& ui) {
  auto& c = context();
  struct Probe {
    const char* ext;
    const char* fn;
    bool optional;
  };
  static const Probe probes[] = {
      {"VK_KHR_external_memory_fd", "vkGetMemoryFdKHR", false},
      {"VK_KHR_external_semaphore_fd", "vkGetSemaphoreFdKHR", false},
      {"VK_KHR_external_fence_fd", "vkGetFenceFdKHR", false},
      {"VK_KHR_get_memory_requirements2", "vkGetImageMemoryRequirements2KHR", false},
      {"VK_KHR_timeline_semaphore", "vkGetSemaphoreCounterValueKHR", true},
  };
  VulkanRequirements req;
  bool haveReq = c.xr.queryVulkanRequirements(req);
  if (haveReq) saveRequirementsCache(defaultRequirementsCachePath(), req);
  auto supported = vkAvailableDeviceExtensions(ui.physicalDevice);
  std::string missingReq, missingOpt;
  for (const auto& p : probes) {
    bool wanted = p.optional || !haveReq || containsExtension(req.deviceExtensions, p.ext);
    if (!wanted || !containsExtension(supported, p.ext)) continue;  // GPU can't do it -> doesn't count
    if (vk().vkGetDeviceProcAddr && vk().vkGetDeviceProcAddr(ui.device, p.fn)) continue;
    std::string& list = p.optional ? missingOpt : missingReq;
    list += (list.empty() ? "" : " ") + std::string(p.ext);
  }
  {
    std::lock_guard<std::mutex> l(c.statusMutex);
    c.vkMissingRequired = missingReq;
    c.vkMissingOptional = missingOpt;
  }
  c.vkChecked = 1;
  if (!missingReq.empty()) {
    LXR_WARN("Unity's Vulkan device lacks extensions the runtime needs: %s - the image may stay black.",
             missingReq.c_str());
  } else {
    LXR_INFO("Vulkan: Unity's device has all required extensions%s%s", missingOpt.empty() ? "" : " (optional missing: ",
             missingOpt.empty() ? "" : (missingOpt + ")").c_str());
  }
}

static bool createSessionLocked() {
  auto& c = context();
  if (c.sessionReady) return true;

  IUnityGraphicsVulkan* vkIf = unityVulkan();
  if (!vkIf) {
    LXR_ERROR("Unity is not running with Vulkan. Start the Editor with Vulkan (status window -> 'Use Vulkan').");
    c.setError("Graphics API is not Vulkan");
    return false;
  }
  UnityVulkanInstance ui = vkIf->Instance();
  if (!ui.instance || !ui.device) {
    LXR_ERROR("Unity's Vulkan device is not available yet");
    c.setError("Unity's Vulkan device not available");
    return false;
  }
  loadVulkanInstanceFunctions(ui.instance);
  loadVulkanDeviceFunctions(ui.device);

  PluginSettings s;
  {
    std::lock_guard<std::mutex> l(c.settingsMutex);
    s = c.settings;
  }

  XrContextConfig cfg;
  cfg.appName = "Unity Editor (LinuxXR)";
  cfg.engineName = "Unity";
  cfg.runtimePreference = s.runtime;
  cfg.origin = s.origin;
  if (!c.xr.createInstance(cfg)) {
    c.setError("OpenXR instance creation failed - " + runtimeReachabilityHint(c.xr.runtime().manifestPath.empty()
                                                                               ? selectRuntime(s.runtime)
                                                                               : c.xr.runtime()));
    return false;
  }

  checkUnityVulkanDevice(ui);

  VulkanBinding b;
  b.instance = ui.instance;
  b.physicalDevice = ui.physicalDevice;
  b.device = ui.device;
  b.queueFamilyIndex = ui.queueFamilyIndex;
  b.queueIndex = 0;  // Unity's graphics queue; access only within AccessQueue (display_provider.cpp)
  if (!c.xr.createSession(b)) {
    c.setError("xrCreateSession failed");
    c.xr.destroy();
    return false;
  }
  c.xrInput.init(c.xr);
  {
    std::lock_guard<std::mutex> l(c.head.mutex);
    c.head.valid = false;
  }
  c.stats.reset();
  c.stats.sessionStartNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now().time_since_epoch()).count();
  c.sessionReady = true;
  c.setError("");
  return true;
}

bool acquireSession(int who) {
  auto& c = context();
  std::lock_guard<std::mutex> l(c.sessionMutex);
  if (who == 0) c.displayWantsSession = true;
  if (who == 1) c.inputWantsSession = true;
  return createSessionLocked();
}

void releaseSession(int who) {
  auto& c = context();
  std::lock_guard<std::mutex> l(c.sessionMutex);
  if (who == 0) c.displayWantsSession = false;
  if (who == 1) c.inputWantsSession = false;
  if (c.displayWantsSession || c.inputWantsSession) return;
  if (!c.sessionReady && !c.xr.hasInstance()) return;
  c.sessionReady = false;
  c.stats.sessionStartNs = 0;
  if (g_swapchainCleanup) g_swapchainCleanup();
  c.xrInput.shutdown();
  c.xr.destroy();
  LXR_INFO("OpenXR session ended");
}

}  // namespace lxr::unity
