// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#pragma once
// OpenXR runtime selection (WiVRn, Monado, active runtime, explicit path).
// The result is passed to the Khronos loader via XR_RUNTIME_JSON.

#include <functional>
#include <string>
#include <vector>

namespace lxr {

enum class RuntimeKind { Unknown, WiVRn, Monado, SteamVR, Other };

struct RuntimeCandidate {
  std::string manifestPath;   // path to the JSON manifest file
  std::string source;         // origin: "explicit", "env", "wivrn", "monado", "active-user", ...
  RuntimeKind kind = RuntimeKind::Unknown;
};

struct RuntimeEnvironment {
  std::string home;             // $HOME
  std::string xdgConfigHome;    // $XDG_CONFIG_HOME (empty -> $HOME/.config)
  std::string xdgRuntimeDir;    // $XDG_RUNTIME_DIR
  std::string xdgDataHome;      // $XDG_DATA_HOME (empty -> $HOME/.local/share)
  std::string xrRuntimeJsonEnv; // XR_RUNTIME_JSON if already set
  std::string linuxxrRuntime;   // LINUXXR_RUNTIME (path or "wivrn"/"monado"/"active"/"auto")

  static RuntimeEnvironment fromProcess();
};

using FileExistsFn = std::function<bool(const std::string&)>;
using GlobFn = std::function<std::vector<std::string>(const std::string& pattern)>;

// All candidates in priority order (existing files only).
// preference: "auto" | "wivrn" | "monado" | "active" | path to a JSON file.
std::vector<RuntimeCandidate> listRuntimeCandidates(const RuntimeEnvironment& env, const std::string& preference,
                                                    const FileExistsFn& exists, const GlobFn& glob);

// Convenience variant using the real file system.
std::vector<RuntimeCandidate> listRuntimeCandidates(const std::string& preference);

// Pick the first candidate; empty if none was found.
RuntimeCandidate selectRuntime(const std::string& preference);

// Set XR_RUNTIME_JSON (call before xrCreateInstance).
void applyRuntime(const RuntimeCandidate& runtime);

RuntimeKind guessRuntimeKind(const std::string& manifestPath);
const char* runtimeKindName(RuntimeKind kind);

// Heuristically checks whether the runtime can currently serve apps
// (WiVRn: $XDG_RUNTIME_DIR/wivrn/comp_ipc accepts connections = headset connected;
//  Monado: $XDG_RUNTIME_DIR/monado_comp_ipc). Unknown runtimes -> true.
bool isRuntimeReachable(const RuntimeCandidate& runtime, const RuntimeEnvironment& env, const FileExistsFn& exists);
bool isRuntimeReachable(const RuntimeCandidate& runtime);

// For status displays: short description of why it is (not) reachable.
std::string runtimeReachabilityHint(const RuntimeCandidate& runtime);

// Is the process running inside a Flatpak sandbox (/.flatpak-info exists)?
bool insideFlatpak();

}  // namespace lxr
