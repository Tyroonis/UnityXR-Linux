// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
// Entry points that Unity looks for in libLinuxXR.so.

#include "IUnityInterface.h"
#include "core/log.h"
#include "unity/provider_context.h"

namespace lxr::unity {
void runPreInit(IUnityInterfaces* interfaces);
}

using namespace lxr::unity;

extern "C" {

// Early at Editor start (before graphics initialization), if the loader implements IXRLoaderPreInit.
void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API XRSDKPreInit(IUnityInterfaces* interfaces) {
  runPreInit(interfaces);
}

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* interfaces) {
  auto& c = context();
  c.interfaces = interfaces;
  c.trace = interfaces->Get<IUnityXRTrace>();
  c.display = interfaces->Get<IUnityXRDisplayInterface>();
  c.input = interfaces->Get<IUnityXRInputInterface>();
  installUnityLogSink(c.trace);
  if (!c.preinitRan) loadSettingsFile(c.settings);

  LXR_INFO("LinuxXR %s loaded", LINUXXR_VERSION);
  registerDisplayLifecycle();
  registerInputLifecycle();
}

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload() {
  auto& c = context();
  c.xr.destroy();
  lxr::setLogSink(nullptr, nullptr);
}

}  // extern "C"
