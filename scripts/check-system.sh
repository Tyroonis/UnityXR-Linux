#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tyroonis
# check-system.sh - Read-only diagnostics for UnityVR-Linux.
# Changes nothing on the system. Output can optionally also go to a file:
#   ./check-system.sh                 # terminal only
#   ./check-system.sh --report        # additionally docs/system-report.txt
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPORT=""
[[ "${1:-}" == "--report" ]] && REPORT="$SCRIPT_DIR/../docs/system-report.txt"

if [[ -t 1 ]]; then
  G=$'\e[32m'; Y=$'\e[33m'; R=$'\e[31m'; B=$'\e[1m'; N=$'\e[0m'
else
  G=""; Y=""; R=""; B=""; N=""
fi

OKS=0; WARNS=0; FAILS=0
ok()   { echo "  ${G}✔${N} $*"; OKS=$((OKS+1)); }
warn() { echo "  ${Y}!${N} $*"; WARNS=$((WARNS+1)); }
fail() { echo "  ${R}✘${N} $*"; FAILS=$((FAILS+1)); }
info() { echo "    $*"; }
hdr()  { echo; echo "${B}== $* ==${N}"; }
have() { command -v "$1" >/dev/null 2>&1; }
pkg_ver() { dpkg-query -W -f='${Version}\n' "$1" 2>/dev/null | head -1; }

