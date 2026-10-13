// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
// Fake Unity Editor: loads libLinuxXR.so via dlopen (no link against the plugin) and replays the calls
// Unity makes at Editor startup and in Play Mode:
//
//   XRSDKPreInit -> Vulkan extensions (real Vulkan loader) -> GetGraphicsAdapterId -> vkCreateDevice
//   -> UnityPluginLoad -> display/input lifecycle (Initialize/Start/Stop/Shutdown) -> gfx callbacks
//   -> C exports (LinuxXR_GetVersion/GetStatus/GetDiagnostics/ListRuntimes/SetSettings) -> UnityPluginUnload
//
// Display/input start need Unity's Vulkan device AND a reachable OpenXR runtime. This test therefore
// covers the NEGATIVE paths (no IUnityGraphics, wrong renderer, no Vulkan device, no runtime):
// Start must fail cleanly with kUnitySubsystemErrorCodeFailure. The positive case with a real runtime (Monado) lives in
// monado_integration.cpp.
//
// The test needs no running OpenXR runtime (see tests/CMakeLists.txt: ENVIRONMENT) and no GPU
// (lavapipe is enough).
//
// Usage: linuxxr_fake_unity_host [/path/to/libLinuxXR.so]

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cstdint>
#include <set>
#include <string>
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

#include <vulkan/vulkan.h>  // with VK_NO_PROTOTYPES: we load everything via vkGetInstanceProcAddr

using V = std::vector<std::string>;

#ifndef LINUXXR_EXPECTED_VERSION
#define LINUXXR_EXPECTED_VERSION "0.3.3"
#endif

namespace {

// ============================================================================================
// Fake state: what the plugin registered with us (the "Editor")
// ============================================================================================

struct LifecycleReg {
  bool registered = false;
  int calls = 0;
  std::string pluginName, id;
  UnityLifecycleProvider provider{};
};

struct FakeHost {
  // IUnityXRTrace
  int traceTotal = 0, traceErrors = 0, traceWarnings = 0;
  V traceLines;
  V traceErrorLines;

  // IUnityXRPreInit
  int preinitRegisterCalls = 0;
  UnityXRPreInitProvider preinit{};

  // IUnityXRDisplayInterface
  LifecycleReg displayLifecycle;
  bool displayMainRegistered = false, displayGfxRegistered = false;
  UnitySubsystemHandle displayMainHandle = nullptr, displayGfxHandle = nullptr;
  UnityXRDisplayProvider displayMain{};
  UnityXRDisplayGraphicsThreadProvider displayGfx{};

  // IUnityXRInputInterface
  LifecycleReg inputLifecycle;
  bool inputProviderRegistered = false;
  UnitySubsystemHandle inputHandle = nullptr;
  UnityXRInputProvider inputProvider{};

  // IUnityInterfaces
  int getInterfaceCalls = 0, unknownGuidCalls = 0;

  // IUnityGraphics / IUnityGraphicsVulkan (switchable per scenario)
  bool provideGraphics = false;
  UnityGfxRenderer renderer = kUnityGfxRendererNull;
  UnityVulkanInstance vkInstance{};

  // Number of error traces explicitly expected in the negative scenarios
  int expectedErrors = 0;
};
FakeHost g;

// ---- IUnityXRTrace -------------------------------------------------------------------------

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
  }
  if (getenv("FAKE_HOST_VERBOSE")) fprintf(stderr, "  [unity-trace %d] %s\n", static_cast<int>(type), line.c_str());
}
IUnityXRTrace makeTrace() {
  IUnityXRTrace t{};
  t.Trace = fakeTrace;
  return t;
}
IUnityXRTrace gTrace = makeTrace();

// ---- IUnityXRPreInit -----------------------------------------------------------------------

void UNITY_INTERFACE_API fakeRegisterPreInitProvider(UnityXRPreInitProvider* provider) {
  ++g.preinitRegisterCalls;
  if (provider) g.preinit = *provider;  // copy: the Editor does not keep the struct beyond the call
}
IUnityXRPreInit makePreInit() {
  IUnityXRPreInit p{};
  p.RegisterPreInitProvider = fakeRegisterPreInitProvider;
  return p;
}
IUnityXRPreInit gPreInit = makePreInit();

// ---- Lifecycle (display and input share the logic) -------------------------------------

UnitySubsystemErrorCode registerLifecycle(LifecycleReg& reg, const char* pluginName, const char* id,
                                          const UnityLifecycleProvider* provider) {
  ++reg.calls;
  if (!provider || !pluginName || !id) return kUnitySubsystemErrorCodeInvalidArguments;
  reg.registered = true;
  reg.pluginName = pluginName;
  reg.id = id;
  reg.provider = *provider;
  return kUnitySubsystemErrorCodeSuccess;
}

// ---- IUnityXRDisplayInterface --------------------------------------------------------------

