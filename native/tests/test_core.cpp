// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
// Unit tests for linuxxr_core: vk_requirements, runtime_select, xr_math, vkChooseDefaultDevice
// (+ VulkanPreInit without a runtime). Runs without GPU, without an OpenXR runtime and without network.

#include <stdlib.h>
#include <unistd.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "core/log.h"
#include "core/runtime_select.h"
#include "core/vk_api.h"
#include "core/vk_preinit.h"
#include "core/vk_requirements.h"
#include "core/xr_math.h"

using namespace lxr;
using V = std::vector<std::string>;

namespace fs = std::filesystem;

// Collect log output of the functions under test (warnings on deliberately wrong input)
// instead of printing it to stderr.
static std::vector<std::string> g_logLines;
static void collectLog(LogLevel level, const char* msg, void*) {
  g_logLines.push_back(std::string(logLevelName(level)) + ": " + msg);
}

// ============================================================================================
// vk_requirements
// ============================================================================================

static void testExtensionLists() {
  // splitExtensions: multiple spaces, tabs, line breaks, duplicates
  CHECK_EQ(splitExtensions(""), V{});
  CHECK_EQ(splitExtensions("   \t\n "), V{});
  CHECK_EQ(splitExtensions("VK_A"), (V{"VK_A"}));
  CHECK_EQ(splitExtensions("  VK_A   VK_B\tVK_C\n"), (V{"VK_A", "VK_B", "VK_C"}));
  CHECK_EQ(splitExtensions("VK_A VK_B VK_A  VK_B VK_C VK_A"), (V{"VK_A", "VK_B", "VK_C"}));

  // joinExtensions
  CHECK_EQ(joinExtensions({}), std::string());
  CHECK_EQ(joinExtensions({"VK_A"}), std::string("VK_A"));
  CHECK_EQ(joinExtensions({"VK_A", "VK_B", "VK_C"}), std::string("VK_A VK_B VK_C"));
  // join o split is stable
  CHECK_EQ(joinExtensions(splitExtensions("  X   Y  X Z ")), std::string("X Y Z"));

  // containsExtension
  CHECK(containsExtension({"VK_A", "VK_B"}, "VK_B"));
  CHECK(!containsExtension({"VK_A", "VK_B"}, "VK_C"));
  CHECK(!containsExtension({}, "VK_A"));
  CHECK(!containsExtension({"VK_AB"}, "VK_A"));  // no substring match

  // mergeUnique: order a before b, duplicates (also within one list) removed
  CHECK_EQ(mergeUnique({"A", "B"}, {"C", "D"}), (V{"A", "B", "C", "D"}));
  CHECK_EQ(mergeUnique({"A", "B"}, {"B", "C", "A", "D"}), (V{"A", "B", "C", "D"}));
  CHECK_EQ(mergeUnique({"B", "A", "B"}, {"A"}), (V{"B", "A"}));
  CHECK_EQ(mergeUnique({}, {"X", "Y"}), (V{"X", "Y"}));
  CHECK_EQ(mergeUnique({"X", "Y"}, {}), (V{"X", "Y"}));
  CHECK_EQ(mergeUnique({}, {}), V{});

  // filterSupported: order of 'requested', dropped = missing ones
  {
    V dropped;
    V out = filterSupported({"A", "B", "C", "D"}, {"D", "A", "X"}, &dropped);
    CHECK_EQ(out, (V{"A", "D"}));
    CHECK_EQ(dropped, (V{"B", "C"}));
  }
  {
    V dropped{"old"};  // appended to, not overwritten
    V out = filterSupported({"A", "B"}, {"A", "B"}, &dropped);
    CHECK_EQ(out, (V{"A", "B"}));
    CHECK_EQ(dropped, (V{"old"}));
  }
  CHECK_EQ(filterSupported({"A", "B"}, {}), V{});               // without dropped pointer
  CHECK_EQ(filterSupported({}, {"A"}), V{});
  CHECK_EQ(filterSupported({"A", "A", "B"}, {"A", "B"}), (V{"A", "B"}));  // avoid duplicates in the output

  // Base lists must not be empty, no duplicates, only VK_ names without whitespace
  for (const V* list : {&fallbackInstanceExtensions(), &fallbackDeviceExtensions()}) {
    CHECK(!list->empty());
    CHECK_EQ(mergeUnique(*list, {}).size(), list->size());
    for (const auto& e : *list) {
      CHECK(e.rfind("VK_", 0) == 0);
      CHECK(e.find_first_of(" \t\n") == std::string::npos);
    }
  }
}

static void testSerialize() {
  VulkanRequirements r;
  r.runtimeManifest = "/usr/share/openxr/1/openxr_wivrn.json";
  r.instanceExtensions = {"VK_KHR_external_memory_capabilities", "VK_KHR_get_physical_device_properties2"};
  r.deviceExtensions = {"VK_KHR_external_memory", "VK_KHR_external_memory_fd", "VK_KHR_timeline_semaphore"};
  r.source = "runtime";

  const std::string text = serializeRequirements(r);
  CHECK(text.find("runtime=/usr/share/openxr/1/openxr_wivrn.json\n") != std::string::npos);
  CHECK(text.find("instance=VK_KHR_external_memory_capabilities VK_KHR_get_physical_device_properties2\n") !=
        std::string::npos);

  VulkanRequirements back;
  CHECK(parseRequirements(text, back));
  CHECK_EQ(back.runtimeManifest, r.runtimeManifest);
  CHECK_EQ(back.instanceExtensions, r.instanceExtensions);
  CHECK_EQ(back.deviceExtensions, r.deviceExtensions);

  // Empty lists survive the round trip (key present, value empty)
  VulkanRequirements empty;
  VulkanRequirements backEmpty;
  backEmpty.instanceExtensions = {"stale"};
  CHECK(parseRequirements(serializeRequirements(empty), backEmpty));
  CHECK_EQ(backEmpty.instanceExtensions, V{});
  CHECK_EQ(backEmpty.deviceExtensions, V{});
  CHECK_EQ(backEmpty.runtimeManifest, std::string());

  // Comments, blank lines, lines without '=' and unknown keys are ignored; order does not matter
  VulkanRequirements tolerant;
  CHECK(parseRequirements("# comment\n\ngarbage without equals sign\nunknown=1\ndevice=D1 D2\ninstance=I1\n", tolerant));
  CHECK_EQ(tolerant.instanceExtensions, (V{"I1"}));
  CHECK_EQ(tolerant.deviceExtensions, (V{"D1", "D2"}));

  // Missing lines -> false
  VulkanRequirements x;
  CHECK(!parseRequirements("", x));
  CHECK(!parseRequirements("# comment only\n", x));
  CHECK(!parseRequirements("runtime=/a.json\n", x));
  CHECK(!parseRequirements("runtime=/a.json\ninstance=VK_A\n", x));  // device missing
  CHECK(!parseRequirements("runtime=/a.json\ndevice=VK_A\n", x));    // instance missing
  CHECK(!parseRequirements("instance VK_A\ndevice VK_B\n", x));      // without '='
}

