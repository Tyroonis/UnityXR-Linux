# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
follows [Semantic Versioning](https://semver.org/) (pre-1.0: minor versions may contain breaking changes).

## [Unreleased]

## [0.3.3] - 2026-10-13

### Changed
- License changed from MIT to GPL-3.0-or-later, with an additional permission (GPLv3 section 7) for use
  with the Unity Editor and the Unity XR SDK headers. SPDX headers in all source files.
- Release workflow: runs the CI tests first, takes the release notes from this changelog, publishes the
  `.tgz` and `.unitypackage` and can sign the tarball with Unity's `upm` CLI when the `UPM_*` secrets are set.

## [0.3.2] - 2026-10-13

### Fixed
- Editor still froze in Play Mode with WiVRn running and no headset: while the 0.3.1 probe waits inside the
  runtime, the runtime's client library is locked, so the input subsystem's start blocked in
  `xrEnumerateInstanceExtensionProperties`. LinuxXR now makes no runtime calls at all while a probe is pending.

## [0.3.1] - 2026-10-13

### Fixed
- XR subsystems were not registered with tarball/git installs: Unity only reads `UnitySubsystemsManifest.json`
  from a direct sub-folder of `Assets/` (or registry packages). The package now installs manifest and native
  plug-in into `Assets/LinuxXR/` on first load (binaries ship in `Runtime/Native~`); one Editor restart needed.
- Editor froze in Play Mode when WiVRn was running without a connected headset (the runtime blocks
  `xrCreateInstance`/`xrGetSystem`). A probe instance on a worker thread now times out after 3 s and Play
  continues without VR.
- No runtime / no headset is now a warning instead of red errors; "lifecycle registration failed" became a
  restart hint.
- Setup wizard: XR loader enabling no longer needs `XRPackageMetadataStore` at compile time.

## [0.3.0] - 2026-10-13

### Added

- **Script-free distribution.** LinuxXR installs through Unity's Package Manager; no scripts and no separate folder are needed any more:
  - Git URL `https://github.com/tyroonis/UnityVR-Linux.git#upm` (or `#upm/v0.3.0` to pin). The `upm` branch is published by the release CI and contains the prebuilt binaries.
  - Tarball `com.tyroonis.xr.linuxopenxr-0.3.0.tgz` from GitHub Releases ("Add package from tarball...").
  - `LinuxXR-0.3.0.unitypackage` (GitHub Releases / Asset Store), which installs into `Assets/Tyroonis/LinuxXR`.
- The **native plug-in lives inside the package**: `Runtime/UnitySubsystemsManifest.json` and `Runtime/Native~/{libLinuxXR.so, libopenxr_loader.so.1}`
  (bundled Khronos loader 1.1.47). All files have `.meta` files (`scripts/ensure-metas.py` creates missing ones).
- **Setup wizard** (*Window -> XR -> LinuxXR Setup*), opened automatically once per session while something required is missing, with one-click fixes: native plug-in and subsystems found,
  Vulkan as Graphics API (+ restart), enable the LinuxXR loader for Standalone, remove other Standalone loaders, Initialize XR on Startup, Flatpak sandbox access to WiVRn,
  install WiVRn from Flathub, start WiVRn, recommended Linear color space and Render Scale 0.7. The checklist can be disabled per project. It warns about local-disk (`file:`) and embedded installs.
- **Bootstrap** that offers to install `com.unity.xr.management` when it is missing (*Window -> XR -> LinuxXR Install XR Plug-in Management*).
- **Flatpak host helper**: for Unity Hub running as `com.unity.UnityHub`, "Allow access to WiVRn" sets `flatpak override --user --filesystem=xdg-run/wivrn` plus read-only WiVRn app directories,
  and "Close Unity Hub + Editor now" restarts the sandbox.
- **CI and release workflows**: `.github/workflows/ci.yml` (build + ctest on ubuntu-24.04) and `release.yml` (on tag `vX.Y.Z`: portable build, GitHub Release with `.tgz` and `.unitypackage`,
  `upm` branch and `upm/vX.Y.Z` tag). `docs/github/` keeps a copy of the templates and workflows. GitLab issue template added.
- **Portable static build**: the release `libLinuxXR.so` is built in an `ubuntu:22.04` container with static libstdc++, so it runs on glibc 2.35 and newer, including the Unity Hub Flatpak.
  `scripts/build-openxr-loader.sh` rebuilds the bundled loader.
- `scripts/pack-unity-package.sh` produces `dist/*.tgz`, `dist/*.unitypackage` and `dist/upm`.
- Issue report: **Create issue report** in the status window saves `Logs/LinuxXR-report-<timestamp>.md` (home directory anonymized as `~`) and copies it to the clipboard.
- Native `[LinuxXR]` log messages are forwarded to the Unity Console (setting **Forward Log To Console**) and live statistics are shown in the status window.

### Changed

- **Breaking for 0.2.0 installs:** the separate `unity/LinuxXR-Native` folder and the install-by-script flow are gone. Remove the old `Packages/*.tgz` / `Assets/LinuxXR` install and the
  `file:` manifest entry, then install 0.3.0 with one of the three ways above and restart the Editor.
- `scripts/install-unity-package.sh` is now a developer tool: `--dev` symlinks `<project>/Assets/LinuxXR` to the repository's package folder, `--tarball` tests the release tarball, `--remove` uninstalls.
  `scripts/build-native.sh` copies `libLinuxXR.so` straight into the package folder.
- The XR Plug-in Management metadata is packageId-aware and lists the loader only in the Linux Editor.
- `setup-host.sh`, `check-system.sh`, `setup-unity-flatpak.sh` and `quest-usb.sh` are optional helpers; nothing requires them.
- Minimum glibc is 2.35 (was 2.38 for the bundled loader).
- The whole codebase (comments, log messages, UI strings) is in English.
- Documentation rewritten: end-user installation first, "Building from source" for developers last; new release procedure in `CONTRIBUTING.md`.

### Fixed

- Race conditions in the native settings and status code (concurrent access from the Unity main thread, the graphics thread and the C API).

## [0.2.0] - 2026-10-13

### Added

- Native `[LinuxXR]` messages are forwarded to the Unity Console (setting **Forward Log To Console**, on by default).
- Live statistics in the status window: headset fps vs. display Hz, Unity fps, `xrWaitFrame` time,
  copy + submit time, frames begun / shown / empty, missed display frames, frames dropped by Unity,
  eye resolution / stereo mode / render scale, Vulkan extension check, session time, warnings and errors.
- **Create issue report** in the status window: a Markdown report (system, Unity, packages, settings, status,
  statistics, diagnostics, last 250 native log lines; home directory replaced by `~`) is copied to the clipboard
  and saved as `Logs/LinuxXR-report-<timestamp>.md`.
- Refreshed status window (**Window -> XR -> LinuxXR Status**) with a setup checklist and actions
  (Start WiVRn, Use Vulkan, XR Settings, Open log, Copy diagnostics).
- **Package (tarball) install mode** (new in 0.2.0, not yet confirmed in a real Editor - please report):
  `scripts/pack-unity-package.sh` builds `dist/com.tyroonis.xr.linuxopenxr-<version>.tgz`;
  `scripts/install-unity-package.sh <project>` copies it to `<project>/Packages/` and references it as `file:<name>.tgz`,
  so Unity extracts it into `Library/PackageCache` where it finds `UnitySubsystemsManifest.json`.
  Also `--tarball FILE` and `--remove`.
- `scripts/setup-unity-flatpak.sh` (`--check`, `--undo`): user-level Flatpak overrides so that an Editor running inside the
  Unity Hub Flatpak can reach the WiVRn socket and the WiVRn Flatpak runtime.
- Bundled OpenXR loader (Khronos OpenXR-SDK 1.1.47, Apache-2.0) next to the plugin, used when the system has none
  (the Unity Hub Flatpak runtime has no OpenXR loader). Environment override: `LINUXXR_OPENXR_LOADER`.
- `LINUXXR_UNITY_PROJECT=<project> ./scripts/build-native.sh` installs the package into the project after each build.
- Environment variables `LINUXXR_FLIP_Y` and `LINUXXR_DIRECT_SUBMIT` for image-orientation and frozen-image workarounds.
- English and German documentation, `THIRD_PARTY_NOTICES.md`, MIT license, issue templates.

### Changed

- All user-visible messages are in English.
- The developer install mode (`--dev`) is now explicit; the default mode is the package mode.
- Old German planning documents moved to `docs/dev/`.

### Fixed

- Editor freeze: the runtime reachability probe (WiVRn / Monado socket) is now a non-blocking socket probe
  (WiVRn does not accept connections without a connected headset; the former blocking `connect()` could hang and froze the Editor).
- Runtime loss (WiVRn quits, headset disconnects) is handled: the plugin never enters `xrWaitFrame` again after the runtime is lost and logs the loss
  instead of hanging (covered by the `linuxxr_monado_instance_loss` test).

## [0.1.0] - 2026-10-12

### Added

- First image on a Meta Quest 3 via WiVRn from the Unity Linux Editor (Vulkan, URP).
- Native Unity XR provider `libLinuxXR.so` with display and input subsystems.
- Input: "LinuxXR HMD" and "LinuxXR Controller - Left/Right" with Unity `CommonUsages`.
- `linuxxr-probe` command-line tool to test the OpenXR chain without Unity.
- Tests: core unit tests, fake Unity host, Monado integration tests (`ctest`).
- Helper scripts: `check-system.sh`, `setup-host.sh`, `build-native.sh`, `fetch-unity-headers.sh`,
  `test-openxr.sh`, `quest-usb.sh`.

[0.3.0]: #030---2026-10-13
[0.2.0]: #020---2026-10-13
[0.1.0]: #010---2026-10-12
