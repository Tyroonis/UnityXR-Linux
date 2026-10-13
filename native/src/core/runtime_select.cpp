// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#include "core/runtime_select.h"

#include <glob.h>
#include <stdlib.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>

#include "core/log.h"

namespace lxr {
namespace {

std::string envOr(const char* name, const std::string& fallback = {}) {
  const char* v = std::getenv(name);
  return (v && *v) ? std::string(v) : fallback;
}

// Is the Unix socket really reachable? (The file alone is not enough: it stays behind after a crash)
bool socketAcceptsUncached(const std::string& path) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0 || !S_ISSOCK(st.st_mode)) return false;
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (path.size() >= sizeof(addr.sun_path)) return true;  // path too long to check -> existence is enough
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
  // NON-blocking: WiVRn does not accept connections without a connected headset; a blocking
  // connect() then hangs once the backlog is full (which would freeze the Unity Editor).
  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0) return false;
  bool ok = false;
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
    ok = true;
  } else if (errno == EINPROGRESS) {
    pollfd p{fd, POLLOUT, 0};
    if (::poll(&p, 1, 50) == 1 && (p.revents & POLLOUT)) {
      int err = 0;
      socklen_t len = sizeof(err);
      ok = ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
    }
  }
  // EAGAIN = backlog full = server is not accepting anything right now -> not reachable
  ::close(fd);
  return ok;
}

// Cache the result for 2 s: the status window asks on every repaint, and the runtime should not
// be flooded with connections.
bool socketAccepts(const std::string& path) {
  struct Entry {
    std::chrono::steady_clock::time_point when;
    bool ok;
  };
  static std::mutex m;
  static std::map<std::string, Entry> cache;
  auto now = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> l(m);
    auto it = cache.find(path);
    if (it != cache.end() && now - it->second.when < std::chrono::seconds(2)) return it->second.ok;
  }
  bool ok = socketAcceptsUncached(path);
  std::lock_guard<std::mutex> l(m);
  cache[path] = {now, ok};
  return ok;
}

bool realExists(const std::string& path) {
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0;  // follows symlinks
}

std::vector<std::string> realGlob(const std::string& pattern) {
  std::vector<std::string> out;
  glob_t g{};
  if (::glob(pattern.c_str(), 0, nullptr, &g) == 0) {
    for (size_t i = 0; i < g.gl_pathc; ++i) out.emplace_back(g.gl_pathv[i]);
  }
  globfree(&g);
  std::sort(out.begin(), out.end());
  return out;
}

bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

}  // namespace

RuntimeEnvironment RuntimeEnvironment::fromProcess() {
  RuntimeEnvironment e;
  e.home = envOr("HOME");
  e.xdgConfigHome = envOr("XDG_CONFIG_HOME");
  e.xdgRuntimeDir = envOr("XDG_RUNTIME_DIR");
  e.xdgDataHome = envOr("XDG_DATA_HOME");
  e.xrRuntimeJsonEnv = envOr("XR_RUNTIME_JSON");
  e.linuxxrRuntime = envOr("LINUXXR_RUNTIME");
  return e;
}

RuntimeKind guessRuntimeKind(const std::string& manifestPath) {
  std::string p = lower(manifestPath);
  if (p.find("wivrn") != std::string::npos) return RuntimeKind::WiVRn;
  if (p.find("monado") != std::string::npos) return RuntimeKind::Monado;
  if (p.find("steamvr") != std::string::npos) return RuntimeKind::SteamVR;
  if (p.empty()) return RuntimeKind::Unknown;
  return RuntimeKind::Other;
}

const char* runtimeKindName(RuntimeKind kind) {
  switch (kind) {
    case RuntimeKind::WiVRn: return "WiVRn";
    case RuntimeKind::Monado: return "Monado";
    case RuntimeKind::SteamVR: return "SteamVR";
    case RuntimeKind::Other: return "other";
    default: return "unknown";
  }
}