static void testWriteExtensionString() {
  const std::string value = "VK_KHR_a VK_KHR_b";  // 17 characters
  const uint32_t needed = static_cast<uint32_t>(value.size() + 1);

  // Phase 1: capacity == 0 -> required size incl. '\0'
  uint32_t count = 999;
  CHECK(writeExtensionString(value, 0, &count, nullptr));
  CHECK_EQ(count, needed);
  CHECK_EQ(count, static_cast<uint32_t>(std::strlen(value.c_str()) + 1));

  // capacity == 0 but a buffer passed -> buffer stays untouched
  char untouched[8];
  std::memset(untouched, 'X', sizeof untouched);
  count = 0;
  CHECK(writeExtensionString(value, 0, &count, untouched));
  CHECK_EQ(count, needed);
  CHECK_EQ(untouched[0], 'X');

  // Buffer too small -> false, countOut = required size, buffer unchanged
  char small[8];
  std::memset(small, 'X', sizeof small);
  count = 0;
  CHECK(!writeExtensionString(value, static_cast<uint32_t>(sizeof small), &count, small));
  CHECK_EQ(count, needed);
  CHECK_EQ(small[0], 'X');
  CHECK(!writeExtensionString(value, needed - 1, &count, small));  // exactly one too small

  // Exactly fitting buffer -> filled, '\0'-terminated
  std::vector<char> exact(needed, 'X');
  count = 0;
  CHECK(writeExtensionString(value, needed, &count, exact.data()));
  CHECK_EQ(count, needed);
  CHECK_EQ(std::string(exact.data()), value);
  CHECK_EQ(exact[needed - 1], '\0');

  // Larger buffer -> rest untouched, countOut = written size (not capacity)
  std::vector<char> big(needed + 10, 'X');
  count = 0;
  CHECK(writeExtensionString(value, static_cast<uint32_t>(big.size()), &count, big.data()));
  CHECK_EQ(count, needed);
  CHECK_EQ(big[needed - 1], '\0');
  CHECK_EQ(big[needed], 'X');

  // Empty string: exactly 1 byte ('\0')
  count = 7;
  CHECK(writeExtensionString("", 0, &count, nullptr));
  CHECK_EQ(count, 1u);
  char one[1] = {'X'};
  CHECK(writeExtensionString("", 1, &count, one));
  CHECK_EQ(one[0], '\0');

  // No countOut -> false (no crash)
  CHECK(!writeExtensionString(value, 0, nullptr, nullptr));
}

static std::string makeTempDir() {
  const char* base = std::getenv("TMPDIR");
  std::string tmpl = std::string(base && *base ? base : "/tmp") + "/linuxxr_test_XXXXXX";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  char* r = ::mkdtemp(buf.data());
  return r ? std::string(r) : std::string();
}

static void testCacheFiles() {
  const std::string dir = makeTempDir();
  CHECK(!dir.empty());
  if (dir.empty()) return;

  VulkanRequirements req;
  req.runtimeManifest = "/usr/share/openxr/1/openxr_monado.json";
  req.instanceExtensions = {"VK_KHR_get_physical_device_properties2"};
  req.deviceExtensions = {"VK_KHR_external_memory_fd", "VK_KHR_timeline_semaphore"};
  req.source = "runtime";

  // Several not-yet-existing subfolders must be created
  const std::string path = dir + "/a/b/linuxxr/vulkan_requirements.txt";
  CHECK(saveRequirementsCache(path, req));
  CHECK(fs::exists(path));

  VulkanRequirements loaded;
  CHECK(loadRequirementsCache(path, req.runtimeManifest, loaded));
  CHECK_EQ(loaded.source, std::string("cache"));  // source is set to "cache"
  CHECK_EQ(loaded.runtimeManifest, req.runtimeManifest);
  CHECK_EQ(loaded.instanceExtensions, req.instanceExtensions);
  CHECK_EQ(loaded.deviceExtensions, req.deviceExtensions);

  // Runtime mismatch -> false, output unchanged
  VulkanRequirements other;
  other.source = "unchanged";
  CHECK(!loadRequirementsCache(path, "/usr/share/openxr/1/openxr_wivrn.json", other));
  CHECK_EQ(other.source, std::string("unchanged"));
  CHECK(other.instanceExtensions.empty());

  // Empty runtime path -> no check, cache is accepted
  VulkanRequirements any;
  CHECK(loadRequirementsCache(path, "", any));
  CHECK_EQ(any.deviceExtensions, req.deviceExtensions);

  // Overwrite (trunc) with a different runtime
  VulkanRequirements req2 = req;
  req2.runtimeManifest = "/usr/share/openxr/1/openxr_wivrn.json";
  req2.deviceExtensions = {"VK_KHR_external_fence_fd"};
  CHECK(saveRequirementsCache(path, req2));
  CHECK(!loadRequirementsCache(path, req.runtimeManifest, other));
  CHECK(loadRequirementsCache(path, req2.runtimeManifest, other));
  CHECK_EQ(other.deviceExtensions, (V{"VK_KHR_external_fence_fd"}));

  // Error cases: file missing, file corrupt, directory cannot be created
  CHECK(!loadRequirementsCache(dir + "/does-not-exist.txt", "", other));
  {
    std::ofstream bad(dir + "/corrupt.txt");
    bad << "this is not a cache\n";
  }
  CHECK(!loadRequirementsCache(dir + "/corrupt.txt", "", other));
  {
    std::ofstream file(dir + "/is-a-file");  // file instead of folder in the path
    file << "x";
  }
  CHECK(!saveRequirementsCache(dir + "/is-a-file/sub/cache.txt", req));

  // defaultRequirementsCachePath follows XDG_CACHE_HOME or HOME
  const char* oldCache = std::getenv("XDG_CACHE_HOME");
  const std::string oldCacheCopy = oldCache ? oldCache : "";
  ::setenv("XDG_CACHE_HOME", "/tmp/xdg-cache-test", 1);
  CHECK_EQ(defaultRequirementsCachePath(), std::string("/tmp/xdg-cache-test/linuxxr/vulkan_requirements.txt"));
  ::unsetenv("XDG_CACHE_HOME");
  const char* oldHome = std::getenv("HOME");
  const std::string oldHomeCopy = oldHome ? oldHome : "";
  ::setenv("HOME", "/home/testuser", 1);
  CHECK_EQ(defaultRequirementsCachePath(), std::string("/home/testuser/.cache/linuxxr/vulkan_requirements.txt"));
  if (oldHome) ::setenv("HOME", oldHomeCopy.c_str(), 1); else ::unsetenv("HOME");
  if (oldCache) ::setenv("XDG_CACHE_HOME", oldCacheCopy.c_str(), 1);

  std::error_code ec;
  fs::remove_all(dir, ec);
}

// ============================================================================================
// runtime_select
// ============================================================================================

