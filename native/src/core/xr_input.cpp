// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#include "core/xr_input.h"

#include <cstring>
#include <vector>

#include "core/log.h"
#include "core/xr_context.h"
#include "core/xr_math.h"

namespace lxr {

XrInput::~XrInput() { shutdown(); }

XrAction XrInput::createAction(const char* name, const char* localized, XrActionType type) {
  XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
  std::strncpy(ci.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
  std::strncpy(ci.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
  ci.actionType = type;
  ci.countSubactionPaths = 2;
  ci.subactionPaths = handPaths_;
  XrAction a = XR_NULL_HANDLE;
  if (!LXR_XR_OK(xr().xrCreateAction(actionSet_, &ci, &a))) return XR_NULL_HANDLE;
  return a;
}

bool XrInput::suggest(const char* profile, const std::vector<std::pair<XrAction, std::string>>& bindings) {
  XrInstance inst = ctx_->instance();
  std::vector<XrActionSuggestedBinding> sb;
  for (auto& [action, path] : bindings) {
    if (!action) continue;
    XrPath p = XR_NULL_PATH;
    if (XR_FAILED(xr().xrStringToPath(inst, path.c_str(), &p))) continue;
    sb.push_back({action, p});
  }
  XrPath profilePath = XR_NULL_PATH;
  if (XR_FAILED(xr().xrStringToPath(inst, profile, &profilePath))) return false;
  XrInteractionProfileSuggestedBinding s{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  s.interactionProfile = profilePath;
  s.countSuggestedBindings = static_cast<uint32_t>(sb.size());
  s.suggestedBindings = sb.data();
  XrResult r = xr().xrSuggestInteractionProfileBindings(inst, &s);
  if (XR_FAILED(r)) {
    LXR_WARN("Bindings for %s rejected: %s", profile, xrResultName(r));
    return false;
  }
  LXR_DEBUG("Bindings for %s registered (%zu)", profile, sb.size());
  return true;
}

bool XrInput::init(XrContext& ctx) {
  if (actionSet_) return true;
  ctx_ = &ctx;
  XrInstance inst = ctx.instance();
  if (!inst || !ctx.session()) return false;

  xr().xrStringToPath(inst, "/user/hand/left", &handPaths_[kLeft]);
  xr().xrStringToPath(inst, "/user/hand/right", &handPaths_[kRight]);

  XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
  std::strcpy(asci.actionSetName, "linuxxr");
  std::strcpy(asci.localizedActionSetName, "LinuxXR");
  asci.priority = 0;
  if (!LXR_XR_OK(xr().xrCreateActionSet(inst, &asci, &actionSet_))) return false;

  gripPose_ = createAction("grip_pose", "Grip pose", XR_ACTION_TYPE_POSE_INPUT);
  aimPose_ = createAction("aim_pose", "Aim pose", XR_ACTION_TYPE_POSE_INPUT);
  trigger_ = createAction("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT);
  triggerTouch_ = createAction("trigger_touch", "Trigger touched", XR_ACTION_TYPE_BOOLEAN_INPUT);
  squeeze_ = createAction("squeeze", "Grip", XR_ACTION_TYPE_FLOAT_INPUT);
  thumbstick_ = createAction("thumbstick", "Stick", XR_ACTION_TYPE_VECTOR2F_INPUT);
  thumbstickClick_ = createAction("thumbstick_click", "Stick pressed", XR_ACTION_TYPE_BOOLEAN_INPUT);
  thumbstickTouch_ = createAction("thumbstick_touch", "Stick touched", XR_ACTION_TYPE_BOOLEAN_INPUT);
  thumbrestTouch_ = createAction("thumbrest_touch", "Thumbrest touched", XR_ACTION_TYPE_BOOLEAN_INPUT);
  primary_ = createAction("primary_button", "A/X", XR_ACTION_TYPE_BOOLEAN_INPUT);
  primaryTouch_ = createAction("primary_touch", "A/X touched", XR_ACTION_TYPE_BOOLEAN_INPUT);
  secondary_ = createAction("secondary_button", "B/Y", XR_ACTION_TYPE_BOOLEAN_INPUT);
  secondaryTouch_ = createAction("secondary_touch", "B/Y touched", XR_ACTION_TYPE_BOOLEAN_INPUT);
  menu_ = createAction("menu_button", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT);
  haptic_ = createAction("haptic", "Vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT);

  // Shared Touch bindings (Quest 2 / Quest 3 / Pro)
  auto touchBindings = [&]() {
    std::vector<std::pair<XrAction, std::string>> b;
    for (const char* side : {"left", "right"}) {
      std::string h = std::string("/user/hand/") + side;
      b.push_back({gripPose_, h + "/input/grip/pose"});
      b.push_back({aimPose_, h + "/input/aim/pose"});
      b.push_back({trigger_, h + "/input/trigger/value"});
      b.push_back({triggerTouch_, h + "/input/trigger/touch"});
      b.push_back({squeeze_, h + "/input/squeeze/value"});
      b.push_back({thumbstick_, h + "/input/thumbstick"});
      b.push_back({thumbstickClick_, h + "/input/thumbstick/click"});
      b.push_back({thumbstickTouch_, h + "/input/thumbstick/touch"});
      b.push_back({thumbrestTouch_, h + "/input/thumbrest/touch"});
      b.push_back({haptic_, h + "/output/haptic"});
    }
    b.push_back({primary_, "/user/hand/left/input/x/click"});
    b.push_back({primaryTouch_, "/user/hand/left/input/x/touch"});
    b.push_back({secondary_, "/user/hand/left/input/y/click"});
    b.push_back({secondaryTouch_, "/user/hand/left/input/y/touch"});
    b.push_back({menu_, "/user/hand/left/input/menu/click"});
    b.push_back({primary_, "/user/hand/right/input/a/click"});
    b.push_back({primaryTouch_, "/user/hand/right/input/a/touch"});
    b.push_back({secondary_, "/user/hand/right/input/b/click"});
    b.push_back({secondaryTouch_, "/user/hand/right/input/b/touch"});
    return b;
  };

  bool any = suggest("/interaction_profiles/oculus/touch_controller", touchBindings());
  if (ctx.isExtensionEnabled("XR_META_touch_controller_plus")) {
    any |= suggest("/interaction_profiles/meta/touch_controller_plus", touchBindings());
  }
  {
    std::vector<std::pair<XrAction, std::string>> b;
    for (const char* side : {"left", "right"}) {
      std::string h = std::string("/user/hand/") + side;
      b.push_back({gripPose_, h + "/input/grip/pose"});
      b.push_back({aimPose_, h + "/input/aim/pose"});
      b.push_back({trigger_, h + "/input/select/click"});
      b.push_back({menu_, h + "/input/menu/click"});
      b.push_back({haptic_, h + "/output/haptic"});
    }
    any |= suggest("/interaction_profiles/khr/simple_controller", b);
  }
  if (!any) {
    LXR_ERROR("No controller bindings accepted");
    shutdown();
    return false;
  }

  for (int h = 0; h < 2; ++h) {
    XrActionSpaceCreateInfo sci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    sci.subactionPath = handPaths_[h];
    sci.poseInActionSpace = poseIdentity();
    sci.action = gripPose_;
    LXR_XR_OK(xr().xrCreateActionSpace(ctx.session(), &sci, &gripSpace_[h]));
    sci.action = aimPose_;
    LXR_XR_OK(xr().xrCreateActionSpace(ctx.session(), &sci, &aimSpace_[h]));
  }

  XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  ai.countActionSets = 1;
  ai.actionSets = &actionSet_;
  if (!LXR_XR_OK(xr().xrAttachSessionActionSets(ctx.session(), &ai))) {
    shutdown();
    return false;
  }
  LXR_INFO("Controller actions ready");
  return true;
}

void XrInput::shutdown() {
  for (int h = 0; h < 2; ++h) {
    if (gripSpace_[h] && xr().xrDestroySpace) xr().xrDestroySpace(gripSpace_[h]);
    if (aimSpace_[h] && xr().xrDestroySpace) xr().xrDestroySpace(aimSpace_[h]);
    gripSpace_[h] = aimSpace_[h] = XR_NULL_HANDLE;
  }
  // Actions are destroyed together with the action set.
  if (actionSet_ && xr().xrDestroyActionSet) xr().xrDestroyActionSet(actionSet_);
  actionSet_ = XR_NULL_HANDLE;
  gripPose_ = aimPose_ = trigger_ = triggerTouch_ = squeeze_ = XR_NULL_HANDLE;
  thumbstick_ = thumbstickClick_ = thumbstickTouch_ = thumbrestTouch_ = XR_NULL_HANDLE;
  primary_ = primaryTouch_ = secondary_ = secondaryTouch_ = menu_ = haptic_ = XR_NULL_HANDLE;
  ctx_ = nullptr;
}

bool XrInput::sync() {
  if (!actionSet_ || !ctx_ || !ctx_->sessionRunning()) return false;
  XrActiveActionSet active{actionSet_, XR_NULL_PATH};
  XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
  si.countActiveActionSets = 1;
  si.activeActionSets = &active;
  XrResult r = xr().xrSyncActions(ctx_->session(), &si);
  return XR_SUCCEEDED(r);  // XR_SESSION_NOT_FOCUSED counts as success but yields inactive actions
}

bool XrInput::readBool(XrAction a, Hand h) const {
  if (!a) return false;
  XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
  gi.action = a;
  gi.subactionPath = handPaths_[h];
  XrActionStateBoolean s{XR_TYPE_ACTION_STATE_BOOLEAN};
  if (XR_FAILED(xr().xrGetActionStateBoolean(ctx_->session(), &gi, &s))) return false;
  return s.isActive && s.currentState;
}

float XrInput::readFloat(XrAction a, Hand h) const {
  if (!a) return 0;
  XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
  gi.action = a;
  gi.subactionPath = handPaths_[h];
  XrActionStateFloat s{XR_TYPE_ACTION_STATE_FLOAT};
  if (XR_FAILED(xr().xrGetActionStateFloat(ctx_->session(), &gi, &s))) return 0;
  return s.isActive ? s.currentState : 0.0f;
}

bool XrInput::readController(Hand h, XrTime time, ControllerState& out) const {
  out = ControllerState{};
  if (!actionSet_ || !ctx_ || !ctx_->hasSession()) return false;

  XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
  gi.action = gripPose_;
  gi.subactionPath = handPaths_[h];
  XrActionStatePose ps{XR_TYPE_ACTION_STATE_POSE};
  if (XR_SUCCEEDED(xr().xrGetActionStatePose(ctx_->session(), &gi, &ps))) out.active = ps.isActive;

  if (time != 0) {
    XrSpaceLocation loc;
    XrSpaceVelocity vel;
    if (ctx_->locateSpace(gripSpace_[h], ctx_->appSpace(), time, loc, &vel)) {
      out.gripPose = loc.pose;
      out.gripPositionValid = (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
      out.gripOrientationValid = (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
      if ((vel.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) &&
          (vel.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT)) {
        out.velocityValid = true;
        out.linearVelocity = vel.linearVelocity;
        out.angularVelocity = vel.angularVelocity;
      }
    }
    if (ctx_->locateSpace(aimSpace_[h], ctx_->appSpace(), time, loc, nullptr)) {
      out.aimPose = loc.pose;
      out.aimValid = (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
    }
  }

  out.trigger = readFloat(trigger_, h);
  out.squeeze = readFloat(squeeze_, h);
  {
    XrActionStateVector2f vs{XR_TYPE_ACTION_STATE_VECTOR2F};
    gi.action = thumbstick_;
    if (XR_SUCCEEDED(xr().xrGetActionStateVector2f(ctx_->session(), &gi, &vs)) && vs.isActive)
      out.thumbstick = vs.currentState;
  }
  out.triggerTouch = readBool(triggerTouch_, h);
  out.thumbstickClick = readBool(thumbstickClick_, h);
  out.thumbstickTouch = readBool(thumbstickTouch_, h);
  out.thumbrestTouch = readBool(thumbrestTouch_, h);
  out.primaryButton = readBool(primary_, h);
  out.primaryTouch = readBool(primaryTouch_, h);
  out.secondaryButton = readBool(secondary_, h);
  out.secondaryTouch = readBool(secondaryTouch_, h);
  out.menuButton = readBool(menu_, h);
  return true;
}

bool XrInput::vibrate(Hand h, float amplitude, float durationSeconds, float frequencyHz) {
  if (!haptic_ || !ctx_ || !ctx_->sessionRunning()) return false;
  XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
  hi.action = haptic_;
  hi.subactionPath = handPaths_[h];
  XrHapticVibration v{XR_TYPE_HAPTIC_VIBRATION};
  v.amplitude = amplitude < 0 ? 0 : (amplitude > 1 ? 1 : amplitude);
  v.duration = durationSeconds <= 0 ? XR_MIN_HAPTIC_DURATION : static_cast<XrDuration>(durationSeconds * 1e9);
  v.frequency = frequencyHz;
  return XR_SUCCEEDED(
      xr().xrApplyHapticFeedback(ctx_->session(), &hi, reinterpret_cast<const XrHapticBaseHeader*>(&v)));
}

bool XrInput::stopVibration(Hand h) {
  if (!haptic_ || !ctx_ || !ctx_->sessionRunning()) return false;
  XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
  hi.action = haptic_;
  hi.subactionPath = handPaths_[h];
  return XR_SUCCEEDED(xr().xrStopHapticFeedback(ctx_->session(), &hi));
}

std::string XrInput::currentProfile(Hand h) const {
  if (!ctx_ || !ctx_->hasSession()) return {};
  XrInteractionProfileState st{XR_TYPE_INTERACTION_PROFILE_STATE};
  if (XR_FAILED(xr().xrGetCurrentInteractionProfile(ctx_->session(), handPaths_[h], &st))) return {};
  if (st.interactionProfile == XR_NULL_PATH) return {};
  char buf[XR_MAX_PATH_LENGTH];
  uint32_t n = 0;
  if (XR_FAILED(xr().xrPathToString(ctx_->instance(), st.interactionProfile, sizeof(buf), &n, buf))) return {};
  return std::string(buf);
}

}  // namespace lxr