std::vector<RuntimeCandidate> listRuntimeCandidates(const RuntimeEnvironment& env, const std::string& preferenceIn,
                                                    const FileExistsFn& exists, const GlobFn& glob) {
  std::vector<RuntimeCandidate> out;
  auto add = [&](const std::string& path, const std::string& source) {
    if (path.empty() || !exists(path)) return;
    for (auto& c : out)
      if (c.manifestPath == path) return;
    // Symlinks (active_runtime.json -> openxr_wivrn.json) so the kind is detected correctly.
    std::string kindPath = path;
    if (char* rp = ::realpath(path.c_str(), nullptr)) {
      kindPath = rp;
      std::free(rp);
    }
    RuntimeKind kind = guessRuntimeKind(kindPath);
    if (kind == RuntimeKind::Other || kind == RuntimeKind::Unknown) kind = guessRuntimeKind(path);
    out.push_back({path, source, kind});
  };
  auto addGlob = [&](const std::string& pattern, const std::string& source) {
    for (auto& p : glob(pattern)) add(p, source);
  };

  const std::string configHome = env.xdgConfigHome.empty() ? env.home + "/.config" : env.xdgConfigHome;
  const std::string dataHome = env.xdgDataHome.empty() ? env.home + "/.local/share" : env.xdgDataHome;

  auto addWiVRn = [&] {
    addGlob("/usr/share/openxr/1/openxr_wivrn*.json", "wivrn");
    addGlob("/usr/local/share/openxr/1/openxr_wivrn*.json", "wivrn");
    add(dataHome + "/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn.json",
        "wivrn-flatpak-user");
    // Inside a Flatpak sandbox (e.g. the Unity Hub Flatpak) XDG_DATA_HOME points into ~/.var/app/... - also try the real paths:
    add(env.home + "/.local/share/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn.json",
        "wivrn-flatpak-user");
    add("/var/lib/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn.json",
        "wivrn-flatpak");
  };
  auto addMonado = [&] {
    add("/usr/share/openxr/1/openxr_monado.json", "monado");
    add("/usr/local/share/openxr/1/openxr_monado.json", "monado");
  };
  auto addActive = [&] {
    add(configHome + "/openxr/1/active_runtime.json", "active-user");
    add(env.home + "/.config/openxr/1/active_runtime.json", "active-user");
    add("/etc/xdg/openxr/1/active_runtime.json", "active-system");
  };

  // Preference: explicit setting > LINUXXR_RUNTIME > "auto"
  // "auto" or empty -> LINUXXR_RUNTIME (if set) wins.
  std::string preference = preferenceIn;
  if ((preference.empty() || lower(preference) == "auto") && !env.linuxxrRuntime.empty()) preference = env.linuxxrRuntime;
  if (preference.empty()) preference = "auto";
  const std::string pref = lower(preference);

  if (endsWith(pref, ".json")) {
    add(preference, "explicit");
    if (out.empty()) LXR_WARN("Runtime manifest '%s' does not exist", preference.c_str());
  }

  if (pref == "wivrn") {
    addWiVRn();
  } else if (pref == "monado") {
    addMonado();
  } else if (pref == "active") {
    addActive();
  }

  // Respect an already set XR_RUNTIME_JSON (e.g. from the launch script).
  if (!env.xrRuntimeJsonEnv.empty()) add(env.xrRuntimeJsonEnv, "env");

  // Fallback order for "auto" (and as the remainder for other preferences):
  addWiVRn();
  addActive();
  addMonado();
  return out;
}

std::vector<RuntimeCandidate> listRuntimeCandidates(const std::string& preference) {
  return listRuntimeCandidates(RuntimeEnvironment::fromProcess(), preference, realExists, realGlob);
}

RuntimeCandidate selectRuntime(const std::string& preference) {
  auto list = listRuntimeCandidates(preference);
  if (list.empty()) return {};
  return list.front();
}

void applyRuntime(const RuntimeCandidate& runtime) {
  if (runtime.manifestPath.empty()) return;
  ::setenv("XR_RUNTIME_JSON", runtime.manifestPath.c_str(), 1);
  LXR_DEBUG("XR_RUNTIME_JSON=%s (%s)", runtime.manifestPath.c_str(), runtime.source.c_str());
}

bool isRuntimeReachable(const RuntimeCandidate& runtime, const RuntimeEnvironment& env, const FileExistsFn& exists) {
  if (env.xdgRuntimeDir.empty()) return true;  // cannot check
  switch (runtime.kind) {
    case RuntimeKind::WiVRn: return exists(env.xdgRuntimeDir + "/wivrn/comp_ipc");
    case RuntimeKind::Monado: return exists(env.xdgRuntimeDir + "/monado_comp_ipc");
    default: return true;
  }
}

bool isRuntimeReachable(const RuntimeCandidate& runtime) {
  return isRuntimeReachable(runtime, RuntimeEnvironment::fromProcess(), socketAccepts);
}

bool insideFlatpak() { return realExists("/.flatpak-info"); }

std::string runtimeReachabilityHint(const RuntimeCandidate& runtime) {
  const std::string sandbox = insideFlatpak()
      ? " Note: this process is running inside a Flatpak sandbox (Unity Hub as Flatpak) - "
        "run ./scripts/setup-unity-flatpak.sh once and restart the Hub."
      : "";
  if (runtime.manifestPath.empty()) return "No OpenXR runtime found (install WiVRn)." + sandbox;
  bool ok = isRuntimeReachable(runtime);
  switch (runtime.kind) {
    case RuntimeKind::WiVRn:
      return ok ? "WiVRn: headset connected."
                : "WiVRn: no headset connected (open the WiVRn app on the Quest)." + sandbox;
    case RuntimeKind::Monado:
      return ok ? "Monado: service running." : "Monado: monado-service is not running.";
    default:
      return std::string(runtimeKindName(runtime.kind)) + ": reachability unknown.";
  }
}

}  // namespace lxr
