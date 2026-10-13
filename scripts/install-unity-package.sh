#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tyroonis
# install-unity-package.sh - FOR DEVELOPERS of LinuxXR. End users install LinuxXR with Unity's Package
# Manager (git URL, tarball or Asset Store) - see docs/INSTALL.md.
#
#   ./install-unity-package.sh <project> --dev       # live development: symlink the package into Assets/
#   ./install-unity-package.sh <project> --tarball   # test the release tarball (packs it first)
#   ./install-unity-package.sh <project> --remove    # remove LinuxXR from the project
#
# --dev   <project>/Assets/LinuxXRDev -> <repo>/unity/Packages/com.tyroonis.xr.linuxopenxr (symlink).
#         On load the package installs its native part into Assets/LinuxXR (like for end users).
#         C# edits and every ./scripts/build-native.sh apply directly.
# --tarball  Packs dist/com.tyroonis.xr.linuxopenxr-<ver>.tgz, copies it to <project>/Packages/ and
#         references it as "file:<name>.tgz" - exactly what "Add package from tarball…" does.
#
# Restart the Unity Editor once after switching (XR subsystems are registered at start-up).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
PKG_NAME="com.tyroonis.xr.linuxopenxr"
PKG_SRC="$REPO/unity/Packages/$PKG_NAME"

usage() { sed -n '4,17p' "$0"; exit 2; }
PROJ="${1:-}"
MODE="${2:-}"
[[ -n "$PROJ" && "$PROJ" != -* && -n "$MODE" ]] || usage

PROJ="$(cd "$PROJ" && pwd)"
MANIFEST="$PROJ/Packages/manifest.json"
[[ -f "$MANIFEST" ]] || { echo "✘ $PROJ is not a Unity project (Packages/manifest.json missing)" >&2; exit 1; }
LINK="$PROJ/Assets/LinuxXRDev"
OLD_LINK="$PROJ/Assets/LinuxXR"   # leftover of older installs (symlink/copy of the package)

set_manifest() {  # $1 = reference or "" to remove
  cp "$MANIFEST" "$MANIFEST.linuxxr-backup"
  python3 - "$MANIFEST" "$PKG_NAME" "$1" <<'PY'
import json, sys
path, name, value = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path) as f:
    data = json.load(f)
deps = data.setdefault("dependencies", {})
old = deps.get(name)
if value:
    deps[name] = value
else:
    deps.pop(name, None)
if deps.get(name) != old:
    with open(path, "w") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    print(f"  manifest.json: {name} = {deps.get(name, '(removed)')}")
PY
}

ensure_xr_management() {
  python3 - "$MANIFEST" <<'PY'
import json, sys
path = sys.argv[1]
data = json.load(open(path))
deps = data.setdefault("dependencies", {})
if "com.unity.xr.management" not in deps:
    deps["com.unity.xr.management"] = "4.5.0"
    json.dump(data, open(path, "w"), indent=2)
    open(path, "a").write("\n")
    print("  manifest.json: com.unity.xr.management = 4.5.0")
PY
}

# Remove LinuxXR from the project, including leftovers of older installs.
cleanup() {
  [[ -L "$LINK" ]] && rm -f -- "$LINK"
  [[ -L "$OLD_LINK" ]] && rm -f -- "$OLD_LINK" "$OLD_LINK.meta"
  [[ -f "$OLD_LINK/package.json" ]] && rm -rf -- "$OLD_LINK" "$OLD_LINK.meta"
  rm -f -- "$LINK.meta"
  rm -rf -- "$PROJ/Packages/$PKG_NAME"                 # embedded copy
  rm -f -- "$PROJ/Packages/$PKG_NAME"-*.tgz
  if [[ -d "$LINK" ]]; then                          # copied native folder (older installs)
    if [[ -f "$LINK/UnitySubsystemsManifest.json" || -f "$LINK/Runtime/UnitySubsystemsManifest.json" ]]; then
      rm -rf -- "$LINK"
    fi
  fi
}

case "$MODE" in
  --dev)
    [[ -f "$PKG_SRC/Runtime/Native~/libLinuxXR.so" ]] || {
      echo "✘ libLinuxXR.so missing - run ./scripts/build-native.sh first" >&2; exit 1; }
    cleanup
    [[ -e "$LINK" ]] && { echo "✘ $LINK exists and is not LinuxXR - move it away first" >&2; exit 1; }
    set_manifest ""
    ensure_xr_management
    ln -s "$PKG_SRC" "$LINK"
    echo "✔ $LINK -> $PKG_SRC"
    ;;
  --tarball)
    bash "$SCRIPT_DIR/pack-unity-package.sh" --no-unitypackage >/dev/null
    VERSION="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$PKG_SRC/package.json")"
    TGZ="$REPO/dist/$PKG_NAME-$VERSION.tgz"
    cleanup
    cp "$TGZ" "$PROJ/Packages/"
    set_manifest "file:$(basename "$TGZ")"
    echo "✔ $PROJ/Packages/$(basename "$TGZ")"
    ;;
  --remove)
    cleanup
    set_manifest ""
    echo "✔ LinuxXR removed from $PROJ"
    exit 0
    ;;
  *) usage ;;
esac
echo "→ Restart the Unity Editor once (XR subsystems are registered at start-up)."
