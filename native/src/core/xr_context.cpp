// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#include "core/xr_context.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "core/log.h"
#include "core/xr_math.h"

namespace lxr {
namespace {

// Local-floor: core in OpenXR 1.1, via XR_EXT_local_floor in 1.0.
#if defined(XR_EXT_local_floor)
constexpr XrReferenceSpaceType kLocalFloor = XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT;
constexpr const char* kLocalFloorExt = XR_EXT_LOCAL_FLOOR_EXTENSION_NAME;
#else
constexpr XrReferenceSpaceType kLocalFloor = static_cast<XrReferenceSpaceType>(1000426000);
constexpr const char* kLocalFloorExt = "XR_EXT_local_floor";
#endif

std::string versionString(XrVersion v) {
  return std::to_string(XR_VERSION_MAJOR(v)) + "." + std::to_string(XR_VERSION_MINOR(v)) + "." +
         std::to_string(XR_VERSION_PATCH(v));
}

const char* spaceTypeName(XrReferenceSpaceType t) {
  switch (t) {
    case XR_REFERENCE_SPACE_TYPE_VIEW: return "VIEW";
    case XR_REFERENCE_SPACE_TYPE_LOCAL: return "LOCAL";
    case XR_REFERENCE_SPACE_TYPE_STAGE: return "STAGE";
    default: return t == kLocalFloor ? "LOCAL_FLOOR" : "?";
  }
}

// ---- Headset probe ---------------------------------------------------------------------------------------
// WiVRn (and Monado-based runtimes) accept IPC connections while no headset is connected, but then block
// xrCreateInstance / xrGetSystem until one connects. Called on Unity's main thread that would freeze the Editor.
// So a throw-away instance is created on a worker thread first; if it does not answer in time, there is no headset.
// A probe that is still blocked keeps running detached (it owns its own instance and destroys it when it returns).

struct ProbeState {
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  XrResult result = XR_SUCCESS;
};
std::atomic<bool> g_probeRunning{false};

XrResult probeHeadset(int timeoutMs) {
  if (g_probeRunning.load()) return XR_TIMEOUT_EXPIRED;  // an earlier probe is still waiting inside the runtime
  PFN_xrGetInstanceProcAddr gipa = xr().xrGetInstanceProcAddr;
  PFN_xrCreateInstance create = xr().xrCreateInstance;
  if (!gipa || !create) return XR_ERROR_RUNTIME_UNAVAILABLE;

  auto st = std::make_shared<ProbeState>();
  g_probeRunning = true;
  std::thread([st, gipa, create] {
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strncpy(ci.applicationInfo.applicationName, "Unity Editor (LinuxXR probe)", XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(ci.applicationInfo.engineName, "Unity", XR_MAX_ENGINE_NAME_SIZE - 1);
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
    XrInstance inst = XR_NULL_HANDLE;
    XrResult r = create(&ci, &inst);
    if (XR_SUCCEEDED(r)) {
      PFN_xrGetSystem getSystem = nullptr;
      PFN_xrDestroyInstance destroyInstance = nullptr;
      gipa(inst, "xrGetSystem", reinterpret_cast<PFN_xrVoidFunction*>(&getSystem));
      gipa(inst, "xrDestroyInstance", reinterpret_cast<PFN_xrVoidFunction*>(&destroyInstance));
      XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
      sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
      XrSystemId sys = XR_NULL_SYSTEM_ID;
      r = getSystem ? getSystem(inst, &sgi, &sys) : XR_ERROR_FUNCTION_UNSUPPORTED;
      if (destroyInstance) destroyInstance(inst);
    }
    {
      std::lock_guard<std::mutex> l(st->m);
      st->result = r;
      st->done = true;
    }
    st->cv.notify_all();
    g_probeRunning = false;
  }).detach();

  std::unique_lock<std::mutex> l(st->m);
  if (!st->cv.wait_for(l, std::chrono::milliseconds(timeoutMs), [&] { return st->done; })) return XR_TIMEOUT_EXPIRED;
  return st->result;
}

}  // namespace

XrContext::~XrContext() { destroy(); }

const char* XrContext::sessionStateName(XrSessionState s) {
  switch (s) {
    case XR_SESSION_STATE_UNKNOWN: return "UNKNOWN";
    case XR_SESSION_STATE_IDLE: return "IDLE";
    case XR_SESSION_STATE_READY: return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
    case XR_SESSION_STATE_STOPPING: return "STOPPING";
    case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
    case XR_SESSION_STATE_EXITING: return "EXITING";
    default: return "?";
  }
}

bool XrContext::isExtensionEnabled(const char* name) const {
  for (const auto& e : enabledExtensions_)
    if (e == name) return true;
  return false;
}

bool XrContext::createInstance(const XrContextConfig& config) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (instance_ != XR_NULL_HANDLE) return true;
  unavailable_ = false;
  config_ = config;
  origin_ = config.origin;

