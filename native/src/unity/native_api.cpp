// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
// C API for the C# side (P/Invoke, [DllImport("LinuxXR")]).
// All strings are UTF-8; buffers are provided by the caller.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "IUnityInterface.h"
#include "core/log.h"
#include "core/runtime_select.h"
#include "core/xr_api.h"
#include "unity/provider_context.h"

using namespace lxr;
using namespace lxr::unity;

namespace {

int copyOut(const std::string& s, char* buffer, int capacity) {
  int needed = static_cast<int>(s.size()) + 1;
  if (buffer && capacity > 0) {
    int n = needed <= capacity ? needed : capacity;
    std::memcpy(buffer, s.c_str(), n - 1);
    buffer[n - 1] = '\0';
  }
  return needed;
}

std::string runtimeSetting() {
  auto& c = context();
  std::lock_guard<std::mutex> l(c.settingsMutex);
  return c.settings.runtime;
}

}  // namespace

// Mirrored in C#: LinuxXRNative.Status (StructLayout.Sequential)
struct LinuxXRStatus {
  int preinitRan;            // 1 = PreInit ran at Editor startup
  int preinitRuntimeQueried; // 1 = Vulkan list came from the runtime itself
  int displayState;          // 0 off, 1 init, 2 running
  int inputState;
  int sessionState;          // XrSessionState (0 = no session)
  int runtimeReachable;      // 1 = headset connected / service running
  float refreshRate;
  unsigned long long framesSubmitted;
};

// Mirrored in C#: LinuxXRNative.Stats (StructLayout.Sequential). First field = size in bytes, so that
// C# and an older/newer libLinuxXR.so never read/write past the end of the struct.
// Only append new fields at the END.
struct LinuxXRStats {
  int structSize;
  int sessionActive;          // 1 = OpenXR session exists
  unsigned long long framesBegun;
  unsigned long long framesRendered;
  unsigned long long framesEmpty;
  unsigned long long framesNoRender;
  unsigned long long framesMissed;
  unsigned long long framesDropped;
  unsigned long long logWarnings;
  unsigned long long logErrors;
  double sessionSeconds;
  float headsetFps;
  float appFps;
  float waitFrameMs;
  float submitMs;
  float displayPeriodMs;
  float renderScale;
  int eyeWidth;
  int eyeHeight;
  int singlePass;
  int vulkanChecked;          // 1 = extension check ran (at session start)
  int vulkanComplete;         // 1 = all required extensions present
  long long swapchainFormat;  // VkFormat
};

extern "C" {

UNITY_INTERFACE_EXPORT const char* LinuxXR_GetVersion() { return LINUXXR_VERSION; }

UNITY_INTERFACE_EXPORT void LinuxXR_GetStatus(LinuxXRStatus* out) {
  if (!out) return;
  auto& c = context();
  out->preinitRan = c.preinitRan ? 1 : 0;
  out->preinitRuntimeQueried = c.preinitRuntimeQueried ? 1 : 0;
  out->displayState = c.displayState;
  out->inputState = c.inputState;
  out->sessionState = c.xr.hasSession() ? static_cast<int>(c.xr.sessionState()) : 0;
  RuntimeCandidate rt = selectRuntime(runtimeSetting());
  out->runtimeReachable = (!rt.manifestPath.empty() && isRuntimeReachable(rt)) ? 1 : 0;
  out->refreshRate = c.xr.hasSession() ? c.xr.currentRefreshRate() : 0.0f;
  out->framesSubmitted = c.xr.framesSubmitted();
}

// Multi-line diagnostics text (runtime, GPU, extensions, last error).
UNITY_INTERFACE_EXPORT int LinuxXR_GetDiagnostics(char* buffer, int capacity) {
  auto& c = context();
  std::string s;
  RuntimeCandidate rt = selectRuntime(runtimeSetting());
  s += "Version: " LINUXXR_VERSION "\n";
  s += "Runtime setting: " + runtimeSetting() + "\n";
  s += "Runtime: " + (rt.manifestPath.empty() ? std::string("(none found)") : rt.manifestPath) + " [" +
       runtimeKindName(rt.kind) + "]\n";
  s += "Status: " + runtimeReachabilityHint(rt) + "\n";
  if (c.vkChecked) {
    std::lock_guard<std::mutex> vl(c.statusMutex);
    s += "Vulkan device: " +
         (c.vkMissingRequired.empty() ? std::string("all required extensions present")
                                      : "MISSING " + c.vkMissingRequired) +
         (c.vkMissingOptional.empty() ? std::string() : " (optional missing: " + c.vkMissingOptional + ")") + "\n";
  } else {
    s += "Vulkan device: checked when Play starts\n";
  }
  if (c.preinitRan) {
    s += "PreInit: ran (" + (c.preinitSource.empty() ? std::string("-") : c.preinitSource) + "), GPU " +
         (c.preinitGpu.empty() ? std::string("-") : c.preinitGpu) + "\n";
  }
  if (c.xr.hasInstance()) s += "OpenXR: " + c.xr.describe() + "\n";
  s += "Log file: " + unityLogFilePath() + "\n";
  s += "OpenXR loader: " + (isOpenXRLoaderLoaded() ? openXRLoaderPath() : std::string("(not loaded yet)")) + "\n";
  {
    std::lock_guard<std::mutex> l(c.statusMutex);
    if (!c.lastError.empty()) s += "Last error: " + c.lastError + "\n";
  }
  return copyOut(s, buffer, capacity);
}

UNITY_INTERFACE_EXPORT void LinuxXR_GetStats(LinuxXRStats* out, int size) {
  if (!out || size < static_cast<int>(sizeof(int))) return;
  auto& c = context();
  auto& st = c.stats;
  LinuxXRStats s{};
  s.structSize = static_cast<int>(sizeof(LinuxXRStats));
  s.sessionActive = c.sessionReady ? 1 : 0;
  s.framesBegun = st.framesBegun;
  s.framesRendered = st.framesRendered;
  s.framesEmpty = st.framesEmpty;
  s.framesNoRender = st.framesNoRender;
  s.framesMissed = st.framesMissed;
  s.framesDropped = st.framesDropped;
  s.logWarnings = logCount(LogLevel::Warning);
  s.logErrors = logCount(LogLevel::Error);
  int64_t start = st.sessionStartNs;
  if (start) {
    int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch()).count();
    s.sessionSeconds = static_cast<double>(now - start) / 1e9;
  }
  s.headsetFps = st.headsetFps;
  s.appFps = st.appFps;
  s.waitFrameMs = st.waitFrameMs;
  s.submitMs = st.submitMs;
  s.displayPeriodMs = st.displayPeriodMs;
  s.renderScale = st.renderScale;
  s.eyeWidth = static_cast<int>(st.eyeWidth.load());
  s.eyeHeight = static_cast<int>(st.eyeHeight.load());
  s.singlePass = st.singlePass;
  s.vulkanChecked = c.vkChecked;
  {
    std::lock_guard<std::mutex> vl(c.statusMutex);
    s.vulkanComplete = c.vkMissingRequired.empty() ? 1 : 0;
  }
  s.swapchainFormat = st.swapchainFormat;
  int n = size < s.structSize ? size : s.structSize;
  std::memcpy(out, &s, static_cast<size_t>(n));
  out->structSize = s.structSize;
}