UnitySubsystemErrorCode UNITY_INTERFACE_API displayRegisterLifecycle(const char* pluginName, const char* id,
                                                                    const UnityLifecycleProvider* provider) {
  return registerLifecycle(g.displayLifecycle, pluginName, id, provider);
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

IUnityXRDisplayInterface makeDisplayInterface() {
  IUnityXRDisplayInterface d{};
  d.RegisterLifecycleProvider = displayRegisterLifecycle;
  d.RegisterProvider = displayRegisterProvider;
  d.RegisterProviderForGraphicsThread = displayRegisterGfx;
  // Remaining functions: harmless stubs (the plugin does not call them without a session)
  d.CreateTexture = [](UnitySubsystemHandle, const UnityXRRenderTextureDesc*, UnityXRRenderTextureId* out) {
    if (out) *out = 1;
    return kUnitySubsystemErrorCodeSuccess;
  };
  d.QueryTextureDesc = [](UnitySubsystemHandle, UnityXRRenderTextureId, UnityXRRenderTextureDesc*) {
    return kUnitySubsystemErrorCodeNotSupported;
  };
  d.DestroyTexture = [](UnitySubsystemHandle, UnityXRRenderTextureId) { return kUnitySubsystemErrorCodeSuccess; };
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

// ---- IUnityXRInputInterface ----------------------------------------------------------------

UnitySubsystemErrorCode UNITY_INTERFACE_API inputRegisterLifecycle(const char* pluginName, const char* id,
                                                                  const UnityLifecycleProvider* provider) {
  return registerLifecycle(g.inputLifecycle, pluginName, id, provider);
}
UnitySubsystemErrorCode UNITY_INTERFACE_API inputRegisterProvider(UnitySubsystemHandle handle,
                                                                 const UnityXRInputProvider* provider) {
  if (!provider) return kUnitySubsystemErrorCodeInvalidArguments;
  g.inputProviderRegistered = true;
  g.inputHandle = handle;
  g.inputProvider = *provider;
  return kUnitySubsystemErrorCodeSuccess;
}

IUnityXRInputInterface makeInputInterface() {
  IUnityXRInputInterface in{};
  in.RegisterLifecycleProvider = inputRegisterLifecycle;
  in.RegisterInputProvider = inputRegisterProvider;
  using E = UnitySubsystemErrorCode;
  using H = UnitySubsystemHandle;
  using Dev = UnityXRInternalInputDeviceId;
  using Def = UnityXRInputDeviceDefinition;
  using St = UnityXRInputDeviceState;
  using Idx = UnityXRInputFeatureIndex;
  // Stubs (the negative scenarios never reach a device state; for features see monado_integration.cpp)
  in.InputSubsystem_DeviceConnected = [](H, Dev) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.InputSubsystem_DeviceDisconnected = [](H, Dev) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.InputSubsystem_DeviceConfigChanged = [](H, Dev) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.InputSubsystem_TrackingOriginUpdated = [](H) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.InputSubsystem_SetTrackingBoundary = [](H, const UnityXRVector3*, uint32_t) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.InputSubsystem_GetPlatformData = [](H, void**) { return E(kUnitySubsystemErrorCodeNotSupported); };
  in.DeviceDefinition_SetName = [](Def*, const char*) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceDefinition_SetCharacteristics = [](Def*, UnityXRInputDeviceCharacteristics) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceDefinition_SetManufacturer = [](Def*, const char*) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceDefinition_SetSerialNumber = [](Def*, const char*) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceDefinition_SetCanQueryForDeviceStateAtTime = [](Def*, bool) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceDefinition_AddFeature = [](Def*, const char*, UnityXRInputFeatureType) { return Idx(0); };
  in.DeviceDefinition_AddCustomFeature = [](Def*, const char*, unsigned int) { return Idx(0); };
  in.DeviceDefinition_AddFeatureWithUsage = [](Def*, const char*, UnityXRInputFeatureType, UnityXRInputFeatureUsage) { return Idx(0); };
  in.DeviceDefinition_AddUsageAtIndex = [](Def*, Idx, UnityXRInputFeatureUsage) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetCustomValue = [](St*, Idx, const void*, unsigned int) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetBinaryValue = [](St*, Idx, bool) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetDiscreteStateValue = [](St*, Idx, unsigned int) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetAxis1DValue = [](St*, Idx, float) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetAxis2DValue = [](St*, Idx, UnityXRVector2) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetAxis3DValue = [](St*, Idx, UnityXRVector3) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetRotationValue = [](St*, Idx, UnityXRVector4) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetBoneValue = [](St*, Idx, UnityXRBone) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetHandValue = [](St*, Idx, UnityXRHand) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetEyesValue = [](St*, Idx, UnityXREyes) { return E(kUnitySubsystemErrorCodeSuccess); };
  in.DeviceState_SetDeviceTime = [](St*, UnityXRTimeStamp) { return E(kUnitySubsystemErrorCodeSuccess); };
  return in;
}
IUnityXRInputInterface gInput = makeInputInterface();

// ---- IUnityGraphics / IUnityGraphicsVulkan (negative cases only: renderer and instance are controllable) ------

UnityGfxRenderer UNITY_INTERFACE_API fakeGetRenderer() { return g.renderer; }
IUnityGraphics makeGraphics() {
  IUnityGraphics gr{};
  gr.GetRenderer = fakeGetRenderer;
  gr.RegisterDeviceEventCallback = [](IUnityGraphicsDeviceEventCallback) {};
  gr.UnregisterDeviceEventCallback = [](IUnityGraphicsDeviceEventCallback) {};
  gr.ReserveEventIDRange = [](int) { return 0; };
  return gr;
}
IUnityGraphics gGraphics = makeGraphics();

UnityVulkanInstance UNITY_INTERFACE_API fakeVkInstance() { return g.vkInstance; }
IUnityGraphicsVulkan makeGraphicsVulkan() {
  IUnityGraphicsVulkan v{};
  v.Instance = fakeVkInstance;
  v.EnsureOutsideRenderPass = []() {};
  v.EnsureInsideRenderPass = []() {};
  // AccessTexture/AccessQueue deliberately nullptr: in the negative cases the plugin must never call them.
  return v;
}
IUnityGraphicsVulkan gGraphicsVk = makeGraphicsVulkan();

// ---- IUnityInterfaces (registry) -----------------------------------------------------------

IUnityInterface* lookup(unsigned long long high, unsigned long long low) {
  ++g.getInterfaceCalls;
  const UnityInterfaceGUID want(high, low);
  if (want == GetUnityInterfaceGUID<IUnityXRTrace>()) return &gTrace;
  if (want == GetUnityInterfaceGUID<IUnityXRPreInit>()) return &gPreInit;
  if (want == GetUnityInterfaceGUID<IUnityXRDisplayInterface>()) return &gDisplay;
  if (want == GetUnityInterfaceGUID<IUnityXRInputInterface>()) return &gInput;
  if (g.provideGraphics && want == GetUnityInterfaceGUID<IUnityGraphics>()) return &gGraphics;
  if (g.provideGraphics && want == GetUnityInterfaceGUID<IUnityGraphicsVulkan>()) return &gGraphicsVk;
  ++g.unknownGuidCalls;
  return nullptr;  // without provideGraphics the fake Editor has neither IUnityGraphics nor IUnityGraphicsVulkan
}

IUnityInterface* UNITY_INTERFACE_API fakeGetInterface(UnityInterfaceGUID guid) {
  return lookup(guid.m_GUIDHigh, guid.m_GUIDLow);
}
IUnityInterface* UNITY_INTERFACE_API fakeGetInterfaceSplit(unsigned long long high, unsigned long long low) {
  return lookup(high, low);
}
void UNITY_INTERFACE_API fakeRegisterInterface(UnityInterfaceGUID, IUnityInterface*) {}
void UNITY_INTERFACE_API fakeRegisterInterfaceSplit(unsigned long long, unsigned long long, IUnityInterface*) {}

IUnityInterfaces gInterfaces = {fakeGetInterface, fakeRegisterInterface, fakeGetInterfaceSplit,
                                fakeRegisterInterfaceSplit};

// ============================================================================================
// Plugin exports (via dlsym)
// ============================================================================================

// Mirror of LinuxXRStatus in src/unity/native_api.cpp (C#: LinuxXRNative.Status)
struct LinuxXRStatus {
  int preinitRan;
  int preinitRuntimeQueried;
  int displayState;
  int inputState;
  int sessionState;
  int runtimeReachable;
  float refreshRate;
  unsigned long long framesSubmitted;
};

struct Plugin {
  void* lib = nullptr;
  void (*preInit)(IUnityInterfaces*) = nullptr;
  void (*load)(IUnityInterfaces*) = nullptr;
  void (*unload)() = nullptr;
  const char* (*getVersion)() = nullptr;
  void (*getStatus)(LinuxXRStatus*) = nullptr;
  int (*getDiagnostics)(char*, int) = nullptr;
  int (*listRuntimes)(char*, int) = nullptr;
  void (*setSettings)(const char*, int, int, int, float, int) = nullptr;
};
Plugin P;

template <class Fn>
bool resolve(Fn& out, const char* name) {
  out = reinterpret_cast<Fn>(dlsym(P.lib, name));
  const bool ok = out != nullptr;
  ++lxr_test::checkCount();
  if (!ok) lxr_test::reportFail(__FILE__, __LINE__, std::string("Symbol missing in libLinuxXR.so: ") + name);
  return ok;
}

LinuxXRStatus status() {
  LinuxXRStatus s;
  memset(&s, 0xAB, sizeof s);  // proves that the plugin writes all fields
  P.getStatus(&s);
  return s;
}

// Two-phase text query of the C exports: (nullptr, 0) -> required size; then fill the buffer.
std::string twoPhaseText(int (*fn)(char*, int)) {
  const int needed = fn(nullptr, 0);
  CHECK(needed >= 1);
  if (needed < 1) return {};
  std::vector<char> buf(static_cast<size_t>(needed), 'X');
  const int written = fn(buf.data(), needed);
  CHECK_EQ(written, needed);
  CHECK_EQ(buf[static_cast<size_t>(needed) - 1], '\0');
  CHECK_EQ(static_cast<int>(strlen(buf.data())), needed - 1);
  return std::string(buf.data());
}

// Two-phase extension string as described in IUnityXRPreInit.
bool extensionString(bool (UNITY_INTERFACE_API* fn)(void*, uint32_t, uint32_t*, char*), void* ud, std::string& out,
                     const char* what) {
  uint32_t needed = 0;
  bool ok = fn(ud, 0, &needed, nullptr);
  CHECK(ok);
  CHECK(needed >= 1);
  if (!ok || needed < 1) return false;
  std::vector<char> buf(needed + 8, 'X');

  // Buffer too small -> false, but countOut reports the required size
  if (needed > 1) {
    uint32_t cnt = 0;
    bool tooSmall = fn(ud, needed - 1, &cnt, buf.data());
    CHECK(!tooSmall);
    CHECK_EQ(cnt, needed);
  }

  uint32_t written = 0;
  ok = fn(ud, static_cast<uint32_t>(buf.size()), &written, buf.data());
  CHECK(ok);
  CHECK_EQ(written, needed);
  CHECK_EQ(buf[needed - 1], '\0');
  out = std::string(buf.data());
  CHECK_EQ(static_cast<uint32_t>(out.size()) + 1, needed);
  std::printf("  %s: %s\n", what, out.c_str());
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

bool contains(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }
bool has(const V& v, const std::string& s) {
  for (auto& e : v)
    if (e == s) return true;
  return false;
}

// ============================================================================================
// Vulkan (real loader, via dlopen)
// ============================================================================================

struct Vk {
  void* lib = nullptr;
  PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
  PFN_vkEnumerateInstanceExtensionProperties enumInstanceExt = nullptr;
  PFN_vkCreateInstance createInstance = nullptr;
  // Instance functions
  PFN_vkDestroyInstance destroyInstance = nullptr;
  PFN_vkEnumeratePhysicalDevices enumPhysicalDevices = nullptr;
  PFN_vkGetPhysicalDeviceProperties getProps = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties getQueueFamilies = nullptr;
  PFN_vkEnumerateDeviceExtensionProperties enumDeviceExt = nullptr;
  PFN_vkCreateDevice createDevice = nullptr;
  PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
  // Device functions
  PFN_vkDestroyDevice destroyDevice = nullptr;
  PFN_vkDeviceWaitIdle deviceWaitIdle = nullptr;
  PFN_vkGetDeviceQueue getDeviceQueue = nullptr;
  // The device created in phaseVulkan is kept for the negative scenario "Vulkan present, runtime missing"
  // and released in phaseUnload.
  VkInstance heldInstance = VK_NULL_HANDLE;
  UnityVulkanInstance unityInstance{};
};
Vk K;

// Vulkan info struct with sType set, rest zeroed (avoids -Wmissing-field-initializers)
template <class T>
T vkInfo(VkStructureType type) {
  T info{};
  info.sType = type;
  return info;
}

bool loadVulkan() {
  K.lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!K.lib) {
    std::fprintf(stderr, "libvulkan.so.1 cannot be loaded: %s\n", dlerror());
    return false;
  }
  K.getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(K.lib, "vkGetInstanceProcAddr"));
  if (!K.getInstanceProcAddr) return false;
  K.enumInstanceExt = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
      K.getInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties"));
  K.createInstance = reinterpret_cast<PFN_vkCreateInstance>(K.getInstanceProcAddr(nullptr, "vkCreateInstance"));
  return K.enumInstanceExt && K.createInstance;
}

#define LOAD_INSTANCE_FN(field, name) \
  K.field = reinterpret_cast<PFN_##name>(K.getInstanceProcAddr(instance, #name))

bool loadVulkanInstanceFns(VkInstance instance) {
  LOAD_INSTANCE_FN(destroyInstance, vkDestroyInstance);
  LOAD_INSTANCE_FN(enumPhysicalDevices, vkEnumeratePhysicalDevices);
  LOAD_INSTANCE_FN(getProps, vkGetPhysicalDeviceProperties);
  LOAD_INSTANCE_FN(getQueueFamilies, vkGetPhysicalDeviceQueueFamilyProperties);
  LOAD_INSTANCE_FN(enumDeviceExt, vkEnumerateDeviceExtensionProperties);
  LOAD_INSTANCE_FN(createDevice, vkCreateDevice);
  LOAD_INSTANCE_FN(getDeviceProcAddr, vkGetDeviceProcAddr);
  return K.destroyInstance && K.enumPhysicalDevices && K.getProps && K.getQueueFamilies && K.enumDeviceExt &&
         K.createDevice && K.getDeviceProcAddr;
}

V availableInstanceExtensions() {
  uint32_t n = 0;
  K.enumInstanceExt(nullptr, &n, nullptr);
  std::vector<VkExtensionProperties> props(n);
  K.enumInstanceExt(nullptr, &n, props.data());
  V out;
  for (uint32_t i = 0; i < n; ++i) out.push_back(props[i].extensionName);
  return out;
}

V availableDeviceExtensions(VkPhysicalDevice pd) {
  uint32_t n = 0;
  K.enumDeviceExt(pd, nullptr, &n, nullptr);
  std::vector<VkExtensionProperties> props(n);
  K.enumDeviceExt(pd, nullptr, &n, props.data());
  V out;
  for (uint32_t i = 0; i < n; ++i) out.push_back(props[i].extensionName);
  return out;
}

std::vector<const char*> cstrs(const V& v) {
  std::vector<const char*> out;
  for (auto& s : v) out.push_back(s.c_str());
  return out;
}

// ============================================================================================
// Test phases
// ============================================================================================

std::string gGpuName;

bool phaseLoad(const char* path) {
  std::printf("[1] Loading plugin: %s\n", path);
  P.lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  CHECK(P.lib != nullptr);
  if (!P.lib) {
    std::fprintf(stderr, "dlopen failed: %s\n", dlerror());
    return false;
  }
  bool ok = true;
  ok &= resolve(P.preInit, "XRSDKPreInit");
  ok &= resolve(P.load, "UnityPluginLoad");
  ok &= resolve(P.unload, "UnityPluginUnload");
  ok &= resolve(P.getVersion, "LinuxXR_GetVersion");
  ok &= resolve(P.getStatus, "LinuxXR_GetStatus");
  ok &= resolve(P.getDiagnostics, "LinuxXR_GetDiagnostics");
  ok &= resolve(P.listRuntimes, "LinuxXR_ListRuntimes");
  ok &= resolve(P.setSettings, "LinuxXR_SetSettings");
  if (!ok) return false;

  // Before any call into plugin code: version and initial status
  CHECK_EQ(std::string(P.getVersion()), std::string(LINUXXR_EXPECTED_VERSION));
  LinuxXRStatus s = status();
  CHECK_EQ(s.preinitRan, 0);
  CHECK_EQ(s.preinitRuntimeQueried, 0);
  CHECK_EQ(s.displayState, 0);
  CHECK_EQ(s.inputState, 0);
  CHECK_EQ(s.sessionState, 0);
  CHECK(s.framesSubmitted == 0ULL);

  // Internal symbols must not be exported (CXX_VISIBILITY_PRESET hidden)
  CHECK(dlsym(P.lib, "_ZN3lxr7unity7contextEv") == nullptr);
  return true;
}

// The fake Editor must be able to resolve the interface GUIDs from the headers cleanly.
void phaseFakeRegistry() {
  std::printf("[2] Fake registry\n");
  CHECK(gInterfaces.Get<IUnityXRTrace>() == &gTrace);
  CHECK(gInterfaces.Get<IUnityXRPreInit>() == &gPreInit);
  CHECK(gInterfaces.Get<IUnityXRDisplayInterface>() == &gDisplay);
  CHECK(gInterfaces.Get<IUnityXRInputInterface>() == &gInput);
  CHECK(gInterfaces.GetInterface(UnityInterfaceGUID(1, 2)) == nullptr);
  g.getInterfaceCalls = 0;
  g.unknownGuidCalls = 0;
}

bool phasePreInit() {
  std::printf("[3] XRSDKPreInit\n");
  P.preInit(&gInterfaces);
  CHECK_EQ(g.preinitRegisterCalls, 1);
  const UnityXRPreInitProvider& pre = g.preinit;
  CHECK(pre.GetPreInitFlags != nullptr);
  CHECK(pre.GetGraphicsAdapterId != nullptr);
  CHECK(pre.GetVulkanInstanceExtensions != nullptr);
  CHECK(pre.GetVulkanDeviceExtensions != nullptr);
  if (!pre.GetPreInitFlags || !pre.GetGraphicsAdapterId || !pre.GetVulkanInstanceExtensions ||
      !pre.GetVulkanDeviceExtensions)
    return false;

  LinuxXRStatus s = status();
  CHECK_EQ(s.preinitRan, 1);

  // Flags: an additional Vulkan graphics queue is requested
  uint64_t flags = 0;
  CHECK(pre.GetPreInitFlags(pre.userData, &flags));
  CHECK((flags & static_cast<uint64_t>(kUnityXRPreInitFlagsRequestAdditionalVulkanGraphicsQueue)) != 0);

  // Non-Vulkan renderer is rejected (Unity in the Editor with OpenGL), without crashing
  uint64_t id = 0;
  CHECK(!pre.GetGraphicsAdapterId(pre.userData, kUnityXRPreInitRendererD3D11, 0, &id));
  CHECK(!pre.GetGraphicsAdapterId(pre.userData, kUnityXRPreInitRendererGLCore, 0, &id));
  return true;
}

bool phaseVulkan() {
  std::printf("[4] Vulkan chain (instance -> adapter -> device)\n");
  const UnityXRPreInitProvider& pre = g.preinit;
  if (!loadVulkan()) {
    CHECK(false && "libvulkan.so.1 could not be loaded");
    return false;
  }

  // 4a. Instance extensions (two phases)
  std::string instStr;
  extensionString(pre.GetVulkanInstanceExtensions, pre.userData, instStr, "Instance extensions");
  const V instExts = splitSpaces(instStr);
  CHECK(!instExts.empty());
  const V supportedInst = availableInstanceExtensions();
  for (auto& e : instExts) CHECK(has(supportedInst, e));  // the plugin filter lets only supported ones through
  CHECK_EQ(instExts.size(), std::set<std::string>(instExts.begin(), instExts.end()).size());  // no duplicates
  // The base list (fallback) must be included, as far as the loader knows it
  for (const char* e : {"VK_KHR_external_fence_capabilities", "VK_KHR_external_memory_capabilities",
                        "VK_KHR_external_semaphore_capabilities", "VK_KHR_get_physical_device_properties2"}) {
    if (has(supportedInst, e)) CHECK(has(instExts, e));
  }
  // A repeated call returns the same
  {
    std::string again;
    extensionString(pre.GetVulkanInstanceExtensions, pre.userData, again, "Instance extensions (2nd call)");
    CHECK_EQ(again, instStr);
  }

  // 4b. Real VkInstance with exactly these extensions
  auto ai = vkInfo<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
  ai.pApplicationName = "fake-unity-host";
  ai.apiVersion = VK_API_VERSION_1_1;
  auto instNames = cstrs(instExts);
  auto ici = vkInfo<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
  ici.pApplicationInfo = &ai;
  ici.enabledExtensionCount = static_cast<uint32_t>(instNames.size());
  ici.ppEnabledExtensionNames = instNames.data();
  VkInstance instance = VK_NULL_HANDLE;
  VkResult r = K.createInstance(&ici, nullptr, &instance);
  CHECK_EQ(static_cast<int>(r), static_cast<int>(VK_SUCCESS));
  if (r != VK_SUCCESS || !loadVulkanInstanceFns(instance)) return false;

  // 4c. GetGraphicsAdapterId -> pointer to a VkPhysicalDevice
  uint64_t id = 0;
  CHECK(pre.GetGraphicsAdapterId(pre.userData, kUnityXRPreInitRendererVulkan,
                                 reinterpret_cast<uint64_t>(instance), &id));
  CHECK(id != 0);
  VkPhysicalDevice pd = VK_NULL_HANDLE;
  if (id != 0) pd = *reinterpret_cast<VkPhysicalDevice*>(static_cast<uintptr_t>(id));
  CHECK(pd != VK_NULL_HANDLE);

  uint32_t devCount = 0;
  K.enumPhysicalDevices(instance, &devCount, nullptr);
  std::vector<VkPhysicalDevice> devs(devCount);
  K.enumPhysicalDevices(instance, &devCount, devs.data());
  bool known = false;
  for (auto d : devs) known = known || d == pd;
  CHECK(known);  // the chosen device belongs to our instance
  CHECK(devCount > 0);
  if (!known || pd == VK_NULL_HANDLE) {
    K.destroyInstance(instance, nullptr);
    return false;
  }
  VkPhysicalDeviceProperties props{};
  K.getProps(pd, &props);
  gGpuName = props.deviceName;
  std::printf("  GPU according to plugin: %s (type %d, %u devices)\n", props.deviceName, static_cast<int>(props.deviceType),
              devCount);
  // Second call with the same instance -> same device
  {
    uint64_t id2 = 0;
    CHECK(pre.GetGraphicsAdapterId(pre.userData, kUnityXRPreInitRendererVulkan,
                                   reinterpret_cast<uint64_t>(instance), &id2));
    CHECK(id2 != 0 && *reinterpret_cast<VkPhysicalDevice*>(static_cast<uintptr_t>(id2)) == pd);
  }

  // 4d. Device extensions (two phases)
  std::string devStr;
  extensionString(pre.GetVulkanDeviceExtensions, pre.userData, devStr, "Device extensions");
  const V devExts = splitSpaces(devStr);
  const V supportedDev = availableDeviceExtensions(pd);
  for (auto& e : devExts) CHECK(has(supportedDev, e));
  CHECK_EQ(devExts.size(), std::set<std::string>(devExts.begin(), devExts.end()).size());

  // 4e. vkCreateDevice with one graphics queue and exactly these extensions
  uint32_t qfCount = 0;
  K.getQueueFamilies(pd, &qfCount, nullptr);
  std::vector<VkQueueFamilyProperties> qf(qfCount);
  K.getQueueFamilies(pd, &qfCount, qf.data());
  uint32_t gfxFamily = UINT32_MAX;
  for (uint32_t i = 0; i < qfCount; ++i)
    if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      gfxFamily = i;
      break;
    }
  CHECK(gfxFamily != UINT32_MAX);
  VkDevice device = VK_NULL_HANDLE;
  if (gfxFamily != UINT32_MAX) {
    float prio = 1.0f;
    auto qci = vkInfo<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    qci.queueFamilyIndex = gfxFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    auto devNames = cstrs(devExts);
    auto dci = vkInfo<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<uint32_t>(devNames.size());
    dci.ppEnabledExtensionNames = devNames.data();
    r = K.createDevice(pd, &dci, nullptr, &device);
    CHECK_EQ(static_cast<int>(r), static_cast<int>(VK_SUCCESS));
    if (r == VK_SUCCESS) {
      K.destroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(K.getDeviceProcAddr(device, "vkDestroyDevice"));
      K.getDeviceQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(K.getDeviceProcAddr(device, "vkGetDeviceQueue"));
      CHECK(K.destroyDevice != nullptr);
      CHECK(K.getDeviceQueue != nullptr);
      if (K.getDeviceQueue) {
        K.unityInstance.instance = instance;
        K.unityInstance.physicalDevice = pd;
        K.unityInstance.device = device;
        K.unityInstance.getInstanceProcAddr = K.getInstanceProcAddr;
        K.unityInstance.queueFamilyIndex = gfxFamily;
        K.getDeviceQueue(device, gfxFamily, 0, &K.unityInstance.graphicsQueue);
        K.heldInstance = instance;
        return true;  // instance + device are kept until phaseUnload
      }
      if (K.destroyDevice) K.destroyDevice(device, nullptr);
    }
  }
  K.destroyInstance(instance, nullptr);
  return true;
}

