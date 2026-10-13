# 03 - Alternativen, Monado-Testtreiber und Container

Stand der Recherche: 2026-10-12. Nicht abgedeckt: WiVRn-Interna und Unitys OpenXR-Paket im Detail (separate Dokumente).

## Kontext-Update (Entscheidung des Users)

- Eigener nativer Unity-XR-Provider (OpenXR + Vulkan).
- WiVRn per PPA auf Ubuntu 26.04. Dort liegen im Archiv: `libopenxr-loader1` 1.1.47 und `monado-service` 25.0.0.
- Unity-Editor laeuft direkt auf dem Host.
- Container nur fuer den Plugin-Build.

Folgen dieser Entscheidung:

- Der Provider ersetzt den Stridemann-Shim (Abschnitt 0). Der Shim bleibt als Referenz und Gegenprobe.
- Das Loader-Versionsrisiko von Ubuntu 24.04 (Loader 1.0.20) entfaellt, da 26.04 den Loader 1.1.47 liefert.
- Ob die WiVRn-PPA fuer 26.04 (resolute) gebaut wird, konnte ich nicht pruefen (launchpad.net war gesperrt). Vor dem Start pruefen.
- Die Container-Hinweise in Abschnitt 7 sind fuer den Plugin-Build angepasst (7.4).

Einschraenkungen der Recherche: gitlab.freedesktop.org, gitlab.com und launchpad.net waren per Proxy gesperrt. Monado-Quellcode und Envision-Repo wurden nicht gelesen. Monado-Fakten stammen aus den Ubuntu-Binaerpaketen (strings/ELF), Doxygen-Seiten und Blender-Doku. Mit (UNVERIFIED) markierte Punkte sind Schlussfolgerungen.

## 0. Zentraler Befund: Unitys OpenXR-Plugin unterstuetzt Linux nicht

- Das offizielle Plugin unterstuetzt weder Linux-Editor noch Linux-Standalone. Unity-Mitarbeiter schrieben im Mai 2023 "investigating", seitdem gibt es keine Antwort. Die Threads sind 2025 und 2026 weiter aktiv.
  - https://discussions.unity.com/t/linux-support-with-openxr/916945
  - https://discussions.unity.com/t/openxr-support-for-linux/1693991
  - https://discussions.unity.com/t/openxr-linux-support/909788
- Fehlermeldung beim Linux-Standalone-Build: "The only standalone targets supported are Windows x64 and OSX with OpenXR."
- Paketquellen (`com.unity.xr.openxr` 1.19.0-pre.2) geprueft: `RuntimeLoaders/` enthaelt nur android, osx, windows/x64. Native Plugins gibt es nur fuer Windows x64, macOS und Android. Es gibt keinen Linux-Loader und keine Linux-`UnityOpenXR.so`.
- Meta Link ist nur unter Windows verfuegbar. Meta nennt als Entwicklungsrechner nur Windows und macOS. https://developers.meta.com/horizon/documentation/unity/unity-req/
- Foren: Meta-SDK ueber v74.0.3 laeuft im Linux-Editor nicht. https://communityforums.atmeta.com/discussions/dev-unity/versions-above-v74-0-3-do-not-work-on-unity-editor-linux/1327522

### Referenz: Stridemann/Unity-XR-on-Linux-for-Meta-Quest

https://github.com/Stridemann/Unity-XR-on-Linux-for-Meta-Quest (MIT, README und TECHNICAL.md gelesen)

- Getestet: Editor-Play auf Linux mit Quest 3 ueber SteamVR + ALVR 20.x, Arch, Unity 6000.3.11f1, com.unity.xr.openxr 1.16.1, Vulkan.
- Prinzip: Das Paket liefert `Runtime/android/x64/libUnityOpenXR.so` (Android x86_64, bionic). Der Shim laesst sie unter glibc laufen:
  1. bionic-auf-glibc-Shimbibliotheken (`libc.so`, `libm.so`, `libdl.so`) ueber `LD_LIBRARY_PATH`.
  2. `LD_PRELOAD`-Fake-JavaVM, der `JNI_OnLoad` aufruft.
  3. OpenXR-Implicit-API-Layer, der `XR_KHR_android_create_instance` aus `xrCreateInstance` entfernt (`STRIP_ANDROID_XR_LAYER=1`).
  4. Vulkan-Implicit-Layer, der `VK_KHR_timeline_semaphore` erzwingt (`ENABLE_FORCE_TIMELINE_SEM=1`).
  5. Patch in `OpenXRLoader.cs`: Unity laedt den System-`libopenxr_loader.so`.
  6. Start mit `-force-vulkan`.
