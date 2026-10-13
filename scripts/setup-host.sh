#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tyroonis
# setup-host.sh - Set up WiVRn + OpenXR base + build tools on Ubuntu 26.04.
#
# WiVRn comes as a Flatpak by default (ships xrizer + OpenComposite, updates itself).
# Everything else is Ubuntu packages. Everything can be removed again (--uninstall).
# Shows what will happen before every change and asks for confirmation (except with --yes).
#
#   ./setup-host.sh                  # base: WiVRn (Flatpak), OpenXR loader, hello_xr, diagnostics, firewall
#   ./setup-host.sh --dev            # + build tools for the native plugin
#   ./setup-host.sh --debug-layers   # + OpenXR/Vulkan validation layers
#   ./setup-host.sh --monado         # + Monado (headset-less testing)
#   ./setup-host.sh --adb            # + adb (USB fallback, logcat, sideloading)
#   ./setup-host.sh --all            # everything above
#   ./setup-host.sh --wivrn=ppa      # WiVRn from ppa:lvra/wivrn instead of Flatpak
#   ./setup-host.sh --wivrn=none     # leave WiVRn alone
#   ./setup-host.sh --dry-run ...    # only show what would be done
#   ./setup-host.sh --uninstall      # remove packages/PPA/Flatpak/firewall rules
set -euo pipefail

FLATPAK_ID="io.github.wivrn.wivrn"
PPA="ppa:lvra/wivrn"
BASE_PKGS=(libopenxr-loader1 libopenxr-utils vulkan-tools vainfo mesa-vulkan-drivers avahi-daemon)
PPA_PKGS=(wivrn-server wivrn-dashboard)
DEV_PKGS=(build-essential cmake ninja-build pkg-config git libvulkan-dev libopenxr-dev)
DEBUG_PKGS=(openxr-layer-apidump openxr-layer-corevalidation vulkan-validationlayers)
MONADO_PKGS=(monado-service libopenxr1-monado monado-cli)
ADB_PKGS=(adb)
PORTS=("5353/udp" "9757/tcp" "9757/udp")

DRY=0; YES=0; DEV=0; DBG=0; MON=0; ADB=0; UNINSTALL=0; WIVRN="flatpak"
for a in "$@"; do
  case "$a" in
    --dry-run) DRY=1 ;;
    --yes|-y) YES=1 ;;
    --dev) DEV=1 ;;
    --debug-layers) DBG=1 ;;
    --monado) MON=1 ;;
    --adb) ADB=1 ;;
    --all) DEV=1; DBG=1; MON=1; ADB=1 ;;
    --wivrn=flatpak|--wivrn=ppa|--wivrn=none) WIVRN="${a#--wivrn=}" ;;
    --uninstall) UNINSTALL=1 ;;
    -h|--help) sed -n '4,19p' "$0"; exit 0 ;;
    *) echo "Unknown option: $a" >&2; exit 2 ;;
  esac
done

B=$'\e[1m'; Y=$'\e[33m'; N=$'\e[0m'; [[ -t 1 ]] || { B=""; Y=""; N=""; }
say()  { echo "${B}» $*${N}"; }
run()  { echo "  \$ $*"; (( DRY )) || "$@"; }
confirm() {
  (( YES || DRY )) && return 0
  read -r -p "$1 [y/N] " ans
  [[ "$ans" =~ ^[jJyY]$ ]]
}
[[ $EUID -eq 0 ]] && { echo "Please run as a normal user (sudo is used when needed)." >&2; exit 1; }
SUDO="sudo"

. /etc/os-release
if [[ "${VERSION_CODENAME:-}" != "resolute" ]]; then
  echo "${Y}Warning:${N} Tested on Ubuntu 26.04 (resolute), found: ${PRETTY_NAME:-?}."
  confirm "Continue anyway?" || exit 1
fi

ufw_active() { LANG=C $SUDO ufw status 2>/dev/null | head -1 | grep -q 'Status: active'; }
installed() { dpkg-query -W -f='${Status}\n' "$1" 2>/dev/null | head -1 | grep -q "ok installed"; }
flatpak_has() { command -v flatpak >/dev/null && flatpak info "$FLATPAK_ID" >/dev/null 2>&1; }