void phaseRuntimeSelection() {
  std::printf("[5] Runtime selection without a running runtime\n");
  const LinuxXRStatus s = status();
  // Without a reachable runtime the base list (or a cache) must apply
  if (s.preinitRuntimeQueried) {
    std::printf("  Note: a real OpenXR runtime responded - fallback check skipped\n");
    return;
  }
  CHECK_EQ(s.preinitRuntimeQueried, 0);
  const std::string diag = twoPhaseText(P.getDiagnostics);
  CHECK(contains(diag, "PreInit: ran (fallback)"));
  // The dummy runtime from the test environment (kind WiVRn, no socket) was chosen but not queried
  if (const char* fake = getenv("XR_RUNTIME_JSON"); fake && *fake) {
    CHECK(contains(diag, (std::string("Runtime: ") + fake + " [WiVRn]").c_str()));
    CHECK(contains(diag, "Status: WiVRn: no headset connected"));
    CHECK_EQ(s.runtimeReachable, 0);
  }
}

// Start must fail: failure code, state stays 1 ("initialized"), no session, at least one error trace.
// The error traces produced in the process are booked as explicitly expected (see phaseUnload).
// errorsExpected = false: "no runtime / no headset" is a normal state and only logged as a warning.
V expectStartFailure(const char* what, bool display, const UnityLifecycleProvider& lp, UnitySubsystemHandle h,
                     bool errorsExpected = true) {
  const size_t before = g.traceErrorLines.size();
  CHECK_EQ(lp.Start(h, lp.userData), kUnitySubsystemErrorCodeFailure);
  V lines(g.traceErrorLines.begin() + static_cast<std::ptrdiff_t>(before), g.traceErrorLines.end());
  if (errorsExpected) CHECK(!lines.empty());  // "clear error message" in the Unity log
  else CHECK(lines.empty());                  // warning only - no red errors without headset/runtime
  g.expectedErrors += static_cast<int>(lines.size());
  const LinuxXRStatus s = status();
  CHECK_EQ(display ? s.displayState : s.inputState, 1);
  CHECK_EQ(s.sessionState, 0);
  CHECK(s.framesSubmitted == 0ULL);
  std::printf("  %s: Start fails as expected (%zu error trace(s); first: %s)\n", what, lines.size(),
              lines.empty() ? "-" : lines[0].c_str());
  return lines;
}