namespace {

// Fake file system: set of existing files + glob table (pattern -> matches).
struct FakeFs {
  std::set<std::string> files;
  std::map<std::string, std::vector<std::string>> globs;
  mutable std::vector<std::string> existsCalls;

  FileExistsFn existsFn() const {
    return [this](const std::string& p) {
      existsCalls.push_back(p);
      return files.count(p) > 0;
    };
  }
  GlobFn globFn() const {
    return [this](const std::string& pattern) {
      auto it = globs.find(pattern);
      return it == globs.end() ? std::vector<std::string>{} : it->second;
    };
  }
};

const char* kWivrnPpa = "/usr/share/openxr/1/openxr_wivrn.json";
const char* kWivrnFlatpakSys = "/var/lib/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn.json";
const char* kMonado = "/usr/share/openxr/1/openxr_monado.json";
const char* kActiveSys = "/etc/xdg/openxr/1/active_runtime.json";

RuntimeEnvironment baseEnv() {
  RuntimeEnvironment e;
  e.home = "/home/tester";
  e.xdgRuntimeDir = "/run/user/1000";
  return e;
}

std::vector<std::string> paths(const std::vector<RuntimeCandidate>& list) {
  std::vector<std::string> out;
  for (auto& c : list) out.push_back(c.manifestPath);
  return out;
}
std::vector<std::string> sources(const std::vector<RuntimeCandidate>& list) {
  std::vector<std::string> out;
  for (auto& c : list) out.push_back(c.source);
  return out;
}

}  // namespace

