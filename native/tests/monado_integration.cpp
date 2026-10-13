// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
// Integration test "fake Unity + real Monado": positive case of the LinuxXR plugin.
//
// The test plays Unity (like fake_unity_host.cpp), but with everything a real session needs:
//   * starts a headless monado-service itself (XRT_COMPOSITOR_NULL=1, SIMULATED_ENABLE=1; stdin = pipe)
//   * XRSDKPreInit -> Vulkan instance/device on lavapipe with EXACTLY the plugin's extension strings
//   * fake IUnityGraphics (Vulkan), IUnityGraphicsVulkan (Instance/AccessTexture/AccessQueue/...),
//     IUnityXRDisplayInterface (CreateTexture creates real VkImages), IUnityXRInputInterface (records devices)
//   * drives the display+input lifecycle and ~150 frames (single-pass, then multi-pass, scale change, sRGB change,
//     aborted frames, Stop->Start without Shutdown), checks render passes, pose, mirror mode, texture lifetime
//     and clean teardown
//   * run 2: AccessQueue callback asynchronously on its own thread
//   * run 3 ("instance loss"): Monado is killed with SIGKILL mid-operation
//
// If monado-service (or the Vulkan/OpenXR loader) is not installed, the test ends with "SKIP" and exit code 77
// (ctest: SKIP_RETURN_CODE). Global timeout via alarm(); Monado is terminated in every case.
//
// Usage: linuxxr_monado_integration /path/to/libLinuxXR.so [run: 1 full | 2 async AccessQueue | 3 Monado dies]

#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "IUnityInterface.h"
#include "IUnityGraphics.h"
#include "IUnityGraphicsVulkan.h"
#include "IUnityXRDisplay.h"
#include "IUnityXRInput.h"
#include "IUnityXRPreInit.h"
#include "IUnityXRTrace.h"
#include "UnitySubsystemTypes.h"
#include "check.h"

#include <vulkan/vulkan.h>  // with VK_NO_PROTOTYPES: everything via vkGetInstanceProcAddr

using V = std::vector<std::string>;

#ifndef LINUXXR_EXPECTED_VERSION
#define LINUXXR_EXPECTED_VERSION "0.3.3"
#endif

constexpr int kExitSkip = 77;

// ============================================================================================
// Opaque Unity structs: defined concretely here so the fakes can record device definitions/states
// ============================================================================================

struct UnityXRInputDeviceDefinition {
  struct Feature {
    std::string name;
    UnityXRInputFeatureType type;
    std::string usage;
  };
  std::string name, manufacturer;
  UnityXRInputDeviceCharacteristics characteristics = static_cast<UnityXRInputDeviceCharacteristics>(0);
  std::vector<Feature> features;
};

struct UnityXRInputDeviceState {
  struct Value {
    UnityXRInputFeatureType type;
    std::array<float, 4> v;
  };
  std::map<UnityXRInputFeatureIndex, Value> values;
};

namespace {

// ============================================================================================
// Process helpers: timeout, Monado child process
// ============================================================================================

volatile pid_t gMonadoPid = 0;

void killMonadoNow() {
  pid_t p = gMonadoPid;
  if (p > 0) {
    kill(-p, SIGKILL);  // process group (monado-service may start helper processes)
    kill(p, SIGKILL);
  }
}

void onFatalSignal(int sig) {
  killMonadoNow();
  const char msg[] = "\nmonado_integration: FATAL SIGNAL - Monado terminated, aborting\n";
  ssize_t r = write(2, msg, sizeof msg - 1);
  (void)r;
  if (sig == SIGALRM) _exit(3);
  signal(sig, SIG_DFL);
  raise(sig);
}

void installSignalHandlers() {
  for (int s : {SIGALRM, SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGTERM, SIGINT}) {
    struct sigaction sa {};
    sa.sa_handler = onFatalSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    sigaction(s, &sa, nullptr);
  }
}

double nowSec() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

bool inPath(const char* exe) {
  const char* path = getenv("PATH");
  if (!path) return false;
  std::string p(path);
  size_t i = 0;
  while (i <= p.size()) {
    size_t j = p.find(':', i);
    if (j == std::string::npos) j = p.size();
    std::string dir = p.substr(i, j - i);
    if (dir.empty()) dir = ".";
    std::string full = dir + "/" + exe;
    if (access(full.c_str(), X_OK) == 0) return true;
    i = j + 1;
  }
  return false;
}

bool socketAccepts(const std::string& path) {
  struct stat st {};
  if (stat(path.c_str(), &st) != 0 || !S_ISSOCK(st.st_mode)) return false;
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return false;
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (path.size() >= sizeof(addr.sun_path)) {
    close(fd);
    return false;
  }
  memcpy(addr.sun_path, path.c_str(), path.size() + 1);
  bool ok = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0;
  close(fd);
  return ok;
}

std::string readTail(const std::string& file, size_t maxBytes = 3000) {
  FILE* f = fopen(file.c_str(), "rb");
  if (!f) return "(no log file)";
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  long from = n > static_cast<long>(maxBytes) ? n - static_cast<long>(maxBytes) : 0;
  fseek(f, from, SEEK_SET);
  std::string s(static_cast<size_t>(n - from), '\0');
  size_t got = fread(s.data(), 1, s.size(), f);
  s.resize(got);
  fclose(f);
  return s;
}

struct Monado {
  pid_t pid = 0;
  int stdinWrite = -1;  // write end of the pipe that is Monado's stdin; closing it = EOF = Monado exits
  std::string logFile;

  bool start(const std::string& runtimeDir) {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return false;
    logFile = runtimeDir + "/monado.log";
    pid_t p = fork();
    if (p < 0) return false;
    if (p == 0) {
      setpgid(0, 0);
      dup2(fds[0], 0);  // dup2 clears FD_CLOEXEC on the target
      int lf = open(logFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
      if (lf >= 0) {
        dup2(lf, 1);
        dup2(lf, 2);
      }
      setenv("XRT_COMPOSITOR_NULL", "1", 1);
      setenv("SIMULATED_ENABLE", "1", 1);
      setenv("XDG_RUNTIME_DIR", runtimeDir.c_str(), 1);
      execlp("monado-service", "monado-service", static_cast<char*>(nullptr));
      _exit(127);
    }
    setpgid(p, p);  // also in the parent (avoid a race with exec)
    close(fds[0]);
    stdinWrite = fds[1];
    pid = p;
    gMonadoPid = p;
    return true;
  }

  bool alive() {
    if (pid <= 0) return false;
    int st = 0;
    pid_t r = waitpid(pid, &st, WNOHANG);
    if (r == 0) return true;
    if (r == pid) {
      exitStatus = st;
      pid = -pid;  // terminated and reaped
    }
    return false;
  }
  int exitStatus = 0;

  bool waitForSocket(const std::string& sock, double timeoutSec) {
    const double t0 = nowSec();
    while (nowSec() - t0 < timeoutSec) {
      if (!alive()) return false;
      if (socketAccepts(sock)) return true;
      usleep(50 * 1000);
    }
    return false;
  }

  // Terminate cleanly: close stdin (EOF), wait briefly, then SIGTERM, then SIGKILL. Returns how it ended.
  std::string stop() {
    if (stdinWrite >= 0) {
      close(stdinWrite);
      stdinWrite = -1;
    }
    if (pid <= 0) return "already terminated";
    const char* how = "cleanly (stdin EOF)";
    double t0 = nowSec();
    while (alive() && nowSec() - t0 < 3.0) usleep(50 * 1000);
    if (pid > 0) {
      how = "SIGTERM";
      kill(-pid, SIGTERM);
      kill(pid, SIGTERM);
      t0 = nowSec();
      while (alive() && nowSec() - t0 < 3.0) usleep(50 * 1000);
    }
    if (pid > 0) {
      how = "SIGKILL";
      kill(-pid, SIGKILL);
      kill(pid, SIGKILL);
      waitpid(pid, nullptr, 0);
      pid = -pid;
    }
    gMonadoPid = 0;
    return how;
  }
};
Monado gMonado;

// ============================================================================================
// Fake state
// ============================================================================================

struct Vk;
struct HostTexture {
  UnityXRRenderTextureId id = 0;
  uint32_t width = 0, height = 0, arrayLength = 0, flags = 0;
  VkFormat format = VK_FORMAT_UNDEFINED;
  uint32_t layers = 1;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct LifecycleReg {
  bool registered = false;
  int calls = 0;
  std::string pluginName, id;
  UnityLifecycleProvider provider{};
};

struct FakeHost {
  int traceTotal = 0, traceErrors = 0, traceWarnings = 0;
  V traceLines, traceErrorLines, traceWarnLines;

  int preinitRegisterCalls = 0;
  UnityXRPreInitProvider preinit{};

  LifecycleReg displayLifecycle, inputLifecycle;
  bool displayMainRegistered = false, displayGfxRegistered = false, inputProviderRegistered = false;
  UnitySubsystemHandle displayMainHandle = nullptr, displayGfxHandle = nullptr, inputHandle = nullptr;
  UnityXRDisplayProvider displayMain{};
  UnityXRDisplayGraphicsThreadProvider displayGfx{};
  UnityXRInputProvider inputProvider{};

  // Textures
  std::map<UnityXRRenderTextureId, std::unique_ptr<HostTexture>> textures;
  UnityXRRenderTextureId nextTexId = 1;
  int texturesCreated = 0, texturesDestroyed = 0;
  std::vector<UnityXRRenderTextureDesc> createdDescs;

  // Vulkan fakes: counters and observations
  UnityVulkanInstance vkInstance{};
  int accessTextureCalls = 0, accessQueueCalls = 0, ensureOutsideCalls = 0, instanceCalls = 0;
  int badAccessLayout = 0, badAccessMode = 0, badQueueFlush = 0, unknownTextureAccess = 0;

  // Input
  std::map<uint32_t, UnityXRInputDeviceDefinition> definitions;  // per device ID (latest definition)
  std::set<uint32_t> connected;
  int connectCalls = 0, disconnectCalls = 0;
};
FakeHost g;

// ============================================================================================
// Vulkan (real loader, dlopen)
// ============================================================================================

#define VK_INSTANCE_FNS(X)                                                                                             \
  X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties)                                  \
  X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkEnumerateDeviceExtensionProperties) X(vkCreateDevice)                \
  X(vkGetDeviceProcAddr) X(vkGetPhysicalDeviceMemoryProperties)

#define VK_DEVICE_FNS(X)                                                                                               \
  X(vkDestroyDevice) X(vkDeviceWaitIdle) X(vkGetDeviceQueue) X(vkCreateImage) X(vkDestroyImage)                        \
  X(vkGetImageMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory)                             \
  X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer)                   \
  X(vkEndCommandBuffer) X(vkResetCommandBuffer) X(vkCmdPipelineBarrier) X(vkCmdClearColorImage) X(vkQueueSubmit)       \
  X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences)

struct Vk {
  void* lib = nullptr;
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
  PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;
  PFN_vkCreateInstance vkCreateInstance = nullptr;
#define DECL(n) PFN_##n n = nullptr;
  VK_INSTANCE_FNS(DECL)
  VK_DEVICE_FNS(DECL)
#undef DECL

  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  VkPhysicalDeviceMemoryProperties memProps{};
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
};
Vk K;

template <class T>
T vkInfo(VkStructureType type) {
  T info{};
  info.sType = type;
  return info;
}