- Die aktive Runtime kommt aus `~/.config/openxr/1/active_runtime.json` oder `XR_RUNTIME_JSON`. Ein Wechsel von SteamVR auf WiVRn waere nur ein Runtime-Wechsel (UNVERIFIED). Issue #1 im Repo regt WiVRn an, ein Ergebnis wurde nicht berichtet.
- Nur das Vulkan-Binding funktioniert. X11 oder Wayland des Editors spielt fuer den XR-Pfad keine Rolle.
- Hinweise fuer den eigenen Provider:
  - Das Plugin braucht den System-Loader (unter Ubuntu `/usr/lib/x86_64-linux-gnu/libopenxr_loader.so.1`).
  - Controller-Profil: Oculus Touch Controller Profile, nicht "Touch Plus". Bei WiVRn neu pruefen.
  - Timeline-Semaphoren muessen im Vulkan-Device aktiviert sein. Monado kennt sie (`OXR_DEBUG_FORCE_TIMELINE_SEMAPHORES` in `libopenxr_monado.so`).
- APK-Builds von Linux brauchen keinen Hack. Bekannter Fehler: Unitys mitgeliefertes Android-NDK hat auf Linux defekte clang-Symlinks (Loesung im README).

## 1. ALVR

- ALVR benoetigt unter Linux zwingend SteamVR. Einen Monado- oder OpenXR-nativen Servermodus gibt es nicht.
- Geklontes Repo (Commit `ebec107`, 2026-10-11, Version `21.0.0-dev14`): nur `server_openvr` und `server_core`, kein OpenXR-Server. `alvr/client_openxr` ist der Headset-Client (Rust, `openxr`-Crate).
- `wiki/Roadmap.md`: "Monado Driver ... Status: blocked on refactors".
- Letzter Release v20.14.1.
- vronlinux.org: ALVR unter Linux nicht empfohlen, besser WiVRn oder Steam Link. https://vronlinux.org/docs/steamvr/alvr/ , https://vronlinux.org/docs/hardware/
- Nutzen fuers Projekt: nur als Gegenprobe (ALVR + SteamVR + Stridemann-Shim), um Fehler einzugrenzen. https://github.com/alvr-org/ALVR/wiki/Installation-guide

## 2. Monado

### 2.1 Ueberblick

- OpenXR-Runtime (BSL-1.0) fuer Linux und Android.
  - `libopenxr_monado.so`: schlanke Client-Bibliothek, die in die App geladen wird.
  - `monado-service`: Compositor und Treiber in eigenem Prozess. Socket `$XDG_RUNTIME_DIR/monado_comp_ipc`, systemd-User-Units `monado.socket` und `monado.service` (Socket-Aktivierung, `ConditionUser=!root`).
  - `monado-cli`, `monado-gui`.
- Monado streamt nicht selbst zur Quest. WiVRn ist Monado plus Streaming-Compositor mit eigenen Patches.
- Doku: https://monado.freedesktop.org/getting-started.html (u.a. `XRT_LOG`, `XRT_COMPOSITOR_LOG`, `XRT_PRINT_OPTIONS=1`, `XRT_DEBUG_GUI=1`, `PROBER_LOG`).

### 2.2 Paket-Tabelle Ubuntu (aus Packages.gz von archive.ubuntu.com und entpackten .deb geprueft)

| Paket | 24.04 noble | 25.04 plucky | 25.10 questing | 26.04 resolute |
|---|---|---|---|---|
| libopenxr-loader1 | 1.0.20 | 1.0.34 | 1.1.47 | 1.1.47 |
| libopenxr-dev | 1.0.20 | 1.0.34 | 1.1.47 | 1.1.47 |
| libopenxr-utils (enthaelt `hello_xr`, `openxr_runtime_list`) | 1.0.20 | 1.0.34 | 1.1.47 | 1.1.47 |
| openxr-layer-apidump / -corevalidation | 1.0.20 | 1.0.34 | 1.1.47 | 1.1.47 |
| libopenxr1-monado, monado-service, monado-cli, monado-gui | 21.0.0+git2905 (alter Snapshot) | 21.0.0+git2905 | 25.0.0 | 25.0.0 |
| xr-hardware (udev-Regeln) | 1.1.0 | - | 1.1.1 | 1.1.1 |
| adb | 34.0.4 | - | 34.0.5 | 34.0.5 |
| scrcpy | 1.25 | - | 1.25 | 3.3.4 |
| distrobox | 1.7.0 | - | 1.8.1 | 1.8.2 |
| podman | 4.9.3 | - | 5.4.2 | 5.7.0 |
| flatpak | 1.14.6 | - | 1.16.1 | 1.16.6 |
| docker.io | 24.0.7 | - | 28.2.2 | 29.1.3 |

