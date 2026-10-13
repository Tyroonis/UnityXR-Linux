# 01 – WiVRn: Technische Recherche (Unity-Editor-Play unter Linux auf Standalone-Headset)

Stand der Recherche: 2026-10-12. Quellcode: Shallow-Clone von `master` (HEAD `bc3e374b`, 2026-10-11), nur gelesen, nie gebaut oder ausgeführt. Dateipfade unten sind relativ zum WiVRn-Repo (https://github.com/WiVRn/WiVRn).

Einschränkung: `gitlab.freedesktop.org` (Monado) war über den Proxy gesperrt. Der gepinnte Monado-Commit (`monado-rev` = `09741cbcb45236f4f4f79790ea133cd90d68d5eb`) konnte nicht gelesen werden. Aussagen zu Monado-Interna stammen aus WiVRns Patches/CMake und den öffentlichen Monado-Docs (main) und sind mit „(abgeleitet)“ markiert.

> ## Relevanz für unser Setup
> **Setup:** Meta Quest 3, AMD-GPU (VAAPI bzw. Vulkan-Encode über Mesa), Ubuntu 26.04 GNOME Wayland, Unity 6 URP, WiVRn per PPA `ppa:lvra/wivrn`.
>
> - **Paket passt:** Das PPA liefert laut Launchpad-API Builds für Serie `resolute` (26.04), neueste Version `26.9-0ubuntu2` (2026-09-18). Installation: `sudo add-apt-repository ppa:lvra/wivrn && sudo apt update && sudo apt install wivrn-dashboard`. Alternativ Flatpak (`io.github.wivrn.wivrn`).
> - **Wayland ist für den Server irrelevant:** Der Server nutzt weder X11, Wayland noch DRM-Lease. Er rendert offscreen per Vulkan und kodiert auf der GPU.
> - **Unity muss Vulkan nutzen** (nicht OpenGL/GLX), dann entfällt die ganze GL-vs-Wayland-Problematik. OpenGL-Apps hatten mit WiVRn `XR_ERROR_GRAPHICS_DEVICE_INVALID`-Probleme (#526, #787).
> - **AMD-Encoding:** Default-Reihenfolge `vulkan` → `vaapi` → `x264`. Der Vulkan-Encoder braucht `videoMaintenance1`. Die Debian-/PPA-Builds bauen laut `debian/rules` Vulkan-Encode **aus** (`-DWIVRN_USE_VULKAN_ENCODE=OFF`), das PPA nutzt also vermutlich VAAPI (h264/h265/av1 über ffmpeg + Mesa-VA-Treiber). Das Flatpak hat Vulkan-Encode an. Quest 3 kann AV1 dekodieren (Allgemeinwissen).
> - **Quest 3:** Profil `meta-quest-touch-plus`, Panel-Override 2064 px; die neue Option „USB networking“ (26.9) ist für Quest 3 vorgesehen und braucht weder ADB noch Entwicklermodus.
> - **Größtes Risiko liegt bei Unity, nicht bei WiVRn:** Das Unity-OpenXR-Plugin hat offiziell **keinen Linux-Support** (weder Editor noch Player). Es gibt nur einen Community-Proof-of-Concept (Stridemann), getestet mit SteamVR+ALVR, nie mit WiVRn. Ob Unity-Vulkan-OpenXR gegen `libopenxr_wivrn.so` läuft, ist unbelegt und muss getestet werden.
> - **URP-Hinweis:** `XR_KHR_composition_layer_depth` ist im WiVRn-Build aus (`server/CMakeLists.txt:52`); `XR_FB_passthrough`-Layer werden nicht gerendert. Beides sollte in Unity nicht als zwingend aktiviert sein.
> - **Workflow:** Server starten → Headset aufsetzen und verbinden → erst dann in Unity Play drücken (Runtime wird erst bei Headset-Verbindung aktiv, außer `--early-active-runtime`).

---

## 0. Kurzfassung

- WiVRn ist eine Monado-basierte OpenXR-Runtime mit Streaming-Server (GPL-3.0). Aktuelle Version v26.9 (2026-09-12), nahezu tägliche Commits, ca. 1,8k Sterne, ca. 111 offene Issues.
- Apps laden die Runtime über den normalen OpenXR-Loader: `active_runtime.json` (Symlink) → `openxr_wivrn.json` → `libopenxr_wivrn.so`; IPC per Unix-Socket `$XDG_RUNTIME_DIR/wivrn/comp_ipc`.
- Unity-OpenXR-Plugin: kein offizieller Linux-Support; Workaround nur über Community-PoC.
- Ubuntu 24.04 wäre problematisch (Quellbau nicht machbar, PPA ohne noble). Für 26.04 (unser Fall) passt das PPA.

---

## 1. Architektur

### 1.1 Komponenten

| Teil | Beschreibung | Fundstelle |
|---|---|---|
| `wivrn-server` | Daemon: Pairing, mDNS, TCP-Listener (Port 9757), forkt bei Headset-Verbindung eine Monado-Instanz, setzt aktive OpenXR/OpenVR-Runtime | `server/main.cpp`, `server/CMakeLists.txt` |
| Monado (vendored) | Kein Submodule. `FetchContent` holt `https://gitlab.freedesktop.org/monado/monado.git` beim SHA aus `monado-rev`, danach 12 Patches aus `patches/monado/*.patch` | `CMakeLists.txt`, `monado-rev`, `patches/apply.sh` |
| OpenXR-Client-Lib | `libopenxr_wivrn.so` (Monado-Target `openxr_monado`, Suffix `wivrn` via `XRT_OXR_RUNTIME_SUFFIX`) und `libmonado_wivrn.so`; installiert nach `<libdir>/wivrn/` (Namen abgeleitet aus Monado-Konvention) | `server/CMakeLists.txt:87-89` und Block `WIVRN_BUILD_SERVER_LIBRARY` |
| Manifest | `share/openxr/1/openxr_wivrn.json`, Vorlage `server/dist/openxr_manifest.in.json` (`"name": "WiVRn"`, `library_path`); Standard `WIVRN_OPENXR_MANIFEST_TYPE=relative` → `../../../<libdir>/wivrn/…` | `server/CMakeLists.txt`, `server/dist/openxr_manifest.in.json` |
| Dashboard | Qt6/Kirigami-GUI `wivrn-dashboard`, spricht per D-Bus (`io.github.wivrn.Server`) mit dem Server | `dashboard/`, `dbus/io.github.wivrn.Server.xml` |
| `wivrnctl` | CLI: `pair`, `unpair`, `rename`, `list-paired`, `stop-server`, `disconnect`, `tab` | `tools/wivrnctl/main.cpp:337-379` |
| Headset-Client | Android-Native-APK (`org.meumeu.wivrn[...]`), selbst eine OpenXR/Vulkan-App, Dekodierung per MediaCodec | `client/`, `AndroidManifest.xml` |
| Linux-Client | nur Debug, kein Audio, keine HW-Dekodierung | README |

Relevante Monado-Patches (`patches/monado/`):
- `0002-Use-extern-socket-fd.patch`: Monados IPC-Server nutzt den von wivrn-server vorab erzeugten Listen-Socket.
- `0003`: Environment-Blend-Mode-Auswahl (sobald ein sichtbarer Client opaque nutzt → opaque).
- `0006`: sendet `XrEventDataInteractionProfileChanged`.
- `0007-don-t-verify-GL-stuff.patch` (Jan 2026): entfernt die Prüfungen `glxFBConfig != NULL` und `visualid != 0` bei `XrGraphicsBindingOpenGLXlibKHR`.

### 1.2 Wie die App die Runtime findet

`server/active_runtime.cpp`:
- Bei Headset-Verbindung (`start_server()` → `runtime_setter.emplace()` in `server/main.cpp`) legt die Klasse `active_runtime` einen **Symlink** `$XDG_CONFIG_HOME/openxr/1/active_runtime[.<abi>].json` → Manifest an. Eine vorhandene Datei wird nach `*.wivrn-backup` verschoben (`backup_and_symlink`).
- Manifest-Ort (`manifest_path()`): im Flatpak der **Host-Pfad** aus `.flatpak-info` (`app-path`) + `share/openxr/1/openxr_wivrn.json`; sonst `<exe>/../openxr_wivrn-dev.json` im Build-Verzeichnis; sonst `WIVRN_INSTALL_PREFIX/share/openxr/1/openxr_wivrn*.json`.
- Schreibt außerdem `~/.config/openvr/openvrpaths.vrpath` (xrizer/OpenComposite; für Unity-OpenXR irrelevant).
- Beim Trennen/Beenden (`update_fsm()` → `runtime_setter.reset()`, Destruktor) wird der Symlink entfernt und das Backup zurückgestellt; veraltete Symlinks räumt `cleanup_openxr()` beim Start auf.
- Server-Flags (`server/main.cpp` ab ca. Zeile 1025): `--no-manage-active-runtime`, `--early-active-runtime` („forcibly manages the active runtime even if no headset present“), `--no-fork`, `--no-publish-service`, `--no-encrypt`, `-f <config>`.
- Der Standard-OpenXR-Loader-Lookup (`XR_RUNTIME_JSON`, dann `$XDG_CONFIG_HOME/openxr/1/active_runtime.json`, dann `/etc/xdg/openxr/1`, `/etc/openxr/1`) funktioniert unverändert. `docs/debugging.md` nennt `XR_RUNTIME_JSON` als manuelle Überschreibung. Für den Unity-Prozess ist `XR_RUNTIME_JSON=<Pfad zu openxr_wivrn.json>` eine Alternative zum Symlink (der Server bedient Apps trotzdem nur bei verbundenem Headset).

### 1.3 IPC App ↔ Server

- Unix-Stream-Socket `u_file_get_path_in_runtime_dir("wivrn/comp_ipc")`, also **`$XDG_RUNTIME_DIR/wivrn/comp_ipc`** (z. B. `/run/user/1000/wivrn/comp_ipc`); erzeugt in `create_listen_socket()` (`server/main.cpp:90-140`). PID-Datei-Name `wivrn.pid`.
- Der Listen-Socket gehört wivrn-server und wird erst bei Headset-Verbindung an den geforkten Monado-Prozess übergeben (Patch 0002). Ohne Headset wird kein Client bedient.
- Monado-IPC reicht Shared Memory und FDs (Swapchain-Images, Semaphoren) durch. App und Server müssen daher **auf demselben Rechner und derselben GPU** laufen (siehe Issue #1119, Hybrid-GPU-Crash). `XRT_COMPOSITOR_FORCE_GPU_INDEX` wählt die Server-GPU (`server/utils/wivrn_vk_bundle.cpp:30`); Standard bevorzugt diskrete vor integrierter GPU.
- Mehrere Apps gleichzeitig möglich (`IPC_MAX_CLIENTS`); Apps können wiederholt starten/stoppen, während das Headset verbunden bleibt (Launcher im Headset dazwischen). Für Play/Stop-Zyklen im Editor plausibel, von mir nicht getestet.
- Headset ↔ Server: eigenes Netzwerkprotokoll (Abschnitt 3).

---

## 2. Grafik-APIs und OpenXR-Oberfläche

Quellen: Monado-Extensionliste (main) https://monado.pages.freedesktop.org/monado/oxr__extension__support_8h_source.html, WiVRn-CMake/Patches/Issues. Der gepinnte Monado-Stand war nicht lesbar.

### 2.1 Grafik

- **Vulkan:** `XR_KHR_vulkan_enable`, `XR_KHR_vulkan_enable2`, `XR_KHR_vulkan_swapchain_format_list`. Erstklassiger Pfad, den WiVRn selbst überall nutzt. **Für Unity Vulkan verwenden.**
- **OpenGL:** `XR_KHR_opengl_enable` (Xlib/GLX-Binding) und `XR_MNDX_egl_enable` (EGL); `XR_KHR_opengl_es_enable` ebenfalls in Monado. Ein Wayland-spezifisches GL-Binding steht in Monados Header-Guards nicht. In der Praxis fragil:
  - #526 (Blender VR Scene Inspection, 2025-10) und #787 (Godot, 2026-02-13, v26.2.1) scheitern mit `XR_ERROR_GRAPHICS_DEVICE_INVALID` bei `xrCreateSession`; #787 ist geschlossen (Vulkan funktionierte). Patch 0007 ist die wahrscheinliche Entschärfung.
  - Blender-Issue 119901 (https://projects.blender.org/blender/blender/issues/119901): derselbe Fehler unter Wayland+NVIDIA mit reinem Monado, unter X11 ok.
  - #1119 (offen, 2026-09): Hybrid-GPU-Laptop (Intel+NVIDIA) stürzt beim Binden an einen OpenGL-Kontext ab.
- **D3D:** nur Windows-Builds, nicht relevant.
- Headset-Client ist reines Vulkan.
- Server-Compositor-Layer (`server/compositor/layer_squasher.cpp`): Projection (mit/ohne Depth), Quad, Cylinder, Equirect2. **Passthrough-, Cube- und Equirect1-Layer sind „unsupported layer type“** (Zeilen ca. 536-541). `XR_FB_passthrough`-Layer von Apps werden also nicht dargestellt; Passthrough gibt es nur als Alpha-Blend-Environment-Blend-Mode, wenn das Headset es meldet (`server/driver/wivrn_hmd.cpp:112-114`).

### 2.2 Extensions und Interaktionsprofile (`server/CMakeLists.txt:33-52`)

AN: `XRT_FEATURE_OPENXR_BODY_TRACKING_FB`, `FACE_TRACKING_ANDROID`, `FACE_TRACKING2_FB`, `FACIAL_TRACKING_HTC`, `HAND_TRACKING_EXT`, `INTERACTION_EXT_HAND` (XR_EXT_hand_interaction), `INTERACTION_EXT_PALM_POSE`, `INTERACTION_BYTEDANCE`, `INTERACTION_VIVE_FOCUS3`, `INTERACTION_TOUCH_PRO`, `INTERACTION_TOUCH_PLUS`, `LAYER_COLOR_SCALE_BIAS`, `OPENXR_ACTIVE_ACTION_SET_PRIORITY`.

**`XRT_FEATURE_OPENXR_LAYER_DEPTH` ist explizit AUS** (`server/CMakeLists.txt:52`). `XR_KHR_composition_layer_depth` wird daher vermutlich nicht angeboten, obwohl der Squasher Depth-Images kennt. Eine Unity-Einstellung „Depth Submission“ greift dann nicht; zum Laufen nicht erforderlich.

Weitere Monado-Defaults (abgeleitet): `XR_EXT_eye_gaze_interaction`, `XR_FB_display_refresh_rate`, `XR_KHR_visibility_mask`, `XR_EXT_local_floor`, `XR_KHR_binding_modification`, `XR_EXT_dpad_binding`, `XR_FB_composition_layer_*`.

Interaktionsprofile werden **dynamisch vom Headset** gemeldet (`common/wivrn_packets.h:185-214`, `tracking::interaction_profiles`): khr/simple, ext/hand_interaction, oculus/touch-Varianten, meta/touch_pro, meta/touch_plus, Pico (ByteDance), HTC Vive/Cosmos/Focus3, HP MR, Microsoft, ML2, Valve Index/Frame, Samsung Odyssey, YVR u. a. Monados Binding-System bildet die von der App vorgeschlagenen Bindings (z. B. Unitys `oculus/touch_controller`) auf das aktive Profil ab. Quest 3/3S → `meta-quest-touch-plus` (`client/hmd_traits.cpp:180-205`).

Tracking-Features hängen von den Berechtigungen der **Headset-App** ab:
- Hand-Tracking: `/user/hand/*`-Geräte (EXT_hand_interaction) werden immer angelegt (`server/driver/wivrn_session.cpp`); Gelenkdaten brauchen die Berechtigung im Headset.
- Eye-Gaze, Face (Android/FB2/HTC), Body: Geräte nur, wenn das Headset sie meldet. Umgebungsvariable `WIVRN_FORCE_EXTENSIONS` (Substring-Liste, z. B. `EXT_eye_gaze_interaction`, `ANDROID_face_tracking`, `FB_face_tracking2`, `HTC_facial_tracking`) erzwingt sie (`server/driver/wivrn_session.cpp:75-82`). Body-Tracking hat keinen Force-Schalter.
- Refresh-Rate: `XR_FB_display_refresh_rate` (`compositor::request_display_refresh_rate`).
- Gamepad: `/user/gamepad` (Xbox-Profil) immer vorhanden, inaktiv bis ein Gamepad vom Headset weitergeleitet wird (`hid-forwarding`).
- Zusätzlich: Audio (virtuelle PipeWire-Senke „WiVRn“ und Quelle „WiVRn(microphone)“, braucht PipeWire), Lighthouse- und SlimeVR/SolarXR-Treiber, Eye-Tracked-Foveated-Encoding.

---

## 3. Transport

| Punkt | Wert | Quelle |
|---|---|---|
| Port | **9757 TCP + UDP** (Standard; `"port"` in config.json) | `common/wivrn_config.h.in`, `docs/configuration.md` |
| Steuerkanal | TCP (TCP_NODELAY), PIN-Pairing, X25519, Stream verschlüsselt (AES-128-CTR auf UDP). Protokoll-Hash von Server und Client muss identisch sein, sonst „Incompatible protocol version“ | `common/wivrn_sockets.cpp`, `server/driver/wivrn_connection.cpp:169`, `common/protocol_version.h` |
| Video/Tracking | UDP auf derselben Portnummer; `tcp-only: true` erzwingt alles über TCP (mehr Latenz) | `docs/configuration.md` |
| Discovery | mDNS/Avahi, Service-Typ **`_wivrn._tcp`**, TXT-Keys `protocol`, `version`, `cookie`. Server publiziert via avahi-daemon; Headset hat eigenen mDNS-Client | `server/main.cpp` (~430), `client/wivrn_discover.cpp:676` |
| Firewall | 5353/udp und 9757/tcp+udp öffnen; firewalld-Dienstdatei `server/dist/firewalld-wivrn.xml`; README-Beispiel: `ufw allow 5353/udp` und `ufw allow 9757` | README |
| Avahi | Ohne laufenden avahi-daemon beendet sich der Server (`cannot_connect_to_avahi`), außer `publish-service: null` bzw. `--no-publish-service`; dann IP manuell im Headset eingeben | `server/main.cpp` (~1075) |

### Kabelgebunden (USB)

1. **ADB-Reverse-Tunnel** (braucht Entwicklermodus + USB-Debugging am Quest). Das Dashboard („Connect via USB“) führt aus (`dashboard/adb.cpp:311-326`):
   - `adb -s <serial> reverse tcp:9757 tcp:9757`
   - `adb -s <serial> shell am start -a android.intent.action.VIEW -d "wivrn+tcp://127.0.0.1:9757" <pkg>` (mit Pairing-PIN: `wivrn+tcp://:<PIN>@127.0.0.1:9757`)
   - Manuell laut README: `adb reverse tcp:9757 tcp:9757` und `adb shell am start -a android.intent.action.VIEW -d "wivrn+tcp://localhost" org.meumeu.wivrn`.
   - Paketnamen: `org.meumeu.wivrn` (Meta Store), `org.meumeu.wivrn.github` (GitHub-Release), `.github.nighly`, `.github.testing`, `.local`.
   - Nur TCP über den Tunnel. README: WLAN am Headset ausschalten, wenn per USB verbunden. Kabel mit mindestens 5 Gb/s verwenden (vronlinux.org).
2. **Natives „USB networking“** (neu in 26.9; Flathub-Changelog „USB networking on Meta headsets“): Schalter in den Headset-Einstellungen, fordert per Android-ConnectivityManager ein `TRANSPORT_USB`-Netz an. Beschreibungstext: „Enables connection by USB without ADB or developer mode. Make sure the server allows IPv6 link-local only connections.“ Angezeigt nur, wenn `hmd_traits.usb_net` (`client/hmd_traits.cpp:201,216`): Quest 3 („eureka“) oder Horizon OS ≥ 2.6 (`ro.hzos.build.display_name`). Discovery läuft über IPv6-Link-Local-mDNS (`client/application.cpp:1912`, `client/scenes/gui_settings.cpp:764`). Für unsere Quest 3 also relevant.
3. `docs/debugging.md`, Abschnitt „USB Tunneling“, wiederholt Variante 1.

### Latenz / WLAN

Das Repo enthält keine Latenzzahlen. README-Troubleshooting bei hoher Motion-Latenz, schwarzen Rändern, Audio-/Bildfehlern: WLAN bei USB abschalten, Einstellungen zurücksetzen, Software-Encoding testen, Bitrate senken, Auflösung in der WiVRn-App senken, „USB oder besseren WLAN-Router“. Der Hinweis zu NVIDIA gilt für uns (AMD) nicht.

Architekturgründe für niedrige Latenz: Kopfpose wird am Headset gesampelt/reprojiziert, jedes Auge hat einen eigenen Encoder-Stream, Pacing-Parameter `WIVRN_CLIENT_MARGIN_MS` und `U_PACING_APP_MIN_MARGIN_MS`, der Client hält Wi-Fi- und Multicast-Lock (`client/wifi_lock.cpp`).

Allgemeine WLAN-Empfehlung (kein Repo-Beleg, übliches Wissen): PC per Ethernet an einen dedizierten 5/6-GHz-Access-Point im selben Raum, kein Mesh, kein VPN.

---

## 4. Video-Encoder (`server/encoder/`, `server/encoder/encoder_settings.cpp`)

Auto-Auswahl (`prober::select_encoder`, ca. Zeilen 235-280): `vulkan` (wenn verfügbar) → `nvenc` (nur NVIDIA, Vendor 0x10DE) → `vaapi` → `x264`. Standard-Codec: bester von av1 > h265 > h264, den Headset und Encoder gemeinsam unterstützen (`docs/configuration.md`). Jedes Auge und der Alpha-Kanal sind eigene Encoder-Streams (max. 3; `WIVRN_MAX_VULKAN_ENCODERS`, Standard 3). Bit-Tiefe standardmäßig 10 Bit, wo Encoder und Decoder es können (h265/av1).

| Backend | Umsetzung | Codecs | Voraussetzungen |
|---|---|---|---|
| `vulkan` | Vulkan Video Encode im Prozess (`video_encoder_vulkan*.cpp`) | h264, h265 (AV1 „not implemented“, `encoder_settings.cpp` ~213) | Treiber mit `VK_KHR_video_encode_h264/h265` auf einer Encode-Queue **und Feature `videoMaintenance1`**, sonst Log „Cannot use vulkan video encode without VideoMaintenance1“. Debian-Build: AUS; Flatpak: AN |
| `nvenc` | **Direkte NVENC-API** (nicht ffmpeg), CUDA-External-Memory-Interop (`external/ffnvcodec`) | h264, h265, av1 | NVIDIA-Proprietärtreiber mit CUDA/NVENC; P2-State-Problem, siehe `docs/nvenc.md` |
| `vaapi` | **ffmpeg** (`h264_vaapi`, `hevc_vaapi`, `av1_vaapi`) mit DRM-PRIME-dmabufs (`server/encoder/ffmpeg/video_encoder_va.cpp`) | h264, h265, av1 | VA-API-Treiber (Mesa `mesa-va-drivers` für AMD), Zugriff auf `/dev/dri/renderD*` (Gerät aus der DRM-Render-Minor der Vulkan-GPU; überschreibbar mit `"device": "/dev/dri/renderD128"`). Der Server setzt selbst `AMD_DEBUG=lowlatencyenc` und `INTEL_DEBUG=noccs` (`server/main.cpp:258-262`) |
| `x264` | libx264, Software | nur h264 | CPU-lastig |
| `raw` | unkomprimiert (Debug) | raw | sehr hohe Bandbreite |

Config (`~/.config/wivrn/config.json`; im Flatpak `~/.var/app/io.github.wivrn.wivrn/config/wivrn/config.json`; zusätzlich `/usr/share/wivrn/config.json`, `/etc/wivrn/config.json`; spätere Dateien überschreiben Top-Level-Werte). Beispiel für unser Setup: `{"encoder":{"encoder":"vaapi","codec":"h265"}}`. Weitere Schlüssel: `bit-depth`, `application`, `tcp-only`, `publish-service`, `openvr-compat-path`, `hid-forwarding`, `port`, `hostname`, `debug-gui`.

Hinweise: H.264/H.265 sind patentbehaftet, bei Ubuntu im PPA/Flatpak unproblematisch. Zu RADV: Issue #831 (Alpha-Artefakt rechtes Auge auf AMD 890M, Workaround `RADV_DEBUG=nodcc`) und #846 (blockiges Bild, RX 9060, 26.2.3).

---

## 5. Braucht der Server Display/Compositor/DRM-Lease?

**Nein.** Belege:
- `server/CMakeLists.txt` deaktiviert alle Monado-Hardwaretreiber und baut `comp_multi` plus WiVRns eigenen Compositor; kein `comp_main`/`comp_target`-Fenster, `XRT_FEATURE_WINDOW_PEEK OFF`, SDL2 aus (außer Debug-GUI). `wivrn::compositor` (`server/compositor/compositor.cpp`) rendert Layer per Compute-Shader (`layer.comp`, `foveation.comp`) in Offscreen-Images und füttert die Encoder. Kein Present-Pfad, kein `VK_KHR_display`/`VK_EXT_acquire_drm_display`, kein Wayland-/xcb-Surface-Code in `server/`.
- Die Debian-Build-Deps nennen libwayland/libx11-xcb/libxrandr nur, weil das vendorte Monado mit diesen optionalen Komponenten kompiliert wird.
- Was der Server zur Laufzeit braucht: Vulkan-GPU, **User-D-Bus-Session-Bus** (exportiert `io.github.wivrn.Server`, libnotify für die PIN; `server/main.cpp` ~910), **avahi-daemon**, `XDG_RUNTIME_DIR`, optional PipeWire (Audio) und systemd-User-Manager (App-Start über transiente Units `wivrn-application-*.service`, `server/start_application.cpp:236`), außerdem die Encoder-Bibliotheken aus Abschnitt 4.
- Das Debian-`postinst` setzt `setcap CAP_SYS_NICE=+ep /usr/bin/wivrn-server` (Prioritäten); im Flatpak nicht möglich.
- Wayland vs. X11: für den Server identisch. Es betrifft nur das Dashboard (Qt, beides funktioniert) und OpenGL-Apps (2.1). Mit Vulkan gibt es keine WSI-Kopplung zwischen OpenXR-Swapchains und dem Desktop (Export als dmabuf/fd).
- Bei Hybrid-GPU-Systemen müssen Unity und wivrn-server dieselbe GPU nutzen (#1119).

---

## 6. Paketierung und Installation (Ubuntu)

### 6.1 Optionen

| Weg | Status |
|---|---|
| **PPA `ppa:lvra/wivrn`** | Laut Launchpad-API (https://api.launchpad.net/devel/~lvra/+archive/ubuntu/wivrn?ws.op=getPublishedSources): 5 Uploads, **alle Serie `resolute` (26.04)**, neueste `26.9-0ubuntu2` (2026-09-18); keine noble/jammy/plucky-Uploads sichtbar. Installation lt. vronlinux.org: `sudo apt install software-properties-common`, `sudo add-apt-repository ppa:lvra/wivrn`, `sudo apt update`, `sudo apt install wivrn-dashboard` (zieht `wivrn-server`) |
| **Flathub `io.github.wivrn.wivrn`** | Alle Distros; v26.9, x86_64+aarch64, ca. 11 MiB, Runtime `org.kde.Platform//6.11`, bündelt xrizer und OpenComposite. Release-Asset `wivrn-server.flatpak` |
| .deb | Nur CI-Artefakte (`.github/workflows/Build.yml`, Job `build-ubuntu`, Container `ubuntu:plucky`); `debian/`-Verzeichnis im Repo |
| AppImage | nicht gefunden |
| Ubuntu-Archiv | Quellpaket `wivrn` nicht vorhanden (Launchpad-Abfrage lieferte 0 Treffer) |
| Andere | AUR, Fedora, Gentoo Guru, NixOS (README) |

### 6.2 Wie eine Nicht-Flatpak-App (Unity über Unity Hub) die Flatpak-Runtime nutzt

Belegt im Quellcode:
1. Das Flatpak hat `--filesystem=xdg-config/openxr:create` und `xdg-run/wivrn:create` (`flatpak/io.github.wivrn.wivrn.yml.in`, `finish-args`). Bei Headset-Verbindung schreibt der Server den Symlink `~/.config/openxr/1/active_runtime.json` auf den **Host-Pfad des Flatpak-Deploy-Verzeichnisses** (`app-path` aus `.flatpak-info`, z. B. `/var/lib/flatpak/app/io.github.wivrn.wivrn/x86_64/stable/<commit>/files/share/openxr/1/openxr_wivrn.json`, bei `--user` unter `~/.local/share/flatpak/app/...`). Das Manifest verweist relativ auf `files/lib/wivrn/libopenxr_wivrn.so`.
2. Der OpenXR-Loader eines Host-Prozesses (Unity, hello_xr) folgt dem Symlink und lädt die `.so` direkt aus dem Flatpak-Deploy-Verzeichnis. Der Pfad enthält den Commit-Hash und ändert sich bei Updates; der Symlink wird bei jeder Verbindung neu geschrieben.
3. **ABI:** Das Flatpak-Modul `wivrn-library` wird mit einer eigenen crosstool-NG-Toolchain gebaut (GCC 11.5, **glibc 2.35**, Linux-5.15-Header; `flatpak/ct-ng-x86_64.config`, `-Wl,--warn-unresolved-symbols`), damit die `.so` in Host-Prozessen ab glibc 2.35 ladbar ist (Ubuntu 22.04: 2.35, 24.04: 2.39, 26.04 neuer). Die genaue DT_NEEDED-Liste habe ich nicht geprüft (nicht gebaut).
4. Der IPC-Socket liegt unter `$XDG_RUNTIME_DIR/wivrn/comp_ipc` und ist auf dem Host sichtbar.
5. Gegenfall: Flatpak-**Apps** (z. B. Flatpak-Steam) brauchen explizite Overrides (README „Steam Flatpak“). Unity aus Unity Hub (.deb/AppImage) ist nicht sandboxed, braucht also keine. Eine Snap/Flatpak-Unity-Installation wäre problematisch.
6. Flatpak-Eigenheiten: kein `setcap`, Lighthouse/uinput eingeschränkt, `adb` wird über `escape_sandbox` am Host aufgerufen (`dashboard/escape_sandbox.h`, `--talk-name=org.freedesktop.Flatpak`), der Host braucht `adb` (`android-tools-adb`); die NVIDIA-GL-Erweiterung müsste zum Treiber passen (für uns nicht relevant).

Für unser Setup (PPA, native Installation) entfällt 6.2 weitgehend: `active_runtime.json` zeigt auf `/usr/share/openxr/1/openxr_wivrn.json`, die Bibliothek liegt unter `/usr/lib/<arch>/wivrn/`.

### 6.3 Quellbau – Abhängigkeiten (`CMakeLists.txt`, `server/CMakeLists.txt`, `debian/control`, `Build.yml`)

Harte Versionsgrenzen:
- CMake ≥ 3.28.
- **Vulkan-Header/Loader ≥ 1.4.304** (`FATAL_ERROR "Vulkan version must be at least 1.4.304"`). Ubuntu vulkan-loader: noble 1.3.275 (zu alt), plucky 1.4.304, questing 1.4.321, resolute 1.4.341 (Launchpad-API).
- C++23 (`CMAKE_CXX_STANDARD 23`); Code nutzt `std::ranges::enumerate_view`/`zip_view`/`ranges::contains` (`server/utils/wivrn_vk_bundle.cpp`, `server/active_runtime.cpp`) → neue libstdc++ (GCC ~15, abgeleitet).
- Boost ≥ 1.75 (Server), ≥ 1.84 (Client), OpenSSL, Eigen3, nlohmann_json, CLI11, glib2/gio-unix, avahi-client + avahi-glib, libnotify, librsvg-2.0, libarchive, libpng, gdbus-codegen, glslang, Python3, git.
- Encoder: libavcodec + libavutil + libdrm (VAAPI), x264, libva; ffnvcodec-Header sind vendored (`external/ffnvcodec`).
- PipeWire (`libpipewire-0.3`), libsystemd/libelogind (wivrnctl), libcap, libudev; X11-/Wayland-/GL-Dev-Header für Monado (libwayland, libx11-xcb, libxrandr, libxcb-randr, mesa-common/libgl/libglu); libcjson, libbsd, libswscale, libavfilter (`debian/control`).
- Dashboard: Qt6 (Core Quick Test Gui QuickControls2 Widgets Network), ECM ≥ 5.115, KF6 (Kirigami I18n CoreAddons QQC2DesktopStyle IconThemes), QCoro6, qml6-module-org-kde-kirigamiaddons-formcard.
- Zur Konfigurationszeit per FetchContent (Netzwerk nötig): Monado (gitlab, in manchen Netzen gesperrt), Boost 1.89 (bei `WIVRN_USE_SYSTEM_BOOST=OFF`), spdlog, stb, imgui u. a.
- Client-APK: Android SDK/NDK, JDK 17, gradle, cmake 3.31.5 (SDK), perl für OpenSSL, `ktx`, `rsvg-convert`, glslang (`docs/building.md`).
- Minimalbefehle (`docs/building.md`): `cmake -B build-server . -GNinja -DWIVRN_BUILD_CLIENT=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-server`; für das Dashboard zusätzlich `-DWIVRN_BUILD_DASHBOARD=ON`. Schalter: `WIVRN_USE_NVENC`, `WIVRN_USE_VAAPI`, `WIVRN_USE_VULKAN_ENCODE`, `WIVRN_USE_X264`, `WIVRN_USE_PIPEWIRE`, `WIVRN_FEATURE_STEAMVR_LIGHTHOUSE`, `WIVRN_BUILD_SERVER_LIBRARY`, `WIVRN_OPENXR_MANIFEST_TYPE={relative,absolute,filename}`.

Fazit: Unter 26.04 PPA verwenden; Quellbau nur nötig, wenn man Vulkan-Encode oder Patches braucht. Unter 24.04 wäre das Flatpak der praktikable Weg.

---

## 7. Client-APK

- **Meta Horizon Store:** App-ID `7959676140827574` (https://www.meta.com/experiences/7959676140827574/). Der Dashboard-Wizard nennt Quest 2, 3, 3S und Pro. Die Store-Version kann dem Server **hinterherhinken**; das Dashboard warnt dann („A newer release is available … the client app on the Meta store may not be compatible“, `dashboard/qml/WizardPage.qml:164`).
- **Sideload:** Das Dashboard lädt `WiVRn-release.apk` passend zur Server-Version von den GitHub-Releases (Nightlies aus `WiVRn/WiVRn-APK`) und führt `adb -s <serial> install -r` aus (`dashboard/apk_installer.cpp:212`). Release-v26.9-Assets: `WiVRn-release.apk`, `wivrn-server.flatpak`, `server-build-deps.tar.xz`. Manuell: `adb install WiVRn-release.apk`. Außerhalb des Stores installiert erscheint die App unter „Unbekannte Quellen“.
- **Entwicklermodus:** nur für ADB-Wege nötig (Sideload, `adb reverse`); Wizard verlinkt https://developers.meta.com/horizon/documentation/native/android/mobile-device-setup/#enable-developer-mode. Store-Installation über WLAN und der „USB networking“-Schalter brauchen ihn nicht. (Entwicklermodus bei Meta erfordert ein Entwickler-Organisationskonto und den Schalter in der Meta-Handy-App.)
- **Strikte Versionsgleichheit:** Protokoll-Hash über die serialisierten Pakettypen (`common/protocol_version.h`, `protocol_revision = 1` in `common/wivrn_packets.h:43`). README: „The VR client and PC server need to be on the same version of WiVRn.“
- Erster Start fragt Mikrofon-/Hand-/Eye-/Face-/Body-Tracking-Berechtigungen ab (später in den Einstellungen nachholbar).
- Pairing: 6-stellige PIN auf der Konsole und als Desktop-Benachrichtigung (`server/main.cpp` ~590); `wivrnctl pair` oder Dashboard aktiviert den Pairing-Modus. Danach wird der öffentliche Schlüssel gespeichert (keine PIN mehr nötig). `--no-encrypt` schaltet Verschlüsselung ab (Debug).
- Android-Anforderungen: Vulkan-Hardware ≥ 1.0 (`AndroidManifest.xml`), OpenXR-System-Service.

---

## 8. Bekannte Probleme (Unity, Godot, andere Engines)

GitHub-Issue-Suche (`api.github.com/search/issues?q=repo:WiVRn/WiVRn+…`): **Kein Issue oder PR befasst sich mit Unity-Editor, Play Mode oder dem Unity-OpenXR-Plugin.** Unity taucht nur auf in:
- **#1153** (offen, 2026-10-03, NVIDIA): „swapchain images freed while GPU work still in flight (causes Xid 31 MMU fault)“. Auslöser: Bonelab (Unity) legt bei Render-Scale-/MSAA-Wechsel Texturen neu an; `client_vk_swapchain_destroy` gibt VkImages sofort frei. Relevant, falls Unity Swapchains zur Laufzeit neu erzeugt (Reporter betrifft NVIDIA).
- **#1104** (geschlossen, „not planned“, 2026-09-12): Unity-Spiel über **OpenVR** + xrizer paniert bei `ERROR_SESSION_LOST`; Spiel läuft weiter und kehrt nicht ins WiVRn-Menü zurück (nicht der Unity-OpenXR-Pfad).
- **#894** (geschlossen): xrizer/OpenComposite-Crash, Log zeigt `application_name: 'Unity Application'`.

Godot und andere:
- **#787:** OpenGL-Streaming aus Godot nach v26.2.1 defekt (`XR_ERROR_GRAPHICS_DEVICE_INVALID`), behoben; Vulkan funktionierte.
- **#95:** `XR_SESSION_STATE_READY` fehlte serverseitig (Godots `session_begun` feuerte nicht), behoben (2024).
- **#396/#238:** `XR_EXT_hand_interaction` weitergeleitet, getestet mit Godots Hand-Tracking-Demo.
- **#526:** Blender VR Scene Inspection (OpenGL) scheitert mit GRAPHICS_DEVICE_INVALID.
- **#957** (geschlossen): Vulkan-`hello_xr` scheitert mit `vk_create_image_from_native size mismatch` auf Quest 3S / RX 7800 XT / Ubuntu-Flatpak, OpenGL funktionierte; später behoben. Zeigt die Fragilität beim Swapchain-Import.
- **#1119** (offen): Hybrid-GPU + OpenGL-Crash.
- **#282** (offen): ruckelnde Kopf-/Controllerposition bei großen Bewegungen.
- **#846:** blockiges Bild bei AMD RX 9060 (26.2.3).

Einordnung: Die Runtime wird vor allem von nativen OpenXR-Apps, Godot, Blender und Proton/xrizer-Spielen genutzt. Vulkan ist die sicherere API. Offene Unity-Fragen: Vulkan-Extension-Anforderungen (`xrGetVulkanDeviceExtensionsKHR`), Timeline-Semaphore-Bedarf (Stridemanns Repo muss `VK_KHR_timeline_semaphore` für SteamVR per Layer erzwingen; für WiVRn/Monado unbekannt), Swapchain-Formate (sRGB/linear, Depth), `XR_KHR_android_create_instance` (Stridemann strippt es per API-Layer), fehlende Depth-Layer-Extension, Meta-spezifische Extensions.

**Unity-seitiger Blocker unabhängig von WiVRn:**
- Unity Discussions „Linux support with OpenXR“ (https://discussions.unity.com/t/linux-support-with-openxr/916945) und „OpenXR Linux Support“ (https://discussions.unity.com/t/openxr-linux-support/909788): Das Plugin verweigert Nicht-Windows-Standalone-Targets („The only standalone target supported is Windows x64 [and OSX] with OpenXR“). Unity-Mitarbeiter SamZ_XR schrieb im Mai 2023, man „untersuche“; danach keine Staff-Antworten; Nutzerbeschwerden bis Sept 2025. Verwandte Threads (Unity-Tags 6.2/6.3, Dez 2025 bis Sep 2026) existieren, Inhalt war nicht lesbar.
- Community-Workaround https://github.com/Stridemann/Unity-XR-on-Linux-for-Meta-Quest: Unity 6000.3.11f1, `com.unity.xr.openxr` 1.16.1, `com.unity.xr.management` 4.5.4, XRI 3.3.1; nur Vulkan (`-force-vulkan`). Bionic-zu-glibc-Shims für Unitys Android-x86_64-`libUnityOpenXR.so`, OpenXR-API-Layer (`STRIP_ANDROID_XR_LAYER=1`) entfernt `XR_KHR_android_create_instance`, Vulkan-Implicit-Layer erzwingt Timeline-Semaphores, `LD_PRELOAD`-Fake-JVM, angepasste `OpenXRLoader.cs` (`/usr/lib/libopenxr_loader.so` unter `UNITY_EDITOR_LINUX`). Getestet auf Arch mit Quest 3 über SteamVR+ALVR; „as-is, no support“. WiVRn, Monado, Wayland und X11 werden nicht erwähnt. Die Runtime wählt der System-Loader, WiVRn dort einzusetzen ist plausibel, aber unbelegt (abgeleitet).
- Aus dem Gedächtnis, nicht verifiziert: Die Plugin-Einstellung „Play Mode OpenXR Runtime“ kann auf „Custom“ mit Pfad zu einer Runtime-JSON gesetzt werden; unter Linux entspräche das `XR_RUNTIME_JSON`.

---

## 9. Release, Aktivität, Lizenz

- Releases: **v26.9 (2026-09-12)**, v26.6.2 (2026-07-14), v26.6.1 (2026-06-19), v26.6 (2026-06-10), v26.2.3 (2026-02-21). Versionierung YY.M[.patch].
- `master`: letzter Commit 2026-10-11; `pushed_at` 2026-10-12; ca. 1.781 Sterne, 235 Forks, 111 offene Issues (GitHub-Search-API), Repo seit 2022-09-21, Sprache C++.
- Lizenz: **GPL-3.0** (GitHub-SPDX `GPL-3.0`; Dateiheader „GPL v3 or later“; `COPYING`, `LICENSE-OFL-1.1` für Schriften). Monado steht unter BSL-1.0.
- Community: LVRA Discord, Matrix `#linux-vr-adventures:matrix.org`, Raum `wivrn`. Doku-Hub: https://vronlinux.org/docs/fossvr/wivrn/. Maintainer in Issues: xytovl, galister, ImSapphire, Meumeu (Guillaume Meunier), Patrick Nicolas.
- CI: GitHub Actions baut Flatpak, deb (plucky), APK (inkl. Upload in den Meta Store).

---

## 10. Sonstiges

- **Application-Launching:** Der Headset-Launcher listet Steam-VR-Spiele und `.desktop`-Dateien mit `X-WiVRn-VR` in `Categories` (XDG-Datenverzeichnisse). Apps starten als transiente systemd-User-Units. Config `"application": ["/pfad/zum/Spiel"]` (String oder Array) startet beim Verbinden automatisch. Für Unity-Editor-Entwicklung nicht nötig (Editor ist einfach ein weiterer OpenXR-Client).
- **Dashboard:** Erststart-Wizard (APK-Installation per adb, Firewall-Check, Pairing, Steam-Startoptionen, OpenVR-Provider, Server-Logs unter `~/.local/state/wivrn/wivrn-dashboard`), Headset-Statistik. systemd-User-Unit `wivrn.service` (`systemctl --user enable --now wivrn`), von Distro-Paketen installiert (`server/dist/wivrn.service.in`; Socket liegt trotz `PrivateTmp=true` in `XDG_RUNTIME_DIR`).
- **Envision:** Im Repo nicht referenziert (grep); Integration nicht verifiziert.
- **OpenVR:** nur über xrizer/OpenComposite; für Unity-OpenXR irrelevant.
- **Umgebungsvariablen (Server):** `XRT_LOG`, `XRT_COMPOSITOR_LOG`, `XRT_COMPOSITOR_FORCE_GPU_INDEX`, `WIVRN_MAX_VULKAN_ENCODERS`, `WIVRN_FORCE_EXTENSIONS`, `WIVRN_ASSUME_DGPU`, `WIVRN_USE_STEAMVR_LH`, `WIVRN_CLIENT_MARGIN_MS`, `U_PACING_APP_MIN_MARGIN_MS`, `WIVRN_DUMP_VIDEO`, `WIVRN_DUMP`, `FFMPEG_LOG_LEVEL`, `ENABLE_VULKAN_RENDERDOC_CAPTURE`, `WIVRN_TRACING` (+`_FILE`, `_BUFFER_KB`, nur Perfetto-Builds), `PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES` (nur Steam), `LH_DISCOVER_WAIT_MS`. App-seitig: `XR_RUNTIME_JSON`, `OXR_VIEWPORT_SCALE_PERCENTAGE` (Viewport-Skalierung pro App, `docs/debugging.md`), `XDG_RUNTIME_DIR` (Pflicht, sonst findet die App den Socket nicht), `XDG_CONFIG_HOME`. Headset-Client: `WIVRN_AUTOCONNECT`, `WIVRN_LOGLEVEL`.
- **Debugging:** `XRT_LOG=debug XRT_COMPOSITOR_LOG=debug wivrn-server`; Headset-Logs mit `adb logcat '*:F'`.
- **Qualität/Foveation:** Eye-Tracked-Foveated-Encoding, Encoder pro Auge; Bitrate und Render-Scale werden in der Headset-App gesteuert (nicht serverseitig). Panel-Overrides in `client/hmd_traits.cpp`: Quest 3: 2064 px, Quest 2/3S: 1832, Quest Pro: 1800, Quest 1: 1440.

---

## 11. Unsicherheiten und offene Punkte

1. Ob Unitys Vulkan-OpenXR-Pfad (enable2) gegen `libopenxr_wivrn.so` funktioniert: kein Bericht gefunden, Test nötig. Gleiches gilt für Unity-Anforderungen an `XR_KHR_composition_layer_depth` (im Build AUS) und Meta-spezifische Extensions (`XR_FB_passthrough`-Layer nicht unterstützt). Unity-Features sollten nur als optional markierte Extensions verlangen.
2. Exakte DT_NEEDED-Liste von `libopenxr_wivrn.so` (nicht gebaut): ob außer libvulkan/libc weitere Host-Bibliotheken (libGL, libEGL, libX11, libwayland-client) nötig sind.
3. Ob „USB networking“ auf Quest 3 mit aktueller Horizon-OS-Version angeboten wird: Code aktiviert es für Quest 3 („eureka“) und Horizon OS ≥ 2.6.
4. Exakte Extensionliste des gepinnten Monado-Commits (gitlab gesperrt); Monado-main-Doku als Ersatz.
5. Ob das PPA-Paket Vulkan-Encode aktiviert hat: `debian/rules` im Repo setzt es AUS; der tatsächliche PPA-Build wurde nicht geprüft. Auf AMD wäre dann VAAPI aktiv (setzt Mesa-VA-Treiber voraus).
6. Ob der Symlink-`active_runtime.json` mit relativem `library_path` bei jeder Loader-Version korrekt aufgelöst wird (in der Praxis bei WiVRn-Nutzern mit Host-Apps ok; Unitys mitgelieferter Loader ist ein aktueller Khronos-Loader, daher erwartbar unproblematisch).
7. WLAN-Latenzzahlen: keine veröffentlicht; hängt vom Router ab.
8. Envision-Integration: nicht im Repo, nicht verifiziert.

---

## Quellen

- WiVRn-Repo (geklont): https://github.com/WiVRn/WiVRn ; README und `docs/` (`building.md`, `configuration.md`, `debugging.md`, `nvenc.md`, `steamvr.md`)
- GitHub-Releases- und Search-API: https://api.github.com/repos/WiVRn/WiVRn/releases ; https://api.github.com/search/issues?q=repo:WiVRn/WiVRn+unity (analog `+Godot`, `+OpenGL`, `+XR_ERROR_GRAPHICS_DEVICE_INVALID`)
- Flathub: https://flathub.org/apps/io.github.wivrn.wivrn
- Launchpad-PPA-API: https://api.launchpad.net/devel/~lvra/+archive/ubuntu/wivrn?ws.op=getPublishedSources ; Ubuntu vulkan-loader-Versionen: https://api.launchpad.net/devel/ubuntu/+archive/primary?ws.op=getPublishedSources&source_name=vulkan-loader&exact_match=true&status=Published
- LVRA-Wiki: https://vronlinux.org/docs/fossvr/wivrn/
- Monado-Extensionliste: https://monado.pages.freedesktop.org/monado/oxr__extension__support_8h_source.html
- Blender/Monado Wayland-GL: https://projects.blender.org/blender/blender/issues/119901
- Unity-Linux-OpenXR-Threads: https://discussions.unity.com/t/linux-support-with-openxr/916945 , https://discussions.unity.com/t/openxr-linux-support/909788
- Unity-auf-Linux-PoC: https://github.com/Stridemann/Unity-XR-on-Linux-for-Meta-Quest
- Release-Artikel: https://www.gamingonlinux.com/2026/02/wireless-vr-streaming-levels-up-on-linux-with-the-latest-wivrn-release