bool anyContains(const V& lines, const char* needle) {
  for (auto& l : lines)
    if (contains(l, needle)) return true;
  return false;
}

struct NegScenario {
  const char* name;
  bool graphics;            // IUnityGraphics/IUnityGraphicsVulkan present?
  UnityGfxRenderer renderer;
  bool realVulkanDevice;    // Instance() returns the real device (otherwise zeros)
  const char* traceNeedle;  // must appear in an error trace
  const char* diagNeedle;   // must appear in the "Last error" of the diagnostics (nullptr = do not check)
  bool errors = true;       // false: expected state (no runtime/headset) - warnings only
};

const NegScenario kScenarios[] = {
    {"no IUnityGraphics", false, kUnityGfxRendererNull, false, "not running with Vulkan", "Graphics API is not Vulkan"},
    {"renderer OpenGLCore", true, kUnityGfxRendererOpenGLCore, false, "not running with Vulkan", "Graphics API is not Vulkan"},
    {"Vulkan, but Instance() empty", true, kUnityGfxRendererVulkan, false, "not available yet", nullptr},
    {"Vulkan device ok, no OpenXR runtime", true, kUnityGfxRendererVulkan, true, "", "OpenXR instance creation failed", false},
};

// A complete Play run (display and input) in one negative scenario.
void runNegativeScenario(const NegScenario& sc) {
  std::printf("  -- Scenario: %s\n", sc.name);
  g.provideGraphics = sc.graphics;
  g.renderer = sc.renderer;
  g.vkInstance = sc.realVulkanDevice ? K.unityInstance : UnityVulkanInstance{};
  const bool vulkanIface = sc.graphics && sc.renderer == kUnityGfxRendererVulkan;

  const UnityLifecycleProvider& dl = g.displayLifecycle.provider;
  const UnityLifecycleProvider& il = g.inputLifecycle.provider;
  UnitySubsystemHandle dh = reinterpret_cast<UnitySubsystemHandle>(static_cast<uintptr_t>(0xD15));
  UnitySubsystemHandle ih = reinterpret_cast<UnitySubsystemHandle>(static_cast<uintptr_t>(0x1AB));

  // ---- Display ----
  g.displayGfxRegistered = g.displayMainRegistered = false;
  CHECK_EQ(dl.Initialize(dh, dl.userData), kUnitySubsystemErrorCodeSuccess);
  CHECK(g.displayGfxRegistered && g.displayMainRegistered);
  CHECK_EQ(status().displayState, 1);
  if (!g.displayGfxRegistered || !g.displayMainRegistered) return;
  const UnityXRDisplayGraphicsThreadProvider& gfx = g.displayGfx;
  const UnityXRDisplayProvider& main = g.displayMain;

  V errs = expectStartFailure("Display", true, dl, dh, sc.errors);
  if (sc.errors) CHECK(anyContains(errs, sc.traceNeedle));
  if (sc.diagNeedle) CHECK(contains(twoPhaseText(P.getDiagnostics), (std::string("Last error: ") + sc.diagNeedle).c_str()));

  // Gfx-thread callbacks without a session: must not crash. Call Gfx.Start only without the Vulkan interface (with the
  // interface the plugin would immediately create Vulkan resources - but Unity only calls Gfx.Start after a successful start).
  UnityXRRenderingCapabilities caps{};
  caps.skipPresentToMainScreen = true;  // control sample: the plugin must set the value actively
  if (!vulkanIface) {
    CHECK_EQ(gfx.Start(dh, gfx.userData, &caps), kUnitySubsystemErrorCodeSuccess);
    CHECK(!caps.skipPresentToMainScreen);  // Game View keeps showing the normal camera
  }
  UnityXRFrameSetupHints hints{};
  hints.appSetup.singlePassRendering = true;
  hints.appSetup.zNear = 0.1f;
  hints.appSetup.zFar = 1000.0f;
  hints.appSetup.sRGB = true;
  hints.appSetup.textureResolutionScale = 1.0f;
  hints.changedFlags = kUnityXRFrameSetupHintsChangedSinglePassRendering;
  for (int frame = 0; frame < 3; ++frame) {
    UnityXRNextFrameDesc next{};
    next.renderPassesCount = 99;  // control sample
    next.mirrorBlitMode = kUnityXRMirrorBlitDefault;
    CHECK_EQ(gfx.PopulateNextFrameDesc(dh, gfx.userData, &hints, &next), kUnitySubsystemErrorCodeSuccess);
    CHECK_EQ(next.renderPassesCount, 0);  // no session -> no render passes
    CHECK_EQ(next.mirrorBlitMode, kUnityXRMirrorBlitLeftEye);  // default: left eye
    CHECK_EQ(gfx.SubmitCurrentFrame(dh, gfx.userData), kUnitySubsystemErrorCodeSuccess);
  }
  CHECK(status().framesSubmitted == 0ULL);

  // Mirror mode follows LinuxXR_SetSettings (0 left, 1 right, 2 both, 3 off)
  {
    const struct { int setting; int expected; } modes[] = {{1, kUnityXRMirrorBlitRightEye},
                                                           {2, kUnityXRMirrorBlitSideBySide},
                                                           {3, kUnityXRMirrorBlitNone},
                                                           {0, kUnityXRMirrorBlitLeftEye}};
    for (auto& m : modes) {
      P.setSettings("auto", 1, 1, m.setting, 1.0f, 0);
      UnityXRNextFrameDesc next{};
      CHECK_EQ(gfx.PopulateNextFrameDesc(dh, gfx.userData, &hints, &next), kUnitySubsystemErrorCodeSuccess);
      CHECK_EQ(next.mirrorBlitMode, m.expected);
    }
  }

  // Main-thread callbacks
  UnityXRDisplayState state{};
  state.displayIsTransparent = true;
  state.focusLost = false;
  CHECK_EQ(main.UpdateDisplayState(dh, main.userData, &state), kUnitySubsystemErrorCodeSuccess);
  CHECK(!state.displayIsTransparent);
  CHECK(!state.contentProtectionEnabled);
  CHECK(state.focusLost);  // without a running session the plugin reports focus loss
  UnityXRMirrorViewBlitInfo info{};
  UnityXRMirrorViewBlitDesc desc{};
  desc.blitParamsCount = 5;
  desc.nativeBlitAvailable = true;
  CHECK_EQ(main.QueryMirrorViewBlitDesc(dh, main.userData, info, &desc), kUnitySubsystemErrorCodeSuccess);
  CHECK_EQ(desc.blitParamsCount, 0);  // no eye textures yet
  CHECK(!desc.nativeBlitAvailable);

  if (!vulkanIface) CHECK_EQ(gfx.Stop(dh, gfx.userData), kUnitySubsystemErrorCodeSuccess);
  dl.Stop(dh, dl.userData);  // Stop after a failed start must be safe
  CHECK_EQ(status().displayState, 1);
  dl.Shutdown(dh, dl.userData);
  CHECK_EQ(status().displayState, 0);

  // ---- Input ----
  g.inputProviderRegistered = false;
  CHECK_EQ(il.Initialize(ih, il.userData), kUnitySubsystemErrorCodeSuccess);
  CHECK(g.inputProviderRegistered);
  CHECK(g.inputHandle == ih);
  CHECK_EQ(status().inputState, 1);
  if (!g.inputProviderRegistered) return;
  const UnityXRInputProvider& in = g.inputProvider;
  // All callbacks Unity calls unchecked must be set
  CHECK(in.Tick && in.FillDeviceDefinition && in.UpdateDeviceState && in.UpdateDeviceConfig && in.HandleEvent);
  CHECK(in.HandleRecenter && in.HandleHapticImpulse && in.HandleHapticBuffer && in.QueryHapticCapabilities);
  CHECK(in.HandleHapticStop && in.QueryTrackingOriginMode && in.QuerySupportedTrackingOriginModes);
  CHECK(in.HandleSetTrackingOriginMode && in.TryGetDeviceStateAtTime);

  errs = expectStartFailure("Input", false, il, ih, sc.errors);
  if (sc.errors) CHECK(!errs.empty());

  if (in.Tick) {  // without a session: cache empty, but success
    CHECK_EQ(in.Tick(ih, in.userData, kUnityXRInputUpdateTypeDynamic), kUnitySubsystemErrorCodeSuccess);
    CHECK_EQ(in.Tick(ih, in.userData, kUnityXRInputUpdateTypeBeforeRender), kUnitySubsystemErrorCodeSuccess);
  }
  if (in.QuerySupportedTrackingOriginModes) {
    UnityXRInputTrackingOriginModeFlags modes = kUnityXRInputTrackingOriginModeUnknown;
    CHECK_EQ(in.QuerySupportedTrackingOriginModes(ih, in.userData, &modes), kUnitySubsystemErrorCodeSuccess);
    CHECK((modes & kUnityXRInputTrackingOriginModeFloor) != 0);
    CHECK((modes & kUnityXRInputTrackingOriginModeDevice) != 0);
  }
  if (in.QueryTrackingOriginMode && in.HandleSetTrackingOriginMode) {
    P.setSettings("auto", 1, 1, 0, 1.0f, 0);  // initial state: Floor
    UnityXRInputTrackingOriginModeFlags mode = kUnityXRInputTrackingOriginModeUnknown;
    CHECK_EQ(in.QueryTrackingOriginMode(ih, in.userData, &mode), kUnitySubsystemErrorCodeSuccess);
    CHECK_EQ(static_cast<int>(mode), static_cast<int>(kUnityXRInputTrackingOriginModeFloor));
    CHECK_EQ(in.HandleSetTrackingOriginMode(ih, in.userData, kUnityXRInputTrackingOriginModeDevice),
             kUnitySubsystemErrorCodeSuccess);
    in.QueryTrackingOriginMode(ih, in.userData, &mode);
    CHECK_EQ(static_cast<int>(mode), static_cast<int>(kUnityXRInputTrackingOriginModeDevice));
    CHECK_EQ(in.HandleSetTrackingOriginMode(ih, in.userData, kUnityXRInputTrackingOriginModeFloor),
             kUnitySubsystemErrorCodeSuccess);
    in.QueryTrackingOriginMode(ih, in.userData, &mode);
    CHECK_EQ(static_cast<int>(mode), static_cast<int>(kUnityXRInputTrackingOriginModeFloor));

    // LinuxXR_SetSettings (C# side) affects the same setting: originFloor=0 -> Device, 1 -> Floor
    P.setSettings("auto", 0, 1, 0, 1.0f, 0);
    in.QueryTrackingOriginMode(ih, in.userData, &mode);
    CHECK_EQ(static_cast<int>(mode), static_cast<int>(kUnityXRInputTrackingOriginModeDevice));
    P.setSettings("auto", 1, 1, 0, 1.0f, 0);
    in.QueryTrackingOriginMode(ih, in.userData, &mode);
    CHECK_EQ(static_cast<int>(mode), static_cast<int>(kUnityXRInputTrackingOriginModeFloor));
  }
  // Device definitions are session-independent (stubs of the fake interface accept any pointer)
  if (in.FillDeviceDefinition) {
    UnityXRInputDeviceDefinition* def = reinterpret_cast<UnityXRInputDeviceDefinition*>(&g);
    for (uint32_t id : {1u, 2u, 3u}) CHECK_EQ(in.FillDeviceDefinition(ih, in.userData, id, def), kUnitySubsystemErrorCodeSuccess);
    CHECK_EQ(in.FillDeviceDefinition(ih, in.userData, 4, def), kUnitySubsystemErrorCodeInvalidArguments);
  }
  if (in.QueryHapticCapabilities) {
    UnityXRHapticCapabilities hc;
    memset(&hc, 0xFF, sizeof hc);
    CHECK_EQ(in.QueryHapticCapabilities(ih, in.userData, 1, &hc), kUnitySubsystemErrorCodeSuccess);
    CHECK(!hc.supportsImpulse);  // HMD: no haptics
    memset(&hc, 0xFF, sizeof hc);
    CHECK_EQ(in.QueryHapticCapabilities(ih, in.userData, 2, &hc), kUnitySubsystemErrorCodeSuccess);
    CHECK(hc.supportsImpulse);  // controller: impulse
    CHECK_EQ(hc.numChannels, 1u);
  }
  if (in.HandleHapticImpulse)  // without a session haptics fail cleanly
    CHECK_EQ(in.HandleHapticImpulse(ih, in.userData, 2, 0, 0.5f, 0.1f), kUnitySubsystemErrorCodeInvalidArguments);
  if (in.HandleHapticStop) CHECK_EQ(in.HandleHapticStop(ih, in.userData, 2), kUnitySubsystemErrorCodeSuccess);
  if (in.HandleRecenter) CHECK_EQ(in.HandleRecenter(ih, in.userData), kUnitySubsystemErrorCodeSuccess);

  // Teardown in Unity's order: Stop -> Shutdown
  il.Stop(ih, il.userData);
  CHECK_EQ(status().inputState, 1);
  il.Shutdown(ih, il.userData);
  CHECK_EQ(status().inputState, 0);
  CHECK_EQ(status().sessionState, 0);
}