- Alles liegt in `universe` (avahi, pipewire, mesa, libvulkan1 in `main`).
- Nicht in den Ubuntu-Archiven: `xrgears`, `wivrn`, `alvr`, `envision` (alle vier Releases durchsucht).
- Details aus den entpackten Paketen:
  - Manifest `/usr/share/openxr/1/openxr_monado.json`. Aktive Runtime `/etc/xdg/openxr/1/active_runtime.json`, verwaltet ueber `update-alternatives openxr1-active-runtime`.
  - `libopenxr_monado.so` braucht GLIBC 2.38 (laeuft also nicht auf 22.04) sowie libvulkan, libGLX, libstdc++ (26.04 zusaetzlich libcjson).
  - Eingebaute Treiber: qwerty, simulated, remote.
  - Env-Variablen im Binary: `QWERTY_ENABLE`, `QWERTY_COMBINE`, `QWERTY_LOG`, `SIMULATED_ENABLE/LEFT/RIGHT/ROTATE/VIEW_COUNT/LOG`, `REMOTE_LOG`, `P_OVERRIDE_ACTIVE_CONFIG`, `XRT_COMPOSITOR_NULL`, `XRT_COMPOSITOR_FORCE_XCB/WAYLAND/WAYLAND_DIRECT/VK_DISPLAY/NVIDIA/RANDR`, `XRT_COMPOSITOR_FORCE_GPU_INDEX`, `XRT_COMPOSITOR_FORCE_CLIENT_GPU_INDEX`, `XRT_NO_STDIN`, `IPC_EXIT_ON_DISCONNECT`.
  - Debug-GUI-Variable: `OXR_DEBUG_GUI` im 24.04-Build, `XRT_DEBUG_GUI` ab 25.0.0 (umbenannt).
- Auf 26.04 (Monado 25.0.0) stehen alle Test-Treiber mit der aktuellen Variablenbenennung zur Verfuegung.

### 2.3 Anleitung: Test ohne Headset (simulated / qwerty / remote)

**simulated**

- Ein "do-nothing"-HMD mit Controllern (Bewegung WOBBLE, ROTATE oder STATIONARY). Wird automatisch gewaehlt, wenn kein HMD gefunden wird. Erzwingen mit `SIMULATED_ENABLE=1`.
- Es oeffnet sich ein Compositor-Fenster (XCB oder Wayland). Laeuft unter X11 und Wayland.
- Blender nutzt genau das fuer Tests ohne Hardware: https://developer.blender.org/docs/features/gpu/tools/monado/
- Referenz: https://monado.pages.freedesktop.org/monado/group__drv__simulated.html

**qwerty**

- Tastatur und Maus steuern HMD und beide Controller (Trigger, Menu, Squeeze, Thumbstick, Trackpad).
- Standardmaessig aus. Start: `QWERTY_ENABLE=1 XRT_DEBUG_GUI=1 monado-service`. Das Debug-GUI ist noetig, weil die Eingaben aus dem SDL-Fenster kommen.
- STRG oder ALT wechselt den fokussierten Geraetekontext. Tastenbelegung im Hilfe-Tab des Qwerty-Widgets.
- `QWERTY_COMBINE=1` ergaenzt nur fehlende Geraete, wenn echte Hardware da ist.
- Quellen:
  - https://gitlab.freedesktop.org/monado/monado/merge_requests/714
  - https://monado.pages.freedesktop.org/monado/group__drv__qwerty.html
  - https://gitlab.freedesktop.org/monado/webpage/-/commit/832eda32db1b467eb7d740183be33ea7f9ecfcaa.diff

**remote**

- Posen und Eingaben ueber Netzwerk steuerbar, skriptbar.
- Start: `P_OVERRIDE_ACTIVE_CONFIG=remote monado-service`, danach `monado-gui remote` und verbinden. Die Meldung "No remote node" kann ignoriert werden. https://monado.pages.freedesktop.org/monado/howto-remote-driver.html

**Fensterwahl**

- `XRT_COMPOSITOR_FORCE_XCB=1` oder `XRT_COMPOSITOR_FORCE_WAYLAND=1` waehlt das Fenstersystem.
- `XRT_COMPOSITOR_NULL` bedeutet kein Fenster (headless).
- GNOME hat kein DRM-Lease, die `*_DIRECT`-Modi entfallen dort.

