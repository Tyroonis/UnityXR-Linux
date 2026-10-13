#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tyroonis
# build-native.sh - Builds libLinuxXR.so, linuxxr-probe and the tests.
#
#   ./build-native.sh              # Release (RelWithDebInfo) into native/build
#   ./build-native.sh debug        # Debug + AddressSanitizer-friendly flags
#   ./build-native.sh test         # build + ctest
#   ./build-native.sh clean
#
# With LINUXXR_UNITY_PROJECT=/path/to/unity-project the package is then installed into the
# project automatically (scripts/install-unity-package.sh).
#
# After the build, libLinuxXR.so is copied into the Unity package automatically:
#   unity/Packages/com.tyroonis.xr.linuxopenxr/Runtime/Native~/
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."
SRC="$ROOT/native"
BUILD="$SRC/build"
MODE="${1:-release}"

need() { command -v "$1" >/dev/null || { echo "✘ $1 missing - ./scripts/setup-host.sh --dev" >&2; exit 1; }; }
need cmake; need g++
GEN=(); command -v ninja >/dev/null && GEN=(-G Ninja)

for h in /usr/include/vulkan/vulkan.h /usr/include/openxr/openxr.h; do
  [[ -f "$h" ]] || { echo "✘ $h missing (libvulkan-dev / libopenxr-dev) - ./scripts/setup-host.sh --dev" >&2; exit 1; }
done
"$SCRIPT_DIR/fetch-unity-headers.sh"

case "$MODE" in
  clean) rm -rf -- "$BUILD"; echo "✔ $BUILD deleted"; exit 0 ;;
  debug) TYPE=Debug ;;
  release|test) TYPE=RelWithDebInfo ;;
  *) sed -n '4,15p' "$0"; exit 2 ;;
esac

cmake -S "$SRC" -B "$BUILD" "${GEN[@]}" -DCMAKE_BUILD_TYPE="$TYPE" -DLINUXXR_BUILD_TESTS=ON \
      -DLINUXXR_UNITY_PLUGIN_DIR="$ROOT/unity/Packages/com.tyroonis.xr.linuxopenxr/Runtime/Native~"
cmake --build "$BUILD" -j"$(nproc)"

if [[ "$MODE" == "test" ]]; then
  ctest --test-dir "$BUILD" --output-on-failure
fi

if [[ -n "${LINUXXR_UNITY_PROJECT:-}" ]]; then
  echo
  bash "$SCRIPT_DIR/install-unity-package.sh" "$LINUXXR_UNITY_PROJECT"
fi

echo
echo "✔ Done:"
ls -l "$BUILD"/libLinuxXR.so "$BUILD"/linuxxr-probe 2>/dev/null | awk '{print "   "$NF" ("$5" bytes)"}'