bool phaseLifecycle() {
  std::printf("[6] UnityPluginLoad and subsystem lifecycle (negative cases: Start fails cleanly)\n");
  P.load(&gInterfaces);

  // Both subsystems registered, with plugin name "LinuxXR" and the expected IDs
  CHECK(g.displayLifecycle.registered);
  CHECK_EQ(g.displayLifecycle.pluginName, std::string("LinuxXR"));
  CHECK_EQ(g.displayLifecycle.id, std::string("LinuxXR Display"));
  CHECK_EQ(g.displayLifecycle.calls, 1);
  CHECK(g.inputLifecycle.registered);
  CHECK_EQ(g.inputLifecycle.pluginName, std::string("LinuxXR"));
  CHECK_EQ(g.inputLifecycle.id, std::string("LinuxXR Input"));
  CHECK_EQ(g.inputLifecycle.calls, 1);
  if (!g.displayLifecycle.registered || !g.inputLifecycle.registered) return false;

  const UnityLifecycleProvider& dl = g.displayLifecycle.provider;
  const UnityLifecycleProvider& il = g.inputLifecycle.provider;
  CHECK(dl.Initialize && dl.Start && dl.Stop && dl.Shutdown);
  CHECK(il.Initialize && il.Start && il.Stop && il.Shutdown);
  if (!(dl.Initialize && dl.Start && dl.Stop && dl.Shutdown && il.Initialize && il.Start && il.Stop && il.Shutdown))
    return false;

  // Before Initialize no providers are registered yet
  CHECK(!g.displayMainRegistered);
  CHECK(!g.displayGfxRegistered);
  CHECK(!g.inputProviderRegistered);

  // Default mirror mode is the left eye
  P.setSettings("auto", 1, 1, 0, 1.0f, 0);

  for (const NegScenario& sc : kScenarios) {
    if (sc.realVulkanDevice) {
      // Only meaningful if really no runtime is reachable (hermetic test environment, see CMakeLists.txt)
      if (K.unityInstance.device == VK_NULL_HANDLE) {
        std::printf("  -- Scenario: %s: SKIPPED (no Vulkan device from phaseVulkan)\n", sc.name);
        continue;
      }
      if (status().runtimeReachable) {
        std::printf("  -- Scenario: %s: SKIPPED (a real OpenXR runtime is reachable)\n", sc.name);
        continue;
      }
    }
    runNegativeScenario(sc);
  }

  // Second Play run: input first, then display (reverse order); providers are registered anew
  {
    std::printf("  -- Scenario: second run, input before display\n");
    g.provideGraphics = false;
    UnitySubsystemHandle dh = reinterpret_cast<UnitySubsystemHandle>(static_cast<uintptr_t>(0xD15));
    UnitySubsystemHandle ih = reinterpret_cast<UnitySubsystemHandle>(static_cast<uintptr_t>(0x1AB));
    g.displayGfxRegistered = g.displayMainRegistered = g.inputProviderRegistered = false;
    CHECK_EQ(il.Initialize(ih, il.userData), kUnitySubsystemErrorCodeSuccess);
    CHECK(g.inputProviderRegistered);
    CHECK_EQ(dl.Initialize(dh, dl.userData), kUnitySubsystemErrorCodeSuccess);
    CHECK(g.displayGfxRegistered && g.displayMainRegistered);
    expectStartFailure("Input", false, il, ih);
    expectStartFailure("Display", true, dl, dh);
    expectStartFailure("Display (again)", true, dl, dh);  // repetition without a Stop in between
    il.Stop(ih, il.userData);
    dl.Stop(dh, dl.userData);
    il.Shutdown(ih, il.userData);
    dl.Shutdown(dh, dl.userData);
    CHECK_EQ(status().inputState, 0);
    CHECK_EQ(status().displayState, 0);
    // Shutdown without a prior Initialize/Start (double teardown) must not crash
    dl.Shutdown(dh, dl.userData);
    il.Shutdown(ih, il.userData);
    CHECK_EQ(status().sessionState, 0);
  }
  g.provideGraphics = false;
  return true;
}