**Pipeline-Test fuer Unity ohne Headset**

1. Installation (26.04): `sudo apt install libopenxr-loader1 libopenxr-dev libopenxr-utils monado-service libopenxr1-monado`.
2. Terminal A: `XRT_DEBUG_GUI=1 QWERTY_ENABLE=1 monado-service` (ohne `QWERTY_ENABLE` kommt das automatisch simulierte HMD).
3. Terminal B, Gegenprobe: `XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json hello_xr -g Vulkan2`. Das prueft Loader, Runtime und GPU.
4. Terminal C: Unity mit demselben `XR_RUNTIME_JSON` und dem eigenen Provider starten, Play druecken. Bei Fehlern `XR_LOADER_DEBUG=all` und `XRT_LOG=debug` setzen.
5. Der Test trennt "Unity/OpenXR unter Linux scheitert" von "Streaming scheitert". Danach dieselbe Kette mit WiVRn.
6. Optional: `openxr-layer-apidump` und `openxr-layer-corevalidation` als Implicit-Layer zum Debuggen der Provider-Aufrufe.

**Weitere Hinweise**

- Unitys Mock-Runtime liefert nur Windows-, macOS- und Android-Binaries und ist unter Linux nicht nutzbar.
- Der XR Device Simulator (XR Interaction Toolkit) simuliert nur Eingaben, nicht die OpenXR-Kette. https://docs.unity3d.com/Packages/com.unity.xr.interaction.toolkit@3.0/manual/xr-device-simulator-overview.html
- Ubuntus Monado registriert sich systemweit per `update-alternatives` und kollidiert mit WiVRn. Lieber `XR_RUNTIME_JSON` pro Start setzen. Unitys "Play Mode OpenXR Runtime -> Other" macht dasselbe und wird beim Beenden auf "System Default" zurueckgesetzt. https://docs.unity3d.com/Packages/com.unity.xr.openxr@1.9/manual/project-configuration.html

## 3. Envision

- GUI, das eine Monado-Umgebung einrichtet (inkl. OpenVR-Kompatibilitaet xrizer/OpenComposite). Profile: System, Lighthouse, OpenHMD, Simulated, Survive, WMR sowie Entwicklungsprofile aus Quellcode. WiVRn-Profile gibt es nicht mehr. https://vronlinux.org/docs/fossvr/envision/
- Paketierung: AUR, Fedora (Terra), Nix. Andere Distributionen, also auch Ubuntu: AppImage. Nicht in Ubuntu-Archiven.
- Bewertung: optional. Mit 26.04 liegt Monado 25.0.0 im Archiv, daher braucht man Envision fuer die Testtreiber nicht. Fuer den WiVRn-Pfad bringt es nichts. Das Envision-Repo selbst konnte ich nicht lesen.

## 4. Meta-Tools unter Linux

| Werkzeug | Linux | Hinweis |
|---|---|---|
| Meta Quest Link (USB/Air Link) | Nein, nur Windows | https://docs.unity3d.com/Packages/com.unity.xr.meta-openxr@2.1/manual/get-started/link.html |
| Meta XR Simulator | Nein, Windows und macOS | https://developers.meta.com/horizon/blog/meta-xr-simulator-pc-mac-unity-unreal-developer-mixed-reality/ |
| Meta Quest Developer Hub | Nein, Windows und macOS | https://developers.meta.com/horizon/documentation/native/android/ts-mqdh-getting-started/ |
| adb | Ja | apt `adb` oder das mitgelieferte Platform-Tools von Unity. Nur ein adb-Binary verwenden, sonst Server-Neustartschleifen. udev-Regeln ueber `android-sdk-platform-tools-common` oder `adb`. |
| scrcpy | Ja | Auf 26.04 3.3.4. Auf 24.04 ist apt 1.25 alt, dort Upstream-Release nutzen. Spiegelt das Headset-Bild per USB oder `adb connect`. |
| Unity Build and Run von Linux | Ja | siehe Abschnitt 5 |
| Steam Link VR | Ja, aber SteamVR noetig | SteamVR 2.17 (2026-09-11), kein Support fuer die Original-Quest. https://www.gamingonlinux.com/2026/09/steamvr-2-17-arrives-ready-to-go-for-the-steam-frame/ |
| Virtual Desktop | Nein (Streamer fuer Windows/macOS) | https://www.vrdesktop.net/ |

