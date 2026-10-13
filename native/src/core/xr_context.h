// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#pragma once
// XrContext - wraps the OpenXR instance, system, session, spaces, swapchains and frame loop.
// Independent of Unity: used by the Unity plugin AND by linuxxr-probe.
//
// Threading:
//   - createInstance/createSession/destroy*: a single thread (Unity: main or gfx thread, see glue).
//   - pollEvents/waitFrame/beginFrame/endFrame are serialized by an internal mutex.
//   - acquire/release of a swapchain: only one thread at a time (caller's responsibility).

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "core/runtime_select.h"
#include "core/vk_requirements.h"
#include "core/xr_api.h"

namespace lxr {

enum class TrackingOrigin { Device, Floor };

struct XrContextConfig {
  std::string appName = "LinuxXR";
  std::string engineName = "Unity";
  std::string runtimePreference = "auto";  // "auto" | "wivrn" | "monado" | "active" | path
  TrackingOrigin origin = TrackingOrigin::Floor;
  bool requestHandTracking = false;
  bool requestRefreshRate = true;
  // How long a throw-away probe instance may take before "no headset" is assumed (the runtime would block).
  int probeTimeoutMs = 3000;
};

struct VulkanBinding {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  uint32_t queueFamilyIndex = 0;
  uint32_t queueIndex = 0;
};

struct SwapchainInfo {
  XrSwapchain handle = XR_NULL_HANDLE;
  int64_t format = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t arraySize = 1;
  uint32_t sampleCount = 1;
  std::vector<VkImage> images;
  uint32_t acquiredIndex = UINT32_MAX;
};

struct ViewPair {
  XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
  XrViewState state{XR_TYPE_VIEW_STATE};
  bool valid() const {
    return (state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
  }
};

class XrContext {
 public:
  XrContext() = default;
  ~XrContext();
  XrContext(const XrContext&) = delete;
  XrContext& operator=(const XrContext&) = delete;

  // ---- Instance / system ----------------------------------------------------
  bool createInstance(const XrContextConfig& config);
  void destroy();  // session + instance
  bool hasInstance() const { return instance_ != XR_NULL_HANDLE; }
  // true if the last createInstance() failed only because no runtime / no headset is available right now
  // (WiVRn not running, Quest not connected). That is a normal state, logged as a warning, not an error.
  bool unavailable() const { return unavailable_; }
  XrInstance instance() const { return instance_; }
  XrSystemId systemId() const { return systemId_; }
  const RuntimeCandidate& runtime() const { return runtime_; }
  const std::string& runtimeName() const { return runtimeName_; }
  XrVersion runtimeVersion() const { return runtimeVersion_; }
  const std::string& systemName() const { return systemName_; }
  bool isExtensionEnabled(const char* name) const;
  const std::vector<std::string>& availableExtensions() const { return availableExtensions_; }
  const std::vector<std::string>& enabledExtensions() const { return enabledExtensions_; }

  const XrViewConfigurationView& viewConfig(int eye) const { return viewConfigViews_[eye]; }
  XrEnvironmentBlendMode blendMode() const { return blendMode_; }
  const std::vector<XrEnvironmentBlendMode>& blendModes() const { return blendModes_; }
  bool supportsAlphaBlend() const;
  void setBlendMode(XrEnvironmentBlendMode mode) { blendMode_ = mode; }

  // ---- Vulkan --------------------------------------------------------------
  // Asks the runtime for the required Vulkan extensions (XR_KHR_vulkan_enable).
  bool queryVulkanRequirements(VulkanRequirements& out) const;
  // Which physical device does the runtime want for this VkInstance?
  VkPhysicalDevice vulkanPhysicalDeviceFor(VkInstance instance) const;
  // Min/max supported Vulkan version (mandatory call before xrCreateSession).
  bool vulkanGraphicsRequirements(XrVersion& minVersion, XrVersion& maxVersion) const;

  // ---- Session ---------------------------------------------------------------
  bool createSession(const VulkanBinding& binding);
  void destroySession();
  bool hasSession() const { return session_ != XR_NULL_HANDLE; }
  XrSession session() const { return session_; }
  XrSessionState sessionState() const { return state_.load(); }
  bool sessionRunning() const { return running_.load(); }
  bool exitRequested() const { return exitRequested_.load(); }
  bool lossPending() const { return lossPending_.load(); }
  bool focused() const { return state_.load() == XR_SESSION_STATE_FOCUSED; }
  void requestExit();

  // Fetch events, start/stop the session automatically. Callable from any thread.
  void pollEvents();

