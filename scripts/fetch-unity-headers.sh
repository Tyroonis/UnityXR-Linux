#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tyroonis
# fetch-unity-headers.sh - Fetches the Unity XR SDK headers (Unity Companion License) from
# ValveSoftware/unity-xr-plugin into native/third_party/unity-xr-headers/.
#
# The headers are NOT checked into the repo (license: only for Unity-dependent projects,
# redistribution of derivatives is tied to Unity) - hence this script instead of vendoring.
#
#   ./fetch-unity-headers.sh            # pinned commit
#   ./fetch-unity-headers.sh --force    # fetch again even if present
set -euo pipefail

REPO="https://github.com/ValveSoftware/unity-xr-plugin.git"
COMMIT="a30a0100daacef8c3f0290ce5b179c8a1148abb0"   # 2025-10-29, com.valve.openvr 1.2.4
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="$SCRIPT_DIR/../native/third_party/unity-xr-headers"

if [[ -f "$DEST/IUnityXRDisplay.h" && "${1:-}" != "--force" ]]; then
  echo "✔ Headers already present: $DEST  (--force to re-fetch)"
  exit 0
fi
command -v git >/dev/null || { echo "git missing" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf -- "$TMP"' EXIT
echo "» Fetching $REPO @ ${COMMIT:0:12}"
git -C "$TMP" init -q
git -C "$TMP" remote add origin "$REPO"
git -C "$TMP" fetch -q --depth 1 origin "$COMMIT"
git -C "$TMP" -c advice.detachedHead=false checkout -q FETCH_HEAD -- CommonHeaders/ProviderInterface

mkdir -p "$DEST"
cp "$TMP"/CommonHeaders/ProviderInterface/*.h "$DEST"/
cp "$TMP"/CommonHeaders/ProviderInterface/LICENSE.md "$DEST"/LICENSE-Unity-Companion.md
echo "$COMMIT" > "$DEST/SOURCE_COMMIT"
echo "✔ $(ls "$DEST"/*.h | wc -l) headers copied to $DEST"