## 5. Fallback: Build and Run auf die Quest mit logcat, Debugger und Profiler

Funktioniert heute unter Linux und braucht kein OpenXR im Editor.

1. Quest: Entwicklermodus (ueber die Meta-Horizon-App) aktivieren, USB-Debugging am Headset freigeben.
2. Unity-Hub: Android-Modul (SDK API 36, NDK r27c, OpenJDK 17) installieren. Bekannter Linux-Fehler: defekte NDK-Symlinks, Loesung siehe Stridemann-README.
3. Player-Einstellungen Android: Mindest-API 29 oder hoeher, ARM64, IL2CPP, OpenXR auf dem Android-Tab, Feature "Meta Quest Support" aktivieren (sonst schwarzes Fenster).
4. Geraet waehlen: File > Build Profiles > Android > Run Device. USB oder `<Enter IP>` fuer drahtloses adb (Port 5555 kann entfallen). Vorher z.B. `adb tcpip 5555` und `adb connect <Quest-IP>:5555`.
5. Build And Run.
6. **Logs:** `adb logcat -s Unity` oder das Paket "Android Logcat" (Fenster in Unity, nutzt das adb des SDK). https://docs.unity3d.com/Packages/com.unity.mobile.android-logcat@1.4/manual/index.html
7. **Managed Debugger:** Development Build mit Script Debugging. Mit "Wait for Managed Debugger" haelt der Start fuer fruehen Code an. Danach in VS Code, Rider oder VS "Attach Unity Debugger" und die Instanz mit dem Geraet waehlen. https://docs.unity3d.com/6000.0/Documentation/Manual/android-debugging-on-an-android-device.html
8. **Profiler:** Development Build mit "Autoconnect Profiler". Verbindung ueber USB (adb) oder IP.
9. Nachteil: jede Aenderung braucht einen kompletten Deploy, daher langsame Iteration. Als dauerhafte Rueckfalllinie behalten.

## 6. Eigenes Streaming (Alternativen)

| Option | Bewertung |
|---|---|
| Unity Render Streaming (WebRTC) | Paket 3.1.0-exp.9 nennt Linux, Clients sind Browser, kein XR/OpenXR. Kein OpenXR-Pfad, nicht empfohlen. https://docs.unity3d.com/Packages/com.unity.renderstreaming@3.1/manual/index.html |
| WebXR im Quest-Browser | Moeglicher Client, aber alles selbst zu bauen. |
| NVIDIA CloudXR 6.0 | Linux-Server (Ubuntu 22.04+), registriert sich als OpenXR-Runtime (`openxr_cloudxr.json`). Quest 2/3/3S ueber Browser (CloudXR.js, WebXR). Nur NVIDIA RTX, proprietaer, unter Linux weder Audio noch Mikrofon, Doku nur fuer Unreal. Plan C. https://docs.nvidia.com/cloudxr-sdk/latest/overview/overview.html , https://docs.nvidia.com/cloudxr-sdk/latest/release_notes/release_notes.html |
| Wiederverwendbare Teile | WiVRn-Client (GPLv3, C++/Vulkan, hat auch ein Linux-Target), ALVR `client_openxr` (MIT, Rust), Monado remote/simulated (BSL-1.0). Laeuft auf eine Neuimplementierung von WiVRn hinaus. |
| Valves OpenVR-XR-Plugin | nur Windows-DLLs (2020), Sackgasse. |

## 7. Container-Mechanik

### 7.1 Was ueber Funktionieren entscheidet