  if (!loadOpenXRLoader()) return false;

  runtime_ = selectRuntime(config.runtimePreference);
  if (runtime_.manifestPath.empty()) {
    LXR_WARN("No OpenXR runtime found - Play continues without VR. Install/start WiVRn or set LINUXXR_RUNTIME.");
    unavailable_ = true;
    return false;
  }
  // A previous probe is still blocked inside the runtime (no headset yet). The runtime's client library holds
  // its lock meanwhile, so ANY call into it (even xrEnumerateInstanceExtensionProperties) would block too.
  if (g_probeRunning.load()) {
    LXR_WARN("No headset connected yet (the OpenXR runtime still waits for one) - Play continues without VR.");
    unavailable_ = true;
    return false;
  }
  applyRuntime(runtime_);
  LXR_INFO("Runtime: %s [%s, source: %s]", runtime_.manifestPath.c_str(), runtimeKindName(runtime_.kind),
           runtime_.source.c_str());

  // Available extensions
  uint32_t count = 0;
  XrResult r = xr().xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
  if (XR_FAILED(r)) {
    LXR_WARN("OpenXR runtime not reachable (%s) - Play continues without VR. %s", xrResultName(r),
             runtimeReachabilityHint(runtime_).c_str());
    unavailable_ = true;
    return false;
  }
  std::vector<XrExtensionProperties> props(count, {XR_TYPE_EXTENSION_PROPERTIES});
  xr().xrEnumerateInstanceExtensionProperties(nullptr, count, &count, props.data());
  availableExtensions_.clear();
  for (auto& p : props) availableExtensions_.emplace_back(p.extensionName);

  auto available = [&](const char* n) { return containsExtension(availableExtensions_, n); };
  enabledExtensions_.clear();
  auto enable = [&](const char* n, bool required) {
    if (available(n)) {
      enabledExtensions_.emplace_back(n);
      return true;
    }
    if (required) LXR_ERROR("Runtime does not support %s", n);
    return false;
  };

  if (!enable(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME, true)) return false;
  enable(XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME, false);
  enable(kLocalFloorExt, false);
  if (config.requestRefreshRate) enable(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME, false);
  if (config.requestHandTracking) enable(XR_EXT_HAND_TRACKING_EXTENSION_NAME, false);
#ifdef XR_META_TOUCH_CONTROLLER_PLUS_EXTENSION_NAME
  enable(XR_META_TOUCH_CONTROLLER_PLUS_EXTENSION_NAME, false);
#else
  enable("XR_META_touch_controller_plus", false);
#endif

  std::vector<const char*> extPtrs;
  for (auto& e : enabledExtensions_) extPtrs.push_back(e.c_str());

  XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
  std::strncpy(ci.applicationInfo.applicationName, config.appName.c_str(), XR_MAX_APPLICATION_NAME_SIZE - 1);
  ci.applicationInfo.applicationVersion = 1;
  std::strncpy(ci.applicationInfo.engineName, config.engineName.c_str(), XR_MAX_ENGINE_NAME_SIZE - 1);
  ci.applicationInfo.engineVersion = 6;
  // Request 1.0: works with loader 1.0.x and 1.1.x and all current runtimes.
  ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
  ci.enabledExtensionCount = static_cast<uint32_t>(extPtrs.size());
  ci.enabledExtensionNames = extPtrs.data();

