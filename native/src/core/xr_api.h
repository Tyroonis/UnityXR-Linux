// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#pragma once
// OpenXR function table. The Khronos loader is loaded at runtime via dlopen,
// so libLinuxXR.so also loads without an installed loader (and Unity does not crash).

#include <time.h>

#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <string>

namespace lxr {

// Global functions (available without an instance)
#define LXR_XR_GLOBAL_FUNCS(X)              \
  X(xrEnumerateInstanceExtensionProperties) \
  X(xrEnumerateApiLayerProperties)          \
  X(xrCreateInstance)

// Core functions (require an XrInstance)
#define LXR_XR_INSTANCE_FUNCS(X)            \
  X(xrDestroyInstance)                      \
  X(xrGetInstanceProperties)                \
  X(xrPollEvent)                            \
  X(xrResultToString)                       \
  X(xrStructureTypeToString)                \
  X(xrGetSystem)                            \
  X(xrGetSystemProperties)                  \
  X(xrEnumerateViewConfigurations)          \
  X(xrGetViewConfigurationProperties)       \
  X(xrEnumerateViewConfigurationViews)      \
  X(xrEnumerateEnvironmentBlendModes)       \
  X(xrCreateSession)                        \
  X(xrDestroySession)                       \
  X(xrEnumerateReferenceSpaces)             \
  X(xrCreateReferenceSpace)                 \
  X(xrGetReferenceSpaceBoundsRect)          \
  X(xrDestroySpace)                         \
  X(xrLocateSpace)                          \
  X(xrEnumerateSwapchainFormats)            \
  X(xrCreateSwapchain)                      \
  X(xrDestroySwapchain)                     \
  X(xrEnumerateSwapchainImages)             \
  X(xrAcquireSwapchainImage)                \
  X(xrWaitSwapchainImage)                   \
  X(xrReleaseSwapchainImage)                \
  X(xrBeginSession)                         \
  X(xrEndSession)                           \
  X(xrRequestExitSession)                   \
  X(xrWaitFrame)                            \
  X(xrBeginFrame)                           \
  X(xrEndFrame)                             \
  X(xrLocateViews)                          \
  X(xrStringToPath)                         \
  X(xrPathToString)                         \
  X(xrCreateActionSet)                      \
  X(xrDestroyActionSet)                     \
  X(xrCreateAction)                         \
  X(xrDestroyAction)                        \
  X(xrSuggestInteractionProfileBindings)    \
  X(xrAttachSessionActionSets)              \
  X(xrGetCurrentInteractionProfile)         \
  X(xrGetActionStateBoolean)                \
  X(xrGetActionStateFloat)                  \
  X(xrGetActionStateVector2f)               \
  X(xrGetActionStatePose)                   \
  X(xrSyncActions)                          \
  X(xrCreateActionSpace)                    \
  X(xrApplyHapticFeedback)                  \
  X(xrStopHapticFeedback)

// Extension functions (optional, may stay nullptr)
#define LXR_XR_EXTENSION_FUNCS(X)           \
  X(xrGetVulkanInstanceExtensionsKHR)       \
  X(xrGetVulkanDeviceExtensionsKHR)         \
  X(xrGetVulkanGraphicsDeviceKHR)           \
  X(xrGetVulkanGraphicsRequirementsKHR)     \
  X(xrConvertTimespecTimeToTimeKHR)         \
  X(xrEnumerateDisplayRefreshRatesFB)       \
  X(xrGetDisplayRefreshRateFB)              \
  X(xrRequestDisplayRefreshRateFB)

struct XrApi {
#define LXR_DECLARE(name) PFN_##name name = nullptr;
  PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr = nullptr;
  LXR_XR_GLOBAL_FUNCS(LXR_DECLARE)
  LXR_XR_INSTANCE_FUNCS(LXR_DECLARE)
  LXR_XR_EXTENSION_FUNCS(LXR_DECLARE)
#undef LXR_DECLARE
};

// Global table (one loader per process).
XrApi& xr();

// Loads libopenxr_loader.so.1 (idempotent). Returns false + log if not present.
bool loadOpenXRLoader();
bool isOpenXRLoaderLoaded();
// Path of the loaded loader library (for diagnostics).
std::string openXRLoaderPath();

// Call after xrCreateInstance: loads instance and extension functions.
bool loadInstanceFunctions(XrInstance instance);
// Before/at xrDestroyInstance: reset the instance functions.
void resetInstanceFunctions();

// XrResult as text (also works without an instance).
const char* xrResultName(XrResult result);

}  // namespace lxr

// Checks an XrResult, logs errors with the expression, returns bool.
#define LXR_XR_OK(expr) ::lxr::detail::checkXr((expr), #expr, __FILE__, __LINE__)

namespace lxr::detail {
bool checkXr(XrResult result, const char* expr, const char* file, int line);
}