main() {
  echo "${B}UnityVR-Linux - System check$(date '+ (%Y-%m-%d %H:%M)')${N}"

  # ---------------------------------------------------------------- OS
  hdr "Operating system & session"
  if [[ -r /etc/os-release ]]; then
    . /etc/os-release
    info "Distribution: ${PRETTY_NAME:-?} (${VERSION_CODENAME:-?})"
    case "${VERSION_CODENAME:-}" in
      resolute) ok "Ubuntu 26.04 - the WiVRn PPA has builds for this version" ;;
      noble)    warn "Ubuntu 24.04 - the WiVRn PPA has NO noble builds -> Flatpak required" ;;
      *)        warn "Untested version '${VERSION_CODENAME:-?}'" ;;
    esac
  fi
  info "Kernel: $(uname -r)"
  info "glibc:  $(ldd --version 2>/dev/null | head -1 | awk '{print $NF}')"
  info "Session: XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-?}  Desktop=${XDG_CURRENT_DESKTOP:-?}"
  [[ -n "${WAYLAND_DISPLAY:-}" ]] && info "WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
  [[ -n "${DISPLAY:-}" ]] && info "DISPLAY=$DISPLAY (XWayland/X11 available)"
  if [[ -n "${XDG_RUNTIME_DIR:-}" && -d "$XDG_RUNTIME_DIR" ]]; then
    ok "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
  else
    fail "XDG_RUNTIME_DIR missing - OpenXR IPC to WiVRn will not work"
  fi

  # ---------------------------------------------------------------- GPU
  hdr "GPU, Vulkan, Video-Encoding"
  if have lspci; then
    lspci -nn 2>/dev/null | grep -Ei 'vga|3d|display' | sed 's/^/    /'
  fi
  local gpu_count=0
  if have vulkaninfo; then
    local vsum
    vsum="$(vulkaninfo --summary 2>/dev/null)"
    if [[ -n "$vsum" ]]; then
      info "Vulkan Instance: $(echo "$vsum" | awk -F'= ' '/Vulkan Instance Version/{print $2; exit}')"
      echo "$vsum" | grep -E 'deviceName|driverName|apiVersion|deviceType' | sed 's/^[[:space:]]*/    /'
      gpu_count=$(echo "$vsum" | grep -c 'deviceName' || true)
      ok "Vulkan works ($gpu_count device(s))"
      if echo "$vsum" | grep -q 'PHYSICAL_DEVICE_TYPE_CPU' && (( gpu_count > 1 )); then
        info "(llvmpipe/CPU device present - normal, Unity/WiVRn use the real GPU)"
      fi
    else
      fail "vulkaninfo returns nothing - check the Vulkan driver (mesa-vulkan-drivers)"
    fi
  else
    warn "vulkaninfo missing (package vulkan-tools) - Vulkan not checked"
  fi
  local mesa; mesa="$(pkg_ver mesa-vulkan-drivers)"
  [[ -n "$mesa" ]] && info "mesa-vulkan-drivers: $mesa"

  if have vainfo; then
    local va; va="$(vainfo 2>/dev/null)"
    if [[ -n "$va" ]]; then
      info "VAAPI driver: $(echo "$va" | awk -F': ' '/Driver version/{print $2; exit}')"
      local enc
      enc="$(echo "$va" | grep -E 'VAEntrypointEncSlice' | awk '{print $1}' | sort -u | tr '\n' ' ')"
      if [[ -n "$enc" ]]; then
        ok "VAAPI encoders: $enc"
        echo "$enc" | grep -q 'HEVC' && info "-> H.265 available (WiVRn default for Quest 3)"
        echo "$enc" | grep -q 'AV1'  && info "-> AV1 available"
      else
        warn "VAAPI without encode entrypoints - WiVRn falls back to x264 (CPU)"
      fi
    else
      warn "vainfo returns nothing (mesa-va-drivers installed?)"
    fi
  else
    warn "vainfo missing (package vainfo) - hardware encoding not checked"
  fi
  if ls /dev/dri/renderD* >/dev/null 2>&1; then
    ok "Render nodes: $(ls /dev/dri/renderD* | tr '\n' ' ')"
  else
    fail "No /dev/dri/renderD* - no GPU access"
  fi

  # ---------------------------------------------------------------- OpenXR
  hdr "OpenXR"
  local lv; lv="$(pkg_ver libopenxr-loader1)"
  if [[ -n "$lv" ]]; then ok "libopenxr-loader1 $lv"; else fail "libopenxr-loader1 missing"; fi
  local dv; dv="$(pkg_ver libopenxr-dev)"
  [[ -n "$dv" ]] && info "libopenxr-dev $dv (for building the plugin)"
  if have hello_xr; then ok "hello_xr present (libopenxr-utils)"; else warn "hello_xr missing (libopenxr-utils) - recommended for the phase 0 test"; fi

  info "Runtime manifests found:"
  local found=0 f
  for f in /usr/share/openxr/1/*.json /usr/local/share/openxr/1/*.json /etc/xdg/openxr/1/*.json \
           "${XDG_CONFIG_HOME:-$HOME/.config}"/openxr/1/*.json; do
    [[ -e "$f" || -L "$f" ]] || continue
    found=1
    if [[ -L "$f" ]]; then info "  $f -> $(readlink "$f")"; else info "  $f"; fi
  done
  (( found )) || info "  (none)"
  local act="${XDG_CONFIG_HOME:-$HOME/.config}/openxr/1/active_runtime.json"
  if [[ -e "$act" ]]; then
    info "Active runtime (user): $(readlink -f "$act")"
  elif [[ -e /etc/xdg/openxr/1/active_runtime.json ]]; then
    info "Active runtime (system): $(readlink -f /etc/xdg/openxr/1/active_runtime.json)"
  else
    info "No active runtime set (normal: WiVRn only sets it once the headset is connected)"
  fi
  [[ -n "${XR_RUNTIME_JSON:-}" ]] && warn "XR_RUNTIME_JSON is set globally: $XR_RUNTIME_JSON"
  [[ -d "$HOME/steamvr" || -d "$HOME/.steam/steam/steamapps/common/SteamVR" ]] && \
    info "SteamVR leftovers found - harmless as long as it is not registered as the active runtime"

  # ---------------------------------------------------------------- WiVRn
  hdr "WiVRn"
  local wv; wv="$(pkg_ver wivrn-server)"
  if [[ -n "$wv" ]]; then
    ok "wivrn-server (apt) $wv"; local dv2; dv2="$(pkg_ver wivrn-dashboard)"; [[ -n "$dv2" ]] && info "wivrn-dashboard $dv2"
  elif have flatpak && flatpak info io.github.wivrn.wivrn >/dev/null 2>&1; then
    ok "WiVRn as Flatpak: $(flatpak info io.github.wivrn.wivrn 2>/dev/null | awk -F': ' '/Version/{print $2; exit}')"
  else
    warn "WiVRn not installed -> scripts/setup-host.sh"
  fi
  if grep -rqs "lvra/wivrn" /etc/apt/sources.list.d/ 2>/dev/null; then ok "PPA lvra/wivrn registered"; fi
  local m; m="$(ls /usr/share/openxr/1/openxr_wivrn*.json \
      "${XDG_DATA_HOME:-$HOME/.local/share}"/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn*.json \
      /var/lib/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn*.json 2>/dev/null | head -1)"
  [[ -n "$m" ]] && info "Manifest: $m -> $(grep -o '"library_path"[^,}]*' "$m" | head -1)"
  if pgrep -f wivrn-server >/dev/null 2>&1; then
    ok "wivrn-server running (PID $(pgrep -f wivrn-server | tr '\n' ' '))"
  else
    info "wivrn-server is not running right now"
  fi
  if systemctl --user list-unit-files wivrn.service >/dev/null 2>&1; then
    info "systemd user unit: $(systemctl --user is-enabled wivrn.service 2>/dev/null || echo '?') / $(systemctl --user is-active wivrn.service 2>/dev/null || echo '?')"
  fi
  if [[ -S "${XDG_RUNTIME_DIR:-/nonexistent}/wivrn/comp_ipc" ]]; then
    ok "IPC socket present -> headset connected, OpenXR apps can start"
  else
    info "No IPC socket (\$XDG_RUNTIME_DIR/wivrn/comp_ipc) -> headset not connected"
  fi
  [[ -f "$HOME/.config/wivrn/config.json" ]] && info "Config: ~/.config/wivrn/config.json"
  [[ -f "$HOME/.var/app/io.github.wivrn.wivrn/config/wivrn/config.json" ]] && info "Config (Flatpak): ~/.var/app/io.github.wivrn.wivrn/config/wivrn/config.json"

  # ---------------------------------------------------------------- Network
  hdr "Network & discovery"
  if systemctl is-active --quiet avahi-daemon 2>/dev/null; then
    ok "avahi-daemon active (mDNS discovery)"
  else
    warn "avahi-daemon not active - the headset will not find the PC automatically"
  fi
  if have ufw; then
    local us; us="$(LANG=C ufw status 2>/dev/null | head -1)"
    if [[ -z "$us" ]]; then
      info "ufw status only readable as root (sudo ufw status)"
    elif echo "$us" | grep -q inactive; then
      ok "ufw inactive - no port rules needed"
    else
      warn "ufw active - ports 5353/udp, 9757/tcp+udp must be open (setup-host.sh)"
    fi
  fi
  if have ip; then
    ip -4 -brief addr 2>/dev/null | grep -v '^lo' | sed 's/^/    /'
    local wl; wl="$(ip -brief link 2>/dev/null | awk '$1 ~ /^wl/ {print $1}' | head -1)"
    [[ -n "$wl" ]] && info "Note: Wi-Fi interface $wl found - ideally connect the PC to the router via LAN"
  fi
  if have ss && ss -lntu 2>/dev/null | grep -q ':9757'; then ok "Port 9757 is listening"; fi

  # ---------------------------------------------------------------- ADB
  hdr "ADB / Quest"
  if have adb; then
    ok "adb: $(adb version 2>/dev/null | head -1)"
    local devs; devs="$(adb devices 2>/dev/null | tail -n +2 | grep -v '^$')"
    if [[ -n "$devs" ]]; then
      echo "$devs" | sed 's/^/    /'
      if echo "$devs" | grep -q unauthorized; then warn "Device not authorized - allow USB debugging in the headset"; fi
      local pk
      pk="$(adb shell pm list packages 2>/dev/null | grep -i wivrn | cut -d: -f2 | tr -d '\r' | tr '\n' ' ')"
      [[ -n "$pk" ]] && ok "WiVRn client on the headset: $pk"
    else
      info "No devices via adb (not needed for Wi-Fi streaming)"
    fi
  else
    info "adb not installed (only needed for USB fallback/logcat/sideloading)"
  fi

  # ---------------------------------------------------------------- Unity
  hdr "Unity"
  local ed found_ed=0
  for ed in "$HOME"/Unity/Hub/Editor/*/Editor/Unity /opt/unity/*/Editor/Unity; do
    [[ -x "$ed" ]] || continue
    found_ed=1
    local v; v="$(basename "$(dirname "$(dirname "$ed")")")"
    info "Editor $v: $ed"
    [[ -d "$(dirname "$ed")/Data/PlaybackEngines/AndroidPlayer" ]] && info "  └ Android module installed"
  done
  (( found_ed )) && ok "Unity Editor found" || warn "No Unity Editor found under ~/Unity/Hub/Editor"
  if pgrep -f '/Editor/Unity' >/dev/null 2>&1; then
    local args; args="$(pgrep -af '/Editor/Unity' | head -1)"
    info "Running Editor: ${args:0:160}"
    echo "$args" | grep -q -- '-force-vulkan' && ok "Editor is running with -force-vulkan"
  fi

  # ---------------------------------------------------------------- Build tools
  hdr "Build tools (phase 1+)"
  local t
  for t in gcc g++ cmake ninja pkg-config git podman; do
    if have "$t"; then info "$t: $("$t" --version 2>/dev/null | head -1)"; else info "$t: –"; fi
  done
  [[ -n "$(pkg_ver libvulkan-dev)" ]] && info "libvulkan-dev $(pkg_ver libvulkan-dev)" || info "libvulkan-dev: –"

  hdr "Summary"
  echo "  ${G}$OKS ok${N}, ${Y}$WARNS notes${N}, ${R}$FAILS errors${N}"
  (( FAILS > 0 )) && return 1 || return 0
}

if [[ -n "$REPORT" ]]; then
  mkdir -p "$(dirname "$REPORT")"
  main 2>&1 | tee >(sed 's/\x1b\[[0-9;]*m//g' > "$REPORT")
  rc=${PIPESTATUS[0]}
  echo; echo "Report saved: $REPORT"
  exit "$rc"
else
  main
fi