  // Never block the Editor: check with a throw-away instance on a worker thread whether a headset answers.
  const XrResult probe = probeHeadset(config.probeTimeoutMs);
  if (probe == XR_TIMEOUT_EXPIRED || probe == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
    LXR_WARN("No headset connected to the OpenXR runtime (%s) - Play continues without VR. "
             "Connect the Quest in WiVRn and press Play again.",
             probe == XR_TIMEOUT_EXPIRED ? "runtime waits for a headset" : xrResultName(probe));
    unavailable_ = true;
    return false;
  }

  r = xr().xrCreateInstance(&ci, &instance_);
  if (XR_FAILED(r)) {
    if (r == XR_ERROR_RUNTIME_UNAVAILABLE || r == XR_ERROR_RUNTIME_FAILURE || r == XR_ERROR_INITIALIZATION_FAILED) {
      LXR_WARN("OpenXR runtime not reachable (%s) - Play continues without VR. %s", xrResultName(r),
               runtimeReachabilityHint(runtime_).c_str());
      unavailable_ = true;
    } else {
      LXR_ERROR("xrCreateInstance: %s - %s", xrResultName(r), runtimeReachabilityHint(runtime_).c_str());
    }
    instance_ = XR_NULL_HANDLE;
    return false;
  }
  if (!loadInstanceFunctions(instance_)) {
    LXR_ERROR("OpenXR functions incomplete");
    destroy();
    return false;
  }

  XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
  if (XR_SUCCEEDED(xr().xrGetInstanceProperties(instance_, &ip))) {
    runtimeName_ = ip.runtimeName;
    runtimeVersion_ = ip.runtimeVersion;
  }

  XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
  sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  r = xr().xrGetSystem(instance_, &sgi, &systemId_);
  if (XR_FAILED(r)) {
    if (r == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
      LXR_WARN("No headset connected to %s - Play continues without VR. Connect the Quest in WiVRn and press Play again.",
               runtimeName_.empty() ? "the OpenXR runtime" : runtimeName_.c_str());
      unavailable_ = true;
    } else {
      LXR_ERROR("xrGetSystem: %s (headset not connected?)", xrResultName(r));
    }
    destroy();
    return false;
  }
  XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
  if (XR_SUCCEEDED(xr().xrGetSystemProperties(instance_, systemId_, &sp))) systemName_ = sp.systemName;

  uint32_t viewCount = 0;
  xr().xrEnumerateViewConfigurationViews(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                         &viewCount, nullptr);
  if (viewCount != 2) {
    LXR_ERROR("Stereo view configuration missing (viewCount=%u)", viewCount);
    destroy();
    return false;
  }
  xr().xrEnumerateViewConfigurationViews(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2,
                                         &viewCount, viewConfigViews_);

  uint32_t bmCount = 0;
  xr().xrEnumerateEnvironmentBlendModes(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                        &bmCount, nullptr);
  blendModes_.resize(bmCount);
  xr().xrEnumerateEnvironmentBlendModes(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, bmCount,
                                        &bmCount, blendModes_.data());
  blendMode_ = blendModes_.empty() ? XR_ENVIRONMENT_BLEND_MODE_OPAQUE : blendModes_.front();
  for (auto m : blendModes_)
    if (m == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) blendMode_ = m;

  LXR_INFO("OpenXR ready: %s %s, system \"%s\", %ux%u per eye", runtimeName_.c_str(),
           versionString(runtimeVersion_).c_str(), systemName_.c_str(),
           viewConfigViews_[0].recommendedImageRectWidth, viewConfigViews_[0].recommendedImageRectHeight);
  return true;
}

bool XrContext::supportsAlphaBlend() const {
  for (auto m : blendModes_)
    if (m == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) return true;
  return false;
}

void XrContext::destroy() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  destroySession();
  if (instance_ != XR_NULL_HANDLE) {
    if (xr().xrDestroyInstance) xr().xrDestroyInstance(instance_);
    instance_ = XR_NULL_HANDLE;
    resetInstanceFunctions();
    LXR_DEBUG("OpenXR instance destroyed");
  }
  systemId_ = XR_NULL_SYSTEM_ID;
  exitRequested_ = false;
  lossPending_ = false;
}

