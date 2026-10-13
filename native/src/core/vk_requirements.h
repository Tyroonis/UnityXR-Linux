// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#pragma once
// Vulkan extensions required by the OpenXR runtime.
//
// Problem: Unity creates VkInstance/VkDevice at Editor start (PreInit). WiVRn only serves apps
// while a headset is connected. Therefore: (1) ask the runtime if reachable, and cache the result;
// (2) otherwise use the cache; (3) always add a known base list (Monado/WiVRn); (4) filter everything
// against what is actually supported so that vkCreateInstance/vkCreateDevice never fails.

#include <cstdint>
#include <string>
#include <vector>

namespace lxr {

struct VulkanRequirements {
  std::vector<std::string> instanceExtensions;
  std::vector<std::string> deviceExtensions;
  std::string source;           // "runtime", "cache", "fallback"
  std::string runtimeManifest;  // which runtime the list belongs to
};

std::vector<std::string> splitExtensions(const std::string& spaceSeparated);
std::string joinExtensions(const std::vector<std::string>& list);
std::vector<std::string> mergeUnique(const std::vector<std::string>& a, const std::vector<std::string>& b);
// requested ∩ available (order of requested). dropped receives the missing ones.
std::vector<std::string> filterSupported(const std::vector<std::string>& requested,
                                         const std::vector<std::string>& available,
                                         std::vector<std::string>* dropped = nullptr);
bool containsExtension(const std::vector<std::string>& list, const std::string& name);

// Base lists that Monado/WiVRn use for swapchain import/synchronization
// (verified against Monado 21/25 via xrGetVulkanDeviceExtensionsKHR).
const std::vector<std::string>& fallbackInstanceExtensions();
const std::vector<std::string>& fallbackDeviceExtensions();

// Cache ($XDG_CACHE_HOME/linuxxr/vulkan_requirements.txt)
std::string defaultRequirementsCachePath();
bool loadRequirementsCache(const std::string& path, const std::string& runtimeManifest, VulkanRequirements& out);
bool saveRequirementsCache(const std::string& path, const VulkanRequirements& req);
// Text serialization (separate for tests)
std::string serializeRequirements(const VulkanRequirements& req);
bool parseRequirements(const std::string& text, VulkanRequirements& out);

// Two-phase output as expected by IUnityXRPreInit:
//  capacity == 0  -> *countOut = required size incl. '\0'
//  capacity >= n  -> fill buffer, *countOut = written size incl. '\0'
bool writeExtensionString(const std::string& value, uint32_t capacity, uint32_t* countOut, char* buffer);

}  // namespace lxr