static void testRuntimeCandidates() {
  FakeFs fs0;  // empty file system
  const RuntimeEnvironment env = baseEnv();

  // Nothing present -> empty list, for every preference
  for (const char* pref : {"auto", "wivrn", "monado", "active", "/x/y.json", ""}) {
    CHECK(listRuntimeCandidates(env, pref, fs0.existsFn(), fs0.globFn()).empty());
  }

  // Everything present
  const std::string userActive = "/home/tester/.config/openxr/1/active_runtime.json";
  const std::string flatpakUser = "/home/tester/.local/share/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn.json";
  FakeFs all;
  all.files = {kWivrnPpa, kWivrnFlatpakSys, kMonado, kActiveSys, userActive, flatpakUser};
  all.globs["/usr/share/openxr/1/openxr_wivrn*.json"] = {kWivrnPpa};

  // "auto": WiVRn (PPA) -> WiVRn Flatpak (user, system) -> active-user -> active-system -> Monado
  {
    auto list = listRuntimeCandidates(env, "auto", all.existsFn(), all.globFn());
    CHECK_EQ(paths(list), (V{kWivrnPpa, flatpakUser, kWivrnFlatpakSys, userActive, kActiveSys, kMonado}));
    CHECK_EQ(sources(list), (V{"wivrn", "wivrn-flatpak-user", "wivrn-flatpak", "active-user", "active-system", "monado"}));
    CHECK(list.size() == 6);
    if (list.size() == 6) {
      CHECK(list[0].kind == RuntimeKind::WiVRn);
      CHECK(list[1].kind == RuntimeKind::WiVRn);
      CHECK(list[3].kind == RuntimeKind::Other);  // active_runtime.json does not reveal the kind
      CHECK(list[5].kind == RuntimeKind::Monado);
    }
    // Empty preference behaves like "auto"
    CHECK_EQ(paths(listRuntimeCandidates(env, "", all.existsFn(), all.globFn())), paths(list));
    // Case of the preference does not matter
    CHECK_EQ(paths(listRuntimeCandidates(env, "AUTO", all.existsFn(), all.globFn())), paths(list));
  }

  // PPA path before active-user before Monado when only these three exist
  {
    FakeFs three;
    three.files = {kWivrnPpa, userActive, kMonado};
    three.globs["/usr/share/openxr/1/openxr_wivrn*.json"] = {kWivrnPpa};
    CHECK_EQ(paths(listRuntimeCandidates(env, "auto", three.existsFn(), three.globFn())),
             (V{kWivrnPpa, userActive, kMonado}));
  }

  // Without a glob match the file is NOT found via the fixed PPA path (the glob is the source)
  {
    FakeFs noGlob;
    noGlob.files = {kWivrnPpa, kMonado};
    CHECK_EQ(paths(listRuntimeCandidates(env, "auto", noGlob.existsFn(), noGlob.globFn())), (V{kMonado}));
  }

  // Multiple glob matches (e.g. openxr_wivrn_dev.json) keep glob order
  {
    FakeFs multi;
    const std::string dev = "/usr/share/openxr/1/openxr_wivrn_dev.json";
    multi.files = {kWivrnPpa, dev, "/usr/local/share/openxr/1/openxr_wivrn.json"};
    multi.globs["/usr/share/openxr/1/openxr_wivrn*.json"] = {kWivrnPpa, dev};
    multi.globs["/usr/local/share/openxr/1/openxr_wivrn*.json"] = {"/usr/local/share/openxr/1/openxr_wivrn.json"};
    CHECK_EQ(paths(listRuntimeCandidates(env, "auto", multi.existsFn(), multi.globFn())),
             (V{kWivrnPpa, dev, "/usr/local/share/openxr/1/openxr_wivrn.json"}));
  }

  // Glob returns paths that (no longer) exist -> missing from the output
  {
    FakeFs ghost;
    ghost.globs["/usr/share/openxr/1/openxr_wivrn*.json"] = {kWivrnPpa};  // files stays empty
    CHECK(listRuntimeCandidates(env, "auto", ghost.existsFn(), ghost.globFn()).empty());
  }

  // Flatpak paths individually
  {
    FakeFs sys;
    sys.files = {kWivrnFlatpakSys};
    auto list = listRuntimeCandidates(env, "auto", sys.existsFn(), sys.globFn());
    CHECK_EQ(paths(list), (V{kWivrnFlatpakSys}));
    CHECK_EQ(sources(list), (V{"wivrn-flatpak"}));
    CHECK(!list.empty() && list[0].kind == RuntimeKind::WiVRn);

    FakeFs usr;
    usr.files = {flatpakUser};  // $HOME/.local/share because XDG_DATA_HOME is empty
    list = listRuntimeCandidates(env, "wivrn", usr.existsFn(), usr.globFn());
    CHECK_EQ(paths(list), (V{flatpakUser}));
    CHECK_EQ(sources(list), (V{"wivrn-flatpak-user"}));

    // $XDG_DATA_HOME takes precedence, $HOME/.local/share follows (the Flatpak sandbox redirects XDG_DATA_HOME)
    RuntimeEnvironment e2 = env;
    e2.xdgDataHome = "/data/xdg";
    const std::string viaXdg = "/data/xdg/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn.json";
    FakeFs both;
    both.files = {flatpakUser, viaXdg};
    CHECK_EQ(paths(listRuntimeCandidates(e2, "auto", both.existsFn(), both.globFn())), (V{viaXdg, flatpakUser}));
  }

  // active-user: $XDG_CONFIG_HOME first, then $HOME/.config
  {
    RuntimeEnvironment e2 = env;
    e2.xdgConfigHome = "/cfg";
    FakeFs f;
    f.files = {"/cfg/openxr/1/active_runtime.json", userActive};
    CHECK_EQ(paths(listRuntimeCandidates(e2, "active", f.existsFn(), f.globFn())),
             (V{"/cfg/openxr/1/active_runtime.json", userActive}));
  }

  // Preference "monado" moves Monado to the front; the rest follows in default order
  {
    auto list = listRuntimeCandidates(env, "monado", all.existsFn(), all.globFn());
    CHECK_EQ(paths(list), (V{kMonado, kWivrnPpa, flatpakUser, kWivrnFlatpakSys, userActive, kActiveSys}));
    CHECK_EQ(sources(list).front(), std::string("monado"));
  }
  // Preference "wivrn" (explicit, as with auto) and "active"
  {
    auto list = listRuntimeCandidates(env, "wivrn", all.existsFn(), all.globFn());
    CHECK_EQ(paths(list).front(), std::string(kWivrnPpa));
    list = listRuntimeCandidates(env, "active", all.existsFn(), all.globFn());
    CHECK_EQ(paths(list), (V{userActive, kActiveSys, kWivrnPpa, flatpakUser, kWivrnFlatpakSys, kMonado}));
  }
  // Preference "monado" without Monado installed -> the others are kept
  {
    FakeFs noMonado;
    noMonado.files = {kWivrnPpa};
    noMonado.globs["/usr/share/openxr/1/openxr_wivrn*.json"] = {kWivrnPpa};
    CHECK_EQ(paths(listRuntimeCandidates(env, "monado", noMonado.existsFn(), noMonado.globFn())), (V{kWivrnPpa}));
  }

  // Explicit .json path comes first (source "explicit"), also with an upper-case extension
  {
    FakeFs f = all;
    f.files.insert("/opt/custom/rt.json");
    f.files.insert("/opt/custom/RT2.JSON");
    auto list = listRuntimeCandidates(env, "/opt/custom/rt.json", f.existsFn(), f.globFn());
    CHECK_EQ(paths(list).front(), std::string("/opt/custom/rt.json"));
    CHECK_EQ(sources(list).front(), std::string("explicit"));
    CHECK(list.size() == 7);
    CHECK(!list.empty() && list[0].kind == RuntimeKind::Other);
    list = listRuntimeCandidates(env, "/opt/custom/RT2.JSON", f.existsFn(), f.globFn());
    CHECK_EQ(paths(list).front(), std::string("/opt/custom/RT2.JSON"));
  }
  // Explicit path that does not exist: missing, a warning is logged, fallbacks remain
  {
    g_logLines.clear();
    auto list = listRuntimeCandidates(env, "/nonexistent/none.json", all.existsFn(), all.globFn());
    CHECK_EQ(paths(list).front(), std::string(kWivrnPpa));
    CHECK(list.size() == 6);
    bool warned = false;
    for (auto& l : g_logLines) warned = warned || l.find("/nonexistent/none.json") != std::string::npos;
    CHECK(warned);
  }
  // Explicit path == already known path -> no duplicates, source "explicit"
  {
    auto list = listRuntimeCandidates(env, kMonado, all.existsFn(), all.globFn());
    CHECK_EQ(paths(list).front(), std::string(kMonado));
    CHECK_EQ(sources(list).front(), std::string("explicit"));
    CHECK(list.size() == 6);
  }

  // XR_RUNTIME_JSON (env.xrRuntimeJsonEnv) is honored
  {
    RuntimeEnvironment e2 = env;
    e2.xrRuntimeJsonEnv = "/srv/xr/openxr_foo.json";
    FakeFs f = all;
    f.files.insert(e2.xrRuntimeJsonEnv);
    auto list = listRuntimeCandidates(e2, "auto", f.existsFn(), f.globFn());
    CHECK_EQ(paths(list).front(), e2.xrRuntimeJsonEnv);          // with "auto" before all fallbacks
    CHECK_EQ(sources(list).front(), std::string("env"));
    CHECK(list.size() == 7);
    // with "monado": Monado, then env, then the rest
    list = listRuntimeCandidates(e2, "monado", f.existsFn(), f.globFn());
    CHECK_EQ(paths(list)[0], std::string(kMonado));
    CHECK_EQ(paths(list)[1], e2.xrRuntimeJsonEnv);
    // Env points to a non-existent file -> ignored
    e2.xrRuntimeJsonEnv = "/srv/xr/does-not-exist.json";
    CHECK(listRuntimeCandidates(e2, "auto", f.existsFn(), f.globFn()).size() == 6);
    // Env == WiVRn path -> no duplicate
    e2.xrRuntimeJsonEnv = kWivrnPpa;
    list = listRuntimeCandidates(e2, "auto", f.existsFn(), f.globFn());
    CHECK(list.size() == 6);
    CHECK_EQ(sources(list).front(), std::string("env"));
  }

  // LINUXXR_RUNTIME (env.linuxxrRuntime) only applies when no preference is passed
  {
    RuntimeEnvironment e2 = env;
    e2.linuxxrRuntime = "monado";
    CHECK_EQ(paths(listRuntimeCandidates(e2, "", all.existsFn(), all.globFn())).front(), std::string(kMonado));
    CHECK_EQ(paths(listRuntimeCandidates(e2, "wivrn", all.existsFn(), all.globFn())).front(), std::string(kWivrnPpa));
    e2.linuxxrRuntime = "/opt/custom/rt.json";
    FakeFs f = all;
    f.files.insert("/opt/custom/rt.json");
    CHECK_EQ(paths(listRuntimeCandidates(e2, "", f.existsFn(), f.globFn())).front(), std::string("/opt/custom/rt.json"));
  }

  // In general: never duplicates, only existing files
  for (const char* pref : {"auto", "wivrn", "monado", "active", kMonado, kWivrnPpa}) {
    auto list = listRuntimeCandidates(env, pref, all.existsFn(), all.globFn());
    std::set<std::string> uniq;
    for (auto& c : list) {
      CHECK(uniq.insert(c.manifestPath).second);
      CHECK(all.files.count(c.manifestPath) == 1);
    }
  }
}