// ---- Vulkan -------------------------------------------------------------------

bool XrContext::queryVulkanRequirements(VulkanRequirements& out) const {
  if (!instance_ || !xr().xrGetVulkanInstanceExtensionsKHR || !xr().xrGetVulkanDeviceExtensionsKHR) return false;
  auto fetch = [&](PFN_xrGetVulkanInstanceExtensionsKHR fn, std::string& str) {
    uint32_t n = 0;
    if (XR_FAILED(fn(instance_, systemId_, 0, &n, nullptr))) return false;
    std::string buf(n, '\0');
    if (XR_FAILED(fn(instance_, systemId_, n, &n, buf.data()))) return false;
    str.assign(buf.c_str());
    return true;
  };
  std::string inst, dev;
  if (!fetch(xr().xrGetVulkanInstanceExtensionsKHR, inst)) return false;
  if (!fetch(reinterpret_cast<PFN_xrGetVulkanInstanceExtensionsKHR>(xr().xrGetVulkanDeviceExtensionsKHR), dev))
    return false;
  out.instanceExtensions = splitExtensions(inst);
  out.deviceExtensions = splitExtensions(dev);
  out.source = "runtime";
  out.runtimeManifest = runtime_.manifestPath;
  return true;
}

VkPhysicalDevice XrContext::vulkanPhysicalDeviceFor(VkInstance vkInstance) const {
  if (!instance_ || !xr().xrGetVulkanGraphicsDeviceKHR) return VK_NULL_HANDLE;
  VkPhysicalDevice pd = VK_NULL_HANDLE;
  if (!LXR_XR_OK(xr().xrGetVulkanGraphicsDeviceKHR(instance_, systemId_, vkInstance, &pd))) return VK_NULL_HANDLE;
  return pd;
}

bool XrContext::vulkanGraphicsRequirements(XrVersion& minVersion, XrVersion& maxVersion) const {
  if (!instance_ || !xr().xrGetVulkanGraphicsRequirementsKHR) return false;
  XrGraphicsRequirementsVulkanKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
  if (!LXR_XR_OK(xr().xrGetVulkanGraphicsRequirementsKHR(instance_, systemId_, &req))) return false;
  minVersion = req.minApiVersionSupported;
  maxVersion = req.maxApiVersionSupported;
  return true;
}

// ---- Session -------------------------------------------------------------------

bool XrContext::createSession(const VulkanBinding& b) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!instance_) return false;
  if (session_) return true;

  // Mandatory per spec before xrCreateSession:
  XrVersion minV = 0, maxV = 0;
  if (!vulkanGraphicsRequirements(minV, maxV)) return false;
  LXR_DEBUG("Runtime supports Vulkan %s - %s", versionString(minV).c_str(), versionString(maxV).c_str());

  VkPhysicalDevice wanted = vulkanPhysicalDeviceFor(b.instance);
  if (wanted != VK_NULL_HANDLE && wanted != b.physicalDevice) {
    LXR_WARN("Runtime wants a different VkPhysicalDevice than the application uses. "
             "With multiple GPUs, set XRT_COMPOSITOR_FORCE_GPU_INDEX or LINUXXR_GPU.");
  }

  XrGraphicsBindingVulkanKHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
  binding.instance = b.instance;
  binding.physicalDevice = b.physicalDevice;
  binding.device = b.device;
  binding.queueFamilyIndex = b.queueFamilyIndex;
  binding.queueIndex = b.queueIndex;

  XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
  sci.next = &binding;
  sci.systemId = systemId_;
  XrResult r = xr().xrCreateSession(instance_, &sci, &session_);
  if (XR_FAILED(r)) {
    LXR_ERROR("xrCreateSession: %s - %s", xrResultName(r), runtimeReachabilityHint(runtime_).c_str());
    session_ = XR_NULL_HANDLE;
    return false;
  }
  state_ = XR_SESSION_STATE_IDLE;
  exitRequested_ = false;
  lossPending_ = false;

  uint32_t n = 0;
  xr().xrEnumerateReferenceSpaces(session_, 0, &n, nullptr);
  referenceSpaces_.resize(n);
  xr().xrEnumerateReferenceSpaces(session_, n, &n, referenceSpaces_.data());

  if (!createSpaces()) {
    destroySession();
    return false;
  }
  LXR_INFO("Session created (queue family %u, index %u), app space %s", b.queueFamilyIndex, b.queueIndex,
           spaceTypeName(appSpaceType_));
  return true;
}