bool loadVulkan() {
  K.lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!K.lib) return false;
  K.vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(K.lib, "vkGetInstanceProcAddr"));
  if (!K.vkGetInstanceProcAddr) return false;
  K.vkEnumerateInstanceExtensionProperties = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
      K.vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties"));
  K.vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(K.vkGetInstanceProcAddr(nullptr, "vkCreateInstance"));
  return K.vkEnumerateInstanceExtensionProperties && K.vkCreateInstance;
}

bool loadVulkanInstanceFns() {
  bool ok = true;
#define LOADI(n)                                                                                    \
  K.n = reinterpret_cast<PFN_##n>(K.vkGetInstanceProcAddr(K.instance, #n));                         \
  ok = ok && K.n != nullptr;
  VK_INSTANCE_FNS(LOADI)
#undef LOADI
  return ok;
}

bool loadVulkanDeviceFns() {
  bool ok = true;
#define LOADD(n)                                                                                    \
  K.n = reinterpret_cast<PFN_##n>(K.vkGetDeviceProcAddr(K.device, #n));                             \
  ok = ok && K.n != nullptr;
  VK_DEVICE_FNS(LOADD)
#undef LOADD
  return ok;
}

V splitSpaces(const std::string& s) {
  V out;
  size_t i = 0;
  while (i < s.size()) {
    size_t j = s.find(' ', i);
    if (j == std::string::npos) j = s.size();
    if (j > i) out.push_back(s.substr(i, j - i));
    i = j + 1;
  }
  return out;
}

std::vector<const char*> cstrs(const V& v) {
  std::vector<const char*> out;
  for (auto& s : v) out.push_back(s.c_str());
  return out;
}