# ------------------------------------------------------------------ Uninstall
if (( UNINSTALL )); then
  REMOVE=()
  for p in "${PPA_PKGS[@]}" libopenxr-utils "${DEBUG_PKGS[@]}" "${MONADO_PKGS[@]}"; do installed "$p" && REMOVE+=("$p"); done
  say "Removing"
  echo "  apt remove:     ${REMOVE[*]:-(nothing)}"
  flatpak_has && echo "  flatpak remove: $FLATPAK_ID (settings in ~/.var/app/$FLATPAK_ID are kept)"
  echo "  Kept: mesa, avahi, vulkan-tools, vainfo, build tools, adb, libopenxr-loader1"
  confirm "Continue?" || exit 1
  (( ${#REMOVE[@]} )) && run $SUDO apt-get remove -y "${REMOVE[@]}"
  flatpak_has && run flatpak uninstall -y "$FLATPAK_ID"
  grep -rqs "lvra/wivrn" /etc/apt/sources.list.d/ && run $SUDO add-apt-repository -y --remove "$PPA"
  if command -v ufw >/dev/null && ufw_active; then
    for p in "${PORTS[@]}"; do run $SUDO ufw delete allow "$p" || true; done
  fi
  say "Done. ~/.config/openxr and ~/.cache/linuxxr were NOT deleted."
  exit 0
fi

# ------------------------------------------------------------------ Install
PKGS=("${BASE_PKGS[@]}")
(( DEV )) && PKGS+=("${DEV_PKGS[@]}")
(( DBG )) && PKGS+=("${DEBUG_PKGS[@]}")
(( MON )) && PKGS+=("${MONADO_PKGS[@]}")
(( ADB )) && PKGS+=("${ADB_PKGS[@]}")
[[ "$WIVRN" == "ppa" ]] && PKGS+=("${PPA_PKGS[@]}")
[[ "$WIVRN" == "flatpak" ]] && ! command -v flatpak >/dev/null && PKGS+=(flatpak)

echo "${B}Planned:${N}"
case "$WIVRN" in
  flatpak) if flatpak_has; then echo "  • WiVRn: Flatpak already installed - update only"; else echo "  • WiVRn: flatpak install flathub $FLATPAK_ID"; fi ;;
  ppa)     echo "  • WiVRn: PPA $PPA + apt (wivrn-server, wivrn-dashboard)" ;;
  none)    echo "  • WiVRn: unchanged" ;;
esac
echo "  • apt install: ${PKGS[*]}"
echo "  • enable avahi-daemon (mDNS, so the Quest can find the PC)"
echo "  • If ufw is active: open ports ${PORTS[*]}"
echo
confirm "Run?" || exit 1

if [[ "$WIVRN" == "ppa" ]] && ! grep -rqs "lvra/wivrn" /etc/apt/sources.list.d/; then
  say "Adding PPA"
  run $SUDO add-apt-repository -y "$PPA"
fi

say "Installing packages"
run $SUDO apt-get update
run $SUDO apt-get install -y --no-install-recommends "${PKGS[@]}"

if [[ "$WIVRN" == "flatpak" ]]; then
  say "WiVRn (Flatpak)"
  run flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
  if flatpak_has; then
    run flatpak update -y "$FLATPAK_ID"
  else
    run flatpak install --user -y flathub "$FLATPAK_ID"
  fi
fi

say "avahi-daemon"
run $SUDO systemctl enable --now avahi-daemon

if command -v ufw >/dev/null && ufw_active; then
  say "Firewall (ufw): opening ports"
  for p in "${PORTS[@]}"; do run $SUDO ufw allow "$p" comment 'WiVRn'; done
else
  say "ufw not active - no firewall change"
fi

if [[ "$WIVRN" == "flatpak" ]]; then
  START="flatpak run $FLATPAK_ID          (dashboard)"
  VER="$(flatpak info "$FLATPAK_ID" 2>/dev/null | awk -F': *' '/Version/{print $2; exit}')"
else
  START="wivrn-dashboard   or   wivrn-server"
  VER="$(dpkg-query -W -f='${Version}\n' wivrn-server 2>/dev/null | head -1)"
fi
cat <<EOF

${B}Done. Next steps:${N}
  1. Quest 3: install the "WiVRn" app - the version must match the server exactly (server: ${VER:-?}).
     The dashboard offers "Install on headset" (via adb); the Meta Store may lag behind.
  2. Start the server:   $START
  3. In the headset open WiVRn -> pick the PC -> enter the PIN from the PC (pairing).
  4. Test without Unity:  ./scripts/test-openxr.sh wivrn
                          ./scripts/build-native.sh && native/build/linuxxr-probe run
EOF
