// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
// IUnityXRPreInit provider: gives Unity the required instance/device extensions and the GPU used
// by the OpenXR runtime before Vulkan initialization.
// Only called if the C# loader's IXRLoaderPreInit.GetPreInitLibraryName returns "LinuxXR".

#include <cstdint>

#include "IUnityXRPreInit.h"
#include "core/log.h"
#include "core/vk_requirements.h"
#include "unity/provider_context.h"

namespace lxr::unity {
namespace {

bool UNITY_INTERFACE_API GetPreInitFlags(void*, uint64_t* flags) {
  // Request a dedicated graphics queue: OpenXR runtimes submit work on the queue they are given,
  // which can collide with Unity's render thread.
  *flags = kUnityXRPreInitFlagsRequestAdditionalVulkanGraphicsQueue;
  return true;
}

bool UNITY_INTERFACE_API GetGraphicsAdapterId(void*, UnityXRPreInitRenderer renderer, uint64_t rendererData,
                                              uint64_t* adapterId) {
  if (renderer != kUnityXRPreInitRendererVulkan) {
    LXR_WARN("Graphics API is not Vulkan (renderer=%d). LinuxXR requires Vulkan: "
             "Player Settings -> Linux -> Graphics APIs -> move Vulkan to the top, or start the Editor with -force-vulkan.",
             static_cast<int>(renderer));
    return false;
  }
  auto& c = context();
  VkInstance instance = reinterpret_cast<VkInstance>(rendererData);
  VkPhysicalDevice pd = c.preinit.choosePhysicalDevice(instance);
  if (pd == VK_NULL_HANDLE) return false;
  c.preinitGpu = c.preinit.chosenDeviceInfo().name;
  *adapterId = reinterpret_cast<uint64_t>(c.preinit.chosenDeviceStorage());
  return true;
}

bool UNITY_INTERFACE_API GetVulkanInstanceExtensions(void*, uint32_t capacity, uint32_t* countOut, char* names) {
  auto& c = context();
  if (capacity == 0 || c.preinitInstanceExts.empty()) {
    c.preinitInstanceExts = c.preinit.instanceExtensionString();
  }
  return writeExtensionString(c.preinitInstanceExts, capacity, countOut, names);
}

bool UNITY_INTERFACE_API GetVulkanDeviceExtensions(void*, uint32_t capacity, uint32_t* countOut, char* names) {
  auto& c = context();
  if (capacity == 0 || c.preinitDeviceExts.empty()) {
    c.preinitDeviceExts = c.preinit.deviceExtensionString();
  }
  bool ok = writeExtensionString(c.preinitDeviceExts, capacity, countOut, names);
  if (capacity != 0 && ok) {
    // Last PreInit step: release the temporary OpenXR instance.
    c.preinit.finish();
  }
  return ok;
}

}  // namespace

void runPreInit(IUnityInterfaces* interfaces) {
  auto& c = context();
  c.interfaces = interfaces;
  c.trace = interfaces->Get<IUnityXRTrace>();
  installUnityLogSink(c.trace);
  loadSettingsFile(c.settings);
  if (c.settings.verboseLog) setLogLevel(LogLevel::Debug);

  LXR_INFO("LinuxXR %s PreInit (runtime preference: %s)", LINUXXR_VERSION, c.settings.runtime.c_str());
  c.preinit.prepare(c.settings.runtime);
  c.preinitRan = true;
  c.preinitRuntimeQueried = c.preinit.runtimeWasQueried();
  c.preinitSource = c.preinit.requirements().source;

  auto* pre = interfaces->Get<IUnityXRPreInit>();
  if (!pre) {
    LXR_ERROR("IUnityXRPreInit not available");
    return;
  }
  static UnityXRPreInitProvider provider{};
  provider.userData = nullptr;
  provider.GetPreInitFlags = GetPreInitFlags;
  provider.GetGraphicsAdapterId = GetGraphicsAdapterId;
  provider.GetVulkanInstanceExtensions = GetVulkanInstanceExtensions;
  provider.GetVulkanDeviceExtensions = GetVulkanDeviceExtensions;
  pre->RegisterPreInitProvider(&provider);
  LXR_INFO("PreInit provider registered (source of the Vulkan list: %s)", c.preinitSource.c_str());
}

}  // namespace lxr::unity