void phaseExports() {
  std::printf("[7] C exports (LinuxXR_*)\n");
  LinuxXRStatus s = status();
  CHECK_EQ(s.preinitRan, 1);
  CHECK_EQ(s.sessionState, 0);
  CHECK(s.framesSubmitted == 0ULL);

  // GetDiagnostics: two phases
  const std::string diag = twoPhaseText(P.getDiagnostics);
  std::printf("  --- Diagnostics ---\n");
  {
    size_t i = 0;
    while (i < diag.size()) {
      size_t j = diag.find('\n', i);
      if (j == std::string::npos) j = diag.size();
      std::printf("  | %s\n", diag.substr(i, j - i).c_str());
      i = j + 1;
    }
  }
  CHECK(contains(diag, "Version: " LINUXXR_EXPECTED_VERSION "\n"));
  CHECK(contains(diag, "Runtime setting: "));
  CHECK(contains(diag, "Runtime: "));
  CHECK(contains(diag, "PreInit: ran ("));
  CHECK(contains(diag, "Vulkan device: "));
  CHECK(contains(diag, "Log file: "));
  CHECK(contains(diag, "OpenXR loader: "));
  if (!gGpuName.empty()) CHECK(contains(diag, ("GPU " + gGpuName).c_str()));

  // Buffer too small: truncated, but '\0'-terminated; return value = required size
  {
    const int needed = P.getDiagnostics(nullptr, 0);
    char small[8];
    memset(small, 'X', sizeof small);
    CHECK_EQ(P.getDiagnostics(small, static_cast<int>(sizeof small)), needed);
    CHECK_EQ(small[sizeof small - 1], '\0');
    CHECK_EQ(std::string(small), diag.substr(0, sizeof small - 1));
    // capacity 1 -> only '\0'
    char one[2] = {'X', 'X'};
    CHECK_EQ(P.getDiagnostics(one, 1), needed);
    CHECK_EQ(one[0], '\0');
    CHECK_EQ(one[1], 'X');
  }

  // ListRuntimes: one line per candidate "<kind>\t<source>\t<path>\t<0|1>"
  const std::string list = twoPhaseText(P.listRuntimes);
  std::printf("  Runtimes:%s\n", list.empty() ? " (none)" : "");
  {
    size_t i = 0;
    int lines = 0;
    while (i < list.size()) {
      size_t j = list.find('\n', i);
      CHECK(j != std::string::npos);  // every line ends with '\n'
      if (j == std::string::npos) break;
      const std::string line = list.substr(i, j - i);
      std::printf("    %s\n", line.c_str());
      int tabs = 0;
      for (char c : line) tabs += c == '\t';
      CHECK_EQ(tabs, 3);
      CHECK(line.size() >= 2 && (line.substr(line.size() - 2) == "\t0" || line.substr(line.size() - 2) == "\t1"));
      // WiVRn/Monado sockets do not exist in $XDG_RUNTIME_DIR (empty) -> not reachable
      if (line.rfind("WiVRn\t", 0) == 0 || line.rfind("Monado\t", 0) == 0) CHECK(line.back() == '0');
      ++lines;
      i = j + 1;
    }
    (void)lines;
  }
  CHECK_EQ(P.listRuntimes(nullptr, 0), static_cast<int>(list.size()) + 1);
  {
    // Buffer too small
    char tiny[4];
    memset(tiny, 'X', sizeof tiny);
    const int needed = P.listRuntimes(tiny, static_cast<int>(sizeof tiny));
    CHECK_EQ(needed, static_cast<int>(list.size()) + 1);
    CHECK_EQ(tiny[sizeof tiny - 1], '\0');
  }

  // nullptr as status pointer must not crash
  P.getStatus(nullptr);
  CHECK(true);
}

