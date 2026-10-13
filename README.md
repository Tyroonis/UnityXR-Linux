# LinuxXR (OpenXR for Linux Editor)

A Unity XR provider for the **Linux Editor**. Press **Play** in Unity 6 on Linux and the scene runs on a
**Meta Quest**, streamed by [WiVRn](https://github.com/WiVRn/WiVRn) (Flatpak `io.github.wivrn.wivrn`) over Wi-Fi or USB - no SteamVR, no Meta Quest Link.
The game runs inside the Editor process, so Console, breakpoints, Profiler, Inspector and the Game View mirror work as usual.

Unity's own `com.unity.xr.openxr` has no Linux support, which is why this package exists.
It is **Editor-only**: Android builds for the Quest keep using Unity OpenXR (Android tab in XR Plug-in Management).

## What is in this package

- `Runtime/` - XR loader (`LinuxXRLoader`), settings (`LinuxXRSettings`) and the P/Invoke bridge to the native plug-in.
- `Runtime/Native~/` with the native plug-in `libLinuxXR.so`
  (display + input subsystems) and a bundled OpenXR loader (`libopenxr_loader.so.1`, Khronos 1.1.47). Unity's importer ignores folders ending in `~`.
- `Editor/` - XR Plug-in Management registration, native install into `Assets/LinuxXR`, setup wizard, bootstrap, console forwarder, issue report and the status window.

On first load (Linux Editor only) the package installs its native part into `Assets/LinuxXR`
(`UnitySubsystemsManifest.json` and `Plugins/Linux/x86_64/{libLinuxXR.so, libopenxr_loader.so.1}`), because Unity registers native XR subsystems only from a direct
sub-folder of `Assets/`. Commit `Assets/LinuxXR` like any other asset or let it be recreated.

Input devices: "LinuxXR HMD", "LinuxXR Controller - Left", "LinuxXR Controller - Right"
(Unity `CommonUsages`; works with the Input System, XR Interaction Toolkit and other XR input users).

## Installation

No scripts needed. Use one of:

1. **Git URL:** *Package Manager -> + -> Add package from git URL...* -> `https://github.com/<owner>/UnityVR-Linux.git#upm`
   (or `#upm/v0.3.3` to pin a version).
2. **Tarball:** download `com.tyroonis.xr.linuxopenxr-0.3.3.tgz` from GitHub Releases -> *Add package from tarball...* (Unity 6.3+ may show "package is not signed"; this is only a warning).
3. **.unitypackage:** import `LinuxXR-0.3.3.unitypackage` (*Assets -> Import Package*); the C# code installs into `Assets/Tyroonis/LinuxXR`, the native files into `Assets/LinuxXR`.

## Usage

1. **Restart the Editor once** after the first install and after every update that changes the native binaries (XR subsystems are registered at Editor start-up; the wizard shows **Restart Editor**).
2. Follow the setup wizard (*Window -> XR -> LinuxXR Setup*; it opens by itself while something required is missing). It fixes Vulkan, XR Plug-in Management,
   Flatpak access to WiVRn (Unity Hub Flatpak only) and can install and start WiVRn.
3. Add an XR Origin to your scene, start WiVRn, connect the Quest, and press **Play**.
   The Console shows `[LinuxXR] Initialized (version 0.3.3)`.
   Without a headset, Play Mode continues without VR and the Console shows a yellow warning.

*Window -> XR -> LinuxXR Status* shows session state and statistics and has **Create issue report** (attach the Markdown report to bug reports).
If `com.unity.xr.management` is missing, *Window -> XR -> LinuxXR Install XR Plug-in Management* installs it.

Full guide (WiVRn, Quest, firewall port 9757 TCP/UDP, settings, scene setup): [docs/INSTALL.md](https://github.com/<owner>/UnityVR-Linux/blob/main/docs/INSTALL.md).
Problems: [docs/TROUBLESHOOTING.md](https://github.com/<owner>/UnityVR-Linux/blob/main/docs/TROUBLESHOOTING.md).

## Requirements

Unity 6 (6000.0 or newer) on Linux with Vulkan and glibc 2.35+, `com.unity.xr.management` 4.5.0 (installed automatically with the package),
WiVRn on the PC and the matching WiVRn app on the Quest.

## License

GPL-3.0-or-later, with an additional permission for the Unity Editor and the Unity XR SDK headers. See `LICENSE.md` and `THIRD_PARTY_NOTICES.md`. Not affiliated with Unity, Meta or Valve.
