// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#pragma once
// Controller input via OpenXR actions.
// One shared set of actions with left/right subaction paths, bindings for
//   /interaction_profiles/oculus/touch_controller        (Quest 2, fallback for all Touch)
//   /interaction_profiles/meta/touch_controller_plus     (Quest 3, if the extension is enabled)
//   /interaction_profiles/khr/simple_controller          (minimal fallback)
// The field names match the Unity CommonUsages (see input_provider.cpp).

#include <string>
#include <utility>
#include <vector>

#include "core/xr_api.h"

namespace lxr {

class XrContext;

enum Hand { kLeft = 0, kRight = 1 };

struct ControllerState {
  bool active = false;  // controller connected/active according to the runtime
  // Poses in app space (OpenXR coordinates)
  XrPosef gripPose{{0, 0, 0, 1}, {0, 0, 0}};
  XrPosef aimPose{{0, 0, 0, 1}, {0, 0, 0}};
  bool gripPositionValid = false, gripOrientationValid = false;
  bool aimValid = false;
  bool velocityValid = false;
  XrVector3f linearVelocity{0, 0, 0};
  XrVector3f angularVelocity{0, 0, 0};
  // Analog
  float trigger = 0, squeeze = 0;
  XrVector2f thumbstick{0, 0};
  // Digital
  bool triggerTouch = false;
  bool thumbstickClick = false, thumbstickTouch = false, thumbrestTouch = false;
  bool primaryButton = false, primaryTouch = false;      // A (right) / X (left)
  bool secondaryButton = false, secondaryTouch = false;  // B (right) / Y (left)
  bool menuButton = false;                               // Menu (left only)
};

class XrInput {
 public:
  ~XrInput();
  // Before the first frame, after createSession. Must be attached before xrSyncActions.
  bool init(XrContext& ctx);
  void shutdown();
  bool initialized() const { return actionSet_ != XR_NULL_HANDLE; }

  // Sync actions (they only deliver values while FOCUSED) and read the state for 'time'.
  bool sync();
  bool readController(Hand hand, XrTime time, ControllerState& out) const;

  bool vibrate(Hand hand, float amplitude, float durationSeconds, float frequencyHz = XR_FREQUENCY_UNSPECIFIED);
  bool stopVibration(Hand hand);

  // Currently active interaction profile, e.g. "/interaction_profiles/oculus/touch_controller"
  std::string currentProfile(Hand hand) const;

 private:
  XrAction createAction(const char* name, const char* localized, XrActionType type);
  bool suggest(const char* profile, const std::vector<std::pair<XrAction, std::string>>& bindings);
  bool readBool(XrAction a, Hand h) const;
  float readFloat(XrAction a, Hand h) const;

  XrContext* ctx_ = nullptr;
  XrActionSet actionSet_ = XR_NULL_HANDLE;
  XrPath handPaths_[2] = {XR_NULL_PATH, XR_NULL_PATH};

  XrAction gripPose_ = XR_NULL_HANDLE, aimPose_ = XR_NULL_HANDLE;
  XrAction trigger_ = XR_NULL_HANDLE, triggerTouch_ = XR_NULL_HANDLE;
  XrAction squeeze_ = XR_NULL_HANDLE;
  XrAction thumbstick_ = XR_NULL_HANDLE, thumbstickClick_ = XR_NULL_HANDLE, thumbstickTouch_ = XR_NULL_HANDLE;
  XrAction thumbrestTouch_ = XR_NULL_HANDLE;
  XrAction primary_ = XR_NULL_HANDLE, primaryTouch_ = XR_NULL_HANDLE;
  XrAction secondary_ = XR_NULL_HANDLE, secondaryTouch_ = XR_NULL_HANDLE;
  XrAction menu_ = XR_NULL_HANDLE;
  XrAction haptic_ = XR_NULL_HANDLE;

  XrSpace gripSpace_[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
  XrSpace aimSpace_[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
};

}  // namespace lxr