void phaseUnload() {
  std::printf("[8] UnityPluginUnload and cleanup\n");
  P.unload();

  // The plugin must have logged via IUnityXRTrace throughout the whole run ("[LinuxXR] ...").
  // Error entries exist exactly in the negative scenarios (expectStartFailure) - otherwise none.
  CHECK(g.traceTotal > 0);
  bool sawLoaded = false, sawPreInit = false;
  for (auto& l : g.traceLines) {
    CHECK(l.rfind("[LinuxXR] ", 0) == 0);
    sawLoaded = sawLoaded || contains(l, "loaded");
    sawPreInit = sawPreInit || contains(l, "PreInit provider registered");
  }
  CHECK(sawLoaded);
  CHECK(sawPreInit);
  CHECK_EQ(g.traceErrors, g.expectedErrors);
  for (auto& l : g.traceErrorLines) std::printf("  (expected) error trace: %s\n", l.c_str());
  std::printf("  Trace: %d messages, %d warnings, %d errors (%d expected)\n", g.traceTotal, g.traceWarnings,
              g.traceErrors, g.expectedErrors);

  // Release the Vulkan device from phaseVulkan (the plugin never owned it)
  if (K.heldInstance) {
    if (K.destroyDevice && K.unityInstance.device) K.destroyDevice(K.unityInstance.device, nullptr);
    if (K.destroyInstance) K.destroyInstance(K.heldInstance, nullptr);
    K.heldInstance = VK_NULL_HANDLE;
  }

  if (P.lib) {
    CHECK_EQ(dlclose(P.lib), 0);
    P.lib = nullptr;
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string path = argc > 1 ? argv[1] : "./libLinuxXR.so";
  if (path.find('/') == std::string::npos) path = "./" + path;  // dlopen should not search the search path

  // Remove a stale Vulkan cache from earlier test runs (the test must see the base list)
  if (const char* cache = getenv("XDG_CACHE_HOME"); cache && *cache) {
    const std::string f = std::string(cache) + "/linuxxr/vulkan_requirements.txt";
    unlink(f.c_str());
  }
  const char* xr = getenv("XDG_RUNTIME_DIR");
  std::printf("fake_unity_host: XDG_RUNTIME_DIR=%s XDG_CACHE_HOME=%s LINUXXR_RUNTIME=%s\n", xr ? xr : "(not set)",
              getenv("XDG_CACHE_HOME") ? getenv("XDG_CACHE_HOME") : "(not set)",
              getenv("LINUXXR_RUNTIME") ? getenv("LINUXXR_RUNTIME") : "(not set)");

  if (phaseLoad(path.c_str())) {
    phaseFakeRegistry();
    if (phasePreInit()) {
      phaseVulkan();
      phaseRuntimeSelection();
    }
    if (phaseLifecycle()) phaseExports();
    phaseUnload();
  }
  return lxr_test::summary("linuxxr_fake_unity_host");
}