void XrContext::destroySession() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!session_) return;
  // xrEndSession is only allowed in the STOPPING state; otherwise destroy directly (spec-compliant).
  if (running_ && state_.load() == XR_SESSION_STATE_STOPPING) xr().xrEndSession(session_);
  running_ = false;
  destroySpaces();
  xr().xrDestroySession(session_);
  session_ = XR_NULL_HANDLE;
  state_ = XR_SESSION_STATE_UNKNOWN;
  frameBegun_ = false;
  LXR_DEBUG("Session destroyed");
}

void XrContext::requestExit() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (session_ && running_) xr().xrRequestExitSession(session_);
}

void XrContext::handleSessionStateChange(const XrEventDataSessionStateChanged& e) {
  XrSessionState old = state_.exchange(e.state);
  LXR_INFO("Session state: %s -> %s", sessionStateName(old), sessionStateName(e.state));
  switch (e.state) {
    case XR_SESSION_STATE_READY: {
      XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
      bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
      if (LXR_XR_OK(xr().xrBeginSession(session_, &bi))) running_ = true;
      break;
    }
    case XR_SESSION_STATE_STOPPING:
      if (running_) {
        LXR_XR_OK(xr().xrEndSession(session_));
        running_ = false;
      }
      frameBegun_ = false;
      break;
    case XR_SESSION_STATE_EXITING:
      exitRequested_ = true;
      break;
    case XR_SESSION_STATE_LOSS_PENDING:
      lossPending_ = true;
      LXR_WARN("Connection to the runtime is being lost (headset disconnected?)");
      break;
    default:
      break;
  }
  if (onStateChanged) onStateChanged(e.state);
}

void XrContext::pollEvents() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!instance_) return;
  for (int guard = 0; guard < 64; ++guard) {
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    XrResult r = xr().xrPollEvent(instance_, &ev);
    if (r == XR_EVENT_UNAVAILABLE) break;
    if (XR_FAILED(r)) {
      LXR_WARN("xrPollEvent: %s", xrResultName(r));
      noteFailure(r);
      break;
    }
    switch (ev.type) {
      case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
        auto& e = *reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
        if (e.session == session_) handleSessionStateChange(e);
        break;
      }
      case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
        lossPending_ = true;
        LXR_WARN("OpenXR instance is about to be lost (runtime exited?)");
        break;
      case XR_TYPE_EVENT_DATA_EVENTS_LOST:
        LXR_WARN("OpenXR events lost: %u",
                 reinterpret_cast<XrEventDataEventsLost*>(&ev)->lostEventCount);
        break;
      case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
        LXR_DEBUG("Interaction profile changed");
        if (onInteractionProfileChanged) onInteractionProfileChanged();
        break;
      case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
        LXR_INFO("Reference space is changing (recenter/guardian)");
        if (onReferenceSpaceChanged) onReferenceSpaceChanged();
        break;
      case XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB: {
        auto& e = *reinterpret_cast<XrEventDataDisplayRefreshRateChangedFB*>(&ev);
        LXR_INFO("Refresh rate: %.0f -> %.0f Hz", e.fromDisplayRefreshRate, e.toDisplayRefreshRate);
        break;
      }
      default:
        LXR_DEBUG("OpenXR event %d", static_cast<int>(ev.type));
        break;
    }
  }
}

// ---- Spaces ---------------------------------------------------------------------

bool XrContext::supportsReferenceSpace(XrReferenceSpaceType type) const {
  for (auto t : referenceSpaces_)
    if (t == type) return true;
  return false;
}

