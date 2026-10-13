// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#pragma once
// Tiny test framework without external dependencies.
//
//   CHECK(cond)            - check a condition
//   CHECK_EQ(a, b)         - check equality, prints both values
//   CHECK_NEAR(a, b, eps)  - floating-point comparison with tolerance
//   lxr_test::summary()    - print the result, return value = process exit code
//
// A failed check does NOT abort the test (so all failures show up at once);
// the exit code is non-zero as soon as at least one check has failed.

#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace lxr_test {

inline int& checkCount() {
  static int n = 0;
  return n;
}
inline int& failCount() {
  static int n = 0;
  return n;
}

// --- Render values as text (for the failure message) --------------------------------------

inline std::string show(const std::string& s) { return "\"" + s + "\""; }
inline std::string show(const char* s) { return s ? "\"" + std::string(s) + "\"" : "(null)"; }
inline std::string show(bool b) { return b ? "true" : "false"; }
inline std::string show(std::nullptr_t) { return "nullptr"; }
inline std::string show(const std::vector<std::string>& v) {
  std::string out = "[";
  for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + show(v[i]);
  return out + "]";
}
template <class T>
std::string show(const T& v) {
  if constexpr (std::is_enum_v<T>) {
    return std::to_string(static_cast<long long>(v));
  } else if constexpr (std::is_floating_point_v<T>) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(v));
    return buf;
  } else if constexpr (std::is_integral_v<T>) {
    if constexpr (std::is_signed_v<T>) return std::to_string(static_cast<long long>(v));
    else return std::to_string(static_cast<unsigned long long>(v));
  } else if constexpr (std::is_pointer_v<T>) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%p", static_cast<const void*>(v));
    return buf;
  } else {
    return "<?>";
  }
}

inline void reportFail(const char* file, int line, const std::string& what) {
  ++failCount();
  std::fprintf(stderr, "%s:%d: FAIL: %s\n", file, line, what.c_str());
}

inline bool check(bool ok, const char* file, int line, const char* expr) {
  ++checkCount();
  if (!ok) reportFail(file, line, std::string("CHECK(") + expr + ")");
  return ok;
}

template <class A, class B>
bool checkEq(const A& a, const B& b, const char* file, int line, const char* ea, const char* eb) {
  ++checkCount();
  const bool ok = (a == b);
  if (!ok) {
    reportFail(file, line,
               std::string("CHECK_EQ(") + ea + ", " + eb + ")\n    left:   " + show(a) + "\n    right:  " + show(b));
  }
  return ok;
}

template <class A, class B, class E>
bool checkNear(const A& a, const B& b, const E& eps, const char* file, int line, const char* ea, const char* eb,
               const char* eeps) {
  ++checkCount();
  const double da = static_cast<double>(a), db = static_cast<double>(b);
  const bool ok = std::fabs(da - db) <= static_cast<double>(eps);  // NaN -> false
  if (!ok) {
    reportFail(file, line,
               std::string("CHECK_NEAR(") + ea + ", " + eb + ")\n    left:   " + show(da) + "\n    right:  " + show(db) +
                   "\n    eps:    " + show(static_cast<double>(eps)) + " (" + eeps + ")");
  }
  return ok;
}

// Prints the summary; the return value can be used directly as main()'s exit code.
inline int summary(const char* name) {
  std::printf("%s: %d checks, %d failed\n", name, checkCount(), failCount());
  return failCount() == 0 ? 0 : 1;
}

}  // namespace lxr_test

#define CHECK(cond) ::lxr_test::check(static_cast<bool>(cond), __FILE__, __LINE__, #cond)
#define CHECK_EQ(a, b) ::lxr_test::checkEq((a), (b), __FILE__, __LINE__, #a, #b)
#define CHECK_NEAR(a, b, eps) ::lxr_test::checkNear((a), (b), (eps), __FILE__, __LINE__, #a, #b, #eps)