  std::function<void(XrSessionState)> onStateChanged;
  std::function<void()> onInteractionProfileChanged;
  std::function<void()> onReferenceSpaceChanged;

  // ---- Spaces --------------------------------------------------------------
  XrSpace appSpace() const { return appSpace_; }
  XrSpace viewSpace() const { return viewSpace_; }
  XrReferenceSpaceType appSpaceType() const { return appSpaceType_; }
  TrackingOrigin trackingOrigin() const { return origin_; }
  bool setTrackingOrigin(TrackingOrigin origin);
  bool supportsReferenceSpace(XrReferenceSpaceType type) const;
  bool locateSpace(XrSpace space, XrSpace base, XrTime time, XrSpaceLocation& location,
                   XrSpaceVelocity* velocity = nullptr) const;
  // Head (VIEW space) in app space
  bool locateHead(XrTime time, XrPosef& pose, XrSpaceLocationFlags* flags = nullptr,
                  XrSpaceVelocity* velocity = nullptr) const;

  // ---- Swapchains ----------------------------------------------------------
  std::vector<int64_t> swapchainFormats() const;
  bool createSwapchain(const std::vector<int64_t>& preferredFormats, uint32_t width, uint32_t height,
                       uint32_t arraySize, uint32_t sampleCount, XrSwapchainUsageFlags usage, SwapchainInfo& out);
  void destroySwapchain(SwapchainInfo& sc);
  bool acquireImage(SwapchainInfo& sc, XrDuration timeout = XR_INFINITE_DURATION);
  bool releaseImage(SwapchainInfo& sc);

  // ---- Frame loop ----------------------------------------------------------
  bool waitFrame(XrFrameState& out);
  bool beginFrame();
  bool locateViews(XrTime time, XrSpace space, ViewPair& out) const;
  bool endFrame(XrTime displayTime, const XrCompositionLayerBaseHeader* const* layers, uint32_t layerCount);
  XrTime lastPredictedDisplayTime() const { return lastPredictedTime_.load(); }
  XrDuration lastPredictedPeriod() const { return lastPredictedPeriod_.load(); }
  bool frameBegun() const { return frameBegun_; }
  uint64_t framesSubmitted() const { return framesSubmitted_.load(); }

  // ---- Refresh rate (XR_FB_display_refresh_rate) -----------------------------
  std::vector<float> refreshRates() const;
  float currentRefreshRate() const;
  bool requestRefreshRate(float hz);

  // ---- Time ----------------------------------------------------------------
  XrTime now() const;  // via XR_KHR_convert_timespec_time, otherwise 0

  // Text summary for logs/status window
  std::string describe() const;

  static const char* sessionStateName(XrSessionState state);

 private:
  bool createSpaces();
  void destroySpaces();
  void handleSessionStateChange(const XrEventDataSessionStateChanged& e);
  void noteFailure(XrResult r);

  XrContextConfig config_;
  RuntimeCandidate runtime_;
  XrInstance instance_ = XR_NULL_HANDLE;
  XrSystemId systemId_ = XR_NULL_SYSTEM_ID;
  XrSession session_ = XR_NULL_HANDLE;
  XrSpace appSpace_ = XR_NULL_HANDLE;
  XrSpace viewSpace_ = XR_NULL_HANDLE;
  XrReferenceSpaceType appSpaceType_ = XR_REFERENCE_SPACE_TYPE_LOCAL;
  TrackingOrigin origin_ = TrackingOrigin::Floor;

  std::string runtimeName_;
  XrVersion runtimeVersion_ = 0;
  bool unavailable_ = false;
  std::string systemName_;
  std::vector<std::string> availableExtensions_;
  std::vector<std::string> enabledExtensions_;
  std::vector<XrReferenceSpaceType> referenceSpaces_;
  XrViewConfigurationView viewConfigViews_[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW},
                                                 {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
  std::vector<XrEnvironmentBlendMode> blendModes_;
  XrEnvironmentBlendMode blendMode_ = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

  std::atomic<XrSessionState> state_{XR_SESSION_STATE_UNKNOWN};
  std::atomic<bool> running_{false};
  std::atomic<bool> exitRequested_{false};
  std::atomic<bool> lossPending_{false};
  std::atomic<XrTime> lastPredictedTime_{0};
  std::atomic<XrDuration> lastPredictedPeriod_{0};
  std::atomic<uint64_t> framesSubmitted_{0};
  bool frameBegun_ = false;

  mutable std::recursive_mutex mutex_;
};

}  // namespace lxr
