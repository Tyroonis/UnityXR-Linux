// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#include "core/log.h"
#include "unity/provider_context.h"

namespace lxr::unity {

ProviderContext& context() {
  static ProviderContext ctx;
  return ctx;
}

namespace {

void unitySink(LogLevel level, const char* message, void* user) {
  auto* trace = static_cast<IUnityXRTrace*>(user);
  XRLogType type = kXRLogTypeLog;
  switch (level) {
    case LogLevel::Debug: type = kXRLogTypeDebug; break;
    case LogLevel::Info: type = kXRLogTypeLog; break;
    case LogLevel::Warning: type = kXRLogTypeWarning; break;
    case LogLevel::Error: type = kXRLogTypeError; break;
  }
  if (trace && trace->Trace) {
    trace->Trace(type, "[LinuxXR] %s\n", message);
  }
}

std::string logFilePath() {
  const char* cache = std::getenv("XDG_CACHE_HOME");
  const char* home = std::getenv("HOME");
  std::string base = (cache && *cache) ? cache : std::string(home ? home : "/tmp") + "/.cache";
  std::string dir = base + "/linuxxr";
  ::mkdir(base.c_str(), 0755);
  ::mkdir(dir.c_str(), 0755);
  return dir + "/linuxxr.log";
}

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  return s.substr(i);
}

}  // namespace

std::string unityLogFilePath() { return logFilePath(); }

void installUnityLogSink(IUnityXRTrace* trace) {
  static bool fileSet = false;
  if (!fileSet) {
    setLogFile(logFilePath().c_str());
    fileSet = true;
  }
  if (trace) setLogSink(unitySink, trace);
}

void loadSettingsFile(PluginSettings& out) {
  std::ifstream f("Library/LinuxXR/settings.txt");
  if (!f) return;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
    if (key == "runtime" && !value.empty()) {
      out.runtime = value;
    } else if (key == "origin") {
      out.origin = value == "device" ? TrackingOrigin::Device : TrackingOrigin::Floor;
    } else if (key == "singlePass") {
      out.singlePass = value != "0" && value != "false";
    } else if (key == "mirrorMode") {
      out.mirrorMode = std::atoi(value.c_str());
    } else if (key == "renderScale") {
      out.renderScale = static_cast<float>(std::atof(value.c_str()));
      if (out.renderScale < 0.25f || out.renderScale > 2.0f) out.renderScale = 1.0f;
    } else if (key == "verboseLog") {
      out.verboseLog = value == "1" || value == "true";
    }
  }
}

}  // namespace lxr::unity
