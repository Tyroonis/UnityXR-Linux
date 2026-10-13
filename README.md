# LinuxXR

**English** | [Deutsch](README.de.md)

**Press Play in the Unity 6 Linux Editor and the scene runs on your Meta Quest.** LinuxXR is a native Unity XR provider
(`libLinuxXR.so` + the UPM package `com.tyroonis.xr.linuxopenxr`) that streams the Editor's Play Mode to a Quest through
[WiVRn](https://github.com/WiVRn/WiVRn) over Wi-Fi or USB. No SteamVR, no Meta Quest Link. Because the game runs inside the
Editor process, debugging works like on Windows with Quest Link: Console, breakpoints, Profiler, Inspector and a Game View mirror.

Unity's own `com.unity.xr.openxr` has no Linux support (no Linux binary, Play Mode is blocked), which is why this exists.
Android builds for the Quest keep using Unity OpenXR (Android tab); your game code stays the same.

## Features

- Play Mode on a Meta Quest from the **Linux Editor**, via WiVRn (OpenXR runtime, Monado-based).
- Vulkan rendering; stereo mode **Single Pass Instanced** (recommended) or **Multi Pass**.
- Input subsystem with **"LinuxXR HMD"**, **"LinuxXR Controller - Left"** and **"LinuxXR Controller - Right"**,
  exposed with Unity `CommonUsages` (works with the Input System, XR Interaction Toolkit and HurricaneVR's XR input).
- Game View mirror (left eye / right eye / both eyes / off), tracking origin (Floor / Device), render scale (0.5-1.5).
- Status window (**Window -> XR -> LinuxXR Status**): setup checklist, session state, live statistics, one-click actions,
  and **Create issue report** (Markdown report for bug reports).
- Native `[LinuxXR]` log messages are forwarded to the Unity Console.
- Runtime selection: automatic (WiVRn, then `active_runtime.json`, then Monado), or via setting / `LINUXXR_RUNTIME`.
- **Script-free install** through Unity's Package Manager (git URL, tarball or `.unitypackage`), with a first-run **setup wizard** that fixes settings in one click.
- Works with the **Unity Hub Flatpak** (the wizard opens the sandbox for WiVRn) and ships a portable native plug-in with a bundled OpenXR loader.
- Test tooling: `linuxxr-probe` (checks the whole chain without Unity) and a `ctest` suite with a fake Unity host and
  headless-Monado integration tests.

## Requirements

| What | Requirement |
|------|-------------|
| OS | Linux with Vulkan, glibc 2.35 or newer (the release binary is built portably). Tested: Ubuntu 26.04 GNOME (Wayland, Editor via XWayland). Unity officially supports Ubuntu 22.04 / 24.04 only. |
| GPU | Vulkan driver (e.g. Mesa RADV) plus video encoding for WiVRn: VAAPI / Vulkan Video on AMD and Intel, NVENC on NVIDIA. |
| Unity | Unity 6 (the package requires `6000.0` or newer), Vulkan as Graphics API. Tested: 6000.6.5f1 with URP. |
| Headset | Meta Quest with the WiVRn app (Meta Horizon Store or sideload). Tested: Quest 3. |
| Streaming | [WiVRn](https://github.com/WiVRn/WiVRn) on the PC (Flatpak `io.github.wivrn.wivrn`), the **same version** in the Quest app. |
| Network | PC on a LAN cable, Quest on 5 GHz / Wi-Fi 6; or USB. Firewall: port 9757 TCP+UDP (and 5353/UDP for mDNS discovery). |
| Building from source | Only for contributors: see [CONTRIBUTING.md](CONTRIBUTING.md) and [Building from source](docs/INSTALL.md#building-from-source-developers). |

No scripts, no build tools and no separate OpenXR loader are needed to use LinuxXR: the package contains the native plugin
and a bundled OpenXR loader.

## Quick start

The shortest working path. Every step is explained in [docs/INSTALL.md](docs/INSTALL.md).

1. **Install WiVRn** on the PC (`flatpak install --user flathub io.github.wivrn.wivrn`; the setup wizard can do this for you),
   install the **WiVRn app** on the Quest, open port 9757 TCP+UDP in your firewall, and pair them.
2. **Add the package** in Unity: *Window -> Package Manager -> + -> Add package from git URL...* and enter

   ```
   https://github.com/tyroonis/UnityVR-Linux.git#upm
   ```

   (use `#upm/v0.3.3` to pin this release). Alternatives: *Add package from tarball...* with the release `.tgz`, or the `.unitypackage`.
   See [Installation](#installation) below.
3. **Restart the Editor** once. On first load the package installs its native plug-in into `Assets/LinuxXR`, and Unity registers XR subsystems only at Editor start-up (the setup wizard shows **Restart Editor**).
4. Follow the **LinuxXR Setup** wizard (*Window -> XR -> LinuxXR Setup*; it opens by itself while something required is missing). Each step has a one-click fix.
5. Add an XR Origin to your scene, start WiVRn, connect the headset, and press **Play**.
   The Console should show `[LinuxXR] Initialized (version 0.3.3)`.

## Installation

Install LinuxXR with Unity's Package Manager. No scripts are involved.

| # | Way | How |
|---|-----|-----|
| 1 | **Git URL** (recommended) | *Package Manager -> + -> Add package from git URL...* -> `https://github.com/tyroonis/UnityVR-Linux.git#upm`, or `...#upm/v0.3.3` to pin a version. The `upm` branch is published by the release CI and contains the prebuilt binaries. |
| 2 | **Tarball** | Download `com.tyroonis.xr.linuxopenxr-0.3.3.tgz` from the GitHub Releases page, then *Package Manager -> + -> Add package from tarball...*. Unity 6.3+ may show "package is not signed"; this is only a warning (see [Troubleshooting](docs/TROUBLESHOOTING.md)). |
| 3 | **.unitypackage** | Download `LinuxXR-0.3.3.unitypackage` (GitHub Releases or Asset Store) and use *Assets -> Import Package -> Custom Package...*. The C# code installs into `Assets/Tyroonis/LinuxXR`, the native files into `Assets/LinuxXR`. |

All ways work the same: on first load (Linux Editor only) the package installs its native part into `Assets/LinuxXR`
(`UnitySubsystemsManifest.json` and `Plugins/Linux/x86_64/{libLinuxXR.so, libopenxr_loader.so.1}`), because Unity registers native XR subsystems only from a
direct sub-folder of `Assets/`. Commit `Assets/LinuxXR` to version control like any other asset, or leave it out; it is recreated automatically.

After installing, and after every update that changes the native binaries, **restart the Editor once** (the setup wizard shows **Restart Editor**). If `com.unity.xr.management` (XR Plug-in Management) is missing, a small bootstrap offers to install it
(menu *Window -> XR -> LinuxXR Install XR Plug-in Management*).

### Setup wizard

*Window -> XR -> LinuxXR Setup* opens automatically once per session while something required is missing (you can switch the checklist off per project).
Steps with one-click fixes:

- native plug-in installed into `Assets/LinuxXR` (**Install**, then **Restart Editor**) and XR subsystems found
- Vulkan as Graphics API for Linux (restarts the Editor)
- LinuxXR loader enabled for Standalone, other Standalone loaders removed, Initialize XR on Startup
- Flatpak sandbox access to WiVRn (only if Unity Hub runs as the Flatpak `com.unity.UnityHub`): **Allow access to WiVRn**, then **Close Unity Hub + Editor now** and start them again
- WiVRn installed (from Flathub, `flatpak install --user`) and started
- recommended: Linear color space, Render Scale 0.7

Optional host helpers (`scripts/check-system.sh`, `setup-host.sh`, `setup-unity-flatpak.sh`, `quest-usb.sh`) are still in the repository, but nothing requires them.

## How it works

```
 Unity Editor (Linux, Vulkan)                          PC (WiVRn)                    Quest
┌─────────────────────────────────────────┐   ┌──────────────────────────┐   ┌───────────────┐
│ Your scene (XR Origin, XRI, ...)         │   │ WiVRn server             │   │ WiVRn app     │
│   renders both eyes into Unity textures  │   │ (OpenXR runtime,         │   │               │
│                  │                       │   │  Monado-based)           │   │ decodes and   │
│ libLinuxXR.so    ▼                       │   │                          │   │ shows video   │
│   copy into OpenXR swapchain             │──▶│ encodes video:           │──▶│               │
│   (in IUnityGraphicsVulkan::AccessQueue) │   │  VAAPI / Vulkan Video    │   │ sends head &  │
│   xrEndFrame (projection layer)          │   │  (AMD, Intel) or NVENC   │◀──│ controller    │
│   input: HMD + 2 controllers  ◀──────────│◀──│  (NVIDIA)                │   │ poses         │
└─────────────────────────────────────────┘   └──────────────────────────┘   └───────────────┘
                                                      Wi-Fi or USB
```

Unity renders both eyes (single-pass instanced texture array, or multi-pass) into its own textures. The plugin copies them into the
OpenXR swapchain inside `IUnityGraphicsVulkan::AccessQueue` and submits a projection layer with `xrEndFrame`. WiVRn encodes and
streams the result to the Quest. Head and controller poses come back through OpenXR and are exposed as Unity XR input devices.
The OpenXR loader is loaded with `dlopen`; a portable copy (Khronos OpenXR-SDK 1.1.47) ships next to the plugin because the
Unity Hub Flatpak runtime has none. Details: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Status

**0.3.3 - early but working: image, head tracking and controller tracking, script-free installation and a setup wizard (see the [changelog](CHANGELOG.md)).**

## Tested hardware

| Component | Tested with |
|-----------|-------------|
| OS | Ubuntu 26.04, GNOME, Wayland (Editor runs via XWayland) |
| GPU / driver | AMD Radeon RX 7600, RADV, Mesa 26 |
| Unity | 6000.6.5f1, URP |
| Unity Hub | Flatpak (`com.unity.UnityHub`) |
| Headset | Meta Quest 3 |
| WiVRn | 26.9, Flatpak (`io.github.wivrn.wivrn`) |
| Result | Image, head and controller tracking work. Unity runs at about 90 fps; about 81 of 90 frames reach the headset at full resolution (2476x2594 per eye). |

At full resolution not every frame reaches the headset. Set **Render Scale to about 0.7** and use a wired LAN connection or USB.
Other setups (NVIDIA, Intel, Quest 2 / Pro, Ubuntu 24.04, X11) are untested. Reports are welcome.

## Known limitations

- No depth layer is submitted, no occlusion mesh yet, no hand tracking yet, no passthrough.
- Meta SDK parts that rely on OVRPlugin (`OVRCameraRig`, `OVRInput`, `OVRManager`) get no data in the Linux Editor.
  Use an XR Origin, XR Interaction Toolkit or Unity XR input for the rig.
- Editor only. Quest builds use Unity OpenXR on the Android tab.
- Unity officially supports Ubuntu 22.04 and 24.04 only; other distributions are at your own risk.
- WiVRn only serves applications while a headset is connected: connect the headset first, then press Play. Without a headset, Play Mode continues without VR and the Console shows a yellow warning (no errors); the Editor does not freeze.

## Project layout

```
.
├── native/                          C++20 native plugin
│   ├── src/core/                    Unity-independent logic (OpenXR, Vulkan, runtime selection, input)
│   ├── src/unity/                   Unity glue (plugin entry, display, input, session, C API)
│   ├── tools/probe/                 linuxxr-probe (test the chain without Unity)
│   ├── tests/                       ctest suites, fake Unity host, Monado integration tests
│   └── third_party/                 unity-xr-headers/ (fetched by script, not committed)
├── unity/
│   └── Packages/com.tyroonis.xr.linuxopenxr/   The complete UPM package
│       ├── Runtime/                 Loader, settings, native bridge (C#)
│       ├── Runtime/Native~/         libLinuxXR.so, libopenxr_loader.so.1, LICENSE-openxr-loader.txt (ignored by Unity's importer)
│       └── Editor/                  Registration (installs Assets/LinuxXR), setup wizard, bootstrap, status window, issue report
├── scripts/                         Developer scripts: build, pack, install (dev), plus optional host helpers
├── .github/workflows/               CI (ci.yml) and release (release.yml); docs/github/ keeps a copy of the templates and workflows
└── docs/                            INSTALL, TROUBLESHOOTING (EN/DE), ARCHITECTURE (EN), dev/ (historical German notes)
```

## Documentation

- [Installation guide](docs/INSTALL.md) ([Deutsch](docs/INSTALL.de.md)) - end-user install first, "Building from source" last
- [Troubleshooting](docs/TROUBLESHOOTING.md) ([Deutsch](docs/TROUBLESHOOTING.de.md))
- [Architecture](docs/ARCHITECTURE.md) (English only)
- [Changelog](CHANGELOG.md)
- [Contributing and release procedure](CONTRIBUTING.md)
- [Historical planning notes (German)](docs/dev/README.md)

## Contributing and reporting bugs

Contributions are welcome, see [CONTRIBUTING.md](CONTRIBUTING.md). To report a bug, open an issue and paste the report from
**Window -> XR -> LinuxXR Status -> Create issue report**. It contains system info, versions, settings, statistics and the last 250
native log lines (your home directory is replaced by `~`).

## Credits

- [WiVRn](https://github.com/WiVRn/WiVRn) - the streaming OpenXR runtime that makes this possible.
- [Monado](https://monado.freedesktop.org/) - the open-source OpenXR runtime WiVRn is based on; also used for tests.
- [Khronos OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) - OpenXR headers and loader (bundled loader: 1.1.47, Apache-2.0).
- [ValveSoftware/unity-xr-plugin](https://github.com/ValveSoftware/unity-xr-plugin) - structural reference for a native Unity XR provider
  and the source of the Unity XR SDK headers (Unity Companion License), fetched at build time and not committed.

## License

LinuxXR is licensed under the [GNU General Public License v3.0 or later](LICENSE) (GPL-3.0-or-later), with an additional permission under
GPLv3 section 7: LinuxXR may be linked and combined with the Unity Editor, the Unity engine runtime and the Unity XR SDK headers (Unity Companion License),
and using LinuxXR as a development tool does not put your own Unity project or game under the GPL. LinuxXR is Editor-only and is not included in player builds.
The full wording and the licenses of third-party components are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Disclaimer

LinuxXR is an independent open-source project. It is not affiliated with, endorsed by or sponsored by Unity Technologies,
Meta Platforms, Valve Corporation, Khronos Group or the WiVRn and Monado projects. Unity, Meta, Quest, SteamVR, OpenXR and
other names are trademarks of their respective owners.
