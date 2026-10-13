// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#include "core/vk_requirements.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "core/log.h"

namespace lxr {

std::vector<std::string> splitExtensions(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream in(s);
  std::string tok;
  while (in >> tok) {
    if (!containsExtension(out, tok)) out.push_back(tok);
  }
  return out;
}

std::string joinExtensions(const std::vector<std::string>& list) {
  std::string out;
  for (const auto& e : list) {
    if (!out.empty()) out += ' ';
    out += e;
  }
  return out;
}

bool containsExtension(const std::vector<std::string>& list, const std::string& name) {
  for (const auto& e : list)
    if (e == name) return true;
  return false;
}

std::vector<std::string> mergeUnique(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  std::vector<std::string> out;
  for (const auto& e : a)
    if (!containsExtension(out, e)) out.push_back(e);
  for (const auto& e : b)
    if (!containsExtension(out, e)) out.push_back(e);
  return out;
}

std::vector<std::string> filterSupported(const std::vector<std::string>& requested,
                                         const std::vector<std::string>& available, std::vector<std::string>* dropped) {
  std::vector<std::string> out;
  for (const auto& e : requested) {
    if (containsExtension(available, e)) {
      if (!containsExtension(out, e)) out.push_back(e);
    } else if (dropped && !containsExtension(*dropped, e)) {
      dropped->push_back(e);
    }
  }
  return out;
}

const std::vector<std::string>& fallbackInstanceExtensions() {
  // Core in Vulkan 1.1, but required for 1.0 instances; Monado queries these.
  static const std::vector<std::string> list = {
      "VK_KHR_external_fence_capabilities",
      "VK_KHR_external_memory_capabilities",
      "VK_KHR_external_semaphore_capabilities",
      "VK_KHR_get_physical_device_properties2",
  };
  return list;
}

const std::vector<std::string>& fallbackDeviceExtensions() {
  static const std::vector<std::string> list = {
      "VK_KHR_dedicated_allocation",
      "VK_KHR_external_fence",
      "VK_KHR_external_fence_fd",
      "VK_KHR_external_memory",
      "VK_KHR_external_memory_fd",
      "VK_KHR_external_semaphore",
      "VK_KHR_external_semaphore_fd",
      "VK_KHR_get_memory_requirements2",
      "VK_KHR_image_format_list",
      "VK_KHR_timeline_semaphore",
  };
  return list;
}

std::string defaultRequirementsCachePath() {
  const char* cache = std::getenv("XDG_CACHE_HOME");
  std::string base;
  if (cache && *cache) {
    base = cache;
  } else {
    const char* home = std::getenv("HOME");
    base = std::string(home ? home : "/tmp") + "/.cache";
  }
  return base + "/linuxxr/vulkan_requirements.txt";
}

std::string serializeRequirements(const VulkanRequirements& req) {
  std::ostringstream out;
  out << "# LinuxXR: Vulkan extensions of the OpenXR runtime (auto-generated)\n";
  out << "runtime=" << req.runtimeManifest << "\n";
  out << "instance=" << joinExtensions(req.instanceExtensions) << "\n";
  out << "device=" << joinExtensions(req.deviceExtensions) << "\n";
  return out.str();
}

bool parseRequirements(const std::string& text, VulkanRequirements& out) {
  std::istringstream in(text);
  std::string line;
  bool haveInstance = false, haveDevice = false;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq), value = line.substr(eq + 1);
    if (key == "runtime") {
      out.runtimeManifest = value;
    } else if (key == "instance") {
      out.instanceExtensions = splitExtensions(value);
      haveInstance = true;
    } else if (key == "device") {
      out.deviceExtensions = splitExtensions(value);
      haveDevice = true;
    }
  }
  return haveInstance && haveDevice;
}

bool loadRequirementsCache(const std::string& path, const std::string& runtimeManifest, VulkanRequirements& out) {
  std::ifstream f(path);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  VulkanRequirements tmp;
  if (!parseRequirements(ss.str(), tmp)) return false;
  if (!runtimeManifest.empty() && tmp.runtimeManifest != runtimeManifest) {
    LXR_DEBUG("Vulkan cache belongs to a different runtime (%s)", tmp.runtimeManifest.c_str());
    return false;
  }
  tmp.source = "cache";
  out = tmp;
  return true;
}

static bool mkdirs(const std::string& dir) {
  if (dir.empty()) return true;
  struct stat st {};
  if (::stat(dir.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
  auto slash = dir.find_last_of('/');
  if (slash != std::string::npos && slash > 0 && !mkdirs(dir.substr(0, slash))) return false;
  return ::mkdir(dir.c_str(), 0755) == 0 || errno == EEXIST;
}

bool saveRequirementsCache(const std::string& path, const VulkanRequirements& req) {
  auto slash = path.find_last_of('/');
  if (slash != std::string::npos && !mkdirs(path.substr(0, slash))) {
    LXR_WARN("Cannot create cache directory: %s", path.c_str());
    return false;
  }
  std::ofstream f(path, std::ios::trunc);
  if (!f) return false;
  f << serializeRequirements(req);
  return static_cast<bool>(f);
}

bool writeExtensionString(const std::string& value, uint32_t capacity, uint32_t* countOut, char* buffer) {
  if (!countOut) return false;
  const uint32_t needed = static_cast<uint32_t>(value.size() + 1);
  if (capacity == 0 || buffer == nullptr) {
    *countOut = needed;
    return true;
  }
  if (capacity < needed) {
    *countOut = needed;
    return false;
  }
  std::memcpy(buffer, value.c_str(), needed);
  *countOut = needed;
  return true;
}

}  // namespace lxr
