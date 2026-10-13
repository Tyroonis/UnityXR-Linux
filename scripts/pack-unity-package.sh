#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tyroonis
# pack-unity-package.sh - builds the release artefacts from unity/Packages/com.tyroonis.xr.linuxopenxr:
#
#   dist/com.tyroonis.xr.linuxopenxr-<version>.tgz          UPM tarball ("Add package from tarball…")
#   dist/LinuxXR-<version>.unitypackage                     Asset Store / "Import Package" (→ Assets/Tyroonis/LinuxXR)
#   dist/upm/                                               package root for the git "upm" branch
#
# All three contain the C# code and the native plugin (Runtime/Native~/libLinuxXR.so,
# built by scripts/build-native.sh or CI). End users install them with Unity's Package Manager - no scripts.
#
#   ./pack-unity-package.sh [--out DIR] [--no-unitypackage]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
PKG_NAME="com.tyroonis.xr.linuxopenxr"
PKG_SRC="$REPO/unity/Packages/$PKG_NAME"
OUT="$REPO/dist"
UNITYPACKAGE=1
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT="$(mkdir -p "$2" && cd "$2" && pwd)"; shift 2 ;;
    --no-unitypackage) UNITYPACKAGE=0; shift ;;
    *) sed -n '4,13p' "$0"; exit 2 ;;
  esac
done

NATIVE="$PKG_SRC/Runtime/Native~"   # "~": ignored by Unity; the Editor code installs it into Assets/LinuxXR
[[ -f "$NATIVE/libLinuxXR.so" ]] || { echo "✘ $NATIVE/libLinuxXR.so missing - run ./scripts/build-native.sh first" >&2; exit 1; }
[[ -f "$NATIVE/libopenxr_loader.so.1" ]] || { echo "✘ $NATIVE/libopenxr_loader.so.1 missing" >&2; exit 1; }
command -v python3 >/dev/null || { echo "✘ python3 missing" >&2; exit 1; }

VERSION="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$PKG_SRC/package.json")"

STAGE="$(mktemp -d)"
trap 'rm -rf -- "$STAGE"' EXIT
P="$STAGE/package"
mkdir -p "$P"
cp -a "$PKG_SRC/." "$P/"

# Docs shown by the Package Manager
cp "$REPO/LICENSE" "$P/LICENSE.md"
cp "$REPO/CHANGELOG.md" "$P/CHANGELOG.md"
[[ -f "$REPO/THIRD_PARTY_NOTICES.md" ]] && cp "$REPO/THIRD_PARTY_NOTICES.md" "$P/THIRD_PARTY_NOTICES.md"

# Ship the plugin without debug info (the build tree keeps the copy with symbols)
if command -v strip >/dev/null; then strip --strip-debug "$P/Runtime/Native~/libLinuxXR.so"; fi

# Every file needs a .meta in an immutable package
python3 "$SCRIPT_DIR/ensure-metas.py" "$P" >/dev/null
python3 "$SCRIPT_DIR/ensure-metas.py" "$P" --check

mkdir -p "$OUT"
TGZ="$OUT/$PKG_NAME-$VERSION.tgz"
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime='2026-01-01 00:00Z' -czf "$TGZ" -C "$STAGE" package
echo "✔ $TGZ ($(du -h "$TGZ" | cut -f1))"

rm -rf -- "$OUT/upm"
cp -a "$P" "$OUT/upm"
echo "✔ $OUT/upm (content of the git 'upm' branch)"

if [[ "$UNITYPACKAGE" == 1 ]]; then
  python3 "$SCRIPT_DIR/make-unitypackage.py" "$P" "Assets/Tyroonis/LinuxXR" "$OUT/LinuxXR-$VERSION.unitypackage"
fi
