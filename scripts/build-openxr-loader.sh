#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tyroonis
# build-openxr-loader.sh - builds the Khronos OpenXR loader (libopenxr_loader.so.1) that ships inside the
# Unity package. Bundling it means users need no libopenxr1 package, and it works inside the Unity Hub
# Flatpak sandbox. Release CI runs this on Ubuntu 22.04 so the result needs only glibc 2.35+.
#
#   ./build-openxr-loader.sh [--version 1.1.47]
#
# Needs: git, cmake, ninja or make, a C++ compiler, python3. No X11/Wayland dev packages are needed
# (the loader is built without the platform-specific test/hello_xr parts).
set -euo pipefail

VERSION="1.1.47"
[[ "${1:-}" == "--version" ]] && VERSION="$2"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
DEST="$REPO/unity/Packages/com.tyroonis.xr.linuxopenxr/Runtime/Native~"

TMP="$(mktemp -d)"
trap 'rm -rf -- "$TMP"' EXIT
SRC="${OPENXR_SDK_SRC:-$TMP/src}"   # OPENXR_SDK_SRC: reuse an existing checkout (CI also takes the headers from it)
if [[ ! -f "$SRC/CMakeLists.txt" ]]; then
  echo "» OpenXR-SDK release-$VERSION"
  git clone -q --depth 1 --branch "release-$VERSION" https://github.com/KhronosGroup/OpenXR-SDK.git "$SRC"
fi

GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)
cmake -S "$SRC" -B "$TMP/build" "${GEN[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=OFF -DBUILD_API_LAYERS=OFF -DDYNAMIC_LOADER=ON \
  -DCMAKE_SHARED_LINKER_FLAGS="-static-libstdc++ -static-libgcc" >/dev/null
cmake --build "$TMP/build" --target openxr_loader -j"$(nproc)"

SO="$(find "$TMP/build" -name 'libopenxr_loader.so.1.*' -type f | head -1)"
[[ -n "$SO" ]] || { echo "✘ loader not built" >&2; exit 1; }
mkdir -p "$DEST"
cp "$SO" "$DEST/libopenxr_loader.so.1"
strip --strip-unneeded "$DEST/libopenxr_loader.so.1"
cp "$SRC/LICENSE" "$DEST/LICENSE-openxr-loader.txt"
echo "✔ $DEST/libopenxr_loader.so.1 ($(du -h "$DEST/libopenxr_loader.so.1" | cut -f1)), max glibc: $(objdump -T "$DEST/libopenxr_loader.so.1" | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1)"