1. **Die OpenXR-Client-Bibliothek wird in den Unity-Prozess geladen.** Kette: Unity -> System-`libopenxr_loader.so` -> Manifest (`active_runtime.json` oder `XR_RUNTIME_JSON`) -> `libopenxr_wivrn.so` -> Unix-Socket -> Server. glibc und libstdc++ muessen im Unity-Prozess passen. WiVRns Flatpak baut diese Bibliothek deshalb mit einer crosstool-ng-Toolchain (GCC 11.5, glibc 2.35). https://github.com/flathub/io.github.wivrn.wivrn (geklont und gelesen)
2. **IPC-Socket:** WiVRn `$XDG_RUNTIME_DIR/wivrn/comp_ipc` (CMake `XRT_IPC_MSG_SOCK_FILENAME`), Monado `$XDG_RUNTIME_DIR/monado_comp_ipc`. Es genuegt, dass Server und Client dasselbe `XDG_RUNTIME_DIR` sehen.
3. **GPU-Puffer ueber Prozessgrenzen:** Der Server legt Swapchain-Bilder an, der Client importiert sie per fd. Beide Seiten brauchen dieselbe GPU und kompatible Vulkan-Treiber. Mesa auf beiden Seiten ist der uebliche Fall (UNVERIFIED fuer Unity). Bei NVIDIA muss die Treiber-Userspace-Version identisch sein (Flatpak-NVIDIA-Erweiterung, `distrobox --nvidia` und nvidia-container-toolkit stellen das sicher).
4. **Aktive Runtime:** Der WiVRn-Server schreibt den Symlink `~/.config/openxr/1/active_runtime.json` erst, wenn ein Headset verbunden ist, und setzt ihn danach zurueck (`server/active_runtime.cpp`, README, v26.6.1). Fuer den Editor: erst Headset verbinden, dann Play, oder `XR_RUNTIME_JSON` fest setzen.
5. **Netzwerk:** WiVRn meldet sich per Avahi (D-Bus). Firewall: UDP 5353 und TCP+UDP 9757. Kabelverbindung: `adb reverse tcp:9757 tcp:9757`, danach `adb shell am start -a android.intent.action.VIEW -d "wivrn+tcp://localhost" org.meumeu.wivrn`. Container brauchen Host-Netzwerk und Zugriff auf Avahi des Hosts.
6. **Audio:** Der Server legt einen virtuellen PipeWire-Lautsprecher (optional Mikrofon) an. Container muessen `$XDG_RUNTIME_DIR/pipewire-0` einbinden. Der Flatpak erlaubt `xdg-run/pipewire-0`.
7. **Encoder:** `vulkan`, `nvenc`, `vaapi`, `x264`. Container brauchen `/dev/dri` oder bei NVIDIA `NVIDIA_DRIVER_CAPABILITIES` mit `graphics` (Vulkan) und `video` (NVENC). Die Variable ersetzt die Defaults, also alles auflisten oder `all`. https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/docker-specialized.html
8. **Flatpak-Grenzen:** Kein SteamVR-Lighthouse-Treiber. Steam-Flatpak braucht `flatpak override` fuer die WiVRn-Pfade (README).

### 7.2 Unity-Editor in Distrobox (nur zur Einordnung)

- Unity unterstuetzt offiziell Ubuntu 22.04 und 24.04 (GNOME unter X11 oder Wayland). Wayland ist experimentell: 22.04 nur AMD, 24.04 AMD und NVIDIA ab 550. Praktisch laeuft Unity ueber XWayland. https://docs.unity.com/en-us/engine/6000.6/manual/get-started/install-and-upgrade/getting-started-installing-unity/system-requirements , https://discussions.unity.com/t/curious-what-the-current-state-of-wayland-support-for-the-linux-unity-editor/1696530
- Distrobox teilt standardmaessig HOME, /dev, X11/Wayland, Audio, `XDG_RUNTIME_DIR` und das Host-Netzwerk. https://distrobox.it/usage/distrobox-create/ , https://wiki.alpinelinux.org/wiki/Using_Distrobox_For_VR_Gaming
- Ein Bericht (Bazzite, NVIDIA, `distrobox create --nvidia`) zeigt ein leeres Editor-Fenster, ungeloest. Ein positiver Bericht fehlt. https://universal-blue.discourse.group/t/running-unityhub-unity-editor/11406
- Fuer die User-Entscheidung (Editor auf dem Host) ist das nur Hintergrund und kein Risiko.

### 7.3 Layout nach User-Entscheidung

```
HOST (Ubuntu 26.04, Wayland oder X11)
  apt:  libopenxr-loader1 (1.1.47), libopenxr-dev, libvulkan1, mesa-vulkan-drivers, vulkan-tools,
        libopenxr-utils (hello_xr), adb oder android-sdk-platform-tools-common (udev),
        avahi-daemon, pipewire (Standard), monado-service + libopenxr1-monado (nur Testlauf)
  PPA:  WiVRn (lvra/wivrn) - Verfuegbarkeit fuer resolute vorab pruefen
  Firewall: 5353/udp, 9757/tcp+udp
  Unity-Hub + Editor 6000.x direkt auf dem Host, Android-Modul unter ~/Unity
  Eigener Provider: nutzt System-Loader, Runtime via active_runtime.json oder XR_RUNTIME_JSON

CONTAINER (nur Plugin-Build)
  Basis ubuntu:26.04, damit glibc und libstdc++ zur Host-Laufzeit passen
  Toolchain: build-essential, cmake, libopenxr-dev, libvulkan-dev, glslang-tools o.ae.
  Ausgabe: .so ins Projekt (Assets/Plugins/...), per bind mount aus HOME
  Kein GPU-, Socket- oder Netzwerkzugriff noetig
```