static void testGuessKind() {
  CHECK(guessRuntimeKind("/usr/share/openxr/1/openxr_wivrn.json") == RuntimeKind::WiVRn);
  CHECK(guessRuntimeKind("/var/lib/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn.json") == RuntimeKind::WiVRn);
  CHECK(guessRuntimeKind("/opt/WiVRn/openxr.json") == RuntimeKind::WiVRn);  // case does not matter
  CHECK(guessRuntimeKind("/usr/share/openxr/1/openxr_monado.json") == RuntimeKind::Monado);
  CHECK(guessRuntimeKind("/home/u/Monado/rt.json") == RuntimeKind::Monado);
  CHECK(guessRuntimeKind("/home/u/.steam/steam/steamapps/common/SteamVR/steamxr_linux64.json") == RuntimeKind::SteamVR);
  CHECK(guessRuntimeKind("/etc/xdg/openxr/1/active_runtime.json") == RuntimeKind::Other);
  CHECK(guessRuntimeKind("/somewhere/foo.json") == RuntimeKind::Other);
  CHECK(guessRuntimeKind("") == RuntimeKind::Unknown);

  CHECK_EQ(std::string(runtimeKindName(RuntimeKind::WiVRn)), std::string("WiVRn"));
  CHECK_EQ(std::string(runtimeKindName(RuntimeKind::Monado)), std::string("Monado"));
  CHECK_EQ(std::string(runtimeKindName(RuntimeKind::SteamVR)), std::string("SteamVR"));
  CHECK_EQ(std::string(runtimeKindName(RuntimeKind::Other)), std::string("other"));
  CHECK_EQ(std::string(runtimeKindName(RuntimeKind::Unknown)), std::string("unknown"));
}

static void testReachable() {
  FakeFs f;
  RuntimeEnvironment env = baseEnv();  // xdgRuntimeDir = /run/user/1000
  RuntimeCandidate wivrn{kWivrnPpa, "wivrn", RuntimeKind::WiVRn};
  RuntimeCandidate monado{kMonado, "monado", RuntimeKind::Monado};
  RuntimeCandidate other{"/opt/x.json", "explicit", RuntimeKind::Other};
  RuntimeCandidate unknown{"", "", RuntimeKind::Unknown};

  // Without sockets: WiVRn/Monado not reachable, others "unknown -> true"
  CHECK(!isRuntimeReachable(wivrn, env, f.existsFn()));
  CHECK(!isRuntimeReachable(monado, env, f.existsFn()));
  CHECK(isRuntimeReachable(other, env, f.existsFn()));
  CHECK(isRuntimeReachable(unknown, env, f.existsFn()));

  // Exactly the expected socket path is checked
  f.existsCalls.clear();
  isRuntimeReachable(wivrn, env, f.existsFn());
  CHECK_EQ(f.existsCalls, (V{"/run/user/1000/wivrn/comp_ipc"}));
  f.existsCalls.clear();
  isRuntimeReachable(monado, env, f.existsFn());
  CHECK_EQ(f.existsCalls, (V{"/run/user/1000/monado_comp_ipc"}));

  // WiVRn socket present -> only WiVRn reachable
  f.files.insert("/run/user/1000/wivrn/comp_ipc");
  CHECK(isRuntimeReachable(wivrn, env, f.existsFn()));
  CHECK(!isRuntimeReachable(monado, env, f.existsFn()));
  // Monado socket present
  f.files.insert("/run/user/1000/monado_comp_ipc");
  CHECK(isRuntimeReachable(monado, env, f.existsFn()));

  // Wrong path (socket in the wrong folder) does not count
  FakeFs wrong;
  wrong.files.insert("/run/user/1000/comp_ipc");
  CHECK(!isRuntimeReachable(wivrn, env, wrong.existsFn()));

  // Empty xdgRuntimeDir -> cannot check -> true (and exists is not queried)
  FakeFs none;
  RuntimeEnvironment noXdg = env;
  noXdg.xdgRuntimeDir.clear();
  CHECK(isRuntimeReachable(wivrn, noXdg, none.existsFn()));
  CHECK(isRuntimeReachable(monado, noXdg, none.existsFn()));
  CHECK(none.existsCalls.empty());
}

// ============================================================================================
// xr_math
// ============================================================================================

namespace {

constexpr float kPi = 3.14159265358979f;

XrQuaternionf axisAngle(float x, float y, float z, float angle) {
  const float n = std::sqrt(x * x + y * y + z * z);
  const float s = std::sin(angle / 2) / n;
  return {x * s, y * s, z * s, std::cos(angle / 2)};
}

XrPosef makePose(XrQuaternionf q, XrVector3f p) {
  XrPosef r;
  r.orientation = quatNormalize(q);
  r.position = p;
  return r;
}

void checkPoseNear(const XrPosef& a, const XrPosef& b, float eps, const char* file, int line) {
  // Quaternions q and -q describe the same rotation -> compare via |dot|
  const float dot = a.orientation.x * b.orientation.x + a.orientation.y * b.orientation.y +
                    a.orientation.z * b.orientation.z + a.orientation.w * b.orientation.w;
  lxr_test::checkNear(std::fabs(dot), 1.0f, eps, file, line, "|dot(a.q,b.q)|", "1", "eps");
  lxr_test::checkNear(a.position.x, b.position.x, eps, file, line, "a.position.x", "b.position.x", "eps");
  lxr_test::checkNear(a.position.y, b.position.y, eps, file, line, "a.position.y", "b.position.y", "eps");
  lxr_test::checkNear(a.position.z, b.position.z, eps, file, line, "a.position.z", "b.position.z", "eps");
}

}  // namespace

#define CHECK_POSE_NEAR(a, b, eps) checkPoseNear((a), (b), (eps), __FILE__, __LINE__)

