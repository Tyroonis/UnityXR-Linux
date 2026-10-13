#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tyroonis
# setup-unity-flatpak.sh - Unity Hub as a Flatpak: give the Hub (and thus the Editor it starts)
# targeted access to WiVRn. Only changes the current user's Flatpak overrides, nothing on the system.
#
#   ./setup-unity-flatpak.sh            # set overrides + check
#   ./setup-unity-flatpak.sh --check    # check only
#   ./setup-unity-flatpak.sh --undo     # remove the overrides again
#
# Background: the Unity Editor runs inside the Unity Hub Flatpak's sandbox. There the following are missing:
#   - the WiVRn socket   $XDG_RUNTIME_DIR/wivrn/comp_ipc   (-> expose xdg-run/wivrn)
#   - the WiVRn runtime  /var/lib/flatpak/app/io.github.wivrn.wivrn/...  (-> expose read-only)
# The OpenXR loader is also missing in the Flatpak runtime - the LinuxXR package ships its own.
set -euo pipefail

HUB="com.unity.UnityHub"
WIVRN="io.github.wivrn.wivrn"
SYS_WIVRN="/var/lib/flatpak/app/$WIVRN"
USER_WIVRN="$HOME/.local/share/flatpak/app/$WIVRN"
MANIFEST_REL="current/active/files/share/openxr/1/openxr_wivrn.json"
OVERRIDES=("--filesystem=xdg-run/wivrn" "--filesystem=$SYS_WIVRN:ro" "--filesystem=$USER_WIVRN:ro")

command -v flatpak >/dev/null || { echo "flatpak is not installed - this script is not needed."; exit 0; }
flatpak info "$HUB" >/dev/null 2>&1 || { echo "Unity Hub is not a Flatpak ($HUB not found) - script not needed."; exit 0; }

check() {
  echo "» Checking from the Hub Flatpak's point of view ..."
  local ok=1
  # shellcheck disable=SC2016
  if flatpak run --command=sh "$HUB" -c "test -e '$SYS_WIVRN/$MANIFEST_REL' || test -e '$USER_WIVRN/$MANIFEST_REL'"; then
    echo "  ✔ WiVRn runtime visible"
  else
    echo "  ✘ WiVRn runtime NOT visible"
    ok=0
  fi
  # shellcheck disable=SC2016
  if flatpak run --command=sh "$HUB" -c 'test -d "$XDG_RUNTIME_DIR/wivrn"'; then
    echo "  ✔ \$XDG_RUNTIME_DIR/wivrn visible"
    # shellcheck disable=SC2016
    if flatpak run --command=sh "$HUB" -c 'test -S "$XDG_RUNTIME_DIR/wivrn/comp_ipc"'; then
      echo "  ✔ WiVRn socket present (headset connected)"
    else
      echo "  • WiVRn socket missing - appears as soon as WiVRn is running and the Quest is connected"
    fi
  else
    echo "  ✘ \$XDG_RUNTIME_DIR/wivrn NOT visible (start the WiVRn server once, then check again)"
    ok=0
  fi
  return $((1 - ok))
}

case "${1:-}" in
  --check)
    check
    ;;
  --undo)
    for o in "${OVERRIDES[@]}"; do
      flatpak override --user "${o/--filesystem=/--nofilesystem=}" "$HUB"
    done
    echo "✔ Overrides removed. Restart Unity Hub."
    ;;
  "")
    echo "» Setting Flatpak overrides for $HUB (your user only):"
    for o in "${OVERRIDES[@]}"; do echo "    $o"; done
    flatpak override --user "${OVERRIDES[@]}" "$HUB"
    echo
    if check; then
      echo
      echo "✔ Done. Quit Unity Hub completely (flatpak kill $HUB) and restart it."
    else
      echo
      if [[ -d "$SYS_WIVRN" ]] && ! flatpak run --command=sh "$HUB" -c "test -e '$SYS_WIVRN/$MANIFEST_REL'"; then
        echo "The system-wide WiVRn installation cannot be exposed inside the Hub sandbox."
        echo "Fix: install WiVRn as a user Flatpak (it then lives in your home):"
        echo "    flatpak install --user flathub $WIVRN"
        echo "    flatpak uninstall --system $WIVRN     # optional, remove the old installation"
        echo "Then run this script again."
      fi
      exit 1
    fi
    ;;
  *) sed -n '4,14p' "$0"; exit 2 ;;
esac