// ---- Log forwarding to the Unity console ------------------------------------------

// Next message from the console queue. Returns the required buffer size incl. '\0',
// 0 = no message. If the buffer is too small the message is NOT kept (it is truncated).
// level: 0 debug, 1 info, 2 warning, 3 error.
UNITY_INTERFACE_EXPORT int LinuxXR_PopLog(int* level, char* buffer, int capacity) {
  LogEntry e;
  if (!logPopQueued(e)) return 0;
  if (level) *level = static_cast<int>(e.level);
  return copyOut(e.text, buffer, capacity);
}

// 1 = C# forwards the queue to the console -> skip the Unity trace (Editor.log only),
// otherwise every message would appear twice in Editor.log.
UNITY_INTERFACE_EXPORT void LinuxXR_SetConsoleForwarding(int enabled) { setLogSinkSuppressed(enabled != 0); }

// Last maxLines messages as text ("HH:MM:SS.mmm [level] text"), oldest first.
UNITY_INTERFACE_EXPORT int LinuxXR_GetLogHistory(int maxLines, char* buffer, int capacity) {
  std::string s;
  for (const auto& e : logHistory(maxLines > 0 ? static_cast<size_t>(maxLines) : kLogHistorySize)) {
    s += e.time + " [" + logLevelName(e.level) + "] " + e.text + "\n";
  }
  return copyOut(s, buffer, capacity);
}

// List of runtime candidates, one per line: "<kind>\t<source>\t<path>\t<reachable>"
UNITY_INTERFACE_EXPORT int LinuxXR_ListRuntimes(char* buffer, int capacity) {
  std::string s;
  for (auto& c : listRuntimeCandidates("auto")) {
    s += std::string(runtimeKindName(c.kind)) + "\t" + c.source + "\t" + c.manifestPath + "\t" +
         (isRuntimeReachable(c) ? "1" : "0") + "\n";
  }
  return copyOut(s, buffer, capacity);
}

// Set settings at runtime (take effect from the next session, i.e. the next Play).
UNITY_INTERFACE_EXPORT void LinuxXR_SetSettings(const char* runtime, int originFloor, int singlePass, int mirrorMode,
                                                float renderScale, int verboseLog) {
  auto& c = context();
  std::lock_guard<std::mutex> l(c.settingsMutex);
  if (runtime && *runtime) c.settings.runtime = runtime;
  c.settings.origin = originFloor ? TrackingOrigin::Floor : TrackingOrigin::Device;
  c.settings.singlePass = singlePass != 0;
  c.settings.mirrorMode = mirrorMode;
  c.settings.renderScale = (renderScale >= 0.25f && renderScale <= 2.0f) ? renderScale : 1.0f;
  c.settings.verboseLog = verboseLog != 0;
  setLogLevel(c.settings.verboseLog ? LogLevel::Debug : LogLevel::Info);
}

}  // extern "C"