static void testXrMath() {
  const float eps = 1e-5f;
  const XrPosef id = poseIdentity();
  const XrPosef p1 = makePose(axisAngle(0, 1, 0, 0.7f), {1.0f, 2.0f, -3.0f});
  const XrPosef p2 = makePose(axisAngle(1, 1, 0, -1.9f), {-0.5f, 0.25f, 4.0f});
  const XrPosef p3 = makePose(axisAngle(0.3f, -0.2f, 0.9f, 2.8f), {10.0f, -7.0f, 0.1f});

  // Identity
  CHECK_EQ(id.orientation.w, 1.0f);
  CHECK_EQ(id.position.x + id.position.y + id.position.z, 0.0f);
  for (const XrPosef& p : {p1, p2, p3}) {
    CHECK_POSE_NEAR(poseMul(id, p), p, eps);
    CHECK_POSE_NEAR(poseMul(p, id), p, eps);
    CHECK_POSE_NEAR(poseMul(id, id), id, eps);
    // p * p^-1 ~ identity, and likewise p^-1 * p
    CHECK_POSE_NEAR(poseMul(p, poseInverse(p)), id, 1e-4f);
    CHECK_POSE_NEAR(poseMul(poseInverse(p), p), id, 1e-4f);
    CHECK_POSE_NEAR(poseInverse(poseInverse(p)), p, 1e-4f);
  }

  // Associativity: (a*b)*c == a*(b*c)
  CHECK_POSE_NEAR(poseMul(poseMul(p1, p2), p3), poseMul(p1, poseMul(p2, p3)), 1e-4f);

  // Order: a*b = first b, then a. A pure translation without rotation adds up;
  // with a rotation in a, the translation of b is rotated too.
  {
    XrPosef a = makePose(axisAngle(0, 1, 0, kPi / 2), {1, 0, 0});
    XrPosef b = makePose({0, 0, 0, 1}, {0, 0, -1});  // 1 m "in front of" a
    XrPosef ab = poseMul(a, b);
    // a rotates 90 deg about Y: -Z -> -X; so a.position + (-1,0,0) = (0,0,0)
    CHECK_NEAR(ab.position.x, 0.0f, 1e-5f);
    CHECK_NEAR(ab.position.y, 0.0f, 1e-5f);
    CHECK_NEAR(ab.position.z, 0.0f, 1e-5f);
  }

  // poseRelative(parent, child) returns the local pose: parent * rel == child
  {
    XrPosef child = poseMul(p1, p2);
    XrPosef rel = poseRelative(p1, child);
    CHECK_POSE_NEAR(rel, p2, 1e-4f);
    CHECK_POSE_NEAR(poseMul(p1, rel), child, 1e-4f);
  }

  // quatRotate: 90 deg about Y (right-handed): -Z (forward) -> -X, +X -> -Z, Y stays
  {
    XrQuaternionf q = axisAngle(0, 1, 0, kPi / 2);
    XrVector3f f = quatRotate(q, {0, 0, -1});
    CHECK_NEAR(f.x, -1.0f, eps);
    CHECK_NEAR(f.y, 0.0f, eps);
    CHECK_NEAR(f.z, 0.0f, eps);
    XrVector3f r = quatRotate(q, {1, 0, 0});
    CHECK_NEAR(r.x, 0.0f, eps);
    CHECK_NEAR(r.y, 0.0f, eps);
    CHECK_NEAR(r.z, -1.0f, eps);
    XrVector3f u = quatRotate(q, {0, 1, 0});
    CHECK_NEAR(u.x, 0.0f, eps);
    CHECK_NEAR(u.y, 1.0f, eps);
    CHECK_NEAR(u.z, 0.0f, eps);
    // Identity and length preservation
    XrVector3f same = quatRotate({0, 0, 0, 1}, {3, -4, 5});
    CHECK_NEAR(same.x, 3.0f, eps);
    CHECK_NEAR(same.y, -4.0f, eps);
    CHECK_NEAR(same.z, 5.0f, eps);
    XrVector3f g = quatRotate(axisAngle(1, 2, 3, 1.3f), {3, -4, 5});
    CHECK_NEAR(std::sqrt(g.x * g.x + g.y * g.y + g.z * g.z), std::sqrt(50.0f), 1e-4f);
    // quatRotate agrees with quatMul: rotate(a*b, v) == rotate(a, rotate(b, v))
    XrQuaternionf qa = axisAngle(0, 1, 0, 0.5f), qb = axisAngle(1, 0, 0, -0.8f);
    XrVector3f v1 = quatRotate(quatMul(qa, qb), {1, 2, 3});
    XrVector3f v2 = quatRotate(qa, quatRotate(qb, {1, 2, 3}));
    CHECK_NEAR(v1.x, v2.x, 1e-5f);
    CHECK_NEAR(v1.y, v2.y, 1e-5f);
    CHECK_NEAR(v1.z, v2.z, 1e-5f);
  }

  // quatNormalize / quatConjugate
  {
    XrQuaternionf n = quatNormalize({0, 0, 0, 2});
    CHECK_EQ(n.w, 1.0f);
    XrQuaternionf zero = quatNormalize({0, 0, 0, 0});  // zero quaternion -> identity
    CHECK_EQ(zero.x, 0.0f);
    CHECK_EQ(zero.w, 1.0f);
    XrQuaternionf c = quatConjugate({1, 2, 3, 4});
    CHECK_EQ(c.x, -1.0f);
    CHECK_EQ(c.y, -2.0f);
    CHECK_EQ(c.z, -3.0f);
    CHECK_EQ(c.w, 4.0f);
  }

  // toUnity of (1,2,3) / (x,y,z,w): header comment -> position (x,y,-z), rotation (-x,-y,z,w)
  {
    Vec3 v = toUnity(XrVector3f{1, 2, 3});
    CHECK_EQ(v.x, 1.0f);
    CHECK_EQ(v.y, 2.0f);
    CHECK_EQ(v.z, -3.0f);
    Quat q = toUnity(XrQuaternionf{0.1f, 0.2f, 0.3f, 0.9f});
    CHECK_EQ(q.x, -0.1f);
    CHECK_EQ(q.y, -0.2f);
    CHECK_EQ(q.z, 0.3f);
    CHECK_EQ(q.w, 0.9f);
    Vec3 w = angularVelocityToUnity({1, 2, 3});
    CHECK_EQ(w.x, -1.0f);
    CHECK_EQ(w.y, -2.0f);
    CHECK_EQ(w.z, 3.0f);
    Pose up = toUnity(XrPosef{{0.1f, 0.2f, 0.3f, 0.9f}, {1, 2, 3}});
    CHECK_EQ(up.position.z, -3.0f);
    CHECK_EQ(up.rotation.x, -0.1f);
    // Looking forward: OpenXR -Z <-> Unity +Z
    Vec3 fwd = toUnity(XrVector3f{0, 0, -1});
    CHECK_EQ(fwd.z, 1.0f);
  }

  // Round trip toUnity/fromUnity (exact, since only sign flips)
  for (const XrPosef& p : {p1, p2, p3, id}) {
    XrPosef back = fromUnity(toUnity(p));
    CHECK_EQ(back.position.x, p.position.x);
    CHECK_EQ(back.position.y, p.position.y);
    CHECK_EQ(back.position.z, p.position.z);
    CHECK_EQ(back.orientation.x, p.orientation.x);
    CHECK_EQ(back.orientation.y, p.orientation.y);
    CHECK_EQ(back.orientation.z, p.orientation.z);
    CHECK_EQ(back.orientation.w, p.orientation.w);
  }
  {
    Vec3 u{1.5f, -2.5f, 3.5f};
    XrVector3f x = fromUnity(u);
    CHECK_EQ(x.z, -3.5f);
    Vec3 u2 = toUnity(x);
    CHECK_EQ(u2.x, u.x);
    CHECK_EQ(u2.y, u.y);
    CHECK_EQ(u2.z, u.z);
  }

  // Consistency of the mirroring: rotation commutes with the conversion
  // toUnity(R * v) == toUnity(R) * toUnity(v)  (same quaternion formula in Unity)
  {
    for (const XrQuaternionf& q : {p1.orientation, p2.orientation, p3.orientation}) {
      XrVector3f v{0.4f, -1.2f, 2.0f};
      Vec3 lhs = toUnity(quatRotate(q, v));
      Quat uq = toUnity(q);
      Vec3 uv = toUnity(v);
      XrVector3f rhs = quatRotate({uq.x, uq.y, uq.z, uq.w}, {uv.x, uv.y, uv.z});
      CHECK_NEAR(lhs.x, rhs.x, 1e-5f);
      CHECK_NEAR(lhs.y, rhs.y, 1e-5f);
      CHECK_NEAR(lhs.z, rhs.z, 1e-5f);
    }
  }

  // tanAngles: signs (left/down negative, right/up positive) and values
  {
    XrFovf fov{-0.8f, 0.7f, 0.6f, -0.5f};  // left, right, up, down (radians)
    TanAngles t = tanAngles(fov);
    CHECK(t.left < 0);
    CHECK(t.right > 0);
    CHECK(t.up > 0);
    CHECK(t.down < 0);
    CHECK_NEAR(t.left, std::tan(-0.8f), 1e-6f);
    CHECK_NEAR(t.right, std::tan(0.7f), 1e-6f);
    CHECK_NEAR(t.up, std::tan(0.6f), 1e-6f);
    CHECK_NEAR(t.down, std::tan(-0.5f), 1e-6f);
    // 45 deg symmetric -> +-1
    TanAngles s = tanAngles({-kPi / 4, kPi / 4, kPi / 4, -kPi / 4});
    CHECK_NEAR(s.left, -1.0f, 1e-5f);
    CHECK_NEAR(s.right, 1.0f, 1e-5f);
    CHECK_NEAR(s.up, 1.0f, 1e-5f);
    CHECK_NEAR(s.down, -1.0f, 1e-5f);
    // Default constructor of the header: +-1
    TanAngles d;
    CHECK_EQ(d.left, -1.0f);
    CHECK_EQ(d.up, 1.0f);
  }

  // fovUnion: enclosing FOV (left/down = minimum, right/up = maximum)
  {
    XrFovf a{-0.5f, 0.3f, 0.4f, -0.2f}, b{-0.4f, 0.5f, 0.2f, -0.6f};
    XrFovf u = fovUnion(a, b);
    CHECK_EQ(u.angleLeft, -0.5f);
    CHECK_EQ(u.angleRight, 0.5f);
    CHECK_EQ(u.angleUp, 0.4f);
    CHECK_EQ(u.angleDown, -0.6f);
    // symmetric and idempotent
    XrFovf u2 = fovUnion(b, a);
    CHECK_EQ(u2.angleLeft, u.angleLeft);
    CHECK_EQ(u2.angleRight, u.angleRight);
    CHECK_EQ(u2.angleUp, u.angleUp);
    CHECK_EQ(u2.angleDown, u.angleDown);
    XrFovf uu = fovUnion(u, u);
    CHECK_EQ(uu.angleLeft, u.angleLeft);
    CHECK_EQ(uu.angleDown, u.angleDown);
    // Union contains both inputs
    CHECK(u.angleLeft <= a.angleLeft && u.angleLeft <= b.angleLeft);
    CHECK(u.angleRight >= a.angleRight && u.angleRight >= b.angleRight);
  }

  // poseIsFinite
  {
    CHECK(poseIsFinite(p1));
    XrPosef bad = p1;
    bad.position.y = std::nanf("");
    CHECK(!poseIsFinite(bad));
    bad = p1;
    bad.orientation.w = INFINITY;
    CHECK(!poseIsFinite(bad));
  }
}

