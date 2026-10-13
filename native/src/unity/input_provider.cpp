// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
// Input subsystem "LinuxXR Input" - head and controllers as Unity XR devices.
//
// Devices: 1 = "LinuxXR HMD"                 (HeadMounted | TrackedDevice)
//          2 = "LinuxXR Controller - Left"   (HeldInHand | TrackedDevice | Controller | Left)
//          3 = "LinuxXR Controller - Right"  (... | Right)
// Feature names/usages match Unity's CommonUsages -> they work with InputDevices.TryGetFeatureValue,
// the Input System (<XRController>{LeftHand}/devicePosition, {TriggerButton} ...), XRI and HurricaneVR (XR input).
// Head tracking for the camera ALWAYS comes from the input subsystem in Unity (CenterEyePosition/Rotation);
// the head pose reported in BeforeRender is passed on to the display (layer poses).

#include <cstring>

#include "core/log.h"
#include "core/xr_math.h"
#include "unity/provider_context.h"

namespace lxr::unity {
namespace {

enum DeviceId : UnityXRInternalInputDeviceId { kHmd = 1, kLeftCtrl = 2, kRightCtrl = 3 };

struct HmdFeatures {
  UnityXRInputFeatureIndex isTracked, trackingState, devicePos, deviceRot, deviceVel, deviceAngVel;
  UnityXRInputFeatureIndex centerPos, centerRot, leftPos, leftRot, rightPos, rightRot, userPresence;
};
struct CtrlFeatures {
  UnityXRInputFeatureIndex isTracked, trackingState, devicePos, deviceRot, deviceVel, deviceAngVel;
  UnityXRInputFeatureIndex pointerPos, pointerRot;
  UnityXRInputFeatureIndex primary2D, trigger, grip, triggerButton, gripButton;
  UnityXRInputFeatureIndex primaryButton, primaryTouch, secondaryButton, secondaryTouch;
  UnityXRInputFeatureIndex stickClick, stickTouch, menuButton, triggerTouch, thumbrestTouch;
};

struct Cache {
  XrPosef head{{0, 0, 0, 1}, {0, 0, 0}};
  XrSpaceLocationFlags headFlags = 0;
  XrSpaceVelocity headVel{XR_TYPE_SPACE_VELOCITY};
  ControllerState ctrl[2];
};

struct InputState {
  UnitySubsystemHandle handle = nullptr;
  HmdFeatures hmd{};
  CtrlFeatures ctrl[2]{};
  Cache cache;
  bool started = false;
  uint64_t ticks = 0;
};
InputState g;

IUnityXRInputInterface* api() { return context().input; }

UnityXRVector3 v3(const Vec3& v) { return {v.x, v.y, v.z}; }
UnityXRVector4 q4(const Quat& q) { return {q.x, q.y, q.z, q.w}; }

UnityXRInputFeatureIndex addFeature(UnityXRInputDeviceDefinition* d, const char* name, UnityXRInputFeatureType type,
                                    UnityXRInputFeatureUsage usage) {
  if (usage) return api()->DeviceDefinition_AddFeatureWithUsage(d, name, type, usage);
  return api()->DeviceDefinition_AddFeature(d, name, type);
}

void defineHmd(UnityXRInputDeviceDefinition* d) {
  api()->DeviceDefinition_SetName(d, "LinuxXR HMD");
  api()->DeviceDefinition_SetManufacturer(d, "LinuxXR");
  api()->DeviceDefinition_SetCharacteristics(
      d, static_cast<UnityXRInputDeviceCharacteristics>(kUnityXRInputDeviceCharacteristicsHeadMounted |
                                                        kUnityXRInputDeviceCharacteristicsTrackedDevice));
  auto& f = g.hmd;
  f.isTracked = addFeature(d, "IsTracked", kUnityXRInputFeatureTypeBinary, kUnityXRInputFeatureUsageIsTracked);
  f.trackingState = addFeature(d, "TrackingState", kUnityXRInputFeatureTypeDiscreteStates,
                               kUnityXRInputFeatureUsageTrackingState);
  f.devicePos = addFeature(d, "DevicePosition", kUnityXRInputFeatureTypeAxis3D, kUnityXRInputFeatureUsageDevicePosition);
  f.deviceRot = addFeature(d, "DeviceRotation", kUnityXRInputFeatureTypeRotation, kUnityXRInputFeatureUsageDeviceRotation);
  f.deviceVel = addFeature(d, "DeviceVelocity", kUnityXRInputFeatureTypeAxis3D, kUnityXRInputFeatureUsageDeviceVelocity);
  f.deviceAngVel = addFeature(d, "DeviceAngularVelocity", kUnityXRInputFeatureTypeAxis3D,
                              kUnityXRInputFeatureUsageDeviceAngularVelocity);
  f.centerPos = addFeature(d, "CenterEyePosition", kUnityXRInputFeatureTypeAxis3D,
                           kUnityXRInputFeatureUsageCenterEyePosition);
  f.centerRot = addFeature(d, "CenterEyeRotation", kUnityXRInputFeatureTypeRotation,
                           kUnityXRInputFeatureUsageCenterEyeRotation);
  f.leftPos = addFeature(d, "LeftEyePosition", kUnityXRInputFeatureTypeAxis3D, kUnityXRInputFeatureUsageLeftEyePosition);
  f.leftRot = addFeature(d, "LeftEyeRotation", kUnityXRInputFeatureTypeRotation, kUnityXRInputFeatureUsageLeftEyeRotation);
  f.rightPos = addFeature(d, "RightEyePosition", kUnityXRInputFeatureTypeAxis3D, kUnityXRInputFeatureUsageRightEyePosition);
  f.rightRot = addFeature(d, "RightEyeRotation", kUnityXRInputFeatureTypeRotation,
                          kUnityXRInputFeatureUsageRightEyeRotation);
  f.userPresence = addFeature(d, "UserPresence", kUnityXRInputFeatureTypeBinary, kUnityXRInputFeatureUsageUserPresence);
}

void defineController(UnityXRInputDeviceDefinition* d, int hand) {
  api()->DeviceDefinition_SetName(d, hand == 0 ? "LinuxXR Controller - Left" : "LinuxXR Controller - Right");
  api()->DeviceDefinition_SetManufacturer(d, "LinuxXR");
  api()->DeviceDefinition_SetCharacteristics(
      d, static_cast<UnityXRInputDeviceCharacteristics>(
             kUnityXRInputDeviceCharacteristicsHeldInHand | kUnityXRInputDeviceCharacteristicsTrackedDevice |
             kUnityXRInputDeviceCharacteristicsController |
             (hand == 0 ? kUnityXRInputDeviceCharacteristicsLeft : kUnityXRInputDeviceCharacteristicsRight)));
  auto& f = g.ctrl[hand];
  f.isTracked = addFeature(d, "IsTracked", kUnityXRInputFeatureTypeBinary, kUnityXRInputFeatureUsageIsTracked);
  f.trackingState = addFeature(d, "TrackingState", kUnityXRInputFeatureTypeDiscreteStates,
                               kUnityXRInputFeatureUsageTrackingState);
  f.devicePos = addFeature(d, "DevicePosition", kUnityXRInputFeatureTypeAxis3D, kUnityXRInputFeatureUsageDevicePosition);
  f.deviceRot = addFeature(d, "DeviceRotation", kUnityXRInputFeatureTypeRotation, kUnityXRInputFeatureUsageDeviceRotation);
  f.deviceVel = addFeature(d, "DeviceVelocity", kUnityXRInputFeatureTypeAxis3D, kUnityXRInputFeatureUsageDeviceVelocity);
  f.deviceAngVel = addFeature(d, "DeviceAngularVelocity", kUnityXRInputFeatureTypeAxis3D,
                              kUnityXRInputFeatureUsageDeviceAngularVelocity);
  // Pointer pose (OpenXR aim) - like "pointerPosition"/"pointerRotation" in the Unity OpenXR plugin
  f.pointerPos = addFeature(d, "PointerPosition", kUnityXRInputFeatureTypeAxis3D, nullptr);
  f.pointerRot = addFeature(d, "PointerRotation", kUnityXRInputFeatureTypeRotation, nullptr);
  f.primary2D = addFeature(d, "Primary2DAxis", kUnityXRInputFeatureTypeAxis2D, kUnityXRInputFeatureUsagePrimary2DAxis);
  f.trigger = addFeature(d, "Trigger", kUnityXRInputFeatureTypeAxis1D, kUnityXRInputFeatureUsageTrigger);
  f.grip = addFeature(d, "Grip", kUnityXRInputFeatureTypeAxis1D, kUnityXRInputFeatureUsageGrip);
  f.triggerButton = addFeature(d, "TriggerButton", kUnityXRInputFeatureTypeBinary, kUnityXRInputFeatureUsageTriggerButton);
  f.gripButton = addFeature(d, "GripButton", kUnityXRInputFeatureTypeBinary, kUnityXRInputFeatureUsageGripButton);
  f.primaryButton = addFeature(d, "PrimaryButton", kUnityXRInputFeatureTypeBinary, kUnityXRInputFeatureUsagePrimaryButton);
  f.primaryTouch = addFeature(d, "PrimaryTouch", kUnityXRInputFeatureTypeBinary, kUnityXRInputFeatureUsagePrimaryTouch);
  f.secondaryButton = addFeature(d, "SecondaryButton", kUnityXRInputFeatureTypeBinary,
                                 kUnityXRInputFeatureUsageSecondaryButton);
  f.secondaryTouch = addFeature(d, "SecondaryTouch", kUnityXRInputFeatureTypeBinary,
                                kUnityXRInputFeatureUsageSecondaryTouch);
  f.stickClick = addFeature(d, "Primary2DAxisClick", kUnityXRInputFeatureTypeBinary,
                            kUnityXRInputFeatureUsagePrimary2DAxisClick);
  f.stickTouch = addFeature(d, "Primary2DAxisTouch", kUnityXRInputFeatureTypeBinary,
                            kUnityXRInputFeatureUsagePrimary2DAxisTouch);
  f.menuButton = addFeature(d, "MenuButton", kUnityXRInputFeatureTypeBinary, kUnityXRInputFeatureUsageMenuButton);
  f.triggerTouch = addFeature(d, "TriggerTouch", kUnityXRInputFeatureTypeBinary, nullptr);
  f.thumbrestTouch = addFeature(d, "ThumbrestTouch", kUnityXRInputFeatureTypeBinary, nullptr);
}

unsigned int trackingFlags(bool pos, bool rot, bool vel) {
  unsigned int f = 0;
  if (pos) f |= kUnityXRInputTrackingStatePosition;
  if (rot) f |= kUnityXRInputTrackingStateRotation;
  if (vel) f |= kUnityXRInputTrackingStateVelocity | kUnityXRInputTrackingStateAngularVelocity;
  return f;
}

// Time for which poses are predicted: the frame Unity renders next.
XrTime targetTime() {
  auto& x = context().xr;
  XrTime t = x.lastPredictedDisplayTime();
  if (t != 0) return t + x.lastPredictedPeriod();
  return x.now();
}

void refreshCache(UnityXRInputUpdateType type) {
  auto& c = context();
  if (!c.sessionReady || c.xr.lossPending()) {
    g.cache = Cache{};
    return;
  }
  if (type == kUnityXRInputUpdateTypeDynamic) c.xrInput.sync();
  XrTime t = targetTime();
  if (t == 0) return;
  XrPosef head;
  XrSpaceLocationFlags flags = 0;
  XrSpaceVelocity vel{XR_TYPE_SPACE_VELOCITY};
  if (c.xr.locateHead(t, head, &flags, &vel)) {
    g.cache.head = head;
    g.cache.headFlags = flags;
    g.cache.headVel = vel;
    if (type == kUnityXRInputUpdateTypeBeforeRender) {
      std::lock_guard<std::mutex> l(c.head.mutex);
      c.head.pose = head;
      c.head.valid = true;
    }
  } else {
    g.cache.headFlags = 0;
  }
  for (int h = 0; h < 2; ++h) c.xrInput.readController(static_cast<Hand>(h), t, g.cache.ctrl[h]);
}

void writeHmd(UnityXRInputDeviceState* s) {
  auto& c = context();
  const Cache& k = g.cache;
  bool rot = (k.headFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
  bool pos = (k.headFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
  bool vel = (k.headVel.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0;
  auto& f = g.hmd;
  Pose head = toUnity(k.head);
  XrPosef eyes[2];
  {
    std::lock_guard<std::mutex> l(c.head.mutex);
    eyes[0] = poseMul(k.head, c.head.eyeInHead[0]);
    eyes[1] = poseMul(k.head, c.head.eyeInHead[1]);
  }
  Pose le = toUnity(eyes[0]), re = toUnity(eyes[1]);
  api()->DeviceState_SetBinaryValue(s, f.isTracked, rot);
  api()->DeviceState_SetDiscreteStateValue(s, f.trackingState, trackingFlags(pos, rot, vel));
  api()->DeviceState_SetAxis3DValue(s, f.devicePos, v3(head.position));
  api()->DeviceState_SetRotationValue(s, f.deviceRot, q4(head.rotation));
  api()->DeviceState_SetAxis3DValue(s, f.deviceVel, v3(toUnity(k.headVel.linearVelocity)));
  api()->DeviceState_SetAxis3DValue(s, f.deviceAngVel, v3(angularVelocityToUnity(k.headVel.angularVelocity)));
  api()->DeviceState_SetAxis3DValue(s, f.centerPos, v3(head.position));
  api()->DeviceState_SetRotationValue(s, f.centerRot, q4(head.rotation));
  api()->DeviceState_SetAxis3DValue(s, f.leftPos, v3(le.position));
  api()->DeviceState_SetRotationValue(s, f.leftRot, q4(le.rotation));
  api()->DeviceState_SetAxis3DValue(s, f.rightPos, v3(re.position));
  api()->DeviceState_SetRotationValue(s, f.rightRot, q4(re.rotation));
  api()->DeviceState_SetBinaryValue(s, f.userPresence, c.xr.focused() || c.xr.sessionState() == XR_SESSION_STATE_VISIBLE);
}

void writeController(UnityXRInputDeviceState* s, int hand) {
  const ControllerState& k = g.cache.ctrl[hand];
  auto& f = g.ctrl[hand];
  bool rot = k.active && k.gripOrientationValid;
  bool pos = k.active && k.gripPositionValid;
  Pose grip = toUnity(k.gripPose);
  Pose aim = toUnity(k.aimPose);
  api()->DeviceState_SetBinaryValue(s, f.isTracked, rot);
  api()->DeviceState_SetDiscreteStateValue(s, f.trackingState, trackingFlags(pos, rot, k.velocityValid));
  api()->DeviceState_SetAxis3DValue(s, f.devicePos, v3(grip.position));
  api()->DeviceState_SetRotationValue(s, f.deviceRot, q4(grip.rotation));
  api()->DeviceState_SetAxis3DValue(s, f.deviceVel, v3(toUnity(k.linearVelocity)));
  api()->DeviceState_SetAxis3DValue(s, f.deviceAngVel, v3(angularVelocityToUnity(k.angularVelocity)));
  api()->DeviceState_SetAxis3DValue(s, f.pointerPos, v3(aim.position));
  api()->DeviceState_SetRotationValue(s, f.pointerRot, q4(aim.rotation));
  api()->DeviceState_SetAxis2DValue(s, f.primary2D, {k.thumbstick.x, k.thumbstick.y});
  api()->DeviceState_SetAxis1DValue(s, f.trigger, k.trigger);
  api()->DeviceState_SetAxis1DValue(s, f.grip, k.squeeze);
  api()->DeviceState_SetBinaryValue(s, f.triggerButton, k.trigger > 0.5f);
  api()->DeviceState_SetBinaryValue(s, f.gripButton, k.squeeze > 0.5f);
  api()->DeviceState_SetBinaryValue(s, f.primaryButton, k.primaryButton);
  api()->DeviceState_SetBinaryValue(s, f.primaryTouch, k.primaryTouch || k.primaryButton);
  api()->DeviceState_SetBinaryValue(s, f.secondaryButton, k.secondaryButton);
  api()->DeviceState_SetBinaryValue(s, f.secondaryTouch, k.secondaryTouch || k.secondaryButton);
  api()->DeviceState_SetBinaryValue(s, f.stickClick, k.thumbstickClick);
  api()->DeviceState_SetBinaryValue(s, f.stickTouch, k.thumbstickTouch || k.thumbstickClick);
  api()->DeviceState_SetBinaryValue(s, f.menuButton, k.menuButton);
  api()->DeviceState_SetBinaryValue(s, f.triggerTouch, k.triggerTouch);
  api()->DeviceState_SetBinaryValue(s, f.thumbrestTouch, k.thumbrestTouch);
}

// ---- Provider callbacks ------------------------------------------------------------

UnitySubsystemErrorCode UNITY_INTERFACE_API Input_Tick(UnitySubsystemHandle, void*, UnityXRInputUpdateType type) {
  g.ticks++;
  refreshCache(type);
  return kUnitySubsystemErrorCodeSuccess;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Input_FillDeviceDefinition(UnitySubsystemHandle, void*,
                                                                        UnityXRInternalInputDeviceId id,
                                                                        UnityXRInputDeviceDefinition* d) {
  switch (id) {
    case kHmd: defineHmd(d); return kUnitySubsystemErrorCodeSuccess;
    case kLeftCtrl: defineController(d, 0); return kUnitySubsystemErrorCodeSuccess;
    case kRightCtrl: defineController(d, 1); return kUnitySubsystemErrorCodeSuccess;
    default: return kUnitySubsystemErrorCodeInvalidArguments;
  }
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Input_UpdateDeviceState(UnitySubsystemHandle, void*,
                                                                     UnityXRInternalInputDeviceId id,
                                                                     UnityXRInputUpdateType, UnityXRInputDeviceState* s) {
  switch (id) {
    case kHmd: writeHmd(s); return kUnitySubsystemErrorCodeSuccess;
    case kLeftCtrl: writeController(s, 0); return kUnitySubsystemErrorCodeSuccess;
    case kRightCtrl: writeController(s, 1); return kUnitySubsystemErrorCodeSuccess;
    default: return kUnitySubsystemErrorCodeInvalidArguments;
  }
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Input_HandleEvent(UnitySubsystemHandle, void*, unsigned int,
                                                               UnityXRInternalInputDeviceId, void*, unsigned int) {
  return kUnitySubsystemErrorCodeNotSupported;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Input_QueryTrackingOriginMode(UnitySubsystemHandle, void*,
                                                                           UnityXRInputTrackingOriginModeFlags* mode) {
  auto& c = context();
  if (c.sessionReady) {
    *mode = c.xr.appSpaceType() == XR_REFERENCE_SPACE_TYPE_LOCAL ? kUnityXRInputTrackingOriginModeDevice
                                                                  : kUnityXRInputTrackingOriginModeFloor;
  } else {
    std::lock_guard<std::mutex> l(c.settingsMutex);
    *mode = c.settings.origin == TrackingOrigin::Floor ? kUnityXRInputTrackingOriginModeFloor
                                                        : kUnityXRInputTrackingOriginModeDevice;
  }
  return kUnitySubsystemErrorCodeSuccess;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Input_QuerySupportedTrackingOriginModes(
    UnitySubsystemHandle, void*, UnityXRInputTrackingOriginModeFlags* modes) {
  *modes = static_cast<UnityXRInputTrackingOriginModeFlags>(kUnityXRInputTrackingOriginModeFloor |
                                                            kUnityXRInputTrackingOriginModeDevice);
  return kUnitySubsystemErrorCodeSuccess;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Input_HandleSetTrackingOriginMode(UnitySubsystemHandle handle, void*,
                                                                               UnityXRInputTrackingOriginModeFlags m) {
  auto& c = context();
  bool floor = (m & kUnityXRInputTrackingOriginModeFloor) != 0;
  {
    std::lock_guard<std::mutex> l(c.settingsMutex);
    c.settings.origin = floor ? TrackingOrigin::Floor : TrackingOrigin::Device;
  }
  if (c.sessionReady) {
    c.pendingOrigin = floor ? 1 : 0;  // applied on the gfx thread before the next frame
  }
  LXR_INFO("Tracking origin: %s", floor ? "Floor" : "Device");
  if (handle) api()->InputSubsystem_TrackingOriginUpdated(handle);
  return kUnitySubsystemErrorCodeSuccess;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Input_TryGetDeviceStateAtTime(UnitySubsystemHandle, void*,
                                                                           UnityXRTimeStamp,
                                                                           UnityXRInternalInputDeviceId,
                                                                           UnityXRInputDeviceState*) {
  return kUnitySubsystemErrorCodeNotSupported;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Input_UpdateDeviceConfig(UnitySubsystemHandle, void*,
                                                                      UnityXRInternalInputDeviceId,
                                                                      UnityXRInputDeviceConfig*) {
  return kUnitySubsystemErrorCodeSuccess;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Input_HandleRecenter(UnitySubsystemHandle, void*) {
  LXR_INFO("Recenter requested - on WiVRn/Quest, long-press the Meta button");
  return kUnitySubsystemErrorCodeSuccess;
}

int handOf(UnityXRInternalInputDeviceId id) { return id == kLeftCtrl ? 0 : (id == kRightCtrl ? 1 : -1); }

UnitySubsystemErrorCode UNITY_INTERFACE_API Input_HandleHapticImpulse(UnitySubsystemHandle, void*,
                                                                       UnityXRInternalInputDeviceId id, int,
                                                                       float amplitude, float duration) {
  int h = handOf(id);
  if (h < 0 || !context().sessionReady) return kUnitySubsystemErrorCodeInvalidArguments;
  return context().xrInput.vibrate(static_cast<Hand>(h), amplitude, duration) ? kUnitySubsystemErrorCodeSuccess
                                                                              : kUnitySubsystemErrorCodeFailure;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Input_HandleHapticBuffer(UnitySubsystemHandle, void*,
                                                                      UnityXRInternalInputDeviceId, int, unsigned int,
                                                                      const unsigned char* const) {
  return kUnitySubsystemErrorCodeNotSupported;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Input_QueryHapticCapabilities(UnitySubsystemHandle, void*,
                                                                           UnityXRInternalInputDeviceId id,
                                                                           UnityXRHapticCapabilities* caps) {
  *caps = {};
  if (handOf(id) >= 0) {
    caps->numChannels = 1;
    caps->supportsImpulse = true;
  }
  return kUnitySubsystemErrorCodeSuccess;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Input_HandleHapticStop(UnitySubsystemHandle, void*,
                                                                    UnityXRInternalInputDeviceId id) {
  int h = handOf(id);
  if (h >= 0 && context().sessionReady) context().xrInput.stopVibration(static_cast<Hand>(h));
  return kUnitySubsystemErrorCodeSuccess;
}

// ---- Lifecycle ----------------------------------------------------------------

UnitySubsystemErrorCode UNITY_INTERFACE_API Lifecycle_Initialize(UnitySubsystemHandle handle, void*) {
  auto& c = context();
  g = InputState{};
  g.handle = handle;
  UnityXRInputProvider p{};
  p.userData = nullptr;
  p.Tick = Input_Tick;
  p.FillDeviceDefinition = Input_FillDeviceDefinition;
  p.UpdateDeviceState = Input_UpdateDeviceState;
  p.UpdateDeviceConfig = Input_UpdateDeviceConfig;
  p.HandleEvent = Input_HandleEvent;
  p.HandleRecenter = Input_HandleRecenter;
  p.HandleHapticImpulse = Input_HandleHapticImpulse;
  p.HandleHapticBuffer = Input_HandleHapticBuffer;
  p.QueryHapticCapabilities = Input_QueryHapticCapabilities;
  p.HandleHapticStop = Input_HandleHapticStop;
  p.QueryTrackingOriginMode = Input_QueryTrackingOriginMode;
  p.QuerySupportedTrackingOriginModes = Input_QuerySupportedTrackingOriginModes;
  p.HandleSetTrackingOriginMode = Input_HandleSetTrackingOriginMode;
  p.TryGetDeviceStateAtTime = Input_TryGetDeviceStateAtTime;
  if (c.input->RegisterInputProvider(handle, &p) != kUnitySubsystemErrorCodeSuccess) {
    LXR_ERROR("RegisterInputProvider failed");
    return kUnitySubsystemErrorCodeFailure;
  }
  c.inputState = 1;
  LXR_INFO("Input subsystem initialized");
  return kUnitySubsystemErrorCodeSuccess;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Lifecycle_Start(UnitySubsystemHandle handle, void*) {
  auto& c = context();
  if (!acquireSession(1)) {
    releaseSession(1);
    if (!c.xr.unavailable()) LXR_ERROR("Input failed to start - see the previous message.");
    return kUnitySubsystemErrorCodeFailure;
  }
  for (UnityXRInternalInputDeviceId id : {kHmd, kLeftCtrl, kRightCtrl}) api()->InputSubsystem_DeviceConnected(handle, id);
  g.started = true;
  c.inputState = 2;
  LXR_INFO("Input subsystem started: HMD + 2 controllers");
  return kUnitySubsystemErrorCodeSuccess;
}

void UNITY_INTERFACE_API Lifecycle_Stop(UnitySubsystemHandle handle, void*) {
  if (g.started) {
    for (UnityXRInternalInputDeviceId id : {kHmd, kLeftCtrl, kRightCtrl})
      api()->InputSubsystem_DeviceDisconnected(handle, id);
  }
  g.started = false;
  context().inputState = 1;
}

void UNITY_INTERFACE_API Lifecycle_Shutdown(UnitySubsystemHandle, void*) {
  releaseSession(1);
  context().inputState = 0;
  g.handle = nullptr;
}

}  // namespace

void registerInputLifecycle() {
  auto& c = context();
  if (!c.input) {
    LXR_ERROR("IUnityXRInputInterface not available");
    return;
  }
  static UnityLifecycleProvider lp{};
  lp.Initialize = Lifecycle_Initialize;
  lp.Start = Lifecycle_Start;
  lp.Stop = Lifecycle_Stop;
  lp.Shutdown = Lifecycle_Shutdown;
  if (c.input->RegisterLifecycleProvider("LinuxXR", "LinuxXR Input", &lp) != kUnitySubsystemErrorCodeSuccess)
    LXR_WARN("Input subsystem not registered - restart the Editor once.");
}

}  // namespace lxr::unity
