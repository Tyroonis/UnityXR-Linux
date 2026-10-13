# Troubleshooting

**English** | [Deutsch](TROUBLESHOOTING.de.md)

Start with **Window -> XR -> LinuxXR Setup** (the setup wizard: native plug-in and subsystems, Vulkan, loaders, Flatpak access, WiVRn) and
**Window -> XR -> LinuxXR Status** (session state and statistics). They usually point at the problem, and most steps have a one-click fix.
Native `[LinuxXR]` messages appear in the Unity Console. To report a bug, use **Create issue report** in the Status window (see [Filing an issue](#filing-an-issue)).

Environment variables mentioned below must be set **before** Unity Hub / the Editor is started (for example `LINUXXR_FLIP_Y=1 unityhub`, or
`flatpak override --user --env=LINUXXR_FLIP_Y=1 com.unity.UnityHub` for the Hub Flatpak). Restart the Hub completely afterwards.

## Symptom, cause, fix

### Setup and startup

| Symptom | Cause | Fix |
|---------|-------|-----|
| Console: `[LinuxXR] Unity does not know the LinuxXR subsystems ...`; wizard / status: **Subsystems** or **Native plugin** red ("subsystems not found") | Unity registers native XR subsystems only at Editor start, and only from `Assets/LinuxXR/UnitySubsystemsManifest.json`, which the package installs on first load. Typical causes: the Editor was **not restarted** after the first install or an update, or `Assets/LinuxXR` was deleted or is incomplete. | **Restart the Editor** (the wizard shows **Restart Editor**). If the wizard's **Native plugin** step offers **Install**, click it and restart. Check that `Assets/LinuxXR/UnitySubsystemsManifest.json` and `Assets/LinuxXR/Plugins/Linux/x86_64/libLinuxXR.so` exist. If it persists, reinstall with one of the three ways in [INSTALL](INSTALL.md#3-install-the-package). |
| XR Plug-in Management: LinuxXR is missing, or "Unable to add package ... cannot be found" when ticking it | `com.unity.xr.management` is not installed, or the package is not resolved yet. | Run *Window -> XR -> LinuxXR Install XR Plug-in Management* (the bootstrap offers to install `com.unity.xr.management`), wait for the compile, restart the Editor. Check `Packages/manifest.json` for `com.tyroonis.xr.linuxopenxr`. |
| Console: `[LinuxXR] libLinuxXR.so could not be loaded ...`; wizard: **Native plugin** red | The native library is missing from the package (for example a git URL pointing at `main` instead of the `upm` branch, which has no binaries), or its dependencies cannot be loaded. | Install from `...git#upm` (or `#upm/v0.3.3`), the release tarball or the `.unitypackage`, not from the source branch. Check that `libLinuxXR.so` exists under `Library/PackageCache/com.tyroonis.xr.linuxopenxr@.../Runtime/Native~/` and, after the install, under `Assets/LinuxXR/Plugins/Linux/x86_64/`. The release binary needs glibc 2.35 or newer. Restart the Editor. |
| Console: `[LinuxXR] The Editor runs with <API>, LinuxXR needs Vulkan ...`; wizard: **Graphics API** red | Unity uses OpenGL Core or another API. | Click **Use Vulkan + restart** in the wizard (or **Use Vulkan** in the status window), or set Player Settings -> Linux -> Graphics APIs with **Vulkan first** (Auto Graphics API off), then restart. |
| LinuxXR does not appear in XR Plug-in Management | Wrong tab, or the package was not picked up. | Use the tab **"Windows, Mac, Linux"**. If it is missing there, see the first two rows and restart the Editor. |
| Package Manager (Unity 6.3+): "package is not signed" | Release tarballs are signed only when the maintainer configured signing; git URL installs usually do not show the message. | Ignore it: it is only a warning and the package works. |
| Console: `[LinuxXR] Only available in the Linux Editor - loader skipped.` | LinuxXR is enabled for a build target / platform where it cannot work. | Keep LinuxXR enabled only on the "Windows, Mac, Linux" tab; the Android tab keeps OpenXR. |
| XR does not start, nothing happens in Play Mode | LinuxXR not ticked, OpenXR ticked at the same time, or "Initialize XR on Startup" off. | In the wizard use **Enable LinuxXR**, **Disable them on this tab** and **Turn on** (Initialize XR on Startup), or set this on the "Windows, Mac, Linux" tab by hand. |
| The setup wizard does not open any more | The checklist is disabled for this project, or everything required is already fine. | Open it from *Window -> XR -> LinuxXR Setup* (re-enable the checklist there). |
| Console: `The subsystems could not be created - native initialization failed` | The native plugin failed to start a subsystem. | Read the `[LinuxXR]` lines above it and the log file (see below); typical reasons: no runtime, no headset, not Vulkan. |

### Runtime and headset

| Symptom | Cause | Fix |
|---------|-------|-----|
| `No OpenXR runtime found. Install WiVRn (wivrn-server) or set LINUXXR_RUNTIME.`; status: **OpenXR runtime** red | No WiVRn / Monado manifest found. | Install WiVRn ([INSTALL](INSTALL.md#2-wivrn-and-the-quest-host-side)) or use the wizard's **Install WiVRn (Flatpak)**, or set the **Runtime** setting to *Custom manifest path*, or set `LINUXXR_RUNTIME=/path/to/openxr_wivrn.json`. Diagnose with `native/build/linuxxr-probe runtimes`. |
| Unity Hub is a Flatpak and WiVRn (Flatpak) is not found; message mentions "running inside a Flatpak sandbox"; wizard: **Flatpak** step open | The Editor runs in the Hub's sandbox and cannot see WiVRn's runtime or socket. | In the wizard click **Allow access to WiVRn** (runs `flatpak override --user --filesystem=xdg-run/wivrn` plus read-only access to the WiVRn app directories), then **Close Unity Hub + Editor now** and start the Hub and the project again. If it still fails, install WiVRn for your user (`flatpak install --user flathub io.github.wivrn.wivrn`, the wizard has a button for it) and remove the system install. Optional command-line equivalent: `scripts/setup-unity-flatpak.sh`. |
| `WiVRn: no headset connected (open the WiVRn app on the Quest).`; status: **Headset** yellow | WiVRn serves applications only while a headset is connected; the socket `$XDG_RUNTIME_DIR/wivrn/comp_ipc` is missing or does not accept connections. | Start the WiVRn server / dashboard, open the WiVRn app on the Quest, connect to the PC, **then** press Play. |
| Headset connects only sometimes, or not at all | Firewall / mDNS / network. | Ports 9757 TCP+UDP and 5353/UDP open (`ufw allow ...`), `avahi-daemon` running, PC and Quest in the same network, Quest on 5 GHz / Wi-Fi 6. Or use USB ("USB networking" in the WiVRn app, or `./scripts/quest-usb.sh`). |
| WiVRn app on the Quest says the versions are incompatible (e.g. "incompatible protocol version") | WiVRn client and server versions must match exactly. | Update both: `flatpak update io.github.wivrn.wivrn` on the PC, and update the WiVRn app on the Quest (the dashboard's "Install on headset" can install the matching version). |
| `Runtime wants a different VkPhysicalDevice than the application uses ...` | Several GPUs; WiVRn and Unity picked different ones. | Set `LINUXXR_GPU=<index or part of the GPU name>` and/or `XRT_COMPOSITOR_FORCE_GPU_INDEX`, then restart the Hub/Editor. |
| `Unity's Vulkan device lacks extensions the runtime needs: ... - the image may stay black.` | Unity's Vulkan device does not enable an extension WiVRn needs. | Make sure Unity really runs with Vulkan on the GPU WiVRn uses (`LINUXXR_GPU`). Include the report from the status window in an issue. |
| `Connection to the OpenXR runtime lost (...)` / `OpenXR instance is about to be lost (runtime exited?)` | WiVRn exited or the headset disconnected during Play. | Stop Play Mode, restart WiVRn, reconnect the headset, press Play again. The Editor does not hang on runtime loss. |

### Editor behavior

| Symptom | Cause | Fix |
|---------|-------|-----|
| Editor freezes when pressing Play or when opening the status window | An OpenXR call blocked. WiVRn blocks OpenXR calls while no headset is connected; LinuxXR avoids this with a probe on a worker thread (3 s timeout). | Connect the Quest in WiVRn first. If the Editor still freezes, restart it, look at the end of the native log (`~/.cache/linuxxr/linuxxr.log`, see [Where are the logs?](#where-are-the-logs)) and file an issue with that log and the issue report. |
| Play Mode starts without VR; Console: yellow `No headset connected ... Play continues without VR` | WiVRn is not running or no headset is connected. This is a warning, not an error. | Start WiVRn, connect the Quest in the WiVRn app, press Play again. |

### Image

| Symptom | Cause | Fix |
|---------|-------|-----|
| Image is upside down | Orientation mismatch of the copy into the swapchain on this setup. | Set `LINUXXR_FLIP_Y=1` before starting the Editor. Please report your setup. |
| Image is black or frozen; Console: `Previous frame did not complete (AccessQueue callback missing?) - try LINUXXR_DIRECT_SUBMIT=1` | The frame submit inside `AccessQueue` did not complete. | Set `LINUXXR_DIRECT_SUBMIT=1`. Also check the Vulkan extension line in the Console / status window, and that the headset is still connected. |
| Image is black, no errors | Headset not actually connected, or `Display subsystem started` never appeared. | Check the status window; look for `Display subsystem started - image goes to ...` in the Console. Test the chain with `native/build/linuxxr-probe run`. |
| Stutter, "missed display frames" rising, fewer headset frames than Unity frames | Full-resolution frames (2476x2594 per eye on Quest 3) do not all make it through encoder / network. The tested setup got about 81 of 90 frames. | **Render Scale about 0.7**, wired LAN for the PC or USB for the headset, 5 GHz / Wi-Fi 6, lower the bitrate in WiVRn, prefer Single Pass Instanced. |
| Colors too dark or too bright | Gamma color space. | Player Settings -> Other Settings -> **Color Space: Linear**. |
| Game View is empty / only one eye | Mirror Mode. | Project Settings -> XR Plug-in Management -> LinuxXR -> **Mirror Mode** (Left eye / Right eye / Both eyes / Off). |

### Input and rig

| Symptom | Cause | Fix |
|---------|-------|-----|
| Head or controllers do not move | Rig not bound to XR input. | Use an XR Origin (XR Interaction Toolkit) or a Tracked Pose Driver with the "LinuxXR HMD" / "LinuxXR Controller" bindings, see [INSTALL](INSTALL.md#6-scene-setup). |
| `OVRCameraRig` / `OVRInput` / `OVRManager` get no data | Meta SDK parts relying on OVRPlugin do not work in the Linux Editor. | Use XR Origin / XR Interaction Toolkit / Unity XR input for the rig. Quest builds can keep using the Meta SDK. |
| Hands, passthrough, depth-based effects missing | Not implemented yet (no hand tracking, no passthrough, no depth layer, no occlusion mesh). | See "Known limitations" in the [README](../README.md). |
| Floor is at head height or the other way round | Tracking Origin. | Switch **Tracking Origin** between Floor and Device. On WiVRn/Quest, long-press the Meta button to recenter. |

## Where are the logs?

| Log | Location |
|-----|----------|
| LinuxXR native log | `~/.cache/linuxxr/linuxxr.log` (respects `XDG_CACHE_HOME`) |
| Same, with Unity Hub as Flatpak | `~/.var/app/com.unity.UnityHub/cache/linuxxr/linuxxr.log` |
| Unity Console | `[LinuxXR]` lines (setting **Forward Log To Console**, on by default) |
| Unity Editor log | `Logs/Editor.log` in the project folder |
| Issue reports | `Logs/LinuxXR-report-<timestamp>.md` in the project folder |
| `test-openxr.sh` logs | `scripts/logs/` |

For more detail enable **Verbose Logging** in the LinuxXR settings, or start with `LINUXXR_LOG=debug`. The status window has **Open log**, **Copy diagnostics** and **Create issue report**.
To debug the OpenXR chain without Unity: `./scripts/test-openxr.sh wivrn --debug` and `native/build/linuxxr-probe run --debug`.

## Filing an issue

1. Reproduce the problem with **Verbose Logging** on if you can.
2. In the Editor open **Window -> XR -> LinuxXR Status** and click **Create issue report**. The Markdown report (system, Unity, packages, settings, status,
   statistics, diagnostics, last 250 native log lines; your home directory is replaced by `~`) is copied to the clipboard and saved as `Logs/LinuxXR-report-<timestamp>.md`.
3. Open an issue on GitHub or GitLab (the bug report template asks for the report) and **attach or paste the report**, together with the steps to reproduce and the expected and actual behavior.
   Check it for anything private first.

Not using the Editor (e.g. the Editor does not even start)? Attach the output of the optional `./scripts/check-system.sh` and the contents of `~/.cache/linuxxr/linuxxr.log`.