// ============================================================================================
// vkChooseDefaultDevice
// ============================================================================================

static PhysicalDeviceInfo dev(const char* name, VkPhysicalDeviceType type) {
  PhysicalDeviceInfo d;
  d.name = name;
  d.type = type;
  return d;
}

static void testChooseDevice() {
  const auto DISCRETE = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
  const auto INTEGRATED = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
  const auto VIRTUAL = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
  const auto CPU = VK_PHYSICAL_DEVICE_TYPE_CPU;
  const auto OTHER = VK_PHYSICAL_DEVICE_TYPE_OTHER;

  using Devs = std::vector<PhysicalDeviceInfo>;

  // Empty list -> -1 (with and without override)
  CHECK_EQ(vkChooseDefaultDevice({}, nullptr), -1);
  CHECK_EQ(vkChooseDefaultDevice({}, ""), -1);
  CHECK_EQ(vkChooseDefaultDevice({}, "0"), -1);
  CHECK_EQ(vkChooseDefaultDevice({}, "Radeon"), -1);

  // Ranking: discrete > integrated > virtual > other > CPU
  Devs a{dev("llvmpipe", CPU), dev("Intel UHD", INTEGRATED), dev("NVIDIA Card", DISCRETE)};
  CHECK_EQ(vkChooseDefaultDevice(a, nullptr), 2);
  Devs b{dev("NVIDIA Card", DISCRETE), dev("Intel UHD", INTEGRATED), dev("llvmpipe", CPU)};
  CHECK_EQ(vkChooseDefaultDevice(b, nullptr), 0);
  Devs c{dev("llvmpipe", CPU), dev("Intel UHD", INTEGRATED)};
  CHECK_EQ(vkChooseDefaultDevice(c, nullptr), 1);
  Devs d{dev("Intel UHD", INTEGRATED), dev("AMD Card", DISCRETE)};
  CHECK_EQ(vkChooseDefaultDevice(d, nullptr), 1);
  Devs e{dev("llvmpipe", CPU), dev("Virt", VIRTUAL), dev("Mystery", OTHER)};
  CHECK_EQ(vkChooseDefaultDevice(e, nullptr), 1);
  Devs f{dev("llvmpipe", CPU), dev("Mystery", OTHER)};
  CHECK_EQ(vkChooseDefaultDevice(f, nullptr), 1);
  // Tie -> first device
  Devs g{dev("GPU one", DISCRETE), dev("GPU two", DISCRETE)};
  CHECK_EQ(vkChooseDefaultDevice(g, nullptr), 0);
  // A CPU-only device (e.g. lavapipe in CI) is still taken, better than nothing
  Devs h{dev("llvmpipe", CPU)};
  CHECK_EQ(vkChooseDefaultDevice(h, nullptr), 0);
  Devs h2{dev("llvmpipe", CPU), dev("llvmpipe 2", CPU)};
  CHECK_EQ(vkChooseDefaultDevice(h2, nullptr), 0);

  // Empty override string = no override
  CHECK_EQ(vkChooseDefaultDevice(a, ""), 2);

  // Override by index (also onto a "worse" device)
  CHECK_EQ(vkChooseDefaultDevice(a, "0"), 0);
  CHECK_EQ(vkChooseDefaultDevice(a, "1"), 1);
  CHECK_EQ(vkChooseDefaultDevice(a, "2"), 2);

  // Override by name part (substring, case-sensitive)
  CHECK_EQ(vkChooseDefaultDevice(a, "llvm"), 0);
  CHECK_EQ(vkChooseDefaultDevice(a, "Intel"), 1);
  CHECK_EQ(vkChooseDefaultDevice(a, "NVIDIA Card"), 2);
  CHECK_EQ(vkChooseDefaultDevice(a, "UHD"), 1);
  CHECK_EQ(vkChooseDefaultDevice(a, "intel"), 2);  // lower case does not match -> default selection

  // Multiple name matches -> first
  Devs two{dev("Foo GPU A", INTEGRATED), dev("Foo GPU B", DISCRETE)};
  CHECK_EQ(vkChooseDefaultDevice(two, "Foo"), 0);

  // Index takes precedence over name part when the override is a valid number
  Devs numbered{dev("Card 1", DISCRETE), dev("Card 0", DISCRETE)};
  CHECK_EQ(vkChooseDefaultDevice(numbered, "1"), 1);

  // Invalid override -> default selection (with warning)
  g_logLines.clear();
  CHECK_EQ(vkChooseDefaultDevice(a, "7"), 2);          // index out of range
  CHECK_EQ(vkChooseDefaultDevice(a, "3"), 2);          // exactly size()
  CHECK_EQ(vkChooseDefaultDevice(a, "-1"), 2);         // negative
  CHECK_EQ(vkChooseDefaultDevice(a, "nonexistent"), 2);  // name matches nothing
  CHECK_EQ(vkChooseDefaultDevice(a, "1x"), 2);         // garbage after the number
  CHECK_EQ(vkChooseDefaultDevice(b, "nonexistent"), 0);
  CHECK(g_logLines.size() >= 6);
  bool warned = false;
  for (auto& l : g_logLines) warned = warned || l.find("nonexistent") != std::string::npos;
  CHECK(warned);
  // Override without a match on a CPU-only system
  CHECK_EQ(vkChooseDefaultDevice(h, "5"), 0);

  // Helper functions from vk_api.h
  CHECK_EQ(std::string(vkDeviceTypeName(DISCRETE)), std::string("discrete"));
  CHECK_EQ(std::string(vkDeviceTypeName(INTEGRATED)), std::string("integrated"));
  CHECK_EQ(std::string(vkDeviceTypeName(OTHER)), std::string("other"));
  CHECK_EQ(std::string(vkDeviceTypeName(CPU)), std::string("CPU"));
  CHECK_EQ(vkVersionString(VK_MAKE_API_VERSION(0, 1, 3, 275)), std::string("1.3.275"));
}

