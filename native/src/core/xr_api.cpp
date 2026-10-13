// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#include "core/xr_api.h"

#include <dlfcn.h>
#include <openxr/openxr_reflection.h>

#include <cstdlib>
#include <cstring>
#include <mutex>

#include "core/log.h"

namespace lxr {
namespace {

XrApi g_api;
void* g_loaderHandle = nullptr;
std::string g_loaderPath;
std::mutex g_loaderMutex;

}  // namespace

XrApi& xr() { return g_api; }

bool isOpenXRLoaderLoaded() { return g_api.xrGetInstanceProcAddr != nullptr; }

std::string openXRLoaderPath() { return g_loaderPath; }

bool loadOpenXRLoader() {
  std::lock_guard<std::mutex> lock(g_loaderMutex);
  if (g_api.xrGetInstanceProcAddr) return true;

  // Bundled loader next to libLinuxXR.so (for environments without a system loader, e.g. the Editor in
  // the Unity Hub Flatpak, whose runtime does not contain an OpenXR loader).
  std::string bundled;
  {
    Dl_info self{};
    if (dladdr(reinterpret_cast<void*>(&loadOpenXRLoader), &self) && self.dli_fname) {
      std::string p = self.dli_fname;
      auto slash = p.find_last_of('/');
      if (slash != std::string::npos) bundled = p.substr(0, slash + 1) + "libopenxr_loader.so.1";
    }
  }
  const char* candidates[] = {
      std::getenv("LINUXXR_OPENXR_LOADER"),  // override for tests
      "libopenxr_loader.so.1",
      "libopenxr_loader.so",
      bundled.empty() ? nullptr : bundled.c_str(),
  };
  for (const char* name : candidates) {
    if (!name || !*name) continue;
    g_loaderHandle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (g_loaderHandle) {
      g_loaderPath = name;
      break;
    }
  }
  if (!g_loaderHandle) {
    LXR_ERROR("OpenXR loader not found (%s). Install the libopenxr-loader1 package.", dlerror());
    return false;
  }

  g_api.xrGetInstanceProcAddr =
      reinterpret_cast<PFN_xrGetInstanceProcAddr>(dlsym(g_loaderHandle, "xrGetInstanceProcAddr"));
  if (!g_api.xrGetInstanceProcAddr) {
    LXR_ERROR("xrGetInstanceProcAddr missing in %s", g_loaderPath.c_str());
    dlclose(g_loaderHandle);
    g_loaderHandle = nullptr;
    return false;
  }

  bool ok = true;
#define LXR_LOAD_GLOBAL(name)                                                                         \
  if (XR_FAILED(g_api.xrGetInstanceProcAddr(XR_NULL_HANDLE, #name,                                    \
                                            reinterpret_cast<PFN_xrVoidFunction*>(&g_api.name)))) {   \
    LXR_ERROR("Global OpenXR function missing: %s", #name);                                           \
    ok = false;                                                                                       \
  }
  LXR_XR_GLOBAL_FUNCS(LXR_LOAD_GLOBAL)
#undef LXR_LOAD_GLOBAL

  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(g_api.xrGetInstanceProcAddr), &info) && info.dli_fname) {
    g_loaderPath = info.dli_fname;
  }
  LXR_DEBUG("OpenXR loader loaded: %s", g_loaderPath.c_str());
  return ok;
}

bool loadInstanceFunctions(XrInstance instance) {
  if (!g_api.xrGetInstanceProcAddr) return false;
  bool ok = true;
#define LXR_LOAD_CORE(name)                                                                \
  if (XR_FAILED(g_api.xrGetInstanceProcAddr(instance, #name,                               \
                                            reinterpret_cast<PFN_xrVoidFunction*>(&g_api.name)))) { \
    LXR_ERROR("OpenXR function missing: %s", #name);                                       \
    ok = false;                                                                            \
  }
  LXR_XR_INSTANCE_FUNCS(LXR_LOAD_CORE)
#undef LXR_LOAD_CORE

#define LXR_LOAD_EXT(name)                                                                 \
  if (XR_FAILED(g_api.xrGetInstanceProcAddr(instance, #name,                               \
                                            reinterpret_cast<PFN_xrVoidFunction*>(&g_api.name)))) { \
    g_api.name = nullptr;                                                                  \
  }
  LXR_XR_EXTENSION_FUNCS(LXR_LOAD_EXT)
#undef LXR_LOAD_EXT
  return ok;
}

void resetInstanceFunctions() {
#define LXR_RESET(name) g_api.name = nullptr;
  LXR_XR_INSTANCE_FUNCS(LXR_RESET)
  LXR_XR_EXTENSION_FUNCS(LXR_RESET)
#undef LXR_RESET
}

const char* xrResultName(XrResult result) {
  switch (result) {
#define LXR_RESULT_CASE(name, value) \
  case name:                         \
    return #name;
    XR_LIST_ENUM_XrResult(LXR_RESULT_CASE)
#undef LXR_RESULT_CASE
    default:
      return "XR_RESULT_UNKNOWN";
  }
}

namespace detail {
bool checkXr(XrResult result, const char* expr, const char* file, int line) {
  if (XR_SUCCEEDED(result)) {
    if (result != XR_SUCCESS) {
      LXR_DEBUG("%s -> %s", expr, xrResultName(result));
    }
    return true;
  }
  const char* base = std::strrchr(file, '/');
  LXR_ERROR("%s failed: %s (%s:%d)", expr, xrResultName(result), base ? base + 1 : file, line);
  return false;
}
}  // namespace detail

}  // namespace lxr