bool XrContext::createSpaces() {
  XrReferenceSpaceCreateInfo ci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  ci.poseInReferenceSpace = poseIdentity();

  ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  if (!LXR_XR_OK(xr().xrCreateReferenceSpace(session_, &ci, &viewSpace_))) return false;

  std::vector<XrReferenceSpaceType> order;
  if (origin_ == TrackingOrigin::Floor) {
    if (isExtensionEnabled(kLocalFloorExt) || supportsReferenceSpace(kLocalFloor)) order.push_back(kLocalFloor);
    order.push_back(XR_REFERENCE_SPACE_TYPE_STAGE);
  }
  order.push_back(XR_REFERENCE_SPACE_TYPE_LOCAL);

  for (auto type : order) {
    if (!supportsReferenceSpace(type)) continue;
    ci.referenceSpaceType = type;
    if (XR_SUCCEEDED(xr().xrCreateReferenceSpace(session_, &ci, &appSpace_))) {
      appSpaceType_ = type;
      if (origin_ == TrackingOrigin::Floor && type == XR_REFERENCE_SPACE_TYPE_LOCAL)
        LXR_WARN("No floor reference space available - using LOCAL (head height = 0)");
      return true;
    }
  }
  LXR_ERROR("Could not create an app reference space");
  return false;
}

void XrContext::destroySpaces() {
  if (appSpace_) xr().xrDestroySpace(appSpace_);
  if (viewSpace_) xr().xrDestroySpace(viewSpace_);
  appSpace_ = viewSpace_ = XR_NULL_HANDLE;
}

bool XrContext::setTrackingOrigin(TrackingOrigin origin) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  origin_ = origin;
  if (!session_) return true;
  if (appSpace_) xr().xrDestroySpace(appSpace_);
  appSpace_ = XR_NULL_HANDLE;
  XrSpace keepView = viewSpace_;
  viewSpace_ = XR_NULL_HANDLE;
  if (keepView) xr().xrDestroySpace(keepView);
  return createSpaces();
}

bool XrContext::locateSpace(XrSpace space, XrSpace base, XrTime time, XrSpaceLocation& location,
                            XrSpaceVelocity* velocity) const {
  if (!session_ || !space || !base || time == 0) return false;
  location = {XR_TYPE_SPACE_LOCATION};
  if (velocity) {
    *velocity = {XR_TYPE_SPACE_VELOCITY};
    location.next = velocity;
  }
  return XR_SUCCEEDED(xr().xrLocateSpace(space, base, time, &location));
}

