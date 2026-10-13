#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tyroonis
# quest-usb.sh - WiVRn over a USB cable instead of Wi-Fi (fallback, needs developer mode + adb).
#
# On the Quest 3, since WiVRn 26.9 this also works WITHOUT adb: enable "USB networking" in the WiVRn
# app. This script is the classic way via adb reverse.
#
#   ./quest-usb.sh            # set up the tunnel + connect WiVRn on the headset to 127.0.0.1
#   ./quest-usb.sh logs       # logcat of the WiVRn client (Ctrl+C quits)
#   ./quest-usb.sh stop       # remove the tunnel
set -euo pipefail
PORT=9757
command -v adb >/dev/null || { echo "adb missing: scripts/setup-host.sh --adb" >&2; exit 1; }

state="$(adb get-state 2>/dev/null || true)"
[[ "$state" == "device" ]] || { echo "No authorized Quest found over USB (adb devices). Allow USB debugging in the headset." >&2; adb devices; exit 1; }

pkg() {
  # Prefer the store version, otherwise GitHub builds
  local all; all="$(adb shell pm list packages 2>/dev/null | tr -d '\r' | cut -d: -f2 | grep '^org.meumeu.wivrn' || true)"
  for p in org.meumeu.wivrn org.meumeu.wivrn.github org.meumeu.wivrn.github.testing org.meumeu.wivrn.github.nightly org.meumeu.wivrn.local; do
    grep -qx "$p" <<<"$all" && { echo "$p"; return; }
  done
  head -1 <<<"$all"
}

case "${1:-start}" in
  start)
    P="$(pkg)"; [[ -n "$P" ]] || { echo "WiVRn app not installed on the headset." >&2; exit 1; }
    echo "» adb reverse tcp:$PORT tcp:$PORT"
    adb reverse "tcp:$PORT" "tcp:$PORT"
    echo "» Starting $P with wivrn+tcp://127.0.0.1:$PORT"
    adb shell am start -a android.intent.action.VIEW -d "wivrn+tcp://127.0.0.1:$PORT" "$P" >/dev/null
    echo "✔ Done. Tip: turn off Wi-Fi on the headset so that nothing goes over the air."
    ;;
  logs)
    P="$(pkg)"; PID="$(adb shell pidof "$P" 2>/dev/null | tr -d '\r' || true)"
    if [[ -n "$PID" ]]; then adb logcat --pid="$PID"; else echo "(client not running - showing filtered logcat)"; adb logcat | grep -i --line-buffered wivrn; fi
    ;;
  stop)
    adb reverse --remove "tcp:$PORT" && echo "✔ Tunnel removed"
    ;;
  *) sed -n '4,11p' "$0"; exit 2 ;;
esac
