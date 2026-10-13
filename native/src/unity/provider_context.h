// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#pragma once
// Shared state of the Unity plugin (one instance per process).

#include <atomic>
#include <mutex>
#include <string>

#include "IUnityInterface.h"
#include "IUnityXRDisplay.h"
#include "IUnityXRInput.h"
#include "IUnityXRTrace.h"
#include "core/vk_preinit.h"
#include "core/xr_context.h"
#include "core/xr_input.h"

struct IUnityGraphicsVulkan;

namespace lxr::unity {

// Settings set by the C# side, or read from Library/LinuxXR/settings.txt.
struct PluginSettings {
  std::string runtime = "auto";  // auto | wivrn | monado | active | /path.json
  TrackingOrigin origin = TrackingOrigin::Floor;
  bool singlePass = true;        // single-pass instanced (texture array)
  int mirrorMode = 0;            // 0 left, 1 right, 2 both, 3 off (as in C# LinuxXRMirrorMode)
  float renderScale = 1.0f;
  bool verboseLog = false;
};

// Head pose with which Unity renders the current frame (set by the input subsystem in BeforeRender,
// used by the display for the layer poses on submit). OpenXR coordinates, app space.
struct HeadSnapshot {
  std::mutex mutex;
  XrPosef pose{{0, 0, 0, 1}, {0, 0, 0}};
  bool valid = false;
  // Eyes relative to the head (from the display via xrLocateViews in VIEW space)
  XrPosef eyeInHead[2] = {{{0, 0, 0, 1}, {-0.032f, 0, 0}}, {{0, 0, 0, 1}, {0.032f, 0, 0}}};
};

// Frame statistics for the status window / diagnostic report. Written on the graphics thread,
// read from the main thread - everything atomic, averages are exponential moving averages.
struct FrameStats {
  std::atomic<uint64_t> framesBegun{0};      // xrBeginFrame succeeded
  std::atomic<uint64_t> framesRendered{0};   // xrEndFrame with Unity's image
  std::atomic<uint64_t> framesEmpty{0};      // xrEndFrame without layer (no image / copy failed)
  std::atomic<uint64_t> framesNoRender{0};   // runtime said shouldRender = false (headset off/idle)
  std::atomic<uint64_t> framesMissed{0};     // display intervals skipped between two predicted times
  std::atomic<uint64_t> framesDropped{0};    // frame begun but Unity never submitted it
  std::atomic<float> headsetFps{0};          // rate of xrEndFrame calls
  std::atomic<float> appFps{0};              // rate of Unity frames (PopulateNextFrameDesc)
  std::atomic<float> waitFrameMs{0};         // time blocked in xrWaitFrame
  std::atomic<float> submitMs{0};            // CPU time copy + xrEndFrame
  std::atomic<float> displayPeriodMs{0};     // predicted display period from the runtime
  std::atomic<uint32_t> eyeWidth{0}, eyeHeight{0};
  std::atomic<int64_t> swapchainFormat{0};
  std::atomic<int> singlePass{1};
  std::atomic<float> renderScale{1};
  std::atomic<int64_t> sessionStartNs{0};    // steady clock, 0 = no session

  void reset() {
    framesBegun = framesRendered = framesEmpty = framesNoRender = framesMissed = framesDropped = 0;
    headsetFps = appFps = waitFrameMs = submitMs = displayPeriodMs = 0;
    eyeWidth = eyeHeight = 0;
    swapchainFormat = 0;
  }
  static void ema(std::atomic<float>& v, float sample, float k = 0.05f) {
    float old = v.load();
    v = old == 0.0f ? sample : old + k * (sample - old);
  }
};

struct ProviderContext {
  IUnityInterfaces* interfaces = nullptr;
  IUnityXRTrace* trace = nullptr;
  IUnityXRDisplayInterface* display = nullptr;
  IUnityXRInputInterface* input = nullptr;

  PluginSettings settings;
  std::mutex settingsMutex;

  VulkanPreInit preinit;
  std::atomic<bool> preinitRan{false};
  std::atomic<bool> preinitRuntimeQueried{false};
  std::string preinitSource;       // runtime | cache | fallback
  std::string preinitGpu;
  std::string preinitInstanceExts;
  std::string preinitDeviceExts;

  // OpenXR runtime context for Play mode (session.cpp)
  XrContext xr;
  XrInput xrInput;
  std::mutex sessionMutex;
  bool displayWantsSession = false;
  bool inputWantsSession = false;
  std::atomic<bool> sessionReady{false};
  // Tracking origin change: requested by the main thread, applied on the gfx thread
  std::atomic<int> pendingOrigin{-1};  // -1 = none, 0 = Device, 1 = Floor

  HeadSnapshot head;
  FrameStats stats;

  // Vulkan check at session start: required device extensions the Unity device lacks
  // (only those the GPU supports and that can be detected; empty = complete)
  std::string vkMissingRequired;
  std::string vkMissingOptional;
  std::atomic<int> vkChecked{0};  // 0 = not yet, 1 = done

  // Status for C#/status window
  std::atomic<int> displayState{0};  // 0=off, 1=initialized, 2=running
  std::atomic<int> inputState{0};
  std::string lastError;
  std::mutex statusMutex;

  void setError(const std::string& e) {
    std::lock_guard<std::mutex> l(statusMutex);
    lastError = e;
  }
};

ProviderContext& context();

// Reads <cwd>/Library/LinuxXR/settings.txt (the Unity Editor runs with the project folder as cwd).
void loadSettingsFile(PluginSettings& out);

// Forward logging to Unity (IUnityXRTrace) + file ~/.cache/linuxxr/linuxxr.log
void installUnityLogSink(IUnityXRTrace* trace);
// Path of the log file (for diagnostics)
std::string unityLogFilePath();

// Subsystem registration (display_provider.cpp / input_provider.cpp)
void registerDisplayLifecycle();
void registerInputLifecycle();

// Session management (session.cpp). who: 0 = display, 1 = input.
// acquireSession creates the OpenXR instance + session with Unity's Vulkan objects if needed.
bool acquireSession(int who);
void releaseSession(int who);
// Registered by the display: releases swapchains/Vulkan resources before the session is destroyed.
void setSwapchainCleanup(void (*fn)());

// Unity's Vulkan interface (nullptr if not Vulkan)
IUnityGraphicsVulkan* unityVulkan();

}  // namespace lxr::unity