// ============================================================================================
// VulkanPreInit without a runtime (fallback and cache paths)
// ============================================================================================

static void testPreInitWithoutRuntime() {
  const std::string dir = makeTempDir();
  CHECK(!dir.empty());
  if (dir.empty()) return;

  // Redirect the environment to an empty directory: cache and sockets live in the temp folder.
  auto getenvCopy = [](const char* n) { const char* v = std::getenv(n); return v ? std::string(v) : std::string(); };
  const std::string oldCache = getenvCopy("XDG_CACHE_HOME"), oldRun = getenvCopy("XDG_RUNTIME_DIR");
  const bool hadCache = std::getenv("XDG_CACHE_HOME"), hadRun = std::getenv("XDG_RUNTIME_DIR");
  ::setenv("XDG_CACHE_HOME", (dir + "/cache").c_str(), 1);
  ::setenv("XDG_RUNTIME_DIR", (dir + "/run").c_str(), 1);
  fs::create_directories(dir + "/run");

  // Hermetic: a dummy manifest file in the temp folder ("wivrn" in the name -> kind WiVRn) is picked via an
  // explicit path as the first runtime. The WiVRn socket is missing in $XDG_RUNTIME_DIR -> "not reachable", so
  // prepare() never queries a real OpenXR runtime - regardless of what is installed on the system.
  const std::string dummy = dir + "/openxr_wivrn.json";
  { std::ofstream(dummy) << "{}\n"; }
  const RuntimeCandidate rt = selectRuntime(dummy);
  CHECK_EQ(rt.manifestPath, dummy);
  CHECK(rt.kind == RuntimeKind::WiVRn);
  CHECK(!isRuntimeReachable(rt));
  {
    const std::string cachePath = defaultRequirementsCachePath();
    CHECK_EQ(cachePath, dir + "/cache/linuxxr/vulkan_requirements.txt");

    {  // Without cache -> base list
      VulkanPreInit pre;
      pre.prepare(dummy);
      CHECK_EQ(pre.requirements().source, std::string("fallback"));
      CHECK(!pre.runtimeWasQueried());
      CHECK_EQ(pre.requirements().instanceExtensions, fallbackInstanceExtensions());
      CHECK_EQ(pre.requirements().deviceExtensions, fallbackDeviceExtensions());
      CHECK_EQ(pre.requirements().runtimeManifest, rt.manifestPath);
      pre.prepare("ignored");  // a second call has no effect
      CHECK_EQ(pre.requirements().source, std::string("fallback"));
      CHECK(!fs::exists(cachePath));  // no cache is written without a runtime query
    }
    {  // With cache (matching runtime) -> cache contents first, base list behind it (no duplicates)
      VulkanRequirements cached;
      cached.runtimeManifest = rt.manifestPath;
      cached.instanceExtensions = {"VK_TEST_instance_ext", "VK_KHR_get_physical_device_properties2"};
      cached.deviceExtensions = {"VK_TEST_device_ext"};
      CHECK(saveRequirementsCache(cachePath, cached));
      VulkanPreInit pre;
      pre.prepare(dummy);
      CHECK_EQ(pre.requirements().source, std::string("cache"));
      const V& inst = pre.requirements().instanceExtensions;
      CHECK(inst.size() >= 2 && inst[0] == "VK_TEST_instance_ext");
      CHECK_EQ(inst, mergeUnique(cached.instanceExtensions, fallbackInstanceExtensions()));
      CHECK_EQ(pre.requirements().deviceExtensions, mergeUnique(cached.deviceExtensions, fallbackDeviceExtensions()));
    }
    {  // Cache of a different runtime is ignored
      VulkanRequirements foreign;
      foreign.runtimeManifest = rt.manifestPath + ".other";
      foreign.instanceExtensions = {"VK_FOREIGN_ext"};
      foreign.deviceExtensions = {"VK_FOREIGN_ext"};
      CHECK(saveRequirementsCache(cachePath, foreign));
      VulkanPreInit pre;
      pre.prepare(dummy);
      CHECK_EQ(pre.requirements().source, std::string("fallback"));
      CHECK(!containsExtension(pre.requirements().instanceExtensions, "VK_FOREIGN_ext"));
    }
  }

  if (hadCache) ::setenv("XDG_CACHE_HOME", oldCache.c_str(), 1); else ::unsetenv("XDG_CACHE_HOME");
  if (hadRun) ::setenv("XDG_RUNTIME_DIR", oldRun.c_str(), 1); else ::unsetenv("XDG_RUNTIME_DIR");
  std::error_code ec;
  fs::remove_all(dir, ec);
}

int main() {
  setLogSink(collectLog, nullptr);  // do not print warnings from the functions under test to stderr

  testExtensionLists();
  testSerialize();
  testWriteExtensionString();
  testCacheFiles();
  testRuntimeCandidates();
  testGuessKind();
  testReachable();
  testXrMath();
  testChooseDevice();
  testPreInitWithoutRuntime();

  return lxr_test::summary("linuxxr_core_tests");
}
