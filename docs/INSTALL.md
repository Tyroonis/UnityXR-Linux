# Installation guide

**English** | [Deutsch](INSTALL.de.md)

This guide takes you from a fresh Linux machine to pressing **Play** in the Unity Linux Editor and seeing the scene on a Meta Quest.
Part 1 to 8 is for **users**: LinuxXR is installed with Unity's Package Manager, no scripts and no build tools needed.
[Building from source](#building-from-source-developers) at the end is for **developers** of LinuxXR.

Contents: [1 Overview](#1-prerequisites-and-overview) - [2 WiVRn and the Quest](#2-wivrn-and-the-quest-host-side) -
[3 Install the package](#3-install-the-package) - [4 First start and setup wizard](#4-first-start-and-setup-wizard) -
[5 Unity settings](#5-unity-settings-what-the-wizard-does) - [6 Scene setup](#6-scene-setup) - [7 Press Play](#7-press-play) -
[8 Performance](#8-performance-tips) - [9 Updating](#9-updating) - [10 Uninstalling](#10-uninstalling) -
[11 Optional helper scripts](#11-optional-helper-scripts) - [Building from source](#building-from-source-developers)

## 1. Prerequisites and overview

You need:

- A Linux PC with a Vulkan-capable GPU and glibc 2.35 or newer. Tested: Ubuntu 26.04 GNOME (Wayland, Editor via XWayland), AMD Radeon RX 7600 (RADV, Mesa 26).
  WiVRn encodes video with VAAPI / Vulkan Video (AMD, Intel) or NVENC (NVIDIA).
- Unity 6 (tested: 6000.6.5f1 with URP), installed through Unity Hub (native or Flatpak). Unity officially supports Ubuntu 22.04 / 24.04 only.
- A Meta Quest (tested: Quest 3).
- Network: the PC on a **LAN cable**, the Quest on **5 GHz / Wi-Fi 6** - or a USB cable. Firewall port 9757 TCP+UDP open.

What happens, in order:

| Step | What | Done once? |
|------|------|-----------|
| 2 | Install WiVRn on the PC and the WiVRn app on the Quest, pair them, open the firewall port | yes |
| 3 | Add the LinuxXR package to your Unity project (Package Manager) | per project |
| 4 | Restart the Editor, follow the setup wizard (Vulkan, XR Plug-in Management, Flatpak access, ...) | per project |
| 5-6 | Check Unity settings, set up the scene (XR Origin) | per project / scene |
| 7 | Start WiVRn, connect the headset, press Play | every time |

The native plug-in and the OpenXR loader are part of the package. You do **not** need a compiler, `libopenxr`, or any script.

## 2. WiVRn and the Quest (host side)

### 2.1 Install WiVRn on the PC

Flatpak (recommended; the setup wizard or `setup-host.sh` can do this for you with `flatpak install --user`):

```bash
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub io.github.wivrn.wivrn
flatpak run io.github.wivrn.wivrn          # starts the dashboard
```

A system-wide install (`flatpak install flathub io.github.wivrn.wivrn`) works too, but see [section 4.3](#43-unity-hub-as-flatpak)
if your Unity Hub is a Flatpak.

Alternative on Ubuntu 26.04: the PPA.

```bash
./scripts/setup-host.sh --wivrn=ppa        # adds ppa:lvra/wivrn and installs wivrn-server wivrn-dashboard
wivrn-dashboard                            # or: wivrn-server
```

### 2.2 Install the WiVRn app on the Quest

Install the app **"WiVRn"** from the Meta Horizon Store, or sideload it. The WiVRn **dashboard** also offers "Install on headset" (through `adb`;
install `adb` with `./scripts/setup-host.sh --adb`). **Client and server versions must match exactly**; the Store version may lag behind the
server. A mismatch shows up as "incompatible protocol version" (see [TROUBLESHOOTING](TROUBLESHOOTING.md)).

### 2.3 Pair

1. Start the WiVRn server / dashboard on the PC.
2. On the Quest open the **WiVRn** app and pick your PC.
3. Enter the PIN shown on the PC (first time only).

Network: 5 GHz or Wi-Fi 6 for the Quest, LAN cable for the PC. Ports: 9757 TCP+UDP, 5353/UDP for mDNS discovery (avahi).

**USB instead of Wi-Fi** (Quest 3): switch on **"USB networking"** in the WiVRn app, or use the adb tunnel:

```bash
./scripts/setup-host.sh --adb              # once, installs adb
./scripts/quest-usb.sh                     # adb reverse tcp:9757, starts the WiVRn app against 127.0.0.1
./scripts/quest-usb.sh logs                # logcat of the WiVRn client
./scripts/quest-usb.sh stop                # remove the tunnel
```

The adb way needs developer mode and USB debugging allowed in the headset. Turn off Wi-Fi on the headset so nothing goes over the air.

### 2.4 Firewall

Open port **9757 TCP and UDP** (WiVRn) and **5353/UDP** (mDNS discovery; needs `avahi-daemon`). With `ufw`:

```bash
sudo ufw allow 9757/tcp && sudo ufw allow 9757/udp && sudo ufw allow 5353/udp
```

`./scripts/setup-host.sh` does this (and more) for you, but it is optional.

## 3. Install the package

Pick **one** of the three ways. All of them use Unity's Package Manager or Import Package dialog; nothing is run in a terminal.
The package is `com.tyroonis.xr.linuxopenxr` 0.3.3 and depends on `com.unity.xr.management` 4.5.0 (XR Plug-in Management).

### 3.1 Git URL (recommended)

*Window -> Package Manager -> + -> Add package from git URL...* and enter:

```
https://github.com/tyroonis/UnityVR-Linux.git#upm
```

To pin a release, use the tag instead: `https://github.com/tyroonis/UnityVR-Linux.git#upm/v0.3.3`.
The `upm` branch is published by the release CI (see [ARCHITECTURE](ARCHITECTURE.md#8-distribution-and-ci)) and contains the package **with the prebuilt binaries**.
Replace `tyroonis` with the GitHub account that hosts the repository. Unity needs `git` installed for this option. Unity usually shows no signature warning for git URL installs.

### 3.2 Tarball

Download `com.tyroonis.xr.linuxopenxr-0.3.3.tgz` from the repository's GitHub Releases page, then
*Package Manager -> + -> Add package from tarball...* and pick the file. Keep the `.tgz` somewhere stable (Unity references it by path);
you can also commit it into your project, for example in a `Packages/` subfolder next to `manifest.json`.

Unity 6.3+ may show **"package is not signed"** for the tarball. This is only a warning and the package works. Release tarballs are signed when the maintainer has configured
signing (see [CONTRIBUTING](../CONTRIBUTING.md#signing-the-tarball)); otherwise the warning stays.

### 3.3 .unitypackage

Download `LinuxXR-0.3.3.unitypackage` (GitHub Releases or the Asset Store) and import it with *Assets -> Import Package -> Custom Package...*.
The C# code installs into `Assets/Tyroonis/LinuxXR`; the native files already come in `Assets/LinuxXR` (see 3.4).

### 3.4 The native part in Assets/LinuxXR

Unity registers native XR subsystems only from a `UnitySubsystemsManifest.json` in a direct sub-folder of `Assets/` (or from registry packages), and only at Editor start.
Tarball, git and embedded packages are not scanned. So that every install method works the same, LinuxXR installs its native part on first load (Linux Editor only):

```
Assets/LinuxXR/UnitySubsystemsManifest.json
Assets/LinuxXR/Plugins/Linux/x86_64/libLinuxXR.so
Assets/LinuxXR/Plugins/Linux/x86_64/libopenxr_loader.so.1
```

(with `.meta` files that have fixed GUIDs). The package itself keeps the binaries in `Runtime/Native~/`; Unity's importer ignores folders ending in `~`.
This works for the git URL, the tarball, the `.unitypackage` and *Add package from disk...*. Commit `Assets/LinuxXR` to version control like any other asset, or leave it out:
it is recreated automatically.

### 3.5 Restart the Editor

**Restart the Unity Editor once** after the first install and after every update that changes the native binaries: XR subsystems are registered at Editor start-up only.
The setup wizard shows **Restart Editor** when this is needed.

As a Package Manager install, `com.unity.xr.management` 4.5.0 is pulled in as a dependency. If it is missing (for example after importing the `.unitypackage`), a small bootstrap offers to install it for you
(menu *Window -> XR -> LinuxXR Install XR Plug-in Management*).

## 4. First start and setup wizard

### 4.1 What the wizard checks

*Window -> XR -> LinuxXR Setup* opens **automatically once per session** while something required is missing, and you can open it from the menu at any time.
Each step shows a status and, where possible, a **one-click fix**:

| Step | One-click fix |
|------|---------------|
| Native plug-in installed in `Assets/LinuxXR` and XR subsystems found | **Install**, then **Restart Editor** (see 3.4) |
| Vulkan as Graphics API for Linux | **Use Vulkan + restart** |
| LinuxXR loader enabled for Standalone ("Windows, Mac, Linux") | **Enable LinuxXR** |
| No other Standalone loaders enabled (e.g. OpenXR) | **Disable them on this tab** |
| Initialize XR on Startup | **Turn on** |
| Flatpak sandbox access to WiVRn (only if Unity Hub is the Flatpak `com.unity.UnityHub`) | **Allow access to WiVRn**, then **Close Unity Hub + Editor now** |
| WiVRn installed | **Install WiVRn (Flatpak)** (`flatpak install --user` from Flathub) or the Flathub page |
| WiVRn running | **Start WiVRn**, or open the Quest app link |
| Recommended: Linear color space | **Use Linear** |
| Recommended: Render Scale 0.7 | **Set 0.7** |

The checklist can be switched off per project (it is then no longer opened automatically; the menu entry still works).

### 4.2 Typical first run

1. Restart the Editor after installing the package.
2. Work through the wizard from top to bottom. Vulkan and Flatpak steps need an Editor restart; the wizard says so.
3. Start WiVRn and connect the headset (see section 2.3), then continue with [section 6](#6-scene-setup) and [Press Play](#7-press-play).

### 4.3 Unity Hub as Flatpak

If you run Unity Hub as a Flatpak (`flatpak list | grep UnityHub` prints `com.unity.UnityHub`), the Editor runs **inside the Hub's sandbox**
and cannot see WiVRn's socket (`$XDG_RUNTIME_DIR/wivrn/comp_ipc`) or the WiVRn Flatpak. The wizard handles it:

1. Click **Allow access to WiVRn**. This runs `flatpak override --user --filesystem=xdg-run/wivrn` plus read-only access to the WiVRn app directories
   (user and system location). Nothing else on the system changes.
2. Click **Close Unity Hub + Editor now**, then start Unity Hub and the project again. Overrides apply only to newly started sandboxes.

The socket exists only while WiVRn runs and the Quest is connected, so "socket missing" before you connect the headset is normal.
If a system-wide WiVRn cannot be exposed, install it for your user instead: `flatpak install --user flathub io.github.wivrn.wivrn` (optionally remove the system install).
The Flatpak runtime has no OpenXR loader; LinuxXR ships its own. To undo the overrides: `flatpak override --user --reset com.unity.UnityHub`
(or the optional `scripts/setup-unity-flatpak.sh --undo`).

## 5. Unity settings (what the wizard does)

The [setup wizard](#4-first-start-and-setup-wizard) applies these settings for you; this section is the manual reference.

1. **Graphics API: Vulkan.** *Edit -> Project Settings -> Player -> Other Settings (Linux tab) -> Auto Graphics API for Linux* off, and put **Vulkan** first in
   *Graphics APIs for Linux*. Or use the wizard step **Use Vulkan + restart** (changes the setting and restarts the Editor).
2. **XR Plug-in Management.** *Edit -> Project Settings -> XR Plug-in Management*, tab **"Windows, Mac, Linux"** (the desktop tab):
   - enable **LinuxXR** and **disable OpenXR** on this tab (only LinuxXR may be ticked);
   - keep **Initialize XR on Startup** on;
   - the **Android** tab keeps **OpenXR** with the *Meta Quest* feature group for device builds.
3. **LinuxXR settings** (*Project Settings -> XR Plug-in Management -> LinuxXR*):

   | Setting | Values / default | Meaning |
   |---------|------------------|---------|
   | Runtime | Automatic (prefer WiVRn) / WiVRn / Monado / Active runtime / Custom manifest path; default Automatic | Which OpenXR runtime to use. Automatic prefers WiVRn, then `active_runtime.json`, then Monado. |
   | Custom Runtime Manifest | path to a `*.json` | Used only with "Custom manifest path". |
   | Tracking Origin | Floor (default) / Device (head) | Tracking space. |
   | Stereo Mode | Single Pass Instanced (recommended, default) / Multi Pass | How both eyes are rendered. |
   | Mirror Mode | Left eye (default) / Right eye / Both eyes / Off | What the Game View shows in Play Mode. |
   | Render Scale | 0.5 - 1.5, default 1.0 | Render resolution relative to the runtime's recommendation. Lower it (e.g. 0.7) if the headset stutters. |
   | Verbose Logging | off | Debug messages in the Console and in `linuxxr.log`. |
   | Forward Log To Console | on | Show native `[LinuxXR]` messages in the Unity Console. |

   Changes apply the next time you press Play.
4. **Color space:** use **Linear** (wizard recommendation) (*Player -> Other Settings -> Color Space*). Otherwise the image looks too dark or too bright in the headset.
5. **Restart the Editor** (also after switching the Graphics API or installing the package).

Environment variables must be set **before** the Hub / Editor starts (see [TROUBLESHOOTING](TROUBLESHOOTING.md) for when you need them):

| Variable | Values | Purpose |
|----------|--------|---------|
| `LINUXXR_RUNTIME` | `auto`, `wivrn`, `monado`, `active`, or `/path/to/runtime.json` | Runtime preference. The Runtime setting wins unless it is "Automatic". |
| `LINUXXR_GPU` | GPU index or part of its name | Choose the GPU on multi-GPU systems. |
| `LINUXXR_LOG` | `debug` | Verbose native log. |
| `LINUXXR_FLIP_Y` | `1` | Flip the image vertically (if it is upside down). |
| `LINUXXR_DIRECT_SUBMIT` | `1` | Workaround if the image is frozen or black, or the log says "Previous frame did not complete". |
| `LINUXXR_OPENXR_LOADER` | path to `libopenxr_loader.so.1` | Use a specific OpenXR loader. |

## 6. Scene setup

Use Unity's XR input, not Meta-specific rigs (`OVRCameraRig` / `OVRInput` / `OVRManager` get no data in the Linux Editor).

**Option A - XR Interaction Toolkit (recommended).** Install *XR Interaction Toolkit* from the Package Manager. In the scene use
*GameObject -> XR -> XR Origin (VR)* (plus the *XR Interaction Setup*/starter assets if you want ready-made interactors). Set the origin's tracking
mode to match your *Tracking Origin* setting (Floor or Device).

**Option B - plain camera.** Put a **Tracked Pose Driver (Input System)** on the Main Camera and bind it to the HMD.
For the Input System use these bindings (the devices are "LinuxXR HMD" and "LinuxXR Controller - Left/Right"):

| Use | Binding |
|-----|---------|
| Head position / rotation | `<XRHMD>/centerEyePosition`, `<XRHMD>/centerEyeRotation` |
| Left controller pose | `<XRController>{LeftHand}/devicePosition`, `<XRController>{LeftHand}/deviceRotation` |
| Right controller pose | `<XRController>{RightHand}/devicePosition`, `<XRController>{RightHand}/deviceRotation` |
| Left trigger / grip | `<XRController>{LeftHand}/trigger`, `<XRController>{LeftHand}/grip` |
| Aim (pointer) pose | `<XRController>{RightHand}/pointerPosition`, `<XRController>{RightHand}/pointerRotation` |

The controllers expose pose, velocity, pointer pose, Trigger, Grip, TriggerButton, GripButton, PrimaryButton / PrimaryTouch, SecondaryButton / SecondaryTouch, MenuButton
and more with Unity's `CommonUsages` names; use the Input Debugger to see everything the devices expose. The devices also work through
`UnityEngine.XR.InputDevices`, XR Interaction Toolkit and HurricaneVR's XR input.

## 7. Press Play

Order: **start WiVRn, connect the headset, then press Play.** WiVRn serves applications only while a headset is connected.

Without a headset (or with WiVRn not running), Play Mode continues without VR: LinuxXR logs a yellow warning (`No headset connected ... Play continues without VR`) and no errors.
WiVRn blocks OpenXR calls while no headset is connected, so LinuxXR checks first with a probe on a worker thread (3 s timeout); the Editor never freezes.
Connect the Quest in WiVRn and press Play again.

Expected sequence:

1. The Console shows `[LinuxXR] Initialized (version 0.3.3)` when XR starts.
2. Native messages follow, for example `Display subsystem initialized`, `Input subsystem initialized`,
   `Vulkan: Unity's device has all required extensions`, `Display subsystem started - image goes to <system name>`,
   `Input subsystem started: HMD + 2 controllers`, `First image: ...`.
3. The scene appears in the headset; the Game View shows the mirror (per Mirror Mode); head and controllers move.

Open **Window -> XR -> LinuxXR Status** for the session state, live statistics and actions (the setup checklist lives in **Window -> XR -> LinuxXR Setup**). Buttons: **Start WiVRn**, **Use Vulkan**, **XR Settings**, **Open log**, **Copy diagnostics**, **Create issue report**.
The statistics are (headset fps vs. display Hz, Unity fps, `xrWaitFrame` time, copy + submit time, frames begun / shown /
empty, missed display frames, frames dropped by Unity, eye resolution / mode / scale, Vulkan extension check, session time, warnings and errors). **Create issue report** saves a Markdown report to `Logs/LinuxXR-report-<timestamp>.md` and copies it to the clipboard; attach it to bug reports.

Logs: native messages go to the Console and to the native log file `~/.cache/linuxxr/linuxxr.log` (with the Hub Flatpak:
`~/.var/app/com.unity.UnityHub/cache/linuxxr/linuxxr.log`). Unity's own log is `Logs/Editor.log` in the project.

If something is wrong, see [TROUBLESHOOTING](TROUBLESHOOTING.md).

## 8. Performance tips

- Wired network for the PC, 5 GHz / Wi-Fi 6 for the Quest, or USB (a good cable, 5 Gb/s or better). Turn off Wi-Fi on the headset when on USB.
- At full resolution (2476x2594 per eye on Quest 3) about 81 of 90 frames arrived on the tested setup. Set **Render Scale to about 0.7**.
- Prefer **Single Pass Instanced** stereo mode.
- In the WiVRn app / server settings lower the bitrate or resolution if the stream stutters.
- Watch the statistics in the status window: "missed display frames" and "dropped by Unity" tell you whether the bottleneck is the GPU, the encoder or the network.

## 9. Updating

- **Git URL:** with `#upm` Package Manager shows an update when the `upm` branch moves (or click *Update*); with `#upm/vX.Y.Z` change the URL to the new tag.
  Remove and re-add the package if Unity keeps the old commit locked.
- **Tarball:** add the new `.tgz` with *Add package from tarball...*.
- **.unitypackage:** delete `Assets/Tyroonis/LinuxXR` and import the new file.

The package refreshes `Assets/LinuxXR` when it loads. Then **restart the Editor** (the wizard shows **Restart Editor**). Keep WiVRn server and Quest app on the same version (`flatpak update io.github.wivrn.wivrn`).

## 10. Uninstalling

1. Disable LinuxXR in *Project Settings -> XR Plug-in Management* ("Windows, Mac, Linux").
2. Remove the package in *Package Manager* (or delete `Assets/Tyroonis/LinuxXR` for the `.unitypackage`).
3. Delete `Assets/LinuxXR` (the native part the package installed).
4. Optional, if you used them: undo the Flatpak override with `flatpak override --user --reset com.unity.UnityHub` and restart the Hub;
   remove WiVRn with `flatpak uninstall io.github.wivrn.wivrn`.

## 11. Optional helper scripts

Nothing below is required. These scripts live in the repository for convenience and for diagnosing a host:

| Script | Purpose |
|--------|---------|
| `scripts/check-system.sh` (`--report`) | Read-only system check; changes nothing |
| `scripts/setup-host.sh` | Installs WiVRn, OpenXR tools, Mesa/VAAPI tools, avahi and firewall rules; `--uninstall` reverts. Options: see below |
| `scripts/setup-unity-flatpak.sh` (`--check`, `--undo`) | Command-line version of the wizard's Flatpak step |
| `scripts/quest-usb.sh` (`logs`, `stop`) | adb tunnel for USB streaming (see 2.3) |
| `scripts/test-openxr.sh` | `hello_xr` through WiVRn / Monado without Unity |

### `setup-host.sh` options

`scripts/setup-host.sh` shows what it plans to do and asks before changing anything. It uses `sudo` when needed
(run it as a normal user, not as root).

```bash
./scripts/setup-host.sh
```

Options:

| Option | Effect |
|--------|--------|
| *(none)* | Base: WiVRn (Flatpak), OpenXR loader, `hello_xr`/`libopenxr-utils`, Vulkan and VAAPI tools, Mesa Vulkan drivers, avahi, firewall rules |
| `--dev` | Adds build tools: `build-essential cmake ninja-build pkg-config git libvulkan-dev libopenxr-dev` |
| `--debug-layers` | Adds OpenXR / Vulkan validation layers |
| `--monado` | Adds Monado (`monado-service libopenxr1-monado monado-cli`) for headset-less testing and the integration tests |
| `--adb` | Adds `adb` (USB fallback, logcat, sideloading) |
| `--all` | `--dev`, `--debug-layers`, `--monado` and `--adb` |
| `--wivrn=flatpak` | WiVRn from Flathub (default) |
| `--wivrn=ppa` | WiVRn from `ppa:lvra/wivrn` (packages `wivrn-server wivrn-dashboard`) |
| `--wivrn=none` | Do not touch WiVRn |
| `--dry-run` | Only show what would be done |
| `--yes` / `-y` | Do not ask for confirmation |

The script also enables `avahi-daemon` (so the Quest finds the PC through mDNS) and, if `ufw` is active, opens the ports
`5353/udp`, `9757/tcp` and `9757/udp`. It warns if the system is not Ubuntu 26.04.

Manual equivalent (Ubuntu):

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  libopenxr-loader1 libopenxr-utils vulkan-tools vainfo mesa-vulkan-drivers avahi-daemon flatpak \
  build-essential cmake ninja-build pkg-config git libvulkan-dev libopenxr-dev
sudo systemctl enable --now avahi-daemon
# only if ufw is active:
sudo ufw allow 5353/udp && sudo ufw allow 9757/tcp && sudo ufw allow 9757/udp
```

Without Unity, you can test the whole native chain with `linuxxr-probe` (built from source, see below) or with `./scripts/test-openxr.sh wivrn`.

## Building from source (developers)

Only needed if you work on LinuxXR itself or want to build the binaries yourself. Run all commands from the repository root.
For the contribution and release workflow see [CONTRIBUTING.md](../CONTRIBUTING.md).

### Build tools

```bash
./scripts/setup-host.sh --dev              # or by hand:
sudo apt-get install -y build-essential cmake ninja-build pkg-config git libvulkan-dev libopenxr-dev python3
```

### Build the native plug-in

```bash
./scripts/fetch-unity-headers.sh     # Unity XR SDK headers (Unity Companion License; not committed). build-native.sh also calls it
./scripts/build-native.sh            # Release (RelWithDebInfo) into native/build
./scripts/build-native.sh test       # build + ctest
./scripts/build-native.sh debug      # Debug build
./scripts/build-native.sh clean      # delete native/build
```

The CMake build copies `libLinuxXR.so` into the package folder:
`unity/Packages/com.tyroonis.xr.linuxopenxr/Runtime/Native~/`. It also builds `native/build/linuxxr-probe`.
`./scripts/build-openxr-loader.sh [--version 1.1.47]` rebuilds the bundled `libopenxr_loader.so.1` from the Khronos OpenXR-SDK.

The tests (`ctest`) consist of core unit tests, a fake Unity host that loads `libLinuxXR.so` through `dlopen`, and Monado integration tests
(headless Monado + lavapipe, including runtime loss without a hang). The Monado tests are skipped when Monado is not installed
(`./scripts/setup-host.sh --monado`).

### Use the working copy in a test project (live development)

```bash
./scripts/install-unity-package.sh /path/to/test-project --dev
```

This symlinks `<project>/Assets/LinuxXRDev` to the repository's package folder, so C# edits and every `./scripts/build-native.sh` apply directly.
The package then installs `Assets/LinuxXR` itself, like for end users. Restart the Editor after switching. Other modes:

```bash
./scripts/install-unity-package.sh /path/to/test-project --tarball   # packs and installs the release tarball, to test what users get
./scripts/install-unity-package.sh /path/to/test-project --remove    # removes LinuxXR from the project again
```

### Pack the release artifacts

```bash
./scripts/pack-unity-package.sh      # writes dist/*.tgz, dist/*.unitypackage and dist/upm
./scripts/ensure-metas.py            # creates missing .meta files (every file in the package needs one)
```

### Test the chain without Unity

Keep the headset connected to WiVRn. First check that OpenXR sees WiVRn:

```bash
./scripts/test-openxr.sh list              # shows runtime / manifest info
./scripts/test-openxr.sh wivrn             # hello_xr (Vulkan) through WiVRn on the Quest
```

Then, after building the plug-in, test the LinuxXR chain itself with the probe tool:

```bash
native/build/linuxxr-probe runtimes        # runtimes found and whether they are reachable
native/build/linuxxr-probe info            # OpenXR instance, system, extensions, Vulkan requirements
native/build/linuxxr-probe preinit         # Vulkan extensions and GPU choice
native/build/linuxxr-probe run             # session: left eye red, right eye blue
```

During `run`, the trigger brightens the eye color, A/X turns it green, B/Y blue-white, and the grip vibrates.
Useful options: `--runtime auto|wivrn|monado|active|/path/manifest.json`, `--seconds N` (default 30, `0` = until Ctrl+C), `--origin floor|device`,
`--no-haptics`, `--quiet`, `--debug`. If you see red and blue in the headset, the whole native chain works.

No headset at hand? Use Monado headless (needs `--monado` in `setup-host.sh`):

```bash
XRT_COMPOSITOR_NULL=1 SIMULATED_ENABLE=1 monado-service        # in its own terminal
native/build/linuxxr-probe run --runtime monado --seconds 10
```

### CI and releases

`.github/workflows/ci.yml` builds and runs `ctest` on ubuntu-24.04 for every push and pull request. `.github/workflows/release.yml` runs on a `vX.Y.Z` tag,
runs the tests, builds portably, optionally signs the tarball, creates the GitHub Release and publishes the `upm` branch. Details: [ARCHITECTURE](ARCHITECTURE.md#8-distribution-and-ci) and [CONTRIBUTING](../CONTRIBUTING.md#release-procedure).