- Der Build-Container ist rein ein Compiler-Container. Es genuegt `podman run --rm -v "$PWD":/src ubuntu:26.04 ...` oder eine Distrobox.
- Die gebaute Plugin-Bibliothek wird im Unity-Prozess auf dem Host geladen. Daher gilt die ABI-Regel: Container-glibc darf nicht neuer sein als die Host-glibc. Mit 26.04 auf beiden Seiten ist das erfuellt. Bei einem aelteren Container-Image ist es ebenfalls sicher.
- Alternative fuer maximale Kompatibilitaet: gegen eine aeltere glibc bauen, wie es WiVRn mit seiner Toolchain tut (glibc 2.35).

### 7.4 Nicht empfohlen: WiVRn-Server im Podman-Container

Waere nur noetig, wenn der Host sauber bleiben muss. Es braucht dann `--network host`, `--device /dev/dri`, `--group-add keep-groups`, `--security-opt label=disable` (SELinux), eingebundenes `$XDG_RUNTIME_DIR` (oder `wivrn/` plus `pipewire-0`), den D-Bus-System-Socket des Hosts (Avahi) und bei NVIDIA CDI (`--device nvidia.com/gpu=all`, `NVIDIA_DRIVER_CAPABILITIES=all`). Zusaetzlich muesste die Client-`.so` samt Manifest auf einen Host-Pfad exportiert werden. Das ist genau die Komplexitaet, die der WiVRn-Flatpak bereits loest. Mit der PPA-Entscheidung entfaellt dieser Weg.

### 7.5 Wayland und X11

- Editor: XWayland als Standard, nativ-Wayland experimentell (24.04).
- WiVRn: kein Fenster beteiligt. Monado-Compositor: Fenster ueber XCB oder Wayland, `XRT_COMPOSITOR_NULL` ohne Fenster.
- Der XR-Pfad (Vulkan-Swapchain) ist davon unabhaengig.

## 8. Debug-Modell: Windows mit Quest Link versus Linux

**Windows mit Quest Link:**

1. Link installiert eine OpenXR-Runtime und kann sie als aktive Runtime setzen (Registry `HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime`).
2. Unitys gebuendelter `openxr_loader.dll` laedt diese Runtime. Die Editor-Einstellung "Play Mode OpenXR Runtime" (System Default, Oculus, SteamVR, WMR, Other = JSON) ueberschreibt das pro Sitzung.
3. Link streamt per USB oder Air Link auf die Quest.
4. Die App ist der Editor-Prozess: normale Konsole, Breakpoints im Editor, Game-View-Spiegelung.

**Linux-Aequivalent:**

1. Loader und XR-Provider, die im Linux-Editor funktionieren (eigener Provider, Referenz Stridemann-Shim).
2. Streaming-Runtime (WiVRn).
3. Runtime-Auswahl per `active_runtime.json` oder `XR_RUNTIME_JSON`.
4. WiVRn-Client-App auf dem Headset.

Das Editor-Debugging ist dann identisch zu Windows, weil der Editor-Prozess die App ist. Der Android-Pfad (Abschnitt 5) ist ein separater Prozess auf der Quest mit logcat, Debugger und Profiler.

## 9. Ranking

| Rang | Ansatz | Ohne SteamVR/Link | Aufwand | Risiko | Anmerkung |
|---|---|---|---|---|---|
| 1 | WiVRn (PPA, 26.04) + eigener nativer Provider (OpenXR+Vulkan) + System-Loader 1.1.47, Editor auf dem Host | Ja | Hoch (eigener Provider) | Mittel | Entscheidung des Users. Passt, Container nur fuer den Build. |
| 2 | Monado simulated/qwerty mit demselben Provider, ohne Headset | Ja | Gering | Gering | Absicherung fuer Rang 1. Mit `hello_xr` als Gegenprobe. |
| 3 | Build and Run auf die Quest mit logcat, Debugger, Profiler | Ja | Gering | Gering | Dauerhafte Rueckfalllinie, langsame Iteration. |
| 4 | Stridemann-Shim + WiVRn (statt eigener Provider) | Ja | Mittel | Mittel | Schneller Weg zur Gegenprobe, fragil bei Unity-Updates. |
| 5 | ALVR oder Steam Link mit SteamVR plus Shim | Nein (SteamVR) | Gering | Gering | Nur als Gegenprobe. |
| 6 | NVIDIA CloudXR 6 + CloudXR.js | Ja | Hoch | Hoch | Nur NVIDIA RTX, proprietaer, kein Linux-Audio. Plan C. |
| 7 | Eigener Stack (Render Streaming, WebXR, WiVRn-Client-Fork) | Ja | Sehr hoch | Hoch | Nicht empfohlen. |
| - | Meta Link, Meta XR Simulator, Quest Developer Hub, Virtual Desktop | - | - | - | Unter Linux nicht verfuegbar. |

