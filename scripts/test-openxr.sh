#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tyroonis
# test-openxr.sh - Test the OpenXR chain without Unity.
#
#   ./test-openxr.sh list              # show runtime/manifest info
#   ./test-openxr.sh wivrn             # hello_xr (Vulkan) via WiVRn on the Quest
#   ./test-openxr.sh monado            # hello_xr against Monado with a keyboard/mouse HMD (no headset needed)
#
# Options (after the mode):
#   --debug      XR_LOADER_DEBUG=all, XRT_LOG=debug
#   --apidump    enable the OpenXR API dump layer (package openxr-layer-apidump) -> log file
#   --validate   enable the OpenXR core validation layer (openxr-layer-corevalidation)
#   --gl         hello_xr with OpenGL instead of Vulkan2 (for comparison only)
#
# Logs end up in ./logs/ (next to this script).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$SCRIPT_DIR/logs"; mkdir -p "$LOG_DIR"
STAMP="$(date +%Y%m%d-%H%M%S)"
MODE="${1:-}"; shift || true
GFX="Vulkan2"; LAYERS=()
for a in "$@"; do
  case "$a" in
    --debug) export XR_LOADER_DEBUG=all XRT_LOG=debug ;;
    --apidump) LAYERS+=(XR_APILAYER_LUNARG_api_dump)
               export XR_API_DUMP_EXPORT_TYPE=text XR_API_DUMP_FILE_NAME="$LOG_DIR/apidump-$STAMP.txt" ;;
    --validate) LAYERS+=(XR_APILAYER_LUNARG_core_validation) ;;
    --gl) GFX="OpenGL" ;;
    *) echo "Unknown option: $a" >&2; exit 2 ;;
  esac
done
if (( ${#LAYERS[@]} )); then
  XR_ENABLE_API_LAYERS="$(IFS=:; echo "${LAYERS[*]}")"
  export XR_ENABLE_API_LAYERS
fi

die() { echo "✘ $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "$1 missing ($2)"; }

find_manifest() { # $1 = name-pattern
  local d
  for d in /usr/share/openxr/1 /usr/local/share/openxr/1 \
           "${XDG_DATA_HOME:-$HOME/.local/share}/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1" \
           /var/lib/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1; do
    # shellcheck disable=SC2086
    ls "$d"/$1 2>/dev/null | head -1 | grep . && return 0
  done
  return 1
}

run_hello() { # $1 = manifest
  need hello_xr "package libopenxr-utils"
  local log="$LOG_DIR/hello_xr-$MODE-$STAMP.log"
  echo "» XR_RUNTIME_JSON=$1"
  echo "» hello_xr -g $GFX -v   (Ctrl+C quits)   Log: $log"
  [[ -n "${XR_ENABLE_API_LAYERS:-}" ]] && echo "» API layers: $XR_ENABLE_API_LAYERS"
  XR_RUNTIME_JSON="$1" hello_xr -g "$GFX" -v 2>&1 | tee "$log"
  local rc=${PIPESTATUS[0]}
  echo
  if grep -q "XR_ERROR" "$log"; then
    echo "✘ OpenXR errors in the log:"; grep -m5 "XR_ERROR" "$log" | sed 's/^/   /'
  fi
  grep -qiE 'session state.*(FOCUSED|VISIBLE)' "$log" && echo "✔ Session reached VISIBLE/FOCUSED - the chain works."
  return "$rc"
}

case "$MODE" in
  list)
    need openxr_runtime_list "package libopenxr-utils"
    for m in /usr/share/openxr/1/*.json; do
      [[ "$m" == */active_runtime*.json ]] && continue
      echo "=== $m"
      grep -E '"name"|library_path' "$m" | sed 's/^/   /'
    done
    echo; echo "=== Active runtime according to the loader:"
    openxr_runtime_list 2>&1 | sed 's/^/   /'
    ;;

  wivrn)
    M="$(find_manifest 'openxr_wivrn*.json')" || die "WiVRn manifest not found - run scripts/setup-host.sh"
    if ! pgrep -f wivrn-server >/dev/null; then
      die "wivrn-server is not running. Start it with: flatpak run io.github.wivrn.wivrn  (or wivrn-dashboard)"
    fi
    if [[ ! -S "$XDG_RUNTIME_DIR/wivrn/comp_ipc" ]]; then
      echo "... Waiting for the headset connection (open WiVRn in the Quest and pick the PC). Abort with Ctrl+C."
      for _ in $(seq 1 120); do [[ -S "$XDG_RUNTIME_DIR/wivrn/comp_ipc" ]] && break; sleep 1; done
      [[ -S "$XDG_RUNTIME_DIR/wivrn/comp_ipc" ]] || die "No headset connected (socket missing after 120 s)"
    fi
    echo "✔ Headset connected"
    run_hello "$M"
    ;;

  monado)
    need monado-service "package monado-service (setup-host.sh --monado)"
    M="$(find_manifest 'openxr_monado.json')" || die "Monado manifest not found"
    pgrep -x wivrn-server >/dev/null && echo "Note: wivrn-server is running in parallel - harmless (different socket)."
    SOCK="$XDG_RUNTIME_DIR/monado_comp_ipc"
    if pgrep -x monado-service >/dev/null; then
      echo "» monado-service is already running - using the running instance"
      MPID=""
    else
      MLOG="$LOG_DIR/monado-$STAMP.log"
      echo "» Starting monado-service with a QWERTY HMD (a 'Monado' window opens). Log: $MLOG"
      echo "  Controls: right mouse button + drag = turn head, WASD = move, Ctrl/Alt = left/right controller"
      QWERTY_ENABLE=1 XRT_DEBUG_GUI=1 OXR_DEBUG_GUI=1 XRT_COMPOSITOR_FORCE_XCB=1 \
        monado-service >"$MLOG" 2>&1 &
      MPID=$!
      trap '[[ -n "${MPID:-}" ]] && kill "$MPID" 2>/dev/null' EXIT
      for _ in $(seq 1 20); do [[ -S "$SOCK" ]] && break; sleep 0.5; done
      [[ -S "$SOCK" ]] || { tail -20 "$MLOG"; die "monado-service did not open a socket"; }
    fi
    run_hello "$M"
    ;;

  *)
    sed -n '4,16p' "$0"; exit 2 ;;
esac
