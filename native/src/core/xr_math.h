// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
#pragma once
// Math helpers: pose composition and OpenXR -> Unity conversion.
//
// OpenXR: right-handed, +Y up, -Z forward.   Unity: left-handed, +Y up, +Z forward.
// Conversion = mirroring at the Z plane:
//   Position         (x, y, z)    -> (x, y, -z)
//   Rotation         (x, y, z, w) -> (-x, -y, z, w)
//   Angular velocity ω            -> (-x, -y, z)     (axial vector)

#include <openxr/openxr.h>

#include <cmath>

namespace lxr {

struct Vec3 {
  float x = 0, y = 0, z = 0;
};
struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
};
struct Pose {
  Vec3 position;
  Quat rotation;
};
struct TanAngles {  // Tangents of the half field-of-view angles (left/down negative)
  float left = -1, right = 1, up = 1, down = -1;
};

inline XrQuaternionf quatMul(const XrQuaternionf& a, const XrQuaternionf& b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
          a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline XrQuaternionf quatConjugate(const XrQuaternionf& q) { return {-q.x, -q.y, -q.z, q.w}; }

inline XrQuaternionf quatNormalize(const XrQuaternionf& q) {
  float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (n < 1e-8f) return {0, 0, 0, 1};
  return {q.x / n, q.y / n, q.z / n, q.w / n};
}

inline XrVector3f quatRotate(const XrQuaternionf& q, const XrVector3f& v) {
  // v' = q * v * q^-1 (optimized form)
  XrVector3f u{q.x, q.y, q.z};
  float s = q.w;
  float dotUV = u.x * v.x + u.y * v.y + u.z * v.z;
  float dotUU = u.x * u.x + u.y * u.y + u.z * u.z;
  XrVector3f cross{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
  return {2.0f * dotUV * u.x + (s * s - dotUU) * v.x + 2.0f * s * cross.x,
          2.0f * dotUV * u.y + (s * s - dotUU) * v.y + 2.0f * s * cross.y,
          2.0f * dotUV * u.z + (s * s - dotUU) * v.z + 2.0f * s * cross.z};
}

inline XrPosef poseIdentity() { return {{0, 0, 0, 1}, {0, 0, 0}}; }

// a * b: apply b first, then a (b is relative to a).
inline XrPosef poseMul(const XrPosef& a, const XrPosef& b) {
  XrPosef r;
  r.orientation = quatNormalize(quatMul(a.orientation, b.orientation));
  XrVector3f p = quatRotate(a.orientation, b.position);
  r.position = {a.position.x + p.x, a.position.y + p.y, a.position.z + p.z};
  return r;
}

inline XrPosef poseInverse(const XrPosef& p) {
  XrPosef r;
  r.orientation = quatConjugate(p.orientation);
  XrVector3f t = quatRotate(r.orientation, p.position);
  r.position = {-t.x, -t.y, -t.z};
  return r;
}

// Pose of 'child' relative to 'parent' (both in the same space): parent^-1 * child
inline XrPosef poseRelative(const XrPosef& parent, const XrPosef& child) { return poseMul(poseInverse(parent), child); }

// --- OpenXR -> Unity ----------------------------------------------------------
inline Vec3 toUnity(const XrVector3f& v) { return {v.x, v.y, -v.z}; }
inline Quat toUnity(const XrQuaternionf& q) { return {-q.x, -q.y, q.z, q.w}; }
inline Pose toUnity(const XrPosef& p) { return {toUnity(p.position), toUnity(p.orientation)}; }
inline Vec3 angularVelocityToUnity(const XrVector3f& w) { return {-w.x, -w.y, w.z}; }

// --- Unity -> OpenXR (same mirroring, self-inverse) -----------------------
inline XrVector3f fromUnity(const Vec3& v) { return {v.x, v.y, -v.z}; }
inline XrQuaternionf fromUnity(const Quat& q) { return {-q.x, -q.y, q.z, q.w}; }
inline XrPosef fromUnity(const Pose& p) { return {fromUnity(p.rotation), fromUnity(p.position)}; }

inline TanAngles tanAngles(const XrFovf& fov) {
  return {std::tan(fov.angleLeft), std::tan(fov.angleRight), std::tan(fov.angleUp), std::tan(fov.angleDown)};
}

// For the culling frustum of both eyes: enclosing FOV.
inline XrFovf fovUnion(const XrFovf& a, const XrFovf& b) {
  return {std::fmin(a.angleLeft, b.angleLeft), std::fmax(a.angleRight, b.angleRight),
          std::fmax(a.angleUp, b.angleUp), std::fmin(a.angleDown, b.angleDown)};
}

inline bool poseIsFinite(const XrPosef& p) {
  return std::isfinite(p.position.x) && std::isfinite(p.position.y) && std::isfinite(p.position.z) &&
         std::isfinite(p.orientation.x) && std::isfinite(p.orientation.y) && std::isfinite(p.orientation.z) &&
         std::isfinite(p.orientation.w);
}

}  // namespace lxr