### Offene Punkte (alle UNVERIFIED)

- WiVRn-PPA fuer Ubuntu 26.04 (resolute) vorhanden und passend zur Client-App-Version?
- Welche Erweiterungen und Interaktionsprofile erwartet WiVRn vom Provider? Unbekannte "required" Extensions werden abgelehnt.
- Multi-GPU: Unity-Vulkan-Device und WiVRn-Encoder muessen dieselbe GPU nutzen (`XRT_COMPOSITOR_FORCE_GPU_INDEX`, `XRT_COMPOSITOR_FORCE_CLIENT_GPU_INDEX`).
- Verhalten des Editors, wenn WiVRn bei Trennung `active_runtime.json` zuruecksetzt (Abhilfe: `XR_RUNTIME_JSON` fest setzen).
- Timeline-Semaphoren im Vulkan-Device des Providers explizit aktivieren.

## 10. Quellen

- Unity OpenXR unter Linux: https://discussions.unity.com/t/linux-support-with-openxr/916945 , https://discussions.unity.com/t/openxr-support-for-linux/1693991 , https://discussions.unity.com/t/openxr-linux-support/909788 , https://discussions.unity.com/t/openxr-doesnt-work-for-dedicated-server-build/950594
- Stridemann-Shim: https://github.com/Stridemann/Unity-XR-on-Linux-for-Meta-Quest
- ALVR: https://github.com/alvr-org/ALVR , https://github.com/alvr-org/ALVR/wiki/Installation-guide , https://vronlinux.org/docs/steamvr/alvr/
- Monado: https://monado.freedesktop.org/getting-started.html , https://monado.pages.freedesktop.org/monado/group__drv__qwerty.html , https://monado.pages.freedesktop.org/monado/group__drv__simulated.html , https://monado.pages.freedesktop.org/monado/howto-remote-driver.html , https://gitlab.freedesktop.org/monado/monado/merge_requests/714 , https://developer.blender.org/docs/features/gpu/tools/monado/
- Ubuntu-Archiv: https://archive.ubuntu.com/ubuntu/dists/{noble,plucky,questing,resolute}/universe/binary-amd64/Packages.gz
- WiVRn: https://github.com/WiVRn/WiVRn , https://github.com/flathub/io.github.wivrn.wivrn , https://vronlinux.org/docs/fossvr/wivrn/ , https://github.com/WiVRn/WiVRn/releases
- Envision: https://vronlinux.org/docs/fossvr/envision/
- Meta: https://developers.meta.com/horizon/documentation/unity/unity-req/ , https://developers.meta.com/horizon/blog/meta-xr-simulator-pc-mac-unity-unreal-developer-mixed-reality/ , https://developers.meta.com/horizon/documentation/native/android/ts-mqdh-getting-started/
- Steam/Virtual Desktop: https://www.gamingonlinux.com/2026/09/steamvr-2-17-arrives-ready-to-go-for-the-steam-frame/ , https://vronlinux.org/docs/hardware/ , https://www.vrdesktop.net/
- CloudXR: https://docs.nvidia.com/cloudxr-sdk/latest/overview/overview.html , https://developer.nvidia.com/blog/stream-high-fidelity-spatial-computing-content-to-any-device-with-nvidia-cloudxr-6-0/
- Unity-Doku: https://docs.unity3d.com/Packages/com.unity.renderstreaming@3.1/manual/index.html , https://docs.unity3d.com/6000.0/Documentation/Manual/android-debugging-on-an-android-device.html , https://docs.unity3d.com/Packages/com.unity.mobile.android-logcat@1.4/manual/index.html , https://docs.unity.com/en-us/engine/6000.6/manual/get-started/install-and-upgrade/getting-started-installing-unity/system-requirements
- Container: https://distrobox.it/usage/distrobox-create/ , https://wiki.alpinelinux.org/wiki/Using_Distrobox_For_VR_Gaming , https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/docker-specialized.html , https://universal-blue.discourse.group/t/running-unityhub-unity-editor/11406
