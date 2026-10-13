// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#pragma once
// Simple logging with a replaceable sink (stderr, Unity trace, file).

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lxr {

enum class LogLevel { Debug = 0, Info = 1, Warning = 2, Error = 3 };

using LogSink = void (*)(LogLevel level, const char* message, void* user);

// Set the sink (nullptr = stderr). Thread-safe.
void setLogSink(LogSink sink, void* user);
// Minimum level that is emitted (default Info, LINUXXR_LOG=debug sets Debug).
void setLogLevel(LogLevel level);
LogLevel logLevel();

// Additional log file (append). nullptr/"" disables it.
void setLogFile(const char* path);

void logf(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void vlogf(LogLevel level, const char* fmt, va_list args);

const char* logLevelName(LogLevel level);

// ---- Buffered messages (for the Unity console and diagnostic reports) -------------------
// Every message that passes the level filter is also stored
//   * in a history ring (last kLogHistorySize messages, for diagnostic reports) and
//   * in a console queue (last kLogQueueSize not yet fetched messages).
// The Unity side drains the console queue on the main thread and forwards it to Debug.Log.
constexpr size_t kLogHistorySize = 1000;
constexpr size_t kLogQueueSize = 1000;

struct LogEntry {
  uint64_t seq = 0;
  LogLevel level = LogLevel::Info;
  std::string time;  // HH:MM:SS.mmm
  std::string text;
};

// Oldest not yet fetched message from the console queue. false = queue empty.
bool logPopQueued(LogEntry& out);
// Number of messages dropped from the console queue because nobody fetched them.
uint64_t logQueueDropped();
// Last maxEntries messages, oldest first.
std::vector<LogEntry> logHistory(size_t maxEntries = kLogHistorySize);
// Count of warnings / errors since start.
uint64_t logCount(LogLevel level);
// When true the sink set via setLogSink is skipped (messages only go to file, history and queue).
// The Unity plugin uses this while C# forwards the queue to the console - avoids duplicates.
void setLogSinkSuppressed(bool suppressed);
bool logSinkSuppressed();

}  // namespace lxr

#define LXR_DEBUG(...) ::lxr::logf(::lxr::LogLevel::Debug, __VA_ARGS__)
#define LXR_INFO(...) ::lxr::logf(::lxr::LogLevel::Info, __VA_ARGS__)
#define LXR_WARN(...) ::lxr::logf(::lxr::LogLevel::Warning, __VA_ARGS__)
#define LXR_ERROR(...) ::lxr::logf(::lxr::LogLevel::Error, __VA_ARGS__)
