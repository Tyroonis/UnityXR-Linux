// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#include "core/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <strings.h>
#include <sys/time.h>

namespace lxr {
namespace {

std::mutex g_mutex;
LogSink g_sink = nullptr;
void* g_sinkUser = nullptr;
FILE* g_file = nullptr;

LogLevel initialLevel() {
  const char* env = std::getenv("LINUXXR_LOG");
  if (env) {
    if (strcasecmp(env, "debug") == 0) return LogLevel::Debug;
    if (strcasecmp(env, "warning") == 0 || strcasecmp(env, "warn") == 0) return LogLevel::Warning;
    if (strcasecmp(env, "error") == 0) return LogLevel::Error;
  }
  return LogLevel::Info;
}

LogLevel g_level = initialLevel();

std::deque<LogEntry> g_history;
std::deque<LogEntry> g_queue;
uint64_t g_seq = 0;
uint64_t g_dropped = 0;
uint64_t g_counts[4] = {0, 0, 0, 0};
bool g_sinkSuppressed = false;

std::string timestamp(bool millis) {
  timeval tv{};
  ::gettimeofday(&tv, nullptr);
  std::time_t secs = tv.tv_sec;
  std::tm tm{};
  ::localtime_r(&secs, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
  if (!millis) return buf;
  char out[40];
  std::snprintf(out, sizeof(out), "%s.%03d", buf, static_cast<int>(tv.tv_usec / 1000));
  return out;
}

}  // namespace

const char* logLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info";
    case LogLevel::Warning: return "warn";
    case LogLevel::Error: return "error";
  }
  return "?";
}

void setLogSink(LogSink sink, void* user) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_sink = sink;
  g_sinkUser = user;
}

void setLogLevel(LogLevel level) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_level = level;
}

LogLevel logLevel() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_level;
}

void setLogFile(const char* path) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_file) {
    std::fclose(g_file);
    g_file = nullptr;
  }
  if (path && *path) {
    g_file = std::fopen(path, "a");
  }
}

void vlogf(LogLevel level, const char* fmt, va_list args) {
  char buffer[2048];
  std::vsnprintf(buffer, sizeof(buffer), fmt, args);

  std::lock_guard<std::mutex> lock(g_mutex);
  if (level < g_level) return;

  const std::string ts = timestamp(true);
  if (g_file) {
    std::fprintf(g_file, "%s [%s] %s\n", ts.c_str(), logLevelName(level), buffer);
    std::fflush(g_file);
  }

  LogEntry e{++g_seq, level, ts, buffer};
  g_counts[static_cast<int>(level)]++;
  g_history.push_back(e);
  if (g_history.size() > kLogHistorySize) g_history.pop_front();
  g_queue.push_back(std::move(e));
  if (g_queue.size() > kLogQueueSize) {
    g_queue.pop_front();
    g_dropped++;
  }

  if (g_sink) {
    if (g_sinkSuppressed) return;
    g_sink(level, buffer, g_sinkUser);
  } else {
    std::fprintf(stderr, "[LinuxXR] %-5s %s\n", logLevelName(level), buffer);
  }
}

bool logPopQueued(LogEntry& out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_queue.empty()) return false;
  out = std::move(g_queue.front());
  g_queue.pop_front();
  return true;
}

uint64_t logQueueDropped() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_dropped;
}

std::vector<LogEntry> logHistory(size_t maxEntries) {
  std::lock_guard<std::mutex> lock(g_mutex);
  size_t n = g_history.size() < maxEntries ? g_history.size() : maxEntries;
  return std::vector<LogEntry>(g_history.end() - static_cast<std::ptrdiff_t>(n), g_history.end());
}

uint64_t logCount(LogLevel level) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_counts[static_cast<int>(level)];
}

void setLogSinkSuppressed(bool suppressed) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_sinkSuppressed = suppressed;
}

bool logSinkSuppressed() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_sinkSuppressed;
}

void logf(LogLevel level, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlogf(level, fmt, args);
  va_end(args);
}

}  // namespace lxr