bool XrContext::locateHead(XrTime time, XrPosef& pose, XrSpaceLocationFlags* flags, XrSpaceVelocity* velocity) const {
  XrSpaceLocation loc;
  if (!locateSpace(viewSpace_, appSpace_, time, loc, velocity)) return false;
  if (flags) *flags = loc.locationFlags;
  if (!(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) return false;
  pose = loc.pose;
  return true;
}

// ---- Swapchains -----------------------------------------------------------------

std::vector<int64_t> XrContext::swapchainFormats() const {
  std::vector<int64_t> out;
  if (!session_) return out;
  uint32_t n = 0;
  xr().xrEnumerateSwapchainFormats(session_, 0, &n, nullptr);
  out.resize(n);
  xr().xrEnumerateSwapchainFormats(session_, n, &n, out.data());
  return out;
}

bool XrContext::createSwapchain(const std::vector<int64_t>& preferred, uint32_t width, uint32_t height,
                                uint32_t arraySize, uint32_t sampleCount, XrSwapchainUsageFlags usage,
                                SwapchainInfo& out) {
  if (!session_) return false;
  auto formats = swapchainFormats();
  int64_t chosen = 0;
  for (auto want : preferred) {
    for (auto f : formats)
      if (f == want) {
        chosen = f;
        break;
      }
    if (chosen) break;
  }
  if (!chosen) {
    if (formats.empty()) return false;
    chosen = formats.front();
    LXR_WARN("No preferred swapchain format available, using %lld", static_cast<long long>(chosen));
  }

  XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  ci.usageFlags = usage;
  ci.format = chosen;
  ci.sampleCount = sampleCount;
  ci.width = width;
  ci.height = height;
  ci.faceCount = 1;
  ci.arraySize = arraySize;
  ci.mipCount = 1;
  XrSwapchain handle = XR_NULL_HANDLE;
  if (!LXR_XR_OK(xr().xrCreateSwapchain(session_, &ci, &handle))) return false;

  uint32_t n = 0;
  xr().xrEnumerateSwapchainImages(handle, 0, &n, nullptr);
  std::vector<XrSwapchainImageVulkanKHR> imgs(n, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
  if (!LXR_XR_OK(xr().xrEnumerateSwapchainImages(handle, n, &n,
                                                 reinterpret_cast<XrSwapchainImageBaseHeader*>(imgs.data())))) {
    xr().xrDestroySwapchain(handle);
    return false;
  }
  out = {};
  out.handle = handle;
  out.format = chosen;
  out.width = width;
  out.height = height;
  out.arraySize = arraySize;
  out.sampleCount = sampleCount;
  for (auto& i : imgs) out.images.push_back(i.image);
  LXR_INFO("Swapchain %ux%u x%u, format %lld, %zu images", width, height, arraySize,
           static_cast<long long>(chosen), out.images.size());
  return true;
}

void XrContext::destroySwapchain(SwapchainInfo& sc) {
  if (sc.handle && xr().xrDestroySwapchain) xr().xrDestroySwapchain(sc.handle);
  sc = {};
}

bool XrContext::acquireImage(SwapchainInfo& sc, XrDuration timeout) {
  if (!sc.handle) return false;
  XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  uint32_t idx = 0;
  if (!LXR_XR_OK(xr().xrAcquireSwapchainImage(sc.handle, &ai, &idx))) return false;
  XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wi.timeout = timeout;
  XrResult r = xr().xrWaitSwapchainImage(sc.handle, &wi);
  if (r == XR_TIMEOUT_EXPIRED || XR_FAILED(r)) {
    LXR_WARN("xrWaitSwapchainImage: %s", xrResultName(r));
    // The image is acquired regardless -> release it so the swapchain does not block.
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xr().xrReleaseSwapchainImage(sc.handle, &ri);
    return false;
  }
  sc.acquiredIndex = idx;
  return true;
}

bool XrContext::releaseImage(SwapchainInfo& sc) {
  if (!sc.handle || sc.acquiredIndex == UINT32_MAX) return false;
  XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  bool ok = LXR_XR_OK(xr().xrReleaseSwapchainImage(sc.handle, &ri));
  sc.acquiredIndex = UINT32_MAX;
  return ok;
}

// ---- Frame loop ----------------------------------------------------------------

void XrContext::noteFailure(XrResult r) {
  // Runtime gone (WiVRn exited, headset disconnected): never enter xrWaitFrame again - the second
  // call otherwise blocks forever on Monado/WiVRn (which would freeze the Unity Editor).
  if (r == XR_ERROR_INSTANCE_LOST || r == XR_ERROR_SESSION_LOST || r == XR_ERROR_RUNTIME_FAILURE) {
    if (!lossPending_.exchange(true)) LXR_ERROR("Connection to the OpenXR runtime lost (%s)", xrResultName(r));
    running_ = false;
    frameBegun_ = false;
  }
}

bool XrContext::waitFrame(XrFrameState& out) {
  // Deliberately without the mutex: xrWaitFrame blocks until the next frame time and must not
  // hold up pollEvents (other threads). External sync is handled by the render thread.
  if (!session_ || !running_ || lossPending_) return false;
  out = {XR_TYPE_FRAME_STATE};
  XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
  XrResult r = xr().xrWaitFrame(session_, &wi, &out);
  if (XR_FAILED(r)) {
    LXR_WARN("xrWaitFrame: %s", xrResultName(r));
    noteFailure(r);
    return false;
  }
  lastPredictedTime_ = out.predictedDisplayTime;
  lastPredictedPeriod_ = out.predictedDisplayPeriod;
  return true;
}

bool XrContext::beginFrame() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!session_ || !running_ || lossPending_) return false;
  XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
  XrResult r = xr().xrBeginFrame(session_, &bi);
  if (r == XR_FRAME_DISCARDED) LXR_DEBUG("Previous frame discarded");
  if (XR_FAILED(r)) {
    LXR_WARN("xrBeginFrame: %s", xrResultName(r));
    noteFailure(r);
    return false;
  }
  frameBegun_ = true;
  return true;
}