bool contains(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

// The host's own command buffer: record once, submit, wait for the fence.
template <class Fn>
bool hostSubmit(Fn&& record) {
  K.vkResetCommandBuffer(K.cmd, 0);
  auto bi = vkInfo<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (K.vkBeginCommandBuffer(K.cmd, &bi) != VK_SUCCESS) return false;
  record(K.cmd);
  if (K.vkEndCommandBuffer(K.cmd) != VK_SUCCESS) return false;
  auto si = vkInfo<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
  si.commandBufferCount = 1;
  si.pCommandBuffers = &K.cmd;
  K.vkResetFences(K.device, 1, &K.fence);
  if (K.vkQueueSubmit(K.queue, 1, &si, K.fence) != VK_SUCCESS) return false;
  return K.vkWaitForFences(K.device, 1, &K.fence, VK_TRUE, 5000000000ull) == VK_SUCCESS;
}

void barrier(VkCommandBuffer cmd, VkImage image, uint32_t layers, VkImageLayout from, VkImageLayout to,
             VkAccessFlags srcA, VkAccessFlags dstA, VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
  auto b = vkInfo<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
  b.oldLayout = from;
  b.newLayout = to;
  b.srcAccessMask = srcA;
  b.dstAccessMask = dstA;
  K.vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// ============================================================================================
// IUnityXRTrace
// ============================================================================================

void UNITY_INTERFACE_API fakeTrace(XRLogType type, const char* fmt, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  ++g.traceTotal;
  std::string line(buf);
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
  g.traceLines.push_back(line);
  if (type == kXRLogTypeError || type == kXRLogTypeAssert || type == kXRLogTypeException) {
    ++g.traceErrors;
    g.traceErrorLines.push_back(line);
  } else if (type == kXRLogTypeWarning) {
    ++g.traceWarnings;
    g.traceWarnLines.push_back(line);
  }
  if (getenv("FAKE_HOST_VERBOSE")) fprintf(stderr, "  [unity-trace %d] %s\n", static_cast<int>(type), line.c_str());
}
IUnityXRTrace makeTrace() {
  IUnityXRTrace t{};
  t.Trace = fakeTrace;
  return t;
}
IUnityXRTrace gTrace = makeTrace();

void UNITY_INTERFACE_API fakeRegisterPreInitProvider(UnityXRPreInitProvider* provider) {
  ++g.preinitRegisterCalls;
  if (provider) g.preinit = *provider;
}
IUnityXRPreInit makePreInit() {
  IUnityXRPreInit p{};
  p.RegisterPreInitProvider = fakeRegisterPreInitProvider;
  return p;
}
IUnityXRPreInit gPreInit = makePreInit();

// ============================================================================================
// IUnityGraphics / IUnityGraphicsVulkan
// ============================================================================================

UnityGfxRenderer UNITY_INTERFACE_API fakeGetRenderer() { return kUnityGfxRendererVulkan; }
IUnityGraphics makeGraphics() {
  IUnityGraphics gr{};
  gr.GetRenderer = fakeGetRenderer;
  gr.RegisterDeviceEventCallback = [](IUnityGraphicsDeviceEventCallback) {};
  gr.UnregisterDeviceEventCallback = [](IUnityGraphicsDeviceEventCallback) {};
  gr.ReserveEventIDRange = [](int) { return 0; };
  return gr;
}
IUnityGraphics gGraphics = makeGraphics();

UnityVulkanInstance UNITY_INTERFACE_API fakeInstance() {
  ++g.instanceCalls;
  return g.vkInstance;
}

// Like Unity: puts the texture into the requested layout (barrier on its own command buffer, synchronous) and
// returns image + metadata.
bool UNITY_INTERFACE_API fakeAccessTexture(void* nativeTexture, const VkImageSubresource* subResource,
                                           VkImageLayout layout, VkPipelineStageFlags stages, VkAccessFlags access,
                                           UnityVulkanResourceAccessMode mode, UnityVulkanImage* out) {
  ++g.accessTextureCalls;
  HostTexture* tex = nullptr;
  for (auto& [id, t] : g.textures)
    if (t.get() == nativeTexture) tex = t.get();
  if (!tex) {
    ++g.unknownTextureAccess;
    return false;
  }
  if (layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) ++g.badAccessLayout;
  if (mode != kUnityVulkanResourceAccess_PipelineBarrier) ++g.badAccessMode;
  (void)subResource;  // UnityVulkanWholeImage (nullptr)
  if (out == nullptr) return false;

  if (tex->layout != layout) {
    const VkImageLayout from = tex->layout;
    bool ok = hostSubmit([&](VkCommandBuffer cmd) {
      barrier(cmd, tex->image, tex->layers, from, layout, VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
              access, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, stages ? stages : static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
    });
    if (!ok) return false;
    tex->layout = layout;
  }
  UnityVulkanImage img{};
  img.image = tex->image;
  img.layout = tex->layout;
  img.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  img.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  img.format = tex->format;
  img.extent = {tex->width, tex->height, 1};
  img.tiling = VK_IMAGE_TILING_OPTIMAL;
  img.type = VK_IMAGE_TYPE_2D;
  img.samples = VK_SAMPLE_COUNT_1_BIT;
  img.layers = static_cast<int>(tex->layers);
  img.mipCount = 1;
  img.memory.memory = tex->memory;
  *out = img;
  return true;
}

// Optionally asynchronous: Unity may also call the AccessQueue callback later from the submission thread
// ("This may or may not be called synchronously or from the submission thread"). The plugin's frame end
// (copy into the swapchain image, xrEndFrame) then runs in parallel to the main thread (input tick) and the next Populate.
struct AsyncQueue {
  std::mutex m;
  std::condition_variable cv;
  std::deque<std::tuple<UnityRenderingEventAndData, int, void*>> q;
  std::thread th;
  bool quit = false, busy = false;
  std::atomic<int> executed{0};

  void start() {
    quit = false;
    th = std::thread([this] {
      std::unique_lock<std::mutex> l(m);
      for (;;) {
        cv.wait(l, [this] { return quit || !q.empty(); });
        if (q.empty() && quit) return;
        auto [cb, id, data] = q.front();
        q.pop_front();
        busy = true;
        l.unlock();
        usleep(3000);  // the submission thread does not get to it immediately
        if (cb) cb(id, data);
        ++executed;
        l.lock();
        busy = false;
        cv.notify_all();
      }
    });
  }
  void push(UnityRenderingEventAndData cb, int id, void* data) {
    std::lock_guard<std::mutex> l(m);
    q.emplace_back(cb, id, data);
    cv.notify_all();
  }
  void drainAndStop() {
    {
      std::unique_lock<std::mutex> l(m);
      cv.wait(l, [this] { return q.empty() && !busy; });
      quit = true;
      cv.notify_all();
    }
    if (th.joinable()) th.join();
  }
};
AsyncQueue gAsync;
bool gAsyncMode = false;

void UNITY_INTERFACE_API fakeAccessQueue(UnityRenderingEventAndData cb, int eventId, void* data, bool flush) {
  ++g.accessQueueCalls;
  if (!flush) ++g.badQueueFlush;
  if (gAsyncMode) {
    gAsync.push(cb, eventId, data);
  } else if (cb) {
    cb(eventId, data);  // synchronous, as allowed
  }
}

IUnityGraphicsVulkan makeGraphicsVulkan() {
  IUnityGraphicsVulkan v{};
  v.Instance = fakeInstance;
  v.EnsureOutsideRenderPass = []() { ++g.ensureOutsideCalls; };
  v.EnsureInsideRenderPass = []() {};
  v.AccessTexture = fakeAccessTexture;
  v.AccessQueue = fakeAccessQueue;
  v.ConfigureEvent = [](int, const UnityVulkanPluginEventConfig*) {};
  v.InterceptInitialization = [](UnityVulkanInitCallback, void*) { return false; };
  v.InterceptVulkanAPI = [](const char*, PFN_vkVoidFunction f) { return f; };
  v.CommandRecordingState = [](UnityVulkanRecordingState*, UnityVulkanGraphicsQueueAccess) { return false; };
  v.AccessRenderBufferTexture = [](UnityRenderBuffer, const VkImageSubresource*, VkImageLayout,
                                   VkPipelineStageFlags, VkAccessFlags, UnityVulkanResourceAccessMode,
                                   UnityVulkanImage*) { return false; };
  v.AccessRenderBufferResolveTexture = v.AccessRenderBufferTexture;
  v.AccessBuffer = [](void*, VkPipelineStageFlags, VkAccessFlags, UnityVulkanResourceAccessMode,
                      UnityVulkanBuffer*) { return false; };
  v.ConfigureSwapchain = [](const UnityVulkanSwapchainConfiguration*) { return false; };
  v.AccessTextureByID = [](UnityTextureID, const VkImageSubresource*, VkImageLayout, VkPipelineStageFlags,
                           VkAccessFlags, UnityVulkanResourceAccessMode, UnityVulkanImage*) { return false; };
  return v;
}
IUnityGraphicsVulkan gGraphicsVk = makeGraphicsVulkan();

// ============================================================================================
// IUnityXRDisplayInterface
// ============================================================================================

UnitySubsystemErrorCode UNITY_INTERFACE_API displayRegisterLifecycle(const char* pluginName, const char* id,
                                                                    const UnityLifecycleProvider* provider) {
  auto& reg = g.displayLifecycle;
  ++reg.calls;
  if (!provider || !pluginName || !id) return kUnitySubsystemErrorCodeInvalidArguments;
  reg.registered = true;
  reg.pluginName = pluginName;
  reg.id = id;
  reg.provider = *provider;
  return kUnitySubsystemErrorCodeSuccess;
}
UnitySubsystemErrorCode UNITY_INTERFACE_API displayRegisterProvider(UnitySubsystemHandle handle,
                                                                   const UnityXRDisplayProvider* provider) {
  if (!provider) return kUnitySubsystemErrorCodeInvalidArguments;
  g.displayMainRegistered = true;
  g.displayMainHandle = handle;
  g.displayMain = *provider;
  return kUnitySubsystemErrorCodeSuccess;
}
UnitySubsystemErrorCode UNITY_INTERFACE_API displayRegisterGfx(UnitySubsystemHandle handle,
                                                              const UnityXRDisplayGraphicsThreadProvider* provider) {
  if (!provider) return kUnitySubsystemErrorCodeInvalidArguments;
  g.displayGfxRegistered = true;
  g.displayGfxHandle = handle;
  g.displayGfx = *provider;
  return kUnitySubsystemErrorCodeSuccess;
}

// CreateTexture: real VkImage (RGBA8 sRGB or UNORM depending on the flag), usage TRANSFER_SRC|COLOR_ATTACHMENT|TRANSFER_DST,
// array layers = max(1, textureArrayLength), device-local. Content: a known color per layer.
UnitySubsystemErrorCode UNITY_INTERFACE_API displayCreateTexture(UnitySubsystemHandle, const UnityXRRenderTextureDesc* desc,
                                                                UnityXRRenderTextureId* out) {
  if (!desc || !out || desc->width == 0 || desc->height == 0) return kUnitySubsystemErrorCodeInvalidArguments;
  g.createdDescs.push_back(*desc);
  auto tex = std::make_unique<HostTexture>();
  tex->width = desc->width;
  tex->height = desc->height;
  tex->arrayLength = desc->textureArrayLength;
  tex->flags = desc->flags;
  tex->layers = desc->textureArrayLength > 1 ? desc->textureArrayLength : 1;
  tex->format = (desc->flags & kUnityXRRenderTextureFlagsSRGB) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

  auto ici = vkInfo<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = tex->format;
  ici.extent = {tex->width, tex->height, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = tex->layers;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (K.vkCreateImage(K.device, &ici, nullptr, &tex->image) != VK_SUCCESS) return kUnitySubsystemErrorCodeFailure;

  VkMemoryRequirements mr{};
  K.vkGetImageMemoryRequirements(K.device, tex->image, &mr);
  uint32_t type = UINT32_MAX;
  for (uint32_t i = 0; i < K.memProps.memoryTypeCount; ++i)
    if ((mr.memoryTypeBits & (1u << i)) &&
        (K.memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      type = i;
      break;
    }
  if (type == UINT32_MAX) {
    K.vkDestroyImage(K.device, tex->image, nullptr);
    return kUnitySubsystemErrorCodeFailure;
  }
  auto mai = vkInfo<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
  mai.allocationSize = mr.size;
  mai.memoryTypeIndex = type;
  if (K.vkAllocateMemory(K.device, &mai, nullptr, &tex->memory) != VK_SUCCESS ||
      K.vkBindImageMemory(K.device, tex->image, tex->memory, 0) != VK_SUCCESS) {
    if (tex->memory) K.vkFreeMemory(K.device, tex->memory, nullptr);
    K.vkDestroyImage(K.device, tex->image, nullptr);
    return kUnitySubsystemErrorCodeFailure;
  }

  // Known content: a different color per layer; final layout TRANSFER_DST (Unity textures are in an arbitrary layout
  // after rendering - the plugin must use AccessTexture to get them into TRANSFER_SRC).
  const bool filled = hostSubmit([&](VkCommandBuffer cmd) {
    barrier(cmd, tex->image, tex->layers, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    for (uint32_t l = 0; l < tex->layers; ++l) {
      VkClearColorValue c{};
      c.float32[0] = l == 0 ? 1.0f : 0.0f;
      c.float32[1] = 0.25f;
      c.float32[2] = l == 0 ? 0.0f : 1.0f;
      c.float32[3] = 1.0f;
      VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, l, 1};
      K.vkCmdClearColorImage(cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &r);
    }
  });
  if (!filled) return kUnitySubsystemErrorCodeFailure;
  tex->layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

  tex->id = g.nextTexId++;
  *out = tex->id;
  g.textures[tex->id] = std::move(tex);
  ++g.texturesCreated;
  return kUnitySubsystemErrorCodeSuccess;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API displayQueryTextureDesc(UnitySubsystemHandle, UnityXRRenderTextureId id,
                                                                   UnityXRRenderTextureDesc* out) {
  auto it = g.textures.find(id);
  if (it == g.textures.end() || !out) return kUnitySubsystemErrorCodeFailure;
  const HostTexture& t = *it->second;
  UnityXRRenderTextureDesc d{};
  d.colorFormat = kUnityXRRenderTextureFormatRGBA32;
  d.color.nativePtr = const_cast<HostTexture*>(&t);  // opaque; AccessTexture maps it to the VkImage
  d.depthFormat = kUnityXRDepthTextureFormat24bitOrGreater;
  d.depth.nativePtr = nullptr;
  d.width = t.width;
  d.height = t.height;
  d.textureArrayLength = t.arrayLength;
  d.flags = t.flags;
  *out = d;
  return kUnitySubsystemErrorCodeSuccess;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API displayDestroyTexture(UnitySubsystemHandle, UnityXRRenderTextureId id) {
  auto it = g.textures.find(id);
  if (it == g.textures.end()) return kUnitySubsystemErrorCodeFailure;
  HostTexture& t = *it->second;
  K.vkDeviceWaitIdle(K.device);
  K.vkDestroyImage(K.device, t.image, nullptr);
  K.vkFreeMemory(K.device, t.memory, nullptr);
  g.textures.erase(it);
  ++g.texturesDestroyed;
  return kUnitySubsystemErrorCodeSuccess;
}

IUnityXRDisplayInterface makeDisplayInterface() {
  IUnityXRDisplayInterface d{};
  d.RegisterLifecycleProvider = displayRegisterLifecycle;
  d.RegisterProvider = displayRegisterProvider;
  d.RegisterProviderForGraphicsThread = displayRegisterGfx;
  d.CreateTexture = displayCreateTexture;
  d.QueryTextureDesc = displayQueryTextureDesc;
  d.DestroyTexture = displayDestroyTexture;
  d.GetPlatformData = [](UnitySubsystemHandle, void** p) {
    if (p) *p = nullptr;
    return kUnitySubsystemErrorCodeNotSupported;
  };
  d.CreateOcclusionMesh = [](UnitySubsystemHandle, uint32_t, uint32_t, UnityXROcclusionMeshId* out) {
    if (out) *out = 1;
    return kUnitySubsystemErrorCodeSuccess;
  };
  d.DestroyOcclusionMesh = [](UnitySubsystemHandle, UnityXROcclusionMeshId) { return kUnitySubsystemErrorCodeSuccess; };
  d.SetOcclusionMesh = [](UnitySubsystemHandle, UnityXROcclusionMeshId, UnityXRVector2*, uint32_t, uint32_t*, uint32_t) {
    return kUnitySubsystemErrorCodeSuccess;
  };
  return d;
}
IUnityXRDisplayInterface gDisplay = makeDisplayInterface();

// ============================================================================================
// IUnityXRInputInterface: records device definitions and states
// ============================================================================================

using E = UnitySubsystemErrorCode;
using H = UnitySubsystemHandle;
using Dev = UnityXRInternalInputDeviceId;
using Def = UnityXRInputDeviceDefinition;
using St = UnityXRInputDeviceState;
using Idx = UnityXRInputFeatureIndex;

E UNITY_INTERFACE_API inputRegisterLifecycle(const char* pluginName, const char* id, const UnityLifecycleProvider* provider) {
  auto& reg = g.inputLifecycle;
  ++reg.calls;
  if (!provider || !pluginName || !id) return kUnitySubsystemErrorCodeInvalidArguments;
  reg.registered = true;
  reg.pluginName = pluginName;
  reg.id = id;
  reg.provider = *provider;
  return kUnitySubsystemErrorCodeSuccess;
}
E UNITY_INTERFACE_API inputRegisterProvider(H handle, const UnityXRInputProvider* provider) {
  if (!provider) return kUnitySubsystemErrorCodeInvalidArguments;
  g.inputProviderRegistered = true;
  g.inputHandle = handle;
  g.inputProvider = *provider;
  return kUnitySubsystemErrorCodeSuccess;
}

Idx addFeature(Def* d, const char* name, UnityXRInputFeatureType type, const char* usage) {
  d->features.push_back({name ? name : "", type, usage ? usage : ""});
  return static_cast<Idx>(d->features.size() - 1);
}

void setValue(St* s, Idx i, UnityXRInputFeatureType t, std::array<float, 4> v) { s->values[i] = {t, v}; }

IUnityXRInputInterface makeInputInterface() {
  IUnityXRInputInterface in{};
  in.RegisterLifecycleProvider = inputRegisterLifecycle;
  in.RegisterInputProvider = inputRegisterProvider;
  in.InputSubsystem_DeviceConnected = [](H, Dev id) {
    ++g.connectCalls;
    g.connected.insert(id);
    return E(kUnitySubsystemErrorCodeSuccess);
  };
  in.InputSubsystem_DeviceDisconnected = [](H, Dev id) {
    ++g.disconnectCalls;
    g.connected.erase(id);
    return E(kUnitySubsystemErrorCodeSuccess);
  };
  in.InputSubsystem_DeviceConfigChanged = [](H, Dev) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.InputSubsystem_TrackingOriginUpdated = [](H) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.InputSubsystem_SetTrackingBoundary = [](H, const UnityXRVector3*, uint32_t) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.InputSubsystem_GetPlatformData = [](H, void**) { return E(kUnitySubsystemErrorCodeNotSupported); };
  in.DeviceDefinition_SetName = [](Def* d, const char* n) {
    d->name = n ? n : "";
    return E(kUnitySubsystemErrorCodeSuccess);
  };
  in.DeviceDefinition_SetCharacteristics = [](Def* d, UnityXRInputDeviceCharacteristics c) {
    d->characteristics = c;
    return E(kUnitySubsystemErrorCodeSuccess);
  };
  in.DeviceDefinition_SetManufacturer = [](Def* d, const char* n) {
    d->manufacturer = n ? n : "";
    return E(kUnitySubsystemErrorCodeSuccess);
  };
  in.DeviceDefinition_SetSerialNumber = [](Def*, const char*) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceDefinition_SetCanQueryForDeviceStateAtTime = [](Def*, bool) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceDefinition_AddFeature = [](Def* d, const char* n, UnityXRInputFeatureType t) { return addFeature(d, n, t, nullptr); };
  in.DeviceDefinition_AddCustomFeature = [](Def* d, const char* n, unsigned int) {
    return addFeature(d, n, kUnityXRInputFeatureTypeCustom, nullptr);
  };
  in.DeviceDefinition_AddFeatureWithUsage = [](Def* d, const char* n, UnityXRInputFeatureType t, UnityXRInputFeatureUsage u) {
    return addFeature(d, n, t, u);
  };
  in.DeviceDefinition_AddUsageAtIndex = [](Def*, Idx, UnityXRInputFeatureUsage) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetCustomValue = [](St*, Idx, const void*, unsigned int) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetBinaryValue = [](St* s, Idx i, bool v) {
    setValue(s, i, kUnityXRInputFeatureTypeBinary, {v ? 1.0f : 0.0f, 0, 0, 0});
    return E(kUnitySubsystemErrorCodeSuccess);
  };
  in.DeviceState_SetDiscreteStateValue = [](St* s, Idx i, unsigned int v) {
    setValue(s, i, kUnityXRInputFeatureTypeDiscreteStates, {static_cast<float>(v), 0, 0, 0});
    return E(kUnitySubsystemErrorCodeSuccess);
  };
  in.DeviceState_SetAxis1DValue = [](St* s, Idx i, float v) {
    setValue(s, i, kUnityXRInputFeatureTypeAxis1D, {v, 0, 0, 0});
    return E(kUnitySubsystemErrorCodeSuccess);
  };
  in.DeviceState_SetAxis2DValue = [](St* s, Idx i, UnityXRVector2 v) {
    setValue(s, i, kUnityXRInputFeatureTypeAxis2D, {v.x, v.y, 0, 0});
    return E(kUnitySubsystemErrorCodeSuccess);
  };
  in.DeviceState_SetAxis3DValue = [](St* s, Idx i, UnityXRVector3 v) {
    setValue(s, i, kUnityXRInputFeatureTypeAxis3D, {v.x, v.y, v.z, 0});
    return E(kUnitySubsystemErrorCodeSuccess);
  };
  in.DeviceState_SetRotationValue = [](St* s, Idx i, UnityXRVector4 v) {
    setValue(s, i, kUnityXRInputFeatureTypeRotation, {v.x, v.y, v.z, v.w});
    return E(kUnitySubsystemErrorCodeSuccess);
  };
  in.DeviceState_SetBoneValue = [](St*, Idx, UnityXRBone) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetHandValue = [](St*, Idx, UnityXRHand) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetEyesValue = [](St*, Idx, UnityXREyes) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetDeviceTime = [](St*, UnityXRTimeStamp) { return E(kUnitySubsystemErrorCodeSuccess); };
  return in;
}
IUnityXRInputInterface gInput = makeInputInterface();

// ---- IUnityInterfaces (registry) -------------------------------------------------------

IUnityInterface* lookup(unsigned long long high, unsigned long long low) {
  const UnityInterfaceGUID want(high, low);
  if (want == GetUnityInterfaceGUID<IUnityXRTrace>()) return &gTrace;
  if (want == GetUnityInterfaceGUID<IUnityXRPreInit>()) return &gPreInit;
  if (want == GetUnityInterfaceGUID<IUnityXRDisplayInterface>()) return &gDisplay;
  if (want == GetUnityInterfaceGUID<IUnityXRInputInterface>()) return &gInput;
  if (want == GetUnityInterfaceGUID<IUnityGraphics>()) return &gGraphics;
  if (want == GetUnityInterfaceGUID<IUnityGraphicsVulkan>()) return &gGraphicsVk;
  return nullptr;
}
IUnityInterface* UNITY_INTERFACE_API fakeGetInterface(UnityInterfaceGUID guid) {
  return lookup(guid.m_GUIDHigh, guid.m_GUIDLow);
}
IUnityInterface* UNITY_INTERFACE_API fakeGetInterfaceSplit(unsigned long long high, unsigned long long low) {
  return lookup(high, low);
}
void UNITY_INTERFACE_API fakeRegisterInterface(UnityInterfaceGUID, IUnityInterface*) {}
void UNITY_INTERFACE_API fakeRegisterInterfaceSplit(unsigned long long, unsigned long long, IUnityInterface*) {}
IUnityInterfaces gInterfaces = {fakeGetInterface, fakeRegisterInterface, fakeGetInterfaceSplit, fakeRegisterInterfaceSplit};

// ============================================================================================
// Plugin exports
// ============================================================================================

struct LinuxXRStatus {  // mirror of src/unity/native_api.cpp
  int preinitRan;
  int preinitRuntimeQueried;
  int displayState;
  int inputState;
  int sessionState;
  int runtimeReachable;
  float refreshRate;
  unsigned long long framesSubmitted;
};

struct LinuxXRStats {  // mirror of src/unity/native_api.cpp
  int structSize;
  int sessionActive;
  unsigned long long framesBegun, framesRendered, framesEmpty, framesNoRender, framesMissed, framesDropped;
  unsigned long long logWarnings, logErrors;
  double sessionSeconds;
  float headsetFps, appFps, waitFrameMs, submitMs, displayPeriodMs, renderScale;
  int eyeWidth, eyeHeight, singlePass, vulkanChecked, vulkanComplete;
  long long swapchainFormat;
};

struct Plugin {
  void* lib = nullptr;
  void (*getStats)(LinuxXRStats*, int) = nullptr;
  int (*popLog)(int*, char*, int) = nullptr;
  int (*getLogHistory)(int, char*, int) = nullptr;
  void (*setConsoleForwarding)(int) = nullptr;
  void (*preInit)(IUnityInterfaces*) = nullptr;
  void (*load)(IUnityInterfaces*) = nullptr;
  void (*unload)() = nullptr;
  const char* (*getVersion)() = nullptr;
  void (*getStatus)(LinuxXRStatus*) = nullptr;
  int (*getDiagnostics)(char*, int) = nullptr;
};
Plugin P;

template <class Fn>
bool resolve(Fn& out, const char* name) {
  out = reinterpret_cast<Fn>(dlsym(P.lib, name));
  ++lxr_test::checkCount();
  if (!out) lxr_test::reportFail(__FILE__, __LINE__, std::string("Symbol missing in libLinuxXR.so: ") + name);
  return out != nullptr;
}

LinuxXRStatus status() {
  LinuxXRStatus s;
  memset(&s, 0xAB, sizeof s);
  P.getStatus(&s);
  return s;
}

// Statistics (LinuxXR_GetStats) and log export (PopLog / GetLogHistory) after a few frames
void checkStatsAndLog() {
  std::printf("  Statistics and log export\n");
  LinuxXRStats st;
  memset(&st, 0, sizeof st);
  P.getStats(&st, static_cast<int>(sizeof st));
  CHECK_EQ(st.structSize, static_cast<int>(sizeof(LinuxXRStats)));
  CHECK_EQ(st.sessionActive, 1);
  CHECK(st.framesBegun > 0);
  CHECK(st.framesRendered > 0);
  CHECK(st.framesRendered + st.framesEmpty + st.framesDropped <= st.framesBegun + 1);
  CHECK(st.headsetFps > 0.0f && st.headsetFps < 1000.0f);
  CHECK(st.appFps > 0.0f);
  CHECK(st.waitFrameMs >= 0.0f);
  CHECK(st.displayPeriodMs > 0.0f);
  CHECK(st.eyeWidth >= 64 && st.eyeHeight >= 64);
  CHECK_EQ(st.singlePass, 1);
  CHECK_EQ(st.vulkanChecked, 1);
  CHECK(st.sessionSeconds > 0.0);
  std::printf("  frames begun %llu, rendered %llu, empty %llu, missed %llu, dropped %llu, headset %.1f fps, "
              "app %.1f fps, wait %.2f ms, submit %.2f ms, eye %dx%d, vulkan complete %d\n",
              st.framesBegun, st.framesRendered, st.framesEmpty, st.framesMissed, st.framesDropped,
              static_cast<double>(st.headsetFps), static_cast<double>(st.appFps),
              static_cast<double>(st.waitFrameMs), static_cast<double>(st.submitMs), st.eyeWidth, st.eyeHeight,
              st.vulkanComplete);

  // Struct too small: only structSize is written, nothing beyond it
  struct {
    int size;
    int guard;
  } small{0, 0x5A5A5A5A};
  P.getStats(reinterpret_cast<LinuxXRStats*>(&small), static_cast<int>(sizeof(int)));
  CHECK_EQ(small.size, static_cast<int>(sizeof(LinuxXRStats)));
  CHECK_EQ(small.guard, 0x5A5A5A5A);

  // Log history contains the session messages
  int need = P.getLogHistory(50, nullptr, 0);
  CHECK(need > 1);
  std::string hist(static_cast<size_t>(need), '\0');
  P.getLogHistory(50, hist.data(), need);
  CHECK(hist.find("[info]") != std::string::npos);

  // Console queue: drain it, then a new message must arrive exactly once
  char buf[4096];
  int level = -1, drained = 0;
  while (P.popLog(&level, buf, sizeof buf) > 0 && drained < 5000) ++drained;
  CHECK(drained > 0);
  CHECK_EQ(P.popLog(&level, buf, sizeof buf), 0);
  // lxr::logf from the library itself (the symbol is exported), so the message goes through its logging
  using LogFn = void (*)(int, const char*, ...);
  auto pluginLog = reinterpret_cast<LogFn>(dlsym(P.lib, "_ZN3lxr4logfENS_8LogLevelEPKcz"));
  CHECK(pluginLog != nullptr);
  if (!pluginLog) return;
  P.setConsoleForwarding(1);
  int tracesBefore = g.traceTotal;
  pluginLog(2, "console-forwarding-test %d", 42);
  CHECK_EQ(g.traceTotal, tracesBefore);  // Unity trace skipped while C# forwards
  int n = P.popLog(&level, buf, sizeof buf);
  CHECK(n > 0);
  CHECK_EQ(level, 2);
  CHECK(std::string(buf).find("console-forwarding-test 42") != std::string::npos);
  P.setConsoleForwarding(0);
  pluginLog(1, "trace-again");
  CHECK(g.traceTotal > tracesBefore);
  while (P.popLog(&level, buf, sizeof buf) > 0) {
  }
}

std::string diagnostics() {
  const int needed = P.getDiagnostics(nullptr, 0);
  std::vector<char> buf(static_cast<size_t>(needed) + 1, 0);
  P.getDiagnostics(buf.data(), needed);
  return std::string(buf.data());
}

bool extensionString(bool (UNITY_INTERFACE_API* fn)(void*, uint32_t, uint32_t*, char*), void* ud, std::string& out) {
  uint32_t needed = 0;
  if (!fn(ud, 0, &needed, nullptr) || needed < 1) return false;
  std::vector<char> buf(needed + 8, 0);
  uint32_t written = 0;
  if (!fn(ud, static_cast<uint32_t>(buf.size()), &written, buf.data())) return false;
  out = std::string(buf.data());
  return true;
}

// ============================================================================================
// Helpers for the frame loop
// ============================================================================================

const UnitySubsystemHandle kDisplayHandle = reinterpret_cast<UnitySubsystemHandle>(static_cast<uintptr_t>(0xD15));
const UnitySubsystemHandle kInputHandle = reinterpret_cast<UnitySubsystemHandle>(static_cast<uintptr_t>(0x1AB));

constexpr int kSessionVisible = 4;  // XR_SESSION_STATE_VISIBLE
constexpr int kSessionFocused = 5;  // XR_SESSION_STATE_FOCUSED

struct Observed {
  bool sawRunning = false, sawFocused = false;
  bool hmdTracked = false;
  int hmdSamples = 0;
  float hmdPos[3] = {0, 0, 0};
  float hmdRot[4] = {0, 0, 0, 1};
  float eyeSeparation = 0;
};
Observed gObs;

float lenSq(const float* v, int n) {
  float s = 0;
  for (int i = 0; i < n; ++i) s += v[i] * v[i];
  return s;
}

bool findFeature(uint32_t dev, const char* name, Idx& idx, UnityXRInputFeatureType* type = nullptr) {
  auto it = g.definitions.find(dev);
  if (it == g.definitions.end()) return false;
  for (size_t i = 0; i < it->second.features.size(); ++i)
    if (it->second.features[i].name == name) {
      idx = static_cast<Idx>(i);
      if (type) *type = it->second.features[i].type;
      return true;
    }
  return false;
}

// Reads states of device `dev` (like Unity per frame) and remembers HMD values.
void updateDevice(uint32_t dev, UnityXRInputUpdateType type, bool strict) {
  const UnityXRInputProvider& in = g.inputProvider;
  UnityXRInputDeviceState st;
  const E rc = in.UpdateDeviceState(kInputHandle, in.userData, dev, type, &st);
  if (strict) CHECK_EQ(rc, kUnitySubsystemErrorCodeSuccess);
  if (dev != 1) return;
  Idx ip, ir, it;
  if (!findFeature(1, "DevicePosition", ip) || !findFeature(1, "DeviceRotation", ir) || !findFeature(1, "IsTracked", it))
    return;
  auto vp = st.values.find(ip), vr = st.values.find(ir), vt = st.values.find(it);
  if (vp == st.values.end() || vr == st.values.end() || vt == st.values.end()) return;
  ++gObs.hmdSamples;
  if (vt->second.v[0] != 0.0f) gObs.hmdTracked = true;
  for (int i = 0; i < 3; ++i) gObs.hmdPos[i] = vp->second.v[static_cast<size_t>(i)];
  for (int i = 0; i < 4; ++i) gObs.hmdRot[i] = vr->second.v[static_cast<size_t>(i)];
}

struct Hints {
  bool singlePass = true;
  bool srgb = true;
  float scale = 1.0f;
};

UnityXRFrameSetupHints makeHints(const Hints& h) {
  UnityXRFrameSetupHints hints{};
  hints.appSetup.singlePassRendering = h.singlePass;
  hints.appSetup.zNear = 0.1f;
  hints.appSetup.zFar = 1000.0f;
  hints.appSetup.sRGB = h.srgb;
  hints.appSetup.textureResolutionScale = h.scale;
  hints.changedFlags = kUnityXRFrameSetupHintsChangedSinglePassRendering;
  return hints;
}

bool finitePose(const UnityXRPose& p) {
  return std::isfinite(p.position.x) && std::isfinite(p.position.y) && std::isfinite(p.position.z) &&
         std::isfinite(p.rotation.x) && std::isfinite(p.rotation.y) && std::isfinite(p.rotation.z) &&
         std::isfinite(p.rotation.w);
}

float quatNorm(const UnityXRVector4& q) { return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); }

bool validProjection(const UnityXRProjection& p) {
  if (p.type != kUnityXRProjectionTypeHalfAngles) return false;
  const auto& h = p.data.halfAngles;
  return std::isfinite(h.left) && std::isfinite(h.right) && std::isfinite(h.top) && std::isfinite(h.bottom) &&
         h.left < h.right && h.bottom < h.top;
}

struct LoopResult {
  int iterations = 0, rendered = 0, emptyPopulates = 0;
  UnityXRNextFrameDesc lastDesc{};
  bool timedOut = false;
};

// Drives frames like Unity: input (Dynamic + BeforeRender), Populate, [Unity renders], Submit.
// Runs until `wantRendered` frames with render passes were produced (or timeout).
LoopResult runFrames(const char* what, const Hints& hints, int wantRendered, int minIterations, double timeoutSec) {
  const UnityXRInputProvider& in = g.inputProvider;
  const UnityXRDisplayGraphicsThreadProvider& gfx = g.displayGfx;
  const UnityXRFrameSetupHints h = makeHints(hints);
  LoopResult r;
  const double t0 = nowSec();
  while ((r.rendered < wantRendered || r.iterations < minIterations) && nowSec() - t0 < timeoutSec) {
    ++r.iterations;

    // --- Input (main thread) ---
    CHECK_EQ(in.Tick(kInputHandle, in.userData, kUnityXRInputUpdateTypeDynamic), kUnitySubsystemErrorCodeSuccess);
    for (uint32_t dev : {1u, 2u, 3u}) updateDevice(dev, kUnityXRInputUpdateTypeDynamic, true);
    CHECK_EQ(in.Tick(kInputHandle, in.userData, kUnityXRInputUpdateTypeBeforeRender), kUnitySubsystemErrorCodeSuccess);
    updateDevice(1, kUnityXRInputUpdateTypeBeforeRender, true);

    // --- Display (gfx thread) ---
    UnityXRNextFrameDesc next{};
    next.renderPassesCount = 99;  // control sample
    next.mirrorBlitMode = kUnityXRMirrorBlitDefault;
    CHECK_EQ(gfx.PopulateNextFrameDesc(kDisplayHandle, gfx.userData, &h, &next), kUnitySubsystemErrorCodeSuccess);
    CHECK_EQ(next.mirrorBlitMode, kUnityXRMirrorBlitLeftEye);  // default: left eye

    const LinuxXRStatus s = status();
    if (s.sessionState == kSessionVisible || s.sessionState == kSessionFocused) gObs.sawRunning = true;
    if (s.sessionState == kSessionFocused) gObs.sawFocused = true;

    if (next.renderPassesCount > 0) {
      ++r.rendered;
      r.lastDesc = next;
      if (hints.singlePass) {
        CHECK_EQ(next.renderPassesCount, 1);
        const auto& pass = next.renderPasses[0];
        CHECK_EQ(pass.renderParamsCount, 2);
        CHECK(pass.textureId != 0);
        CHECK(g.textures.count(pass.textureId) == 1);
        CHECK_EQ(pass.cullingPassIndex, 0u);
        CHECK_EQ(pass.renderParams[0].textureArraySlice, 0);
        CHECK_EQ(pass.renderParams[1].textureArraySlice, 1);
        for (int e = 0; e < 2; ++e) {
          CHECK(finitePose(pass.renderParams[e].deviceAnchorToEyePose));
          CHECK_NEAR(quatNorm(pass.renderParams[e].deviceAnchorToEyePose.rotation), 1.0f, 1e-3f);
          CHECK(validProjection(pass.renderParams[e].projection));
        }
        const float sep = pass.renderParams[1].deviceAnchorToEyePose.position.x -
                          pass.renderParams[0].deviceAnchorToEyePose.position.x;
        gObs.eyeSeparation = sep;
        CHECK(sep > 0.0f && sep < 0.2f);  // left eye left of the right one, plausible eye distance
        auto it = g.textures.find(pass.textureId);
        if (it != g.textures.end()) CHECK_EQ(it->second->layers, 2u);
      } else {
        CHECK_EQ(next.renderPassesCount, 2);
        CHECK(next.renderPasses[0].textureId != 0 && next.renderPasses[1].textureId != 0);
        CHECK(next.renderPasses[0].textureId != next.renderPasses[1].textureId);
        for (int e = 0; e < 2; ++e) {
          const auto& pass = next.renderPasses[e];
          CHECK_EQ(pass.renderParamsCount, 1);
          CHECK_EQ(pass.renderParams[0].textureArraySlice, 0);
          CHECK(g.textures.count(pass.textureId) == 1);
          CHECK(finitePose(pass.renderParams[0].deviceAnchorToEyePose));
          CHECK(validProjection(pass.renderParams[0].projection));
          auto it = g.textures.find(pass.textureId);
          if (it != g.textures.end()) CHECK_EQ(it->second->layers, 1u);
        }
      }
      CHECK(finitePose(next.cullingPasses[0].deviceAnchorToCullingPose));
      CHECK(validProjection(next.cullingPasses[0].projection));
    } else {
      ++r.emptyPopulates;
      usleep(5 * 1000);  // session not running yet: do not busy-spin
    }

    // (Unity would render here.) Submit: Unity also calls Submit with 0 render passes.
    CHECK_EQ(gfx.SubmitCurrentFrame(kDisplayHandle, gfx.userData), kUnitySubsystemErrorCodeSuccess);
  }
  r.timedOut = r.rendered < wantRendered;
  CHECK(!r.timedOut);
  std::printf("  %s: %d iterations, %d frames with render passes, %d empty populates, %.1f s\n", what, r.iterations,
              r.rendered, r.emptyPopulates, nowSec() - t0);
  return r;
}

// Check the mirror blit description against the most recently delivered render passes.
void checkMirror(const Hints& hints, const UnityXRNextFrameDesc& d) {
  const UnityXRDisplayProvider& main = g.displayMain;
  auto query = [&](int mode, UnityXRMirrorViewBlitDesc& desc) {
    UnityXRMirrorViewBlitInfo info{};
    info.mirrorBlitMode = mode;
    desc = {};
    desc.blitParamsCount = 77;
    desc.nativeBlitAvailable = true;
    CHECK_EQ(main.QueryMirrorViewBlitDesc(kDisplayHandle, main.userData, info, &desc), kUnitySubsystemErrorCodeSuccess);
    CHECK(!desc.nativeBlitAvailable);
  };
  const UnityXRRenderTextureId t0 = d.renderPasses[0].textureId;
  const UnityXRRenderTextureId t1 = hints.singlePass ? t0 : d.renderPasses[1].textureId;
  const int slice1 = hints.singlePass ? 1 : 0;
  UnityXRMirrorViewBlitDesc desc;

  for (int mode : {kUnityXRMirrorBlitDefault, kUnityXRMirrorBlitLeftEye}) {  // default = left eye
    query(mode, desc);
    CHECK_EQ(desc.blitParamsCount, 1);
    CHECK_EQ(desc.blitParams[0].srcTexId, t0);
    CHECK_EQ(desc.blitParams[0].srcTexArraySlice, 0);
    CHECK_NEAR(desc.blitParams[0].destRect.width, 1.0f, 1e-6f);
    CHECK_NEAR(desc.blitParams[0].destRect.height, 1.0f, 1e-6f);
  }
  query(kUnityXRMirrorBlitRightEye, desc);
  CHECK_EQ(desc.blitParamsCount, 1);
  CHECK_EQ(desc.blitParams[0].srcTexId, t1);
  CHECK_EQ(desc.blitParams[0].srcTexArraySlice, slice1);
  query(kUnityXRMirrorBlitSideBySide, desc);
  CHECK_EQ(desc.blitParamsCount, 2);
  CHECK_EQ(desc.blitParams[0].srcTexId, t0);
  CHECK_EQ(desc.blitParams[0].srcTexArraySlice, 0);
  CHECK_EQ(desc.blitParams[1].srcTexId, t1);
  CHECK_EQ(desc.blitParams[1].srcTexArraySlice, slice1);
  CHECK_NEAR(desc.blitParams[0].destRect.x, 0.0f, 1e-6f);
  CHECK_NEAR(desc.blitParams[1].destRect.x, 0.5f, 1e-6f);
  query(kUnityXRMirrorBlitNone, desc);
  CHECK_EQ(desc.blitParamsCount, 0);
}

// ============================================================================================
// One Play run
// ============================================================================================

size_t countLines(const char* needle) {
  size_t n = 0;
  for (auto& l : g.traceLines)
    if (contains(l, needle)) ++n;
  return n;
}

void checkDevices() {
  const UnityXRInputProvider& in = g.inputProvider;
  g.definitions.clear();
  for (uint32_t id : {1u, 2u, 3u}) {
    UnityXRInputDeviceDefinition def;
    CHECK_EQ(in.FillDeviceDefinition(kInputHandle, in.userData, id, &def), kUnitySubsystemErrorCodeSuccess);
    CHECK(!def.name.empty());
    CHECK_EQ(def.manufacturer, std::string("LinuxXR"));
    CHECK(def.features.size() >= 6);
    std::set<std::string> names;
    for (auto& f : def.features) names.insert(f.name);
    CHECK_EQ(names.size(), def.features.size());  // names unique
    CHECK(names.count("DevicePosition") == 1 && names.count("DeviceRotation") == 1 && names.count("IsTracked") == 1);
    const unsigned c = static_cast<unsigned>(def.characteristics);
    CHECK((c & kUnityXRInputDeviceCharacteristicsTrackedDevice) != 0);
    if (id == 1) {
      CHECK_EQ(def.name, std::string("LinuxXR HMD"));
      CHECK((c & kUnityXRInputDeviceCharacteristicsHeadMounted) != 0);
      CHECK(names.count("CenterEyePosition") == 1 && names.count("LeftEyePosition") == 1 &&
            names.count("RightEyePosition") == 1);
    } else {
      CHECK((c & kUnityXRInputDeviceCharacteristicsController) != 0);
      CHECK((c & kUnityXRInputDeviceCharacteristicsHeldInHand) != 0);
      CHECK((c & (id == 2 ? kUnityXRInputDeviceCharacteristicsLeft : kUnityXRInputDeviceCharacteristicsRight)) != 0);
      CHECK((c & (id == 2 ? kUnityXRInputDeviceCharacteristicsRight : kUnityXRInputDeviceCharacteristicsLeft)) == 0);
      CHECK(names.count("Trigger") == 1 && names.count("Primary2DAxis") == 1 && names.count("Grip") == 1);
    }
    g.definitions[id] = std::move(def);
  }
  UnityXRInputDeviceDefinition bad;
  CHECK_EQ(in.FillDeviceDefinition(kInputHandle, in.userData, 99, &bad), kUnitySubsystemErrorCodeInvalidArguments);
}


enum class Mode {
  Full,        // all phases: single/multi-pass, scale, sRGB, Populate without Submit, Stop->Start without Shutdown
  AsyncQueue,  // AccessQueue callback runs asynchronously on its own thread
  KillMonado,  // Monado is killed mid-operation (-9): the plugin must not crash/hang
};

// Initialize + Start of both subsystems (+ Gfx.Start). Returns false if Start fails.
bool startSubsystems() {
  const UnityLifecycleProvider& dl = g.displayLifecycle.provider;
  const UnityLifecycleProvider& il = g.inputLifecycle.provider;
  const UnityXRDisplayGraphicsThreadProvider& gfx = g.displayGfx;
  CHECK_EQ(dl.Start(kDisplayHandle, dl.userData), kUnitySubsystemErrorCodeSuccess);
  CHECK_EQ(il.Start(kInputHandle, il.userData), kUnitySubsystemErrorCodeSuccess);
  if (status().displayState != 2 || status().inputState != 2) {
    std::fprintf(stderr, "  Start failed - error traces:\n");
    for (auto& l : g.traceErrorLines) std::fprintf(stderr, "    %s\n", l.c_str());
    return false;
  }
  CHECK(status().sessionState != 0);  // session exists (state IDLE/READY/...)
  CHECK(g.connected == (std::set<uint32_t>{1, 2, 3}));
  UnityXRRenderingCapabilities caps{};
  caps.skipPresentToMainScreen = true;
  CHECK_EQ(gfx.Start(kDisplayHandle, gfx.userData, &caps), kUnitySubsystemErrorCodeSuccess);
  CHECK(!caps.skipPresentToMainScreen);
  CHECK(caps.invalidateRenderStateAfterEachCallback);
  return true;
}

// Unity's order when ending Play: Gfx.Stop -> Display.Stop -> Input.Stop (the session persists)
void stopSubsystems() {
  const UnityLifecycleProvider& dl = g.displayLifecycle.provider;
  const UnityLifecycleProvider& il = g.inputLifecycle.provider;
  const UnityXRDisplayGraphicsThreadProvider& gfx = g.displayGfx;
  CHECK_EQ(gfx.Stop(kDisplayHandle, gfx.userData), kUnitySubsystemErrorCodeSuccess);
  dl.Stop(kDisplayHandle, dl.userData);
  il.Stop(kInputHandle, il.userData);
  CHECK_EQ(g.textures.size(), 0u);  // Gfx.Stop releases all eye textures
  CHECK(g.connected.empty());
  CHECK_EQ(status().displayState, 1);
  CHECK_EQ(status().inputState, 1);
}

void checkHostInvariants() {
  CHECK_EQ(g.badAccessLayout, 0);  // AccessTexture always with TRANSFER_SRC_OPTIMAL
  CHECK_EQ(g.badAccessMode, 0);    // ... and PipelineBarrier
  CHECK_EQ(g.unknownTextureAccess, 0);
  CHECK_EQ(g.badQueueFlush, 0);    // AccessQueue always with flush=true
  CHECK(g.accessTextureCalls > 0);
  CHECK(g.accessQueueCalls > 0);
  CHECK(g.ensureOutsideCalls > 0);
  std::printf("  Host counters: AccessTexture %d, AccessQueue %d, EnsureOutsideRenderPass %d\n", g.accessTextureCalls,
              g.accessQueueCalls, g.ensureOutsideCalls);
}

void checkHmdAndControllers() {
  const UnityXRInputProvider& in = g.inputProvider;
  CHECK(gObs.hmdSamples > 0);
  CHECK(gObs.hmdTracked);
  CHECK(std::isfinite(gObs.hmdPos[0]) && std::isfinite(gObs.hmdPos[1]) && std::isfinite(gObs.hmdPos[2]));
  CHECK(lenSq(gObs.hmdPos, 3) < 100.0f);  // < 10 m from the origin
  CHECK_NEAR(std::sqrt(lenSq(gObs.hmdRot, 4)), 1.0f, 1e-3f);
  std::printf("  HMD: pos (%.3f, %.3f, %.3f) rot (%.3f, %.3f, %.3f, %.3f), eye separation %.3f m\n",
              static_cast<double>(gObs.hmdPos[0]), static_cast<double>(gObs.hmdPos[1]),
              static_cast<double>(gObs.hmdPos[2]), static_cast<double>(gObs.hmdRot[0]),
              static_cast<double>(gObs.hmdRot[1]), static_cast<double>(gObs.hmdRot[2]),
              static_cast<double>(gObs.hmdRot[3]), static_cast<double>(gObs.eyeSeparation));
  // Controllers: consistency only (finite values); the simulated controllers may not be "tracked"
  for (uint32_t dev : {2u, 3u}) {
    UnityXRInputDeviceState st;
    CHECK_EQ(in.UpdateDeviceState(kInputHandle, in.userData, dev, kUnityXRInputUpdateTypeDynamic, &st),
             kUnitySubsystemErrorCodeSuccess);
    Idx ip;
    if (findFeature(dev, "DevicePosition", ip) && st.values.count(ip)) {
      const auto& v = st.values[ip].v;
      CHECK(std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]));
    } else {
      CHECK(false && "Controller DevicePosition not written");
    }
  }
}

// Populate without a following Submit (Unity aborts a frame): the plugin must end the open frame empty.
void populateWithoutSubmit(const Hints& hints, int count) {
  const UnityXRDisplayGraphicsThreadProvider& gfx = g.displayGfx;
  const UnityXRFrameSetupHints h = makeHints(hints);
  int withPasses = 0;
  for (int i = 0; i < count; ++i) {
    UnityXRNextFrameDesc next{};
    CHECK_EQ(gfx.PopulateNextFrameDesc(kDisplayHandle, gfx.userData, &h, &next), kUnitySubsystemErrorCodeSuccess);
    if (next.renderPassesCount > 0) ++withPasses;
  }
  CHECK_EQ(withPasses, count);
  std::printf("  Populate without Submit: %d/%d populates delivered render passes\n", withPasses, count);
}

// Called when a plugin call does not return (hang): report the failure, kill Monado, terminate the process hard
// (the blocked thread cannot be cancelled).
std::string gTmpDir;  // deleted on abort/end

[[noreturn]] void abortStuck(const char* what) {
  killMonadoNow();
  if (!gTmpDir.empty() && !getenv("LINUXXR_TEST_KEEP_TMP")) {
    std::string cmd = "rm -rf '" + gTmpDir + "'";
    int rc = system(cmd.c_str());
    (void)rc;
  }
  lxr_test::reportFail(__FILE__, __LINE__, std::string("HANG: ") + what);
  std::printf("linuxxr_monado_integration: %d checks, %d failed (aborted because of a hang)\n",
              lxr_test::checkCount(), lxr_test::failCount());
  fflush(stdout);
  _exit(1);
}

// Kill Monado mid-operation and keep requesting frames. Expectation: no crash, no hang,
// Populate/Submit/Tick keep returning Success (empty frames), Stop/Shutdown work.
// The frame driver runs on its own thread so that a hang (e.g. xrWaitFrame after instance loss) can be reported as a
// clear test failure instead of a timeout of the whole test.
void killMonadoMidSession(const Hints& hints) {
  const UnityXRInputProvider& in = g.inputProvider;
  const UnityXRDisplayGraphicsThreadProvider& gfx = g.displayGfx;
  const UnityXRFrameSetupHints h = makeHints(hints);
  std::printf("  Monado is being killed (SIGKILL) ...\n");
  killMonadoNow();
  waitpid(gMonado.pid, nullptr, 0);
  gMonado.pid = -gMonado.pid;
  gMonadoPid = 0;

  std::mutex m;
  std::condition_variable cv;
  std::atomic<int> iteration{0};
  bool done = false;
  double worst = 0;
  int withPasses = 0;
  std::thread driver([&] {
    for (int i = 0; i < 40; ++i) {
      iteration = i + 1;
      const double t0 = nowSec();
      CHECK_EQ(in.Tick(kInputHandle, in.userData, kUnityXRInputUpdateTypeDynamic), kUnitySubsystemErrorCodeSuccess);
      for (uint32_t dev : {1u, 2u, 3u}) updateDevice(dev, kUnityXRInputUpdateTypeDynamic, true);
      CHECK_EQ(in.Tick(kInputHandle, in.userData, kUnityXRInputUpdateTypeBeforeRender), kUnitySubsystemErrorCodeSuccess);
      UnityXRNextFrameDesc next{};
      CHECK_EQ(gfx.PopulateNextFrameDesc(kDisplayHandle, gfx.userData, &h, &next), kUnitySubsystemErrorCodeSuccess);
      if (next.renderPassesCount > 0) ++withPasses;
      CHECK_EQ(gfx.SubmitCurrentFrame(kDisplayHandle, gfx.userData), kUnitySubsystemErrorCodeSuccess);
      const double dt = nowSec() - t0;
      if (dt > worst) worst = dt;
      usleep(20 * 1000);
    }
    std::lock_guard<std::mutex> l(m);
    done = true;
    cv.notify_all();
  });
  {
    std::unique_lock<std::mutex> l(m);
    if (!cv.wait_for(l, std::chrono::seconds(20), [&] { return done; })) {
      char msg[200];
      snprintf(msg, sizeof msg,
               "Plugin did not return after Monado died (iteration %d, probably xrWaitFrame after XR_ERROR_INSTANCE_LOST)",
               iteration.load());
      driver.detach();
      l.unlock();
      abortStuck(msg);
    }
  }
  driver.join();
  CHECK(worst < 10.0);
  std::printf("  After Monado death: 40 iterations without crash/hang, longest iteration %.3f s, %d with render passes\n",
              worst, withPasses);
}

void play(int round, Mode mode) {
  const char* modeName = mode == Mode::Full ? "full" : mode == Mode::AsyncQueue ? "async AccessQueue" : "Monado dies";
  std::printf("[Play %d: %s] Initialize/Start\n", round, modeName);
  const UnityLifecycleProvider& dl = g.displayLifecycle.provider;
  const UnityLifecycleProvider& il = g.inputLifecycle.provider;
  g.displayGfxRegistered = g.displayMainRegistered = g.inputProviderRegistered = false;
  g.connected.clear();
  g.connectCalls = g.disconnectCalls = 0;
  g.accessTextureCalls = g.accessQueueCalls = g.ensureOutsideCalls = 0;
  g.badAccessLayout = g.badAccessMode = g.badQueueFlush = g.unknownTextureAccess = 0;
  g.texturesCreated = g.texturesDestroyed = 0;
  g.createdDescs.clear();
  gObs = Observed{};
  const int errorsBefore = g.traceErrors;
  const size_t traceStart = g.traceLines.size();
  gAsyncMode = mode == Mode::AsyncQueue;
  if (gAsyncMode) gAsync.start();

  CHECK_EQ(dl.Initialize(kDisplayHandle, dl.userData), kUnitySubsystemErrorCodeSuccess);
  CHECK(g.displayGfxRegistered && g.displayMainRegistered);
  CHECK_EQ(il.Initialize(kInputHandle, il.userData), kUnitySubsystemErrorCodeSuccess);
  CHECK(g.inputProviderRegistered);
  CHECK_EQ(status().displayState, 1);
  CHECK_EQ(status().inputState, 1);
  if (!g.displayGfxRegistered || !g.displayMainRegistered || !g.inputProviderRegistered) return;

  // Start needs a Vulkan device + a reachable runtime -> with Monado: success
  if (!startSubsystems()) return;
  CHECK_EQ(g.connectCalls, 3);
  checkDevices();

  Hints hints;
  const unsigned long long frames0 = status().framesSubmitted;

  // ---- Single-pass (texture array), >= 150 iterations ----
  const bool full = mode == Mode::Full;
  LoopResult a = runFrames("Single-Pass", hints, full ? 100 : 40, full ? 150 : 50, 60.0);
  const LinuxXRStatus sA = status();
  CHECK(sA.sessionState == kSessionVisible || sA.sessionState == kSessionFocused);
  CHECK(gObs.sawRunning);
  CHECK(sA.framesSubmitted > frames0);
  CHECK(sA.refreshRate >= 0.0f);
  std::printf("  Session state %d, refreshRate %.1f, framesSubmitted %llu (before %llu), FOCUSED seen: %s\n",
              sA.sessionState, static_cast<double>(sA.refreshRate), sA.framesSubmitted, frames0,
              gObs.sawFocused ? "yes" : "no");
  CHECK_EQ(g.texturesCreated, 1);  // single-pass: exactly one array texture
  if (!g.createdDescs.empty()) {
    const auto& d = g.createdDescs.back();
    CHECK_EQ(d.textureArrayLength, 2u);
    CHECK(d.width >= 64 && d.height >= 64);
    CHECK(d.colorFormat == kUnityXRRenderTextureFormatRGBA32);
    CHECK((d.flags & kUnityXRRenderTextureFlagsSRGB) != 0);
    std::printf("  Eye texture: %ux%u, array %u\n", d.width, d.height, d.textureArrayLength);
  }
  checkMirror(hints, a.lastDesc);
  checkHmdAndControllers();
  checkStatsAndLog();

  if (mode == Mode::KillMonado) {
    killMonadoMidSession(hints);
  } else {
    // ---- Multi-pass ----
    hints.singlePass = false;
    const int createdBeforeB = g.texturesCreated, destroyedBeforeB = g.texturesDestroyed;
    LoopResult b = runFrames("Multi-Pass", hints, 30, 30, 60.0);
    CHECK_EQ(b.lastDesc.renderPassesCount, 2);
    CHECK_EQ(g.texturesCreated - createdBeforeB, 2);
    CHECK(g.texturesDestroyed - destroyedBeforeB >= 1);  // old array texture was released
    CHECK_EQ(g.textures.size(), 2u);
    if (g.createdDescs.size() >= 2) CHECK_EQ(g.createdDescs.back().textureArrayLength, 0u);
    checkMirror(hints, b.lastDesc);

    // ---- back to single-pass ----
    hints.singlePass = true;
    LoopResult c = runFrames("Single-Pass (again)", hints, 15, 15, 60.0);
    CHECK_EQ(c.lastDesc.renderPassesCount, 1);
    CHECK_EQ(g.textures.size(), 1u);

    if (full) {
      // ---- half resolution (swapchain rebuild mid-operation) ----
      const uint32_t fullW = g.textures.empty() ? 0 : g.textures.begin()->second->width;
      hints.scale = 0.5f;
      LoopResult d = runFrames("Single-Pass, scale 0.5", hints, 15, 15, 60.0);
      CHECK_EQ(d.lastDesc.renderPassesCount, 1);
      CHECK_EQ(g.textures.size(), 1u);
      if (!g.textures.empty() && fullW > 128) {
        const uint32_t halfW = g.textures.begin()->second->width;
        CHECK(halfW < fullW);
        std::printf("  Scale 0.5: width %u -> %u\n", fullW, halfW);
      }
      hints.scale = 1.0f;
      runFrames("Single-Pass, scale 1.0", hints, 10, 10, 60.0);

      // ---- Linear (sRGB=false) at runtime ----
      hints.srgb = false;
      runFrames("Linear (sRGB=false)", hints, 10, 10, 60.0);
      if (!g.createdDescs.empty()) CHECK((g.createdDescs.back().flags & kUnityXRRenderTextureFlagsSRGB) == 0);
      hints.srgb = true;
      runFrames("sRGB (again)", hints, 10, 10, 60.0);

      // ---- Populate without Submit ----
      populateWithoutSubmit(hints, 3);
      runFrames("after aborted frames", hints, 10, 10, 60.0);

      // ---- Stop -> Start without Shutdown (session persists) ----
      std::printf("  Stop -> Start without Shutdown\n");
      stopSubsystems();
      CHECK(status().sessionState != 0);
      CHECK(startSubsystems());
      LoopResult e = runFrames("after restart", hints, 20, 20, 60.0);
      CHECK_EQ(e.lastDesc.renderPassesCount, 1);
    }
    CHECK(gMonado.alive());
  }
  checkHostInvariants();

  // ---- Teardown in Unity's order ----
  std::printf("[Play %d] Stop/Shutdown\n", round);
  stopSubsystems();  // in async mode without draining first: the plugin must wait for the open frame
  CHECK_EQ(g.texturesCreated, g.texturesDestroyed);
  CHECK_EQ(g.disconnectCalls, g.connectCalls);
  dl.Shutdown(kDisplayHandle, dl.userData);
  il.Shutdown(kInputHandle, il.userData);
  if (gAsyncMode) {
    gAsync.drainAndStop();
    gAsyncMode = false;
    CHECK(gAsync.executed.load() > 0);
  }
  CHECK_EQ(status().displayState, 0);
  CHECK_EQ(status().inputState, 0);
  CHECK_EQ(status().sessionState, 0);  // session was destroyed at the last Shutdown
  CHECK(g.textures.empty());

  // "stopped: N frames requested, M sent to the headset" - M > 0 means projection layers were really submitted
  int stopLines = 0;
  for (size_t i = traceStart; i < g.traceLines.size(); ++i) {
    unsigned long long asked = 0, sent = 0;
    const char* p = strstr(g.traceLines[i].c_str(), "stopped:");
    if (p && sscanf(p, "stopped: %llu frames requested, %llu sent to the headset", &asked, &sent) == 2) {
      ++stopLines;
      CHECK(sent > 0);
      std::printf("  Plugin reports: %llu frames requested, %llu sent to the runtime with a layer\n", asked, sent);
    }
  }
  CHECK(stopLines >= 1);
  if (mode == Mode::KillMonado) {
    std::printf("  Error/warning traces after Monado death (expected): %d errors, %d warnings in total\n",
                g.traceErrors - errorsBefore, g.traceWarnings);
  } else {
    CHECK_EQ(g.traceErrors, errorsBefore);  // no error traces in the positive case
    CHECK(gMonado.alive());                 // Monado survives the end of the session
  }
}

}  // namespace

// ============================================================================================
// main
// ============================================================================================

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);  // progress visible immediately, even if the timeout aborts the process
  std::string path = argc > 1 ? argv[1] : "./libLinuxXR.so";
  if (char* rp = realpath(path.c_str(), nullptr)) {
    path = rp;
    free(rp);
  } else {
    std::fprintf(stderr, "monado_integration: plugin '%s' not found\n", path.c_str());
    return 1;
  }

  // ---- Prerequisites: otherwise SKIP ----
  const char* manifestEnv = getenv("LINUXXR_TEST_MONADO_MANIFEST");
  const std::string manifest = manifestEnv && *manifestEnv ? manifestEnv : "/usr/share/openxr/1/openxr_monado.json";
  if (!inPath("monado-service")) {
    std::printf("SKIP: monado-service not in PATH - Monado integration test skipped\n");
    return kExitSkip;
  }
  if (access(manifest.c_str(), R_OK) != 0) {
    std::printf("SKIP: Monado manifest %s not readable (set LINUXXR_TEST_MONADO_MANIFEST)\n", manifest.c_str());
    return kExitSkip;
  }
  if (!dlopen("libopenxr_loader.so.1", RTLD_NOW | RTLD_GLOBAL)) {
    std::printf("SKIP: libopenxr_loader.so.1 cannot be loaded - Monado integration test skipped\n");
    return kExitSkip;
  }
  if (!loadVulkan()) {
    std::printf("SKIP: libvulkan.so.1 cannot be loaded - Monado integration test skipped\n");
    return kExitSkip;
  }

  // ---- Hermetic environment: own (short!) runtime directory, cache and cwd ----
  const char* base = getenv("LINUXXR_TEST_TMPDIR");
  std::string tmpl = std::string(base && *base ? base : "/tmp") + "/lxr-it-XXXXXX";
  std::vector<char> tbuf(tmpl.begin(), tmpl.end());
  tbuf.push_back('\0');
  if (!mkdtemp(tbuf.data())) {
    std::perror("mkdtemp");
    return 1;
  }
  const std::string tmp = tbuf.data();
  gTmpDir = tmp;
  const std::string runtimeDir = tmp + "/run", cacheDir = tmp + "/cache", cwdDir = tmp + "/cwd";
  for (auto& d : {runtimeDir, cacheDir, cwdDir}) mkdir(d.c_str(), 0700);
  chmod(runtimeDir.c_str(), 0700);
  setenv("XDG_RUNTIME_DIR", runtimeDir.c_str(), 1);
  setenv("XDG_CACHE_HOME", cacheDir.c_str(), 1);
  setenv("XR_RUNTIME_JSON", manifest.c_str(), 1);
  setenv("LINUXXR_RUNTIME", manifest.c_str(), 1);
  unsetenv("LINUXXR_FLIP_Y");
  unsetenv("LINUXXR_DIRECT_SUBMIT");
  if (chdir(cwdDir.c_str()) != 0) std::perror("chdir");  // no Library/LinuxXR/settings.txt -> default settings
  std::printf("monado_integration: tmp=%s manifest=%s\n", tmp.c_str(), manifest.c_str());

  installSignalHandlers();
  alarm(150);  // global timeout (ctest TIMEOUT is 180 s)
  atexit(killMonadoNow);

  auto cleanup = [&](int rc) {
    const std::string how = gMonado.stop();
    std::printf("  Monado terminated: %s\n", how.c_str());
    if (rc != 0 && !gMonado.logFile.empty())
      std::fprintf(stderr, "---- monado.log (tail) ----\n%s\n---------------------------\n",
                   readTail(gMonado.logFile).c_str());
    if (!getenv("LINUXXR_TEST_KEEP_TMP")) {
      std::string cmd = "rm -rf '" + tmp + "'";
      int r = system(cmd.c_str());
      (void)r;
    }
    return rc;
  };

  // ---- Start Monado ----
  std::printf("[1] Starting Monado (headless, simulated)\n");
  if (!gMonado.start(runtimeDir)) {
    std::fprintf(stderr, "Could not start Monado\n");
    return cleanup(1);
  }
  const std::string sock = runtimeDir + "/monado_comp_ipc";
  if (!gMonado.waitForSocket(sock, 20.0)) {
    std::fprintf(stderr, "Monado socket %s does not appear (Monado %s)\n", sock.c_str(),
                 gMonado.alive() ? "still running" : "has exited");
    CHECK(false && "Monado not reachable");
    return cleanup(lxr_test::summary("linuxxr_monado_integration") | 1);
  }
  std::printf("  Monado running (pid %d)\n", static_cast<int>(gMonado.pid));

  // ---- Load plugin ----
  std::printf("[2] Loading plugin: %s\n", path.c_str());
  P.lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!P.lib) {
    std::fprintf(stderr, "dlopen: %s\n", dlerror());
    CHECK(false && "dlopen");
    return cleanup(1);
  }
  bool ok = resolve(P.preInit, "XRSDKPreInit") & resolve(P.load, "UnityPluginLoad") &
            resolve(P.unload, "UnityPluginUnload") & resolve(P.getVersion, "LinuxXR_GetVersion") &
            resolve(P.getStatus, "LinuxXR_GetStatus") & resolve(P.getDiagnostics, "LinuxXR_GetDiagnostics") &
            resolve(P.getStats, "LinuxXR_GetStats") & resolve(P.popLog, "LinuxXR_PopLog") &
            resolve(P.getLogHistory, "LinuxXR_GetLogHistory") &
            resolve(P.setConsoleForwarding, "LinuxXR_SetConsoleForwarding");
  if (!ok) return cleanup(1);
  CHECK_EQ(std::string(P.getVersion()), std::string(LINUXXR_EXPECTED_VERSION));

  // ---- PreInit ----
  std::printf("[3] XRSDKPreInit (with a reachable runtime)\n");
  P.preInit(&gInterfaces);
  CHECK_EQ(g.preinitRegisterCalls, 1);
  const UnityXRPreInitProvider& pre = g.preinit;
  if (!pre.GetVulkanInstanceExtensions || !pre.GetVulkanDeviceExtensions || !pre.GetGraphicsAdapterId) {
    CHECK(false && "PreInit provider incomplete");
    return cleanup(lxr_test::summary("linuxxr_monado_integration") | 1);
  }
  {
    const LinuxXRStatus s = status();
    CHECK_EQ(s.preinitRan, 1);
    CHECK_EQ(s.preinitRuntimeQueried, 1);  // Monado is running -> the Vulkan list comes from the runtime itself
    CHECK_EQ(s.runtimeReachable, 1);
    CHECK(contains(diagnostics(), "PreInit: ran (runtime)"));
  }

  // ---- Vulkan like Unity: instance -> adapter -> device with exactly the plugin extensions ----
  std::printf("[4] Vulkan instance and device (lavapipe) with the plugin's extensions\n");
  std::string instStr, devStr;
  CHECK(extensionString(pre.GetVulkanInstanceExtensions, pre.userData, instStr));
  const V instExts = splitSpaces(instStr);
  std::printf("  Instance extensions: %s\n", instStr.c_str());
  auto ai = vkInfo<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
  ai.pApplicationName = "monado-integration-host";
  ai.apiVersion = VK_API_VERSION_1_1;
  auto instNames = cstrs(instExts);
  auto ici = vkInfo<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
  ici.pApplicationInfo = &ai;
  ici.enabledExtensionCount = static_cast<uint32_t>(instNames.size());
  ici.ppEnabledExtensionNames = instNames.data();
  VkResult vr = K.vkCreateInstance(&ici, nullptr, &K.instance);
  if (vr != VK_SUCCESS) {
    std::printf("SKIP: vkCreateInstance failed (%d) - no Vulkan driver?\n", static_cast<int>(vr));
    return cleanup(kExitSkip);
  }
  CHECK(loadVulkanInstanceFns());

  uint64_t adapter = 0;
  CHECK(pre.GetGraphicsAdapterId(pre.userData, kUnityXRPreInitRendererVulkan, reinterpret_cast<uint64_t>(K.instance),
                                 &adapter));
  CHECK(adapter != 0);
  if (adapter == 0) return cleanup(lxr_test::summary("linuxxr_monado_integration") | 1);
  K.physicalDevice = *reinterpret_cast<VkPhysicalDevice*>(static_cast<uintptr_t>(adapter));
  VkPhysicalDeviceProperties props{};
  K.vkGetPhysicalDeviceProperties(K.physicalDevice, &props);
  std::printf("  GPU according to plugin: %s\n", props.deviceName);
  K.vkGetPhysicalDeviceMemoryProperties(K.physicalDevice, &K.memProps);

  CHECK(extensionString(pre.GetVulkanDeviceExtensions, pre.userData, devStr));
  const V devExts = splitSpaces(devStr);
  std::printf("  Device extensions: %s\n", devStr.c_str());

  uint32_t qfCount = 0;
  K.vkGetPhysicalDeviceQueueFamilyProperties(K.physicalDevice, &qfCount, nullptr);
  std::vector<VkQueueFamilyProperties> qf(qfCount);
  K.vkGetPhysicalDeviceQueueFamilyProperties(K.physicalDevice, &qfCount, qf.data());
  K.queueFamily = UINT32_MAX;
  for (uint32_t i = 0; i < qfCount; ++i)
    if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      K.queueFamily = i;
      break;
    }
  CHECK(K.queueFamily != UINT32_MAX);
  if (K.queueFamily == UINT32_MAX) return cleanup(1);
  float prio = 1.0f;
  auto qci = vkInfo<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
  qci.queueFamilyIndex = K.queueFamily;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  auto devNames = cstrs(devExts);
  bool hasTimeline = false;
  for (auto& e : devExts) hasTimeline = hasTimeline || e == "VK_KHR_timeline_semaphore";
  auto timeline = vkInfo<VkPhysicalDeviceTimelineSemaphoreFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES);
  timeline.timelineSemaphore = VK_TRUE;
  auto dci = vkInfo<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
  dci.pNext = hasTimeline ? &timeline : nullptr;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = static_cast<uint32_t>(devNames.size());
  dci.ppEnabledExtensionNames = devNames.data();
  vr = K.vkCreateDevice(K.physicalDevice, &dci, nullptr, &K.device);
  CHECK_EQ(static_cast<int>(vr), static_cast<int>(VK_SUCCESS));
  if (vr != VK_SUCCESS || !loadVulkanDeviceFns()) return cleanup(1);
  K.vkGetDeviceQueue(K.device, K.queueFamily, 0, &K.queue);

  auto pci = vkInfo<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pci.queueFamilyIndex = K.queueFamily;
  CHECK_EQ(static_cast<int>(K.vkCreateCommandPool(K.device, &pci, nullptr, &K.pool)), static_cast<int>(VK_SUCCESS));
  auto cbi = vkInfo<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
  cbi.commandPool = K.pool;
  cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbi.commandBufferCount = 1;
  CHECK_EQ(static_cast<int>(K.vkAllocateCommandBuffers(K.device, &cbi, &K.cmd)), static_cast<int>(VK_SUCCESS));
  auto fci = vkInfo<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
  CHECK_EQ(static_cast<int>(K.vkCreateFence(K.device, &fci, nullptr, &K.fence)), static_cast<int>(VK_SUCCESS));

  g.vkInstance.instance = K.instance;
  g.vkInstance.physicalDevice = K.physicalDevice;
  g.vkInstance.device = K.device;
  g.vkInstance.graphicsQueue = K.queue;
  g.vkInstance.getInstanceProcAddr = K.vkGetInstanceProcAddr;
  g.vkInstance.queueFamilyIndex = K.queueFamily;

  // ---- UnityPluginLoad ----
  std::printf("[5] UnityPluginLoad\n");
  P.load(&gInterfaces);
  CHECK(g.displayLifecycle.registered);
  CHECK(g.inputLifecycle.registered);
  CHECK_EQ(g.displayLifecycle.id, std::string("LinuxXR Display"));
  CHECK_EQ(g.inputLifecycle.id, std::string("LinuxXR Input"));
  const bool lifecycleOk = g.displayLifecycle.registered && g.inputLifecycle.registered;

  // ---- Play 1 and Play 2 (Play -> Stop -> Play in the Editor) ----
  if (lifecycleOk) {
    // argv[2] selects runs: "1" full, "2" async queue, "3" Monado dies; default "12"
    const std::string sel = argc > 2 ? argv[2] : "12";
    auto wanted = [&](int n) { return sel.find(static_cast<char>('0' + n)) != std::string::npos; };
    if (wanted(1)) play(1, Mode::Full);
    if (wanted(2) && g.traceErrors == 0) play(2, Mode::AsyncQueue);  // follow-up runs only if the previous one was clean
    if (wanted(3) && g.traceErrors == 0 && gMonado.alive()) play(3, Mode::KillMonado);  // last: kills Monado
  }

  // ---- Teardown ----
  std::printf("[6] UnityPluginUnload and cleanup\n");
  P.unload();
  CHECK_EQ(status().sessionState, 0);
  K.vkDeviceWaitIdle(K.device);
  K.vkDestroyFence(K.device, K.fence, nullptr);
  K.vkDestroyCommandPool(K.device, K.pool, nullptr);
  K.vkDestroyDevice(K.device, nullptr);
  K.vkDestroyInstance(K.instance, nullptr);

  for (auto& l : g.traceLines) CHECK(l.rfind("[LinuxXR] ", 0) == 0);
  std::printf("  Trace: %d messages, %d warnings, %d errors\n", g.traceTotal, g.traceWarnings, g.traceErrors);
  for (auto& l : g.traceWarnLines) std::printf("  Warning: %s\n", l.c_str());
  for (auto& l : g.traceErrorLines) std::fprintf(stderr, "  Error trace: %s\n", l.c_str());
  CHECK(countLines("First image") >= 1);
  for (auto& l : g.traceLines)
    if (contains(l, "First image")) std::printf("  %s\n", l.c_str());

  // The plugin stays loaded until process exit (dlclose of an OpenXR loader user is uncommon in Unity as well);
  // we still give the test child the chance to exit cleanly.
  const int rc = lxr_test::summary("linuxxr_monado_integration");
  return cleanup(rc);
}