bool XrContext::locateViews(XrTime time, XrSpace space, ViewPair& out) const {
  if (!session_ || !space) return false;
  XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
  li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  li.displayTime = time;
  li.space = space;
  out.state = {XR_TYPE_VIEW_STATE};
  out.views[0] = {XR_TYPE_VIEW};
  out.views[1] = {XR_TYPE_VIEW};
  uint32_t n = 0;
  return XR_SUCCEEDED(xr().xrLocateViews(session_, &li, &out.state, 2, &n, out.views)) && n == 2;
}

bool XrContext::endFrame(XrTime displayTime, const XrCompositionLayerBaseHeader* const* layers, uint32_t layerCount) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!session_ || !frameBegun_) return false;
  XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
  ei.displayTime = displayTime;
  ei.environmentBlendMode = blendMode_;
  ei.layerCount = layerCount;
  ei.layers = layers;
  frameBegun_ = false;
  XrResult r = xr().xrEndFrame(session_, &ei);
  if (XR_FAILED(r)) {
    LXR_WARN("xrEndFrame: %s", xrResultName(r));
    noteFailure(r);
    return false;
  }
  framesSubmitted_++;
  return true;
}

// ---- Refresh rate ---------------------------------------------------------------

std::vector<float> XrContext::refreshRates() const {
  std::vector<float> out;
  if (!session_ || !xr().xrEnumerateDisplayRefreshRatesFB) return out;
  uint32_t n = 0;
  xr().xrEnumerateDisplayRefreshRatesFB(session_, 0, &n, nullptr);
  out.resize(n);
  xr().xrEnumerateDisplayRefreshRatesFB(session_, n, &n, out.data());
  return out;
}

float XrContext::currentRefreshRate() const {
  float hz = 0;
  if (session_ && xr().xrGetDisplayRefreshRateFB) xr().xrGetDisplayRefreshRateFB(session_, &hz);
  return hz;
}

bool XrContext::requestRefreshRate(float hz) {
  if (!session_ || !xr().xrRequestDisplayRefreshRateFB) return false;
  return LXR_XR_OK(xr().xrRequestDisplayRefreshRateFB(session_, hz));
}

XrTime XrContext::now() const {
  if (!instance_ || !xr().xrConvertTimespecTimeToTimeKHR) return 0;
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  XrTime t = 0;
  if (XR_FAILED(xr().xrConvertTimespecTimeToTimeKHR(instance_, &ts, &t))) return 0;
  return t;
}

std::string XrContext::describe() const {
  std::ostringstream o;
  o << "Runtime:      " << (runtimeName_.empty() ? "-" : runtimeName_) << " " << versionString(runtimeVersion_)
    << "\nManifest:     " << runtime_.manifestPath << " (" << runtime_.source << ")"
    << "\nSystem:       " << systemName_
    << "\nResolution:   " << viewConfigViews_[0].recommendedImageRectWidth << "x"
    << viewConfigViews_[0].recommendedImageRectHeight << " per eye (max "
    << viewConfigViews_[0].maxImageRectWidth << "x" << viewConfigViews_[0].maxImageRectHeight << ")"
    << "\nBlend modes:  ";
  for (auto m : blendModes_)
    o << (m == XR_ENVIRONMENT_BLEND_MODE_OPAQUE        ? "opaque "
          : m == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND ? "alpha_blend "
                                                       : "additive ");
  o << "\nExtensions:   " << enabledExtensions_.size() << " enabled / " << availableExtensions_.size()
    << " available";
  return o.str();
}

}  // namespace lxr
