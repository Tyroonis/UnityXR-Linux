# UnityVR-Linux – Projektplan

Stand: 2026-10-12 · Status: Phase 0 – Skripte geliefert, WiVRn als Flatpak installiert, `hello_xr`-Abnahme nicht bestätigt · Phase 1 – nativer Teil fertig, Unity-Test ausstehend (siehe [PHASE1.md](PHASE1.md)) · Autor: Tyroonis (Planung mit Claude)

Ziel in einem Satz: In Unity 6 unter Ubuntu 26.04 auf **Play** drücken, und das Spiel läuft auf der Meta Quest 3 (WLAN oder USB), die Game View im Editor zeigt das Bild, und Debug.Log, Breakpoints, Profiler und Inspector funktionieren wie unter Windows mit Quest Link. Ohne SteamVR, ohne Meta Link.

Dieses Dokument ist das Hauptdokument. Begründungen einzelner Entscheidungen stehen kurz in [ENTSCHEIDUNGEN.md](ENTSCHEIDUNGEN.md), die Rohdaten in `docs/research/`. Bauen, Testen und Unity-Einbindung von Phase 1 stehen in [PHASE1.md](PHASE1.md):

- [PHASE1.md](PHASE1.md) – Phase 1: was fertig ist, Bauen, Test ohne Unity, Unity-Einbindung, Fehlerbehebung
- [01-wivrn.md](research/01-wivrn.md) – WiVRn: Architektur, Transport, Encoder, Paketierung
- [02-unity-openxr-linux.md](research/02-unity-openxr-linux.md) – Unitys OpenXR-Paket unter Linux, Community-Hack, XR-SDK-Header
- [03-alternativen-und-container.md](research/03-alternativen-und-container.md) – Alternativen, Monado-Testtreiber, Container

Konvention: **„zu verifizieren"** bedeutet, dass die Aussage aus Recherche oder Schlussfolgerung stammt und noch nicht auf dem Zielrechner getestet wurde. Die Recherche lief in einer Sandbox ohne Unity, GPU und Headset; nichts davon wurde ausgeführt. Der Code aus Phase 1 wurde in derselben Sandbox gegen headless Monado getestet, **nicht** mit WiVRn/Quest und **nicht** in Unity (Details und Stand in [PHASE1.md](PHASE1.md)).

---

## Inhalt

1. [Ausgangslage und Zielbild](#1-ausgangslage-und-zielbild)
2. [Kernproblem](#2-kernproblem)
3. [Strategie in Kürze](#3-strategie-in-kürze)
4. [Was wir nutzen, was wir bauen](#4-was-wir-nutzen-was-wir-bauen)
5. [Architektur](#5-architektur)
6. [Technisches Design des Providers „LinuxXR"](#6-technisches-design-des-providers-linuxxr)
7. [Phasen und Meilensteine](#7-phasen-und-meilensteine)
8. [Risiken und Gegenmaßnahmen](#8-risiken-und-gegenmaßnahmen)
9. [Netzwerk-Empfehlungen](#9-netzwerk-empfehlungen)
10. [Debug-Werkzeuge](#10-debug-werkzeuge)
11. [Ordnerstruktur](#11-ordnerstruktur)
12. [Arbeitsteilung und Modellwahl](#12-arbeitsteilung-und-modellwahl)
13. [Offene Fragen an Tyroonis](#13-offene-fragen-an-tyroonis)
14. [Quellen](#14-quellen)

---

## 1. Ausgangslage und Zielbild

| Punkt | Wert |
|---|---|
| Headset | Meta Quest 3 (WiVRn-Profil `meta-quest-touch-plus`, Panel-Override 2064 px) |
| GPU | AMD Radeon RX 7600, Mesa/RADV. Encoding: Vulkan Video (WiVRn-Flatpak), VAAPI als Alternative |
| OS | Ubuntu 26.04.1 „resolute", Kernel 7.0, glibc 2.43, GNOME unter Wayland (X11 muss ebenfalls laufen) |
| Engine | Unity 6 (6000.6.5f1, mit Android-Modul), Universal Render Pipeline; Testprojekt „XRTestbed" (frisches URP-Projekt) |
| Grafik-API | Vulkan (einzige unterstützte Linux-API für XR in diesem Projekt) |
| XR-API | OpenXR (Loader `libopenxr-loader1` 1.1.47 aus dem Ubuntu-Archiv) |
| Streaming | WiVRn als Flatpak `io.github.wivrn.wivrn` (bringt xrizer und OpenComposite mit, aktualisiert sich selbst); Alternative PPA `ppa:lvra/wivrn` (Version 26.9 laut Recherche), siehe ADR 2 |
| Editor | direkt auf dem Host, kein Container |
| Netz | PC hängt per USB-WLAN-Stick im Netz. Empfehlung: LAN-Kabel oder USB zur Quest (Abschnitt 9) |
| Einschränkung | möglichst nur Ubuntu-Pakete (WiVRn als Flatpak) |

**Zielbild (Definition of Done des Gesamtprojekts):**

1. Server starten, Quest-Client verbinden, Unity-Editor öffnen, **Play** drücken: Bild auf der Quest, Kopf- und Controller-Tracking, Game View gespiegelt.
2. Play-Stop-Play beliebig oft, ohne Editor-Neustart und ohne WiVRn-Neustart.
3. Editor startet auch ohne verbundenes Headset und ohne laufenden WiVRn-Server.
4. Gleicher Spielcode für Editor-Streaming und Android-Build (OpenXR + Meta Quest Support auf dem Android-Tab).
5. Läuft unter GNOME Wayland (Editor via XWayland) und unter X11.

---

## 2. Kernproblem

Unitys offizielles Paket `com.unity.xr.openxr` hat **keine Linux-Unterstützung**. Belege aus dem Paketinhalt (Recherche 02, Abschnitt 2):

- Keine Linux-Binärdatei: nur `UnityOpenXR.dll` (Windows x64), `UnityOpenXR.dylib` (macOS) und `libUnityOpenXR.so` für Android arm64. Auch kein Linux-`openxr_loader`.
- `OpenXRProjectValidation.cs` enthält eine Regel, die den **Play Mode blockiert**, wenn das aktive Build-Target `StandaloneLinux64` ist („The only standalone targets supported are Windows x64 and OSX with OpenXR").
- `OpenXRLoader.cs` hat in `LoadOpenXRSymbols()` nur Zweige für `UNITY_EDITOR_WIN` und `UNITY_EDITOR_OSX`; `[XRSupportedBuildTarget]` nennt nur `StandaloneWindows64` und Android.
- Unity hat seit Mai 2023 („we're currently investigating") keine Zusage gemacht; die Foren-Threads laufen bis 2026 ohne Staff-Antwort.
- Meta Link, Meta XR Simulator und Quest Developer Hub gibt es nicht für Linux.

Der einzige Community-Weg (Stridemann: Android-x86_64-Binärdatei mit bionic-auf-glibc-Shims, gepatchtem Paket, Vulkan-Layer-Workarounds) ist gepinnt auf `com.unity.xr.openxr` ≤ 1.16.1, weil die Android-x64-Binärdatei ab 1.17.0-pre.2 entfernt wurde. Er wurde nur mit SteamVR + ALVR getestet, nie mit WiVRn oder Monado, und bricht bei jedem Unity-Update potenziell. Wir behandeln ihn als Sackgasse und höchstens als Diagnose-Spike (Phase 0c).

---

## 3. Strategie in Kürze

1. **WiVRn** als Streaming-Runtime nutzen (fertig, gepflegt, headless; als Flatpak installiert, alternativ PPA für 26.04).
2. **Eigenen nativen Unity-XR-Provider „LinuxXR"** schreiben: eine C++-Shared-Library, die Unitys XR-SDK-Provider-Interfaces implementiert und intern den Khronos-OpenXR-Loader benutzt. Damit umgehen wir Unitys OpenXR-Plugin vollständig, inklusive seiner Plattformsperren.
3. **Das Spiel läuft im Editor-Prozess.** Deshalb gelten Debugger, Profiler, Console und Live-Inspector ohne Besonderheiten.
4. **Absicherung:** Headset-lose Tests mit Monado (qwerty/simulated), `hello_xr` als Gegenprobe, Android-„Build and Run" als dauerhafte Rückfalllinie.

Das Risiko liegt vollständig im Provider (Abschnitt 6). Alles andere ist Standardinstallation.

---

## 4. Was wir nutzen, was wir bauen

| Baustein | Rolle | Begründung / Hinweis |
|---|---|---|
| **WiVRn** (Flatpak `io.github.wivrn.wivrn`; Alternative PPA `ppa:lvra/wivrn`) | **nutzen** | Streaming-Server und Quest-Client, Monado-Fork, headless, GPL-3.0. Wir sprechen nur über die OpenXR-Schnittstelle (dynamisches Laden der Runtime durch den Loader), kein Linken gegen WiVRn-Code. |
| **Khronos OpenXR-Loader** (`libopenxr-loader1` 1.1.47) | **nutzen** | Runtime-Auswahl, API-Layer, Standardverhalten. Wird zur Laufzeit per `dlopen` geladen. |
| **Unity-XR-SDK-Header** (aus `ValveSoftware/unity-xr-plugin`, `CommonHeaders/ProviderInterface`) | **nutzen** | Einzige öffentlich greifbare Quelle der Provider-Schnittstellen („Unity XR SDK is no longer available" laut Unity-Doku). Lizenz: Unity Companion License v1.2 (Abschnitt 8, Risiko R8). |
| **Valves OpenVR-Provider** (`Providers/Display/Display.cpp` ~1550 Z., `Input/Input.cpp` ~1080 Z., Linux-CMake) | **Strukturvorlage** | Zeigt Lifecycle-Registrierung, Vulkan-Textur-Zugriff, Input-Gerätedefinitionen und den Linux-Build. Code wird nicht kopiert, sondern als Muster gelesen. |
| **Unity-OpenXR-Paket** (`com.unity.xr.openxr`) | **nur für Android-Build** | Auf dem Android-Tab unverändert (OpenXR + Meta Quest Support). Im Linux-Editor nicht aktiv. |
| **Stridemann-Hack** | **nur Diagnose** | Optionaler Spike (Phase 0c, max. 1 Abend) und Gegenprobe. Keine Basis für das Projekt. |
| **Monado** (`monado-service`, `libopenxr1-monado`) | **nur Test** | Headset-loses Testen (qwerty/simulated). |
| **Khronos-Werkzeuge** (`hello_xr`, `openxr-layer-apidump`, `openxr-layer-corevalidation`) | **nur Test/Debug** | Gegenprobe und Fehlersuche. |
| ALVR | **verworfen** | Erfordert unter Linux zwingend SteamVR, kein OpenXR-nativer Servermodus (Roadmap: „blocked on refactors"). |
| SteamVR / Steam Link VR | **verworfen** | Ausdrücklich ausgeschlossen (Anforderung „ohne SteamVR"). |
| NVIDIA CloudXR 6 | **verworfen** | Nur NVIDIA RTX, proprietär, unter Linux ohne Audio/Mikrofon. Wir haben AMD. |
| Unity Render Streaming / WebXR / WiVRn-Client-Fork | **verworfen** | Kein OpenXR-Pfad bzw. Neuimplementierung von WiVRn. |
| Valves OpenVR-Plugin + xrizer auf WiVRn | **verworfen** | Spekulativ, ungetestet, bindet an SteamVR-Konzepte (Multipass-Crash laut Valve-README). |
| Meta Link / MQDH / Meta XR Simulator | **nicht verfügbar** | Nur Windows/macOS. |
| Container für Unity oder WiVRn | **verworfen** | Siehe ADR 3. Nur optionaler Build-Container für das Plugin. |

---

## 5. Architektur

### 5.1 Gesamtkette

```
 Ubuntu 26.04 Host (GNOME Wayland oder X11)
┌──────────────────────────────────────────────────────────────────────────────┐
│  UNITY EDITOR-PROZESS  (XWayland; Vulkan; Spiel läuft hier, Debugger hängt hier)
│ ┌──────────────────────────────────────────────────────────────────────────┐ │
│ │ C#  (Package com.tyroonis.xr.linuxopenxr, nur #if UNITY_EDITOR_LINUX)    │ │
│ │   LinuxXRLoader : XRLoaderHelper ─► XR Plug-in Management (Standalone)   │ │
│ │   LinuxXRSettings, Editor-Fenster „LinuxXR Status", Projekt-Validierung  │ │
│ └───────────────┬──────────────────────────────────────────────────────────┘ │
│                 │ UnitySubsystemsManifest.json  (displays + inputs)           │
│ ┌───────────────▼──────────────────────────────────────────────────────────┐ │
│ │ Unity XR Subsystems:  Display (IUnityXRDisplay)   Input (IUnityXRInput)  │ │
│ └───────────────┬──────────────────────────────────────────────────────────┘ │
│                 │ XR-SDK-Provider-Interfaces (C ABI)                          │
│ ┌───────────────▼──────────────────────────────────────────────────────────┐ │
│ │ native: libLinuxXR.so  (C++, wir bauen)                                  │ │
│ │   PreInit (IUnityXRPreInit)  Session/Frame-Loop  Swapchains  Actions     │ │
│ │   IUnityGraphicsVulkan  ◄── Unitys VkInstance/VkDevice/VkQueue           │ │
│ └───────────────┬──────────────────────────────────────────────────────────┘ │
│                 │ dlopen + xrGetInstanceProcAddr                              │
│ ┌───────────────▼──────────────────────────────────────────────────────────┐ │
│ │ libopenxr_loader.so.1  (Khronos 1.1.47, Ubuntu-Paket)                    │ │
│ │   Runtime-Wahl: XR_RUNTIME_JSON > ~/.config/openxr/1/active_runtime.json │ │
│ │   optionale API-Layer (apidump, core_validation)                         │ │
│ └───────────────┬──────────────────────────────────────────────────────────┘ │
│                 │                                                            │
│ ┌───────────────▼──────────────────────────────────────────────────────────┐ │
│ │ libopenxr_wivrn.so  (Client-Teil der Runtime, im Unity-Prozess geladen)  │ │
│ └───────────────┬──────────────────────────────────────────────────────────┘ │
└─────────────────┼────────────────────────────────────────────────────────────┘
                  │ Unix-Socket  $XDG_RUNTIME_DIR/wivrn/comp_ipc
                  │ (Shared Memory + fd-Weitergabe: Swapchain-Images, Semaphoren)
┌─────────────────▼────────────────────────────────────────────────────────────┐
│  wivrn-server  (Flatpak, headless: kein Display, kein DRM-Lease)             │
│    Monado-Fork mit Offscreen-Compositor (Layer-Squasher, Compute-Shader)     │
│    Encoder: VAAPI (ffmpeg/Mesa) oder Vulkan Video; ein Stream pro Auge       │
│    avahi (mDNS _wivrn._tcp), D-Bus io.github.wivrn.Server, Port 9757 TCP+UDP │
└─────────────────┬────────────────────────────────────────────────────────────┘
                  │ WLAN (5/6 GHz) oder USB (adb reverse / „USB-Netzwerk")
┌─────────────────▼────────────────────────────────────────────────────────────┐
│  Meta Quest 3:  WiVRn-Client-App (org.meumeu.wivrn), MediaCodec-Decoder,     │
│  Head-/Controller-/Hand-Tracking, Reprojektion am Headset                    │
└──────────────────────────────────────────────────────────────────────────────┘
```

Wichtig: App (Unity) und Server müssen **auf demselben Rechner und derselben GPU** laufen, weil Swapchain-Images als fd zwischen den Prozessen geteilt werden (Recherche 01, Abschnitt 1.3). Bei Single-GPU-AMD ist das unkritisch.

### 5.2 Warum Wayland/X11 egal ist

- Der WiVRn-Server nutzt weder X11 noch Wayland noch DRM-Lease; er rendert offscreen und kodiert auf der GPU.
- Unity läuft als Editor unter GNOME Wayland vermutlich über XWayland (nativ-Wayland ist laut Doku „experimentell"; zu verifizieren).
- Der XR-Pfad (Vulkan-Swapchain der Runtime) hat keine WSI-Kopplung zum Desktop. Nur Unitys eigene Game-View-Darstellung hängt am Fenstersystem. Was ungetestet ist: Unitys Vulkan-Swapchain für die Editor-Fenster zusammen mit unserem Provider unter XWayland vs. X11 – wird in Phase 3 explizit auf beiden getestet.

### 5.3 Zwei getrennte Wege, ein Spielcode

| Weg | Zweck | Plugin-Stack |
|---|---|---|
| **Editor-Streaming (neu)** | Iteration: Play → Quest | Standalone-Tab: XR Plug-in Management → **LinuxXR** → WiVRn |
| **Android-Build (Fallback)** | Test auf dem Gerät, Release | Android-Tab: XR Plug-in Management → **OpenXR + Meta Quest Support** (unverändert) |

Spielcode darf nicht von Plugin-spezifischen APIs abhängen; er nutzt `UnityEngine.XR`, Input System bzw. XR Interaction Toolkit.

---

## 6. Technisches Design des Providers „LinuxXR"

Arbeitsname **LinuxXR**, Paket z. B. `com.tyroonis.xr.linuxopenxr`. Native Bibliothek `libLinuxXR.so`, C++ gegen die öffentlichen Unity-XR-SDK-Header, Khronos-OpenXR-Header und Vulkan-Header.

Grundsatz: Die Header-Signaturen (`IUnityXRDisplay.h`, `IUnityXRInput.h`, `IUnityXRPreInit.h`, `IUnityGraphicsVulkan.h`) sind die maßgebliche Quelle. Wo dieses Dokument Details nennt, die aus der Recherche stammen, sind sie beim Implementieren gegen die Header zu prüfen. Ob die Header in Unity 6.x gegenüber Valves Stand (Oktober 2025) neue Pflichtfelder haben, ist nicht geprüft.

### 6.1 Komponenten (native)

| Datei (geplant, `native/src/`) | Aufgabe |
|---|---|
| `plugin_main.cpp` | `UnityPluginLoad`, `UnityPluginUnload`, `XRSDKPreInit`; holt Interfaces, registriert Provider |
| `preinit.cpp` | `IUnityXRPreInit`-Provider (Abschnitt 6.2) |
| `xr_runtime.cpp/.h` | Lazy-Verbindung zu OpenXR: Loader laden, Instanz, Session, State-Machine, Events |
| `vk_interop.cpp/.h` | Zugriff auf Unitys Vulkan-Objekte über `IUnityGraphicsVulkan`, Geräte-Abgleich |
| `display.cpp` | `IUnityXRDisplay`-Lifecycle und -Provider: Swapchains, Frame-Loop, Views, Mirror |
| `input.cpp` | `IUnityXRInput`-Lifecycle und -Provider: Geräte, Actions, Haptik |
| `settings.cpp` | Projekteinstellungen vom C#-Teil (Runtime-Pfad, Swapchain-Optionen) |
| `log.cpp` | Logging nach Unity (`IUnityXRTrace`) und zusätzlich in Datei |
| `CMakeLists.txt` | Build, Linux-Zweig nach Vorbild Valve (`-DLINUX -DPOSIX`, rpath `$ORIGIN`) |

**Ist-Stand Phase 1.** Die Aufteilung weicht von der Tabelle ab: Unity-unabhängige Logik liegt in `native/src/core/` (`log`, `xr_api` und `vk_api` für `dlopen`, `runtime_select`, `vk_requirements`, `vk_preinit`, `xr_context`, `xr_input`, `xr_math.h`), der Unity-Glue in `native/src/unity/` (`plugin_main`, `unity_log`, `preinit`, `display_provider`, `input_provider`, `native_api`). Dazu kommen `native/tools/probe/` (linuxxr-probe) und `native/tests/`. `vk_interop` und die Display-Logik mit Swapchain-Kopie folgen in Phase 2. Das C#-Paket enthält `Runtime/` (`LinuxXRLoader`, `LinuxXRSettings`, `LinuxXRNative`) und `Editor/` (`LinuxXRPackageMetadata`, `LinuxXRStatusWindow`). Siehe [PHASE1.md](PHASE1.md).

Der OpenXR-Loader wird **nicht gelinkt**, sondern per `dlopen("libopenxr_loader.so.1")` und `xrGetInstanceProcAddr` benutzt. Vorteile: Der Editor startet auch ohne installierten Loader; die Build-Umgebung braucht nur die Header; Loader-Versionen des Hosts sind austauschbar.

### 6.2 a) PreInit – Vulkan-Gerät und Extensions vor dem Editor-Start

`IUnityXRPreInit` wird aufgerufen, **bevor Unity Vulkan initialisiert**. Der Provider liefert dort:

- `GetPreInitFlags`: `kUnityXRPreInitFlagsRequestAdditionalVulkanGraphicsQueue` (eigene Queue für XR-Rendering; „Plugin should check for the additional queue before using it").
- `GetVulkanInstanceExtensions` und `GetVulkanDeviceExtensions`: zweiphasig (erst Größe mit `namesCapacityIn = 0`, dann Puffer füllen); Ausgabe ist ein einziger, durch Leerzeichen getrennter String.
- `GetGraphicsAdapterId`: bei Vulkan ist `rendererData` die `VkInstance`, `adapterId` bekommt den Zeiger auf das zu verwendende `VkPhysicalDevice`.

Die C#-Seite meldet die Bibliothek über `IXRLoaderPreInit.GetPreInitLibraryName(...)`.

**Designproblem:** Die WiVRn-Runtime existiert nur, solange ein Headset verbunden ist (der Server setzt `active_runtime.json` nur dann, und bedient Apps nur dann). Beim Editor-Start ist also u. U. kein `xrCreateInstance` möglich. Der Editor soll aber jederzeit starten können. Lösung in drei Stufen:

1. **Runtime erreichbar → abfragen und cachen.** Kurzlebige OpenXR-Instanz mit `XR_KHR_vulkan_enable` (v1, siehe ADR 10), Extensions über `xrGetVulkanInstanceExtensionsKHR`/`xrGetVulkanDeviceExtensionsKHR` abfragen, Ergebnis cachen. Umgesetzt in Phase 1: Cache-Datei `$XDG_CACHE_HOME/linuxxr/vulkan_requirements.txt` (Schlüssel: Runtime-Manifest) statt im `Library/`-Ordner des Projekts.
2. **Sonst Cache oder feste Liste.** Gibt es einen Cache, wird er genutzt. Andernfalls eine fest einkompilierte, bekannte Liste für Monado/WiVRn, zu verifizieren gegen die tatsächliche Ausgabe (Kandidaten laut Recherche und Vulkan-Interop-Üblichem: `VK_KHR_external_memory_capabilities`, `VK_KHR_external_semaphore_capabilities` auf Instanzebene; `VK_KHR_external_memory`, `VK_KHR_external_memory_fd`, `VK_KHR_external_semaphore`, `VK_KHR_external_semaphore_fd`, `VK_KHR_timeline_semaphore`, `VK_KHR_dedicated_allocation`, `VK_KHR_get_memory_requirements2` auf Geräteebene – **endgültige Liste ist zu verifizieren**, nicht raten; die Abfrage in Stufe 1 liefert sie am Ende verbindlich). Stand Phase 1: Die einkompilierte Basisliste deckte sich in der Sandbox exakt mit Monados Anforderung; für WiVRn ist das noch zu verifizieren. Die Liste wird immer gegen das vom Treiber tatsächlich Unterstützte gefiltert, damit `vkCreateInstance`/`vkCreateDevice` nicht an einer fehlenden Extension scheitern.
3. **Prüfung beim Play.** Beim ersten `xrCreateSession` das von OpenXR gewünschte `VkPhysicalDevice` (`xrGetVulkanGraphicsDeviceKHR` bzw. `xrGetVulkanGraphicsDevice2KHR`) per Geräte-UUID mit Unitys Gerät vergleichen. Stimmt es nicht überein: klare Fehlermeldung im Status-Fenster („Unity rendert auf GPU A, WiVRn auf GPU B – `XRT_COMPOSITOR_FORCE_GPU_INDEX` setzen"). Bei Single-GPU-AMD ist das unkritisch.

Auch `GetGraphicsAdapterId` braucht kein laufendes Headset: Es genügt, das physische Gerät zu wählen, das zur GPU des WiVRn-Servers passt (Standard: das einzige/diskrete Gerät; Override per Einstellung oder Umgebungsvariable).

**Designfrage: Wer erzeugt `VkInstance`/`VkDevice`?** (Entscheidung: Variante A, siehe ADR 10; Stand Phase 1 umgesetzt, mit WiVRn zu verifizieren)

- *Variante A (gewählt):* Unity erzeugt Instanz und Gerät selbst, mit den im PreInit gemeldeten Extensions. Wir fragen Anforderungen und Gerät über die Funktionen von `XR_KHR_vulkan_enable` (v1) ab und binden mit `XrGraphicsBindingVulkanKHR`; Abfrage und Binding gehören damit zur selben Extension-Version. Funktioniert ohne Headset beim Editor-Start. In der Sandbox lief das mit Monado bis `FOCUSED` (`linuxxr-probe run`); ob WiVRn es genauso akzeptiert, ist **zu verifizieren**.
- *Variante B:* `IUnityGraphicsVulkan::InterceptInitialization` ersetzt Unitys `vkCreateInstance`/`vkCreateDevice` durch `xrCreateVulkanInstanceKHR`/`xrCreateVulkanDeviceKHR` (so macht es Unitys eigenes Plugin; dessen Fehlermeldungen prüfen genau, dass `VkDevice` und `VkPhysicalDevice` übereinstimmen). Nachteil: Eine OpenXR-Instanz müsste schon beim Editor-Start existieren, also Headset vorher verbunden – das widerspricht dem Zielbild.

Entscheidung vorläufig: A; B nur als Rückfall, wenn WiVRn A nicht akzeptiert (dann mit „Headset zuerst verbinden"-Einschränkung).

### 6.3 b) OpenXR-Instanz und Session – erst bei Play (lazy)

Beim Editor-Start wird **nichts** an OpenXR gebunden außer PreInit. Instanz und Session entstehen erst, wenn das Display-Subsystem startet (Play).

1. **Runtime wählen.** Projekteinstellung „Runtime": *Auto* (Reihenfolge: WiVRn-Manifest – PPA `/usr/share/openxr/1/openxr_wivrn*.json`, Flatpak `~/.local/share/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn.json` bzw. `/var/lib/flatpak/app/…` – > `~/.config/openxr/1/active_runtime.json` > Monado `/usr/share/openxr/1/openxr_monado.json`; Symlinks von `active_runtime.json` werden aufgelöst) oder expliziter Pfad. Das Ergebnis wird per `setenv("XR_RUNTIME_JSON", …)` **vor** `xrCreateInstance` gesetzt. Ein fester Pfad macht uns unabhängig davon, ob WiVRn den `active_runtime.json`-Symlink gerade gesetzt hat (er tut das nur bei verbundenem Headset). Bedienen wird der Server die App trotzdem nur bei verbundenem Headset – das Status-Fenster zeigt das an.
2. **`xrCreateInstance`** mit `XR_KHR_vulkan_enable` (v1, Pflicht; ADR 10). `XR_KHR_vulkan_enable2` wäre nur für Variante B nötig. Optionale Extensions nur anfordern, wenn die Runtime sie meldet (`xrEnumerateInstanceExtensionProperties`): `XR_EXT_hand_tracking`, `XR_FB_display_refresh_rate`, `XR_EXT_local_floor`, später `XR_KHR_visibility_mask`. **Keine** Extension als zwingend voraussetzen, die WiVRn nicht anbietet (insbesondere nicht `XR_KHR_composition_layer_depth`, im WiVRn-Build aus; keine `XR_FB_passthrough`-Layer, werden nicht gerendert).
3. **`xrGetSystem`** (`XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY`), View-Konfiguration `PRIMARY_STEREO`, empfohlene Auflösung und Sample-Count lesen.
4. **`xrGetVulkanGraphicsRequirementsKHR`** (v1; bei Variante B die `2KHR`-Variante): Versionsprüfung gegen Unitys Vulkan-Version.
5. **Geräteabgleich** (6.2, Punkt 3).
6. **`xrCreateSession`** mit `XrGraphicsBindingVulkanKHR` (v1) aus `IUnityGraphicsVulkan::Instance()` (Instance, PhysicalDevice, Device, QueueFamilyIndex, QueueIndex der zusätzlichen Queue). Queue-Familie/-Index müssen der Queue entsprechen, die wir tatsächlich für OpenXR-Aufrufe nutzen.
7. **Reference Spaces:** `LOCAL`, `LOCAL_FLOOR` (falls `XR_EXT_local_floor` oder Core-Equivalent), `STAGE`, `VIEW`.
8. **Event-Schleife** (`xrPollEvent`) und Session-State-Machine: `IDLE → READY` (`xrBeginSession`, `PRIMARY_STEREO`) `→ SYNCHRONIZED → VISIBLE → FOCUSED`; `STOPPING` (`xrEndSession`) `→ IDLE`; `EXITING`/`LOSS_PENDING` → sauber abbauen und neu verbinden. Weitere Events: `INTERACTION_PROFILE_CHANGED`, `REFERENCE_SPACE_CHANGE_PENDING`, `DISPLAY_REFRESH_RATE_CHANGED`.
9. **Play-Stop:** `xrEndSession`, Swapchains, Spaces, Actions, Session und Instanz in umgekehrter Reihenfolge zerstören; keine globalen Zustände, die einen Domain Reload überleben müssen (Abschnitt 6.8). Erneutes Play erzeugt alles neu.
10. **Wiederverbinden:** Bricht die Headset-Verbindung ab (`SESSION_LOSS_PENDING`/`INSTANCE_LOSS_PENDING`), meldet das Status-Fenster „Verbindung verloren"; beim nächsten Play (oder automatisch mit Backoff, wenn eingestellt) wird neu aufgebaut.

### 6.4 c) Display-Subsystem (`IUnityXRDisplay`)

Registrierung: `RegisterLifecycleProvider` mit der ID aus `UnitySubsystemsManifest.json` (z. B. `"LinuxXR Display"`), danach `RegisterProvider` und `RegisterProviderForGraphicsThread` (Callback-Zuordnung zu den Threads: **zu verifizieren** gegen den Header, siehe Threading unten).

**Swapchains.**
- `xrEnumerateSwapchainFormats`; Auswahl in Reihenfolge der Präferenz (zu verifizieren, was WiVRn meldet): sRGB-Format `VK_FORMAT_R8G8B8A8_SRGB` bei Linear-Farbraum, sonst `VK_FORMAT_R8G8B8A8_UNORM`; `VK_KHR_swapchain_format_list` nutzt WiVRn selbst (Recherche 01, 2.1).
- Bevorzugt **eine Array-Swapchain** mit `arraySize = 2` für Single-Pass-Instanced (Multiview) in URP; Fallback **Multi-Pass** mit zwei Swapchains. Welche Variante Unitys Vulkan-Pfad mit dem XR-SDK stabil unterstützt, ist **zu verifizieren**; Valves README warnt für Linux/Vulkan vor Multi-Pass-Crashes bei OpenVR – das ist OpenVR-spezifisch, aber ein Hinweis, früh zu testen.
- Die Swapchain-`VkImage`s (`xrEnumerateSwapchainImages` → `XrSwapchainImageVulkanKHR`) werden Unity als **native Render-Texturen** bekannt gemacht (`CreateTexture` mit nativem Zeiger in `UnityXRRenderTextureDesc`; genaue Struktur gegen `IUnityXRDisplay.h` prüfen). **Vorschlag ADR 9 (ersetzt dieses Direkt-Rendering, falls angenommen):** Unity alloziert die Render-Texturen selbst, das Plugin kopiert in `SubmitCurrentFrame` per `vkCmdCopyImage`/Blit in das erworbene Swapchain-Bild (Zugriff über `IUnityGraphicsVulkan::AccessTexture` und `AccessQueue` mit `flush`); Kosten eine GPU-Kopie pro Frame, geschätzt unter 1 ms. Pro Swapchain-Image eine Unity-Textur; pro Frame wird die aktuell erworbene (`xrAcquireSwapchainImage` + `xrWaitSwapchainImage`) in `PopulateNextFrameDesc` als Ziel des Render-Passes eingetragen, nach dem Rendern `xrReleaseSwapchainImage` (vor `xrEndFrame`).
- Tiefe: Unity legt Depth selbst an. **Kein Depth-Layer** an OpenXR übergeben (WiVRn bietet `XR_KHR_composition_layer_depth` nicht an); Einstellung „Depth Submission" gibt es bei uns nicht.
- Auflösung: aus `recommendedImageRectWidth/Height`, skalierbar über Einstellung „Render Scale" (zusätzlich lässt sich WiVRn selbst per `OXR_VIEWPORT_SCALE_PERCENTAGE` begrenzen).

**Frame-Loop.**
- `PopulateNextFrameDesc` (Unity fragt „was soll als Nächstes gerendert werden"): `xrWaitFrame` (liefert `predictedDisplayTime`, `predictedDisplayPeriod`), `xrBeginFrame`, `xrLocateViews` für die vorhergesagte Zeit; Views, Posen und FOV in `UnityXRNextFrameDesc` eintragen; Swapchain-Image erwerben.
- `SubmitCurrentFrame` (Unity hat gerendert): Swapchain-Images freigeben, `xrEndFrame` mit einem **Projection-Layer** (`XrCompositionLayerProjection`, zwei `XrCompositionLayerProjectionView` mit Pose und FOV aus `xrLocateViews`, `imageRect` und `imageArrayIndex`). `environmentBlendMode = OPAQUE` (später `ALPHA_BLEND` für Passthrough, Phase 6). Ist `shouldRender == false`, Frame ohne Layer beenden.
- **Views/FOV:** pro Auge `XrFovf` (asymmetrisch). Projektionsmatrix aus den vier Winkeln; Unity erwartet je nach Header entweder Matrix oder FOV-Tangenten (gegen Header prüfen). Nah-/Fernebene kommen von Unity.
- **Pose-Konvention:** OpenXR (rechtshändig, +Y oben, −Z vorn) → Unity (linkshändig, +Y oben, +Z vorn): Z-Achse spiegeln, Quaternionen entsprechend umrechnen. Zentrale Hilfsfunktionen in `xr_math.h`, mit Unit-Tests (Phase 3).
- **Mirror/Game View:** `QueryMirrorViewBlitDesc` und `BlitToMirrorViewRenderTarget` implementieren; Standard: linkes Auge, optional beide Augen nebeneinander. Falls eine Option für ein anderes Mirror-Verfahren nötig wird (Kosten sparen), als Einstellung anbieten.
- **Refresh-Rate:** wenn `XR_FB_display_refresh_rate` vorhanden: `xrEnumerateDisplayRefreshRatesFB` und `xrRequestDisplayRefreshRateFB` (72/90/120 Hz, Einstellung); sonst die von der Runtime vorgegebene Rate (`predictedDisplayPeriod`).
- **Threading:** XR-SDK-Callbacks laufen teils im Main-Thread, teils im Render-Thread; OpenXR-Frame-Funktionen (`xrWaitFrame`/`xrBeginFrame`/`xrEndFrame`) müssen pro Session konsistent und nicht gleichzeitig aufgerufen werden. Entwurf: ein interner Frame-Zustand mit Mutex; `xrWaitFrame/xrBeginFrame` in der von Unity vorgesehenen „Frame-Vorbereitung", `xrEndFrame` im Graphics-Thread-Callback `SubmitCurrentFrame`. Die genaue Zuordnung der Callbacks zu Threads ist **zu verifizieren** (Valves `Display.cpp` ist hier das Vorbild).
- **Vulkan-Synchronisation:** Zugriff auf Images, die die Runtime besitzt: Image-Layout-Übergänge entsprechend den Vorgaben der Runtime (laut OpenXR-Vulkan-Spec: Layout `COLOR_ATTACHMENT_OPTIMAL` bei Freigabe); zusätzlicher Queue-Ownership-Transfer, falls die zusätzliche Queue eine andere Familie hat. Timeline-Semaphore müssen im Vulkan-Device aktiv sein (Monado kennt sie; Stridemanns Layer erzwingt sie für SteamVR, für WiVRn unbekannt, **zu verifizieren**). `VK_KHR_timeline_semaphore` ist deshalb in der Device-Extension-Liste (6.2).

### 6.5 d) Input-Subsystem (`IUnityXRInput`)

Registrierung analog Display mit ID aus dem Manifest (`"LinuxXR Input"`, `disablesLegacyInput`/`disablesLegacyVr` wie bei Valve prüfen).

**Geräte** (über `FillDeviceDefinition`, `UpdateDeviceState`, `Tick`):

| Gerät | Merkmale (Feature-Namen kompatibel zu Unitys `CommonUsages`) |
|---|---|
| Head (HMD) | `centerEyePosition/Rotation`, `leftEyePosition/Rotation`, `rightEyePosition/Rotation`, `devicePosition`, `deviceRotation`, `isTracked`, `trackingState`; Velocity/Angular Velocity soweit verfügbar |
| Left/Right Controller | `devicePosition`, `deviceRotation`, `pointerPosition`, `pointerRotation`, `isTracked`, `trackingState`, `trigger` (float), `triggerButton`, `grip` (float), `gripButton`, `primary2DAxis`, `primary2DAxisClick`, `primary2DAxisTouch`, `primaryButton` (A/X), `secondaryButton` (B/Y), `menuButton`, optional `thumbrest`-Touch |

Ziel: Bindings wie `<XRController>{LeftHand}/devicePosition`, `{TriggerButton}`, `{Primary2DAxis}` im Input System und alle Standard-Presets des XR Interaction Toolkit (XRI) funktionieren ohne Anpassung. Welche Layouts genau erwartet werden (Unity führt Layouts für `XRController`/`XRHMD` aus den Geräte-Characteristics), wird in Phase 4 gegen XRI 3.x getestet (Risiko R6).

**OpenXR-Actions.**
- Ein Action-Set („gameplay"), Actions für Pose (grip, aim), Trigger (float), Squeeze (float), Thumbstick (vec2), Thumbstick click/touch, A/X, B/Y, Menu, Haptik (Output).
- **Bindings** vorschlagen für `/interaction_profiles/meta/touch_controller_plus` (Quest 3) und `/interaction_profiles/oculus/touch_controller` als Fallback; WiVRns Monado-Binding-System mappt auf das vom Headset gemeldete Profil (Quest 3/3S → Touch Plus). Optional zusätzlich `khr/simple_controller`.
- `xrSuggestInteractionProfileBindings`, `xrAttachSessionActionSets`, pro Tick `xrSyncActions` und `xrGetActionState*`, Posen via `xrLocateSpace` zur vorhergesagten Anzeigezeit.
- **Haptik:** `HandleHapticImpulse`/`HandleHapticStop`, `QueryHapticCapabilities` → `xrApplyHapticFeedback` (`XrHapticVibration`).
- **Tracking-Origin:** `QueryTrackingOriginMode`/`HandleSetTrackingOriginMode` auf Reference Spaces abbilden: Floor → `LOCAL_FLOOR` (bzw. `STAGE`), Device → `LOCAL`. `HandleRecenter` → Local-Space neu setzen.
- **Später (Phase 6):** Handtracking über `XR_EXT_hand_tracking` (Gelenkdaten brauchen die Berechtigung in der Headset-App) und `XR_EXT_hand_interaction`; Eye-Gaze nur wenn WiVRn es meldet.

### 6.6 e) C#-Seite und Integration

Paket `unity/Packages/com.tyroonis.xr.linuxopenxr/`:

- `Runtime/LinuxXRLoader.cs`: Subklasse von `XRLoaderHelper` (`CreateSubsystem`/`StartSubsystem`/`StopSubsystem`/`DestroySubsystem` für Display und Input), mit `IXRLoaderPreInit` (Rückgabe des Bibliotheksnamens) nur im Editor; `[XRSupportedBuildTarget(BuildTargetGroup.Standalone, new[]{BuildTarget.StandaloneLinux64})]`. Der gesamte Code ist in `#if UNITY_EDITOR_LINUX` eingeklammert, damit Builds für Android und Windows unberührt bleiben.
- `Runtime/UnitySubsystemsManifest.json`: `name`, `version`, `libraryName: "LinuxXR"`, ein `displays`-Eintrag und ein `inputs`-Eintrag; die IDs müssen exakt mit denen in `RegisterLifecycleProvider` übereinstimmen.
- `Runtime/LinuxXRSettings.cs`: ScriptableObject (Runtime-Wahl, Render Scale, Refresh-Rate, Swapchain-Modus, Mirror-Modus, Log-Level).
- `Editor/LinuxXRPackage.cs`: `IXRPackage` + Loader-Metadaten für XR Plug-in Management. Der Filter in `XRLoaderInfoManager.FilteredLoaderInfos` prüft nur die `BuildTargetGroup`, ein Loader für `Standalone` erscheint also im Linux-Editor (Recherche 02, 9.7).
- `Plugins/Linux/x86_64/libLinuxXR.so` + `.meta` (Editor und Linux64 aktiv, CPU x86_64).
- **Integration:** Im Linux-Editor wird der Loader unter **XR Plug-in Management → Tab „Windows, Mac, Linux"** aktiviert. Der Play Mode im Editor nutzt die Standalone-Einstellungen – **zu verifizieren** (insbesondere, ob bei aktivem Build-Target „Android" im Editor der Android-Tab statt des Standalone-Tabs greift; falls ja: Build-Target im Testprojekt auf Linux Standalone halten oder Loader-Auswahl per Editor-Skript umschalten).
- **Konflikt mit Unitys OpenXR-Paket:** Ist `com.unity.xr.openxr` im Projekt (für den Android-Build nötig) und auf dem Standalone-Tab ebenfalls angehakt, würde dessen Validierung den Play Mode blockieren. Deshalb: auf dem Standalone-Tab **nur LinuxXR** aktivieren; die Validierungsregel greift dann nur, wenn OpenXR dort aktiv wäre (zu verifizieren, ob die Regel auch unabhängig davon feuert; Rückfall: Regel per Editor-Skript unterdrücken, ohne das Paket zu patchen).
- **Android-Tab:** OpenXR + Meta Quest Support bleiben wie gehabt; der Spielcode ist identisch.

### 6.7 f) Editor-Tooling

- **Fenster „LinuxXR Status"** (`Window → XR → LinuxXR Status`): Runtime gefunden (Pfad, Name, Version)? WiVRn-Server läuft? Headset verbunden? Session-State, aktuelle Refresh-Rate, Frame-Timing (Wait/Render/Submit, Frame-Drops), Render-Auflösung. **Stand Phase 1 (umgesetzt):** Zeilen Runtime, Headset, Grafik-API, PreInit, Session sowie ein Diagnosefeld; Buttons „WiVRn starten" (`flatpak run io.github.wivrn.wivrn`), „Vulkan setzen", „Log öffnen", „Diagnose kopieren", „XR-Einstellungen". **Geplant:** Frame-Timing, Refresh-Rate, Pairing-Hilfe und Runtime-Check (`openxr_runtime_list`-Äquivalent); Pairing und Server-Log laufen beim Flatpak über das WiVRn-Dashboard bzw. die Ausgabe von `flatpak run`.
- **Logging:** Native Logs über `IUnityXRTrace` bzw. Debug-Callback in die Unity-Console; zusätzlich in Datei (Pfad in den Einstellungen). Log-Level einstellbar; bei „Verbose" `XR_LOADER_DEBUG` weiterreichen.
- **Projekt-Validierung** (Menüeintrag und Hinweise beim Play): Vulkan muss die erste Linux-Grafik-API sein (Player Settings → Other Settings → Graphics APIs for Linux; sonst Hinweis und optionaler Start mit `-force-vulkan`), Color Space, „Vulkan Additional Graphics Queue"-Äquivalent aktiv, Loader auf Standalone-Tab aktiv, URP-Einstellungen (kein MSAA über Runtime-Limit, kein Depth-Layer), Controller-Profile.
- Kein stilles Reparieren: Die Validierung zeigt Befunde mit „Fix"-Button.

### 6.8 g) Debugging-Erlebnis und Editor-Robustheit

Weil das Spiel im Editor-Prozess läuft, gelten unverändert: `Debug.Log` in der Console, Breakpoints in Rider/VS Code (Attach to Unity), Profiler, Frame Debugger, Inspector-Live-Edit, Hot Reload. Das ist dasselbe Modell wie Quest Link unter Windows (der Editor ist die App).

Besonderheiten, die der Provider berücksichtigen muss:

- **Haltepunkte im Managed-Code** stoppen den Main-Thread. Die Runtime erwartet weiterhin `xrWaitFrame`/`xrEndFrame`-Zyklen; bei langem Halt geht die Session aus `FOCUSED/VISIBLE` oder der Server zeigt das letzte Bild/Warte-Szene. Das ist erwartetes Verhalten und wird in der Doku erwähnt; der Provider darf nach Fortsetzen nicht abstürzen (Phase 5).
- **Domain Reload:** Die native Bibliothek bleibt im Prozess geladen; der gesamte C++-Zustand wird an Subsystem-Start/-Stop gebunden, nicht an Dateiebene.
- **Play-Stop** muss alle Vulkan-Ressourcen freigeben, bevor Unity das Gerät abbaut. Hinweis aus Recherche 01: Swapchain-Images, die zur Laufzeit neu angelegt werden, können auf dem Server zu GPU-Problemen führen (Issue #1153, NVIDIA); wir legen Swapchains daher nur bei Session-Beginn und Auflösungsänderung neu an und warten vorher auf `vkQueueWaitIdle`/Fences.

---

## 7. Phasen und Meilensteine

Aufwand in **Abenden à 3 Stunden**, grobe Schätzung mit Unsicherheit (unbekanntes Terrain: Unity-XR-SDK-Header sind undokumentiert). Summe Phasen 0–5: etwa **21–33 Abende**, mit den optionalen Phasen 0b/0c bis etwa 35.

### Phase 0 – Basis (1–2 Abende)

- **Ziel:** WiVRn und Quest funktionieren mit einer Referenz-OpenXR-App, ohne Unity.
- **Aufgaben:**
  1. Systemcheck: `scripts/check-system.sh`.
  2. Host einrichten: `scripts/setup-host.sh` (WiVRn als Flatpak `io.github.wivrn.wivrn`; Alternativen `--wivrn=ppa` mit `wivrn-server`/`wivrn-dashboard` aus `ppa:lvra/wivrn`, `--wivrn=none`; dazu `libopenxr-loader1`, `libopenxr-utils`, `vulkan-tools`, `avahi-daemon`, mit `--dev` Build-Werkzeuge und `libopenxr-dev`, mit `--adb` `adb`; Firewall `5353/udp`, `9757/tcp+udp`).
  3. Quest-Client installieren (Meta Store, App-ID `7959676140827574`, oder Sideload der zur Server-Version passenden APK; **Protokollversion muss exakt übereinstimmen**).
  4. Server starten (`flatpak run io.github.wivrn.wivrn`), Pairing per PIN in der Brille.
  5. Encoder prüfen: WiVRn-Auswahl `vulkan → vaapi → x264`. Das Flatpak baut mit Vulkan-Video-Encode (passend zur RX 7600); das PPA-Paket baut laut `debian/rules` vermutlich ohne (also VAAPI; zu verifizieren). Config beim Flatpak bei Bedarf `~/.var/app/io.github.wivrn.wivrn/config/wivrn/config.json` (PPA: `~/.config/wivrn/config.json`), z. B. `{"encoder":{"encoder":"vaapi","codec":"h265"}}`.
  6. Test: `scripts/test-openxr.sh wivrn` bzw. direkt `hello_xr -g Vulkan2` mit gesetzter Runtime auf der Brille.
- **Abnahme:** `hello_xr -g Vulkan2` zeigt auf der Quest ein stabiles Bild; Kopf- und Controller-Tracking sichtbar; Server-Log (Dashboard bzw. Ausgabe von `flatpak run`; beim PPA `journalctl --user -u wivrn`) ohne Fehler; Encoder-Backend im Log erkennbar. USB-Variante einmal getestet.
- **Aufwand:** 1–2 Abende.
- **Status (2026-10-12):** Skripte geliefert, WiVRn als Flatpak installiert. Die Abnahme mit `hello_xr` ist von Tyroonis noch nicht bestätigt.

### Phase 0b (optional) – Headset-los mit Monado (1 Abend)

- **Ziel:** Provider später ohne Headset testbar.
- **Aufgaben:** `monado-service`, `libopenxr1-monado`; `XRT_DEBUG_GUI=1 QWERTY_ENABLE=1 monado-service`; `XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json hello_xr -g Vulkan2`; alternativ `SIMULATED_ENABLE=1`. Hinweis: `monado-service` registriert sich systemweit per `update-alternatives` – wir wählen pro Prozess über `XR_RUNTIME_JSON` und fassen die systemweite Auswahl nicht an.
- **Abnahme:** `hello_xr` rendert im Monado-Fenster; Kopf per Tastatur/Maus bewegbar.
- **Aufwand:** 0,5–1 Abend.
- **Status:** Für die Tests von Phase 1 wurde Monado in der Sandbox headless betrieben (`XRT_COMPOSITOR_NULL=1 SIMULATED_ENABLE=1`, lavapipe), siehe [PHASE1.md](PHASE1.md), Abschnitt 6. Der Weg mit `hello_xr` und Fenster/qwerty ist nicht getestet.

### Phase 0c (optional) – Diagnose-Spike Stridemann (max. 1 Abend)

- **Ziel:** Früh sehen, ob Unitys Vulkan-Pfad überhaupt mit WiVRn/Monado interoperiert (Extension-Anforderungen, Timeline-Semaphore, Swapchain-Formate).
- **Aufgaben:** Auf einer **Kopie** eines Testprojekts `com.unity.xr.openxr` auf 1.16.1 pinnen, Stridemann-Patches anwenden, mit `-force-vulkan` starten, Runtime WiVRn bzw. Monado per `XR_RUNTIME_JSON`.
- **Abnahme:** Befund notieren (Log), egal ob Erfolg oder Abbruch. Danach **Spike stoppen** und Ergebnisse in diese Doku übernehmen (Extensionliste, Formate). Nicht weiterverfolgen.
- **Aufwand:** ≤ 1 Abend (Zeitbox hart).

### Phase 1 – „Hello Provider" (2–3 Abende) – nativer Teil fertig, Unity-Test ausstehend

- **Status (2026-10-12):** Fertig sind `libLinuxXR.so` (core + Unity-Glue), `linuxxr-probe`, die Tests (2 ctest-Suites, etwa 780 Checks, grün) und das UPM-Paket `com.tyroonis.xr.linuxopenxr` (Loader mit `IXRLoaderPreInit`, Settings, Status-Fenster, P/Invoke). Display und Input sind Stubs (`renderPassesCount = 0`, keine Geräte). In der Sandbox gegen headless Monado 21 (lavapipe) geprüft: PreInit mit und ohne erreichbare Runtime, kompletter Session-Zyklus bis `FOCUSED` und `EXITING`, Swapchain-Array mit 2 Ebenen, Frames, Kopf-Tracking. **Nicht geprüft:** WiVRn/Quest und Unity (macht Tyroonis). Anleitung, Erfolgskriterien und Fehlerbehebung: [PHASE1.md](PHASE1.md).
- **Ziel:** Native `.so` und C#-Loader werden im Linux-Editor geladen.
- **Aufgaben:** CMake-Gerüst (`native/`), Unity-XR-SDK-Header unter `native/third_party/unity-xr-headers/` (inkl. Lizenzdatei), `UnityPluginLoad` mit Logging, `XRSDKPreInit` mit Dummy-Extensions, Paketgerüst `com.tyroonis.xr.linuxopenxr` (Loader, Manifest, Settings, `.meta`), Testprojekt `unity/XRTestbed/` (Unity 6, URP, Vulkan als erste Linux-API), Eintrag in XR Plug-in Management.
- **Abnahme:** Editor startet; „LinuxXR" erscheint unter XR Plug-in Management → Standalone; Console zeigt die `[LinuxXR]`-Zeilen (PreInit-Provider registriert, Display-Subsystem initialisiert, beim Play „Display (Gfx-Thread) gestartet – Phase-1-Stub" und „Erster PopulateNextFrameDesc"; vollständige Liste in [PHASE1.md](PHASE1.md), Abschnitt 7.5); Play startet das Display-Subsystem (leere Callbacks genügen), ohne Absturz; kein `DllNotFoundException`. Offen bis zum Unity-Test: R1, R2, R7.
- **Aufwand:** 2–3 Abende.

### Phase 2 – Instanz/Session und erstes Bild (4–6 Abende)

- **Ziel:** Echte OpenXR-Session, pro Auge eine Clear-Color auf der Quest.
- **Aufgaben:** Loader per `dlopen`, Runtime-Wahl (`XR_RUNTIME_JSON`), Instanz/System/Session mit Unitys Vulkan-Objekten (Variante A mit `XR_KHR_vulkan_enable`, ADR 10), Geräteabgleich per UUID, Event-Schleife und State-Machine, Swapchains (zunächst Multi-Pass, einfacher), Frame-Loop mit Projection-Layer, Test erst mit Monado (Phase 0b), dann WiVRn. Debug mit `openxr-layer-corevalidation` und Vulkan-Validation-Layer.
- **Stand:** Laden des OpenXR-Loaders per `dlopen`, Runtime-Wahl, Instanz, Session, Event-Schleife, Geräteabgleich, Swapchains und Frame-Loop liegen schon in `XrContext` (`native/src/core/`) und laufen mit `linuxxr-probe` gegen Monado. Offen ist die Anbindung an das Display-Subsystem: **Kopie** der Unity-Textur in das Swapchain-Bild per `IUnityGraphicsVulkan::AccessTexture`/`AccessQueue` (ADR 9, vorgeschlagen) und `xrEndFrame` in `SubmitCurrentFrame`.
- **Abnahme:** In der Quest sind zwei unterschiedliche Farben (links/rechts) zu sehen; Session erreicht `FOCUSED`; Play-Stop beendet die Session sauber (`STOPPING → IDLE`), WiVRn-Server läuft weiter.
- **Aufwand:** 4–6 Abende (Haupt-Unbekannte: Vulkan-Interop, Header-Details, Thread-Zuordnung).

### Phase 3 – Stereo-Rendering, Head-Tracking, Mirror (6–10 Abende)

- **Ziel:** Echte URP-Szene in Stereo mit Kopf-Tracking; Game View spiegelt.
- **Aufgaben:** Array-Swapchain und Single-Pass-Instanced (Fallback Multi-Pass), Views/FOV/Projektionsmatrizen, Pose-Konvertierung OpenXR→Unity mit Unit-Tests, Head-Device im Input-Subsystem, Mirror-Blit, Refresh-Rate, Auflösungsskalierung, Vergleich Wayland(XWayland)/X11, Messung Frame-Timing.
- **Abnahme:** Szene mit Referenzobjekten (Maßstab 1 m Würfel, Boden, Horizontlinie) hat korrekten Maßstab und Tiefe, kein verdrehtes Stereo, keine Bildspiegelung; Kopfbewegung ohne sichtbare Latenzartefakte über WLAN; Game View zeigt das Bild; ≥ 72 fps stabil, Ziel 90 fps; funktioniert unter Wayland und X11.
- **Aufwand:** 6–10 Abende.

### Phase 4 – Controller, Haptik, XRI-Kompatibilität (4–6 Abende)

- **Ziel:** Spielbare Interaktion.
- **Aufgaben:** Actions und Bindings (`meta/touch_controller_plus`, `oculus/touch_controller`), Controller-Geräte mit CommonUsages-kompatiblen Features, Haptik, Tracking-Origin-Modi, Test mit Input System und XRI (Default-Presets, Ray Interactor, Grab).
- **Abnahme:** Eine XRI-Beispielszene (Greifen, Teleportieren oder Ray-Interaktion) funktioniert ohne Anpassung der Bindings; Vibration spürbar; Controller-Pose passt zur realen Hand.
- **Aufwand:** 4–6 Abende.

### Phase 5 – Robustheit und Status-Fenster (4–6 Abende)

- **Ziel:** Tägliche Nutzbarkeit.
- **Aufgaben:** Play-Stop/Start × 50 ohne Leck und ohne Absturz; Domain Reload; Headset ab- und aufsetzen; WLAN-Abbruch und Reconnect; Editor ohne Headset und ohne Server starten; Haltepunkt-Test (lange Pause im Managed-Code); Status-Fenster; Validierung; Log-Datei.
- **Abnahme:** Testliste (Anhang im Repo, `docs/TESTPLAN.md` – noch zu schreiben) komplett grün; kein Neustart von Editor oder Server nötig im Alltag.
- **Aufwand:** 4–6 Abende.

### Phase 6 – Komfort und Extras (offen, 6–12 Abende)

- Handtracking (`XR_EXT_hand_tracking`), Passthrough per Alpha-Blend-Environment-Blend-Mode (WiVRn rendert `XR_FB_passthrough`-Layer nicht), Refresh-Rate-Auswahl, Performance-Overlay, UPM-Git-Paket (installierbar per Package-Manager-URL), Dokumentation, Beispielszene, ggf. Veröffentlichung (Lizenzfrage).

### Parallel immer verfügbar – Fallback „Build and Run" auf die Quest

Funktioniert heute unter Linux ohne OpenXR im Editor: Android-Modul im Unity Hub, Entwicklermodus an der Quest, ADB über USB oder WLAN (`adb tcpip 5555`, `adb connect <IP>:5555`), `adb logcat -s Unity` oder das Paket *Android Logcat*, *Attach Managed Debugger* (Development Build, „Wait for Managed Debugger"), Profiler mit Autoconnect. Bekannter Linux-Fehler: defekte clang-Symlinks im mitgelieferten NDK (Lösung im Stridemann-README). Nachteil: jede Änderung braucht einen Deploy. Als dauerhafte Rückfalllinie behalten.

### Aufwandsübersicht

| Phase | Abende (à 3 h) |
|---|---|
| 0 Basis | 1–2 |
| 0b Monado (optional) | 0,5–1 |
| 0c Diagnose-Spike (optional) | ≤ 1 |
| 1 Hello Provider (nativer Teil fertig, Unity-Test offen) | 2–3 |
| 2 Instanz/Session/erstes Bild | 4–6 |
| 3 Stereo + Tracking + Mirror | 6–10 |
| 4 Controller/Haptik/XRI | 4–6 |
| 5 Robustheit | 4–6 |
| 6 Extras | 6–12 (offen) |
| **Summe 0–5** | **ca. 21–33 (mit optionalen 0b/0c bis ca. 35)** |

---

## 8. Risiken und Gegenmaßnahmen

| Nr. | Risiko | Wirkung | Gegenmaßnahme |
|---|---|---|---|
| R1 | **Unity-Editor auf Ubuntu 26.04 ist offiziell nicht unterstützt** (Unity nennt 22.04/24.04, GNOME auf X11 oder Wayland, Wayland experimentell) | Editor-Abstürze, Grafik-/Fensterfehler unabhängig von unserem Code | Früh in Phase 1 eine Stunde Editor-Stabilität testen (Vulkan, Hub, Package Manager). Fehler nach „Unity allein" und „Unity + LinuxXR" trennen. Rückfall: Editor in Distrobox/Ubuntu-24.04-Container (Recherche 03, 7.2, nur ein negativer Bericht bekannt) oder 24.04-Host – WiVRn als Flatpak ist davon unabhängig (das PPA hat nur `resolute`). |
| R2 | **Unity-XR-SDK-Header sind undokumentiert** und laut Unity-Doku „no longer available"; Änderungen in Unity 6.x gegenüber Valves Stand (Okt 2025) nicht geprüft | Kompilierfehler, falsche ABI-Annahmen, Abstürze beim Laden | Header-Stand aus Valves Repo pinnen und im Repo mitführen; Valves Provider (läuft auf Unity 6?) als Referenzimplementierung lesen; Phase 1 testet Laden früh; in der Doku Unity-Version festschreiben. Valves Plugin im Testprojekt als Funktionsnachweis der Provider-Schnittstelle nutzen, falls nötig (nur zur Gegenprobe; benötigt SteamVR). |
| R3 | **Vulkan-Device-Mismatch** (Unity-`VkPhysicalDevice` ≠ von der Runtime erwartetes Gerät; Unity erzeugt Instanz/Gerät vor der OpenXR-Instanz) | `xrCreateSession` schlägt fehl (`XR_ERROR_GRAPHICS_DEVICE_INVALID`) | UUID-Vergleich vor `xrCreateSession`; PreInit wählt GPU gezielt; bei Multi-GPU `XRT_COMPOSITOR_FORCE_GPU_INDEX` dokumentieren; Variante B (6.2) als Rückfall. |
| R4 | **Falsche/fehlende Vulkan-Extensions**, weil beim Editor-Start keine Runtime erreichbar | Session scheitert oder Import der Swapchain-Images schlägt fehl | Abfrage-Cache, feste Liste als Fallback, Abgleich der Liste mit realer Runtime-Antwort (Phase 0c/2), Warnung im Status-Fenster bei Abweichung. |
| R5 | **`XR_KHR_composition_layer_depth` fehlt in WiVRn**; `XR_FB_passthrough`-Layer unsupported | Funktionen fehlen, nicht blockierend | Nie als zwingend voraussetzen; Depth-Submission nicht implementieren; Passthrough über Alpha-Blend (Phase 6). |
| R6 | **Input-Layouts von XRI/Input System** (auch HurricaneVR liest Unity-XR-Input) erwarten bestimmte Geräte-Characteristics und Feature-Namen | Bindings greifen nicht | CommonUsages-Namen strikt einhalten; Tests mit XRI-Default-Presets in Phase 4; Valves `Input.cpp` als Muster für Gerätedefinitionen. |
| R7 | **Play Mode nutzt Standalone-Settings?** (zu verifizieren) | Loader wird im Play Mode nicht gestartet | Phase 1 klärt es; ggf. Editor-Skript, das Loader für den aktiven Target-Gruppe setzt. |
| R8 | **Lizenz der Unity-Header** (Unity Companion License v1.2: Nutzung nur für Inhalte, die eine gültige Unity-Engine-Lizenz voraussetzen; „AS IS") | Veröffentlichung/Weitergabe des Plugins mit Headern fraglich | Header mit `LICENSE.md` in `third_party/` unverändert mitführen; rechtlich nicht geprüft – vor Veröffentlichung klären (Abschnitt 13). Für privaten Gebrauch unkritisch. |
| R9 | **WLAN-Latenz/Qualität** | Rucklige Bewegung, Artefakte | Siehe Abschnitt 9. |
| R10 | **Runtime nur bei verbundenem Headset aktiv** | Play ohne Headset erzeugt Fehler | Lazy-Verbindung, klare Meldung im Status-Fenster, optional `--early-active-runtime` am Server, fester `XR_RUNTIME_JSON`-Pfad; Monado-Fallback „Auto". |
| R11 | **Protokollversion Client ≠ Server** | „Incompatible protocol version" | Meta-Store-Version kann hinterherhinken; passende APK vom GitHub-Release sideloaden; Dashboard warnt. Updates von Flatpak (bzw. PPA) und Client gemeinsam durchführen: `flatpak update io.github.wivrn.wivrn` und Client neu installieren. |
| R12 | **WiVRn-Besonderheiten bei RADV** (Issues #831 Alpha-Artefakt rechtes Auge mit `RADV_DEBUG=nodcc`, #846 blockiges Bild) | Bildfehler auf AMD | Mit `hello_xr` in Phase 0 vorab prüfen; Workarounds dokumentieren. |
| R13 | **Swapchain-Neuanlage zur Laufzeit** (WiVRn #1153) | GPU-Fehler auf dem Server | Swapchains nur bei Session-Start/Auflösungswechsel neu; vorher GPU-Idle abwarten. |
| R14 | **Unity-Update bricht den Provider** (ABI) | Plugin lädt nicht mehr | Unity-Version im Testprojekt pinnen; CI-ähnlicher Test (Phase 1) nach jedem Editor-Update; Header-Diff beobachten. |
| R15 | **WiVRn-Update bricht die Kette** | Nach `apt upgrade` geht nichts mehr | Server-/Client-Version (Flatpak bzw. PPA) in `docs/` festhalten (`flatpak info io.github.wivrn.wivrn`); Updates bewusst durchführen; Phase-0-Test (`hello_xr`) nach jedem Update. |
| R16 | **Ubuntu-glibc-ABI** der gebauten `.so` | Plugin lädt auf anderer Maschine nicht | Host-Build für den eigenen Rechner; optional Build-Container (`ubuntu:24.04`, glibc 2.39), siehe ADR 3. |
| R17 | **Meta-SDK-Komponenten auf OVRPlugin-Basis** (`OVRCameraRig`, `OVRInput`, `OVRManager`) bekommen im Linux-Editor **keine Daten**: OVRPlugin gibt es nicht für Linux. Außerdem kompilieren Meta-XR-SDK-Versionen über v74.0.3 laut Meta-Forum im Linux-Editor nicht (Recherche 02). **HurricaneVR und XRI** lesen dagegen Unity-XR-Input (`InputDevices`/`CommonUsages` bzw. Input-System-`XRController`) und funktionieren mit LinuxXR, sobald Phase 3/4 Geräte liefern | Rig oder Interaktion läuft im Editor nicht, Projekt kompiliert im Linux-Editor nicht | Rig auf **XR Origin / Unity-XR-Basis** aufbauen; Meta SDK nur für Android-Build-Features nutzen und per Assembly-Define oder Plattformabfrage (z. B. `UNITY_EDITOR_LINUX`) vom übrigen Code trennen; früh klären, welche Meta-SDK-Teile genutzt werden (Abschnitt 13, Frage 2). |

---

## 9. Netzwerk-Empfehlungen

Latenzzahlen sind in WiVRn nicht veröffentlicht; die folgenden Punkte sind übliche Empfehlungen (kein Repo-Beleg), außer wo vermerkt.

- **PC per Kabel (Ethernet) am Router/Access Point**, nicht per WLAN. Die Quest allein im WLAN. Ist-Zustand bei Tyroonis: der PC hängt per USB-WLAN-Stick im Netz; das ist für Streaming die schwächste Stelle. Empfehlung: LAN-Kabel, oder USB-Verbindung zur Quest (siehe unten).
- **5 GHz oder 6 GHz (Wi-Fi 6/6E)**, eigener Kanal, Quest im gleichen Raum mit Sichtverbindung zum Access Point; kein Mesh-Repeater, kein VPN auf dem PC oder in der Quest.
- Router: DFS-Kanäle meiden, falls Abbrüche auftreten; Kanalbreite 80 MHz; Multicast/mDNS nicht blockieren.
- **Firewall:** `5353/udp` (mDNS) und `9757/tcp+udp` (WiVRn) öffnen. Beispiel: `sudo ufw allow 5353/udp && sudo ufw allow 9757`. Ohne laufenden `avahi-daemon` beendet sich der Server (außer `--no-publish-service`; dann IP manuell im Headset eingeben).
- **Bitrate und Auflösung** in der WiVRn-Client-App anpassen (nicht serverseitig): bei Rucklern erst Bitrate senken, dann Auflösung, dann Codec prüfen. `tcp-only: true` in der Server-Config nur als Notlösung (mehr Latenz).
- **USB-Alternativen:** (1) „USB-Netzwerk" in den WiVRn-Client-Einstellungen (neu in 26.9, für Quest 3 vorgesehen, braucht weder ADB noch Entwicklermodus; Server muss IPv6-Link-Local zulassen; zu verifizieren, dass der Schalter bei der aktuellen Horizon-OS-Version angezeigt wird); (2) `adb reverse tcp:9757 tcp:9757` plus `adb shell am start -a android.intent.action.VIEW -d "wivrn+tcp://127.0.0.1:9757" org.meumeu.wivrn` (braucht Entwicklermodus; nur TCP; **WLAN am Headset ausschalten**; USB-Kabel mit mindestens 5 Gb/s).
- Bei Ruckeln mit hoher Bewegungs-Latenz laut WiVRn-README: Einstellungen zurücksetzen, Software-Encoding testen, Bitrate senken, Auflösung in der WiVRn-App senken.
- Server-Pacing-Parameter für Experten: `WIVRN_CLIENT_MARGIN_MS`, `U_PACING_APP_MIN_MARGIN_MS`.

---

## 10. Debug-Werkzeuge

| Zweck | Werkzeug / Befehl |
|---|---|
| OpenXR-Loader-Log (Runtime-Suche, Layer, Fehler) | `XR_LOADER_DEBUG=all` (vor Start des Unity-Editors setzen) |
| WiVRn-/Monado-Server-Log | Flatpak: Ausgabe von `flatpak run io.github.wivrn.wivrn` im Terminal bzw. Dashboard; PPA: `XRT_LOG=debug XRT_COMPOSITOR_LOG=debug wivrn-server` bzw. `journalctl --user -u wivrn -f` |
| LinuxXR-Log und Probe | `~/.cache/linuxxr/linuxxr.log`, `LINUXXR_LOG=debug`; `native/build/linuxxr-probe runtimes\|info\|preinit\|run` (siehe [PHASE1.md](PHASE1.md)) |
| Alle OpenXR-Aufrufe protokollieren | API-Layer `openxr-layer-apidump` (Paket `openxr-layer-apidump`), per Umgebungsvariable aktivieren (Layername laut Paketdoku, zu verifizieren: `XR_APILAYER_LUNARG_api_dump`) |
| OpenXR-Nutzungsfehler prüfen | `openxr-layer-corevalidation` (`XR_APILAYER_LUNARG_core_validation`, zu verifizieren) |
| Vulkan-Fehler | `vulkan-validationlayers`; Aktivierung per `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` (im Unity-Prozess zunächst ausschließlich zum Test) |
| Verfügbare Runtimes | `openxr_runtime_list` (aus `libopenxr-utils`) |
| Gegenprobe der Kette ohne Unity | `hello_xr -g Vulkan2` mit gleichem `XR_RUNTIME_JSON` |
| Quest-Client-Log | `adb logcat '*:F'`, Client-Paket `org.meumeu.wivrn`; Env im Client: `WIVRN_LOGLEVEL` |
| WiVRn-Kommandozeile | `wivrnctl list-paired`, `pair`, `disconnect`, `stop-server` (Paket-Installation; beim Flatpak primär das Dashboard) |
| Server-GPU wählen | `XRT_COMPOSITOR_FORCE_GPU_INDEX` |
| Viewport-Skalierung pro App | `OXR_VIEWPORT_SCALE_PERCENTAGE` |
| Frame-Dump / Renderdoc | `WIVRN_DUMP_VIDEO`, `ENABLE_VULKAN_RENDERDOC_CAPTURE` (Server) |
| Headset-los | `QWERTY_ENABLE=1 XRT_DEBUG_GUI=1 monado-service`, `SIMULATED_ENABLE=1`, `XRT_COMPOSITOR_NULL` |
| Unity | Editor-Log `~/.config/unity3d/Editor.log`, `-force-vulkan`, `-logFile -`; Profiler und Frame Debugger im Editor |
| Native Debugging | `gdb -p <Unity-PID>` (Attach), Provider mit `-g` bauen; Symbole der `.so` getrennt aufheben |

Vorgehen bei Fehlern in Reihenfolge: (1) `hello_xr` geht? → Kette/Netz/Encoder in Ordnung; (1b) `linuxxr-probe run` geht? → Runtime-Wahl, PreInit-Logik und Session-Aufbau von LinuxXR in Ordnung, Fehler liegt dann in der Unity-Anbindung; (2) Monado statt WiVRn → Fehler im Provider oder im Streaming?; (3) `XR_LOADER_DEBUG=all` + `apidump` → welcher OpenXR-Aufruf scheitert; (4) Vulkan-Validation; (5) Serverlog.

---

## 11. Ordnerstruktur

```
UnityVR-Linux/
├── README.md
├── docs/
│   ├── PLAN.md                    (dieses Dokument)
│   ├── PHASE1.md                  (Phase 1: Bauen, Testen, Unity-Einbindung)
│   ├── ENTSCHEIDUNGEN.md          (ADRs)
│   └── research/
│       ├── 01-wivrn.md
│       ├── 02-unity-openxr-linux.md
│       └── 03-alternativen-und-container.md
├── scripts/
│   ├── check-system.sh            (Phase 0: Systemcheck, nur lesend)
│   ├── setup-host.sh              (Phase 0: WiVRn-Flatpak, Pakete, Firewall; --dev, --wivrn=ppa|none)
│   ├── test-openxr.sh             (Phase 0: hello_xr gegen WiVRn/Monado)
│   ├── quest-usb.sh               (WiVRn über USB statt WLAN)
│   ├── fetch-unity-headers.sh     (Phase 1: Unity-XR-Header holen)
│   └── build-native.sh            (Phase 1: libLinuxXR.so, linuxxr-probe, Tests)
├── native/                        (Phase 1)
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── core/                  (Unity-unabhängig: OpenXR, Vulkan, Runtime-Wahl, Input)
│   │   └── unity/                 (Unity-Glue: PreInit, Display, Input, C-API)
│   ├── tools/probe/               (linuxxr-probe)
│   ├── tests/                     (ctest-Suites, Fake-Unity-Host)
│   └── third_party/
│       └── unity-xr-headers/      (per fetch-unity-headers.sh geholt, nicht im Repo; Lizenzdatei wird mitkopiert)
├── unity/
│   ├── Packages/
│   │   └── com.tyroonis.xr.linuxopenxr/
│   │       ├── Runtime/
│   │       ├── Editor/
│   │       └── Plugins/Linux/x86_64/
│   └── XRTestbed/                 (Unity-6-URP-Testprojekt)
└── container/
    └── Containerfile              (optional: reproduzierbarer Plugin-Build)
```

Die Skripte `scripts/*.sh` sind geliefert; `fetch-unity-headers.sh` und `build-native.sh` sind neu in Phase 1.

---

## 12. Arbeitsteilung und Modellwahl

Nach Präferenz des Users: **Nur reiner Code läuft auf dem starken Modell (Opus)**; Dokumentation, Tests, Recherche, Planung und sonstige Aufgaben laufen auf einfacheren/günstigeren Modellen (Sonnet oder Haiku). Die Zuordnung entscheidet Claude selbst. Rückfrage zur Modellwahl nur zu Beginn einer Aufgabe, nie mittendrin.

| Aufgabe | Modell | Begründung |
|---|---|---|
| C++-Provider (`native/src/*`) | **Opus** | Reiner Code, Threading, Vulkan-Interop, ABI |
| C#-Loader, Settings, Editor-Fenster, Validierung | **Opus** | Reiner Code |
| CMake-Dateien, Skripte (`scripts/*.sh`), Containerfile | Sonnet | Build-Infrastruktur, überschaubar, gut prüfbar |
| Testfälle / Testpläne schreiben und ausführen, Log-Auswertung | Sonnet / Haiku | Kein Produktionscode |
| Dokumentation (README, PLAN, ENTSCHEIDUNGEN, Testplan) | Sonnet | Dokumentation |
| Recherche (Header, Spec-Details, Issues) | Sonnet / Haiku | Recherche |
| Planung, Aufwandsschätzung, Review-Notizen | Sonnet | Planung |
| Reine Formatierung, Kleinkram (Umbenennen, Listen, Links prüfen) | Haiku | Triviale Aufgaben |

Grenzfälle (z. B. Unit-Tests für die Pose-Konvertierung, Shell-Skripte mit Logik) entscheidet Claude pro Aufgabe; im Zweifel das günstigere Modell, solange das Ergebnis per Test prüfbar ist.

---

## 13. Offene Fragen an Tyroonis

1. **Testprojekt:** *Beantwortet (2026-10-12):* frisches Unity-6-URP-Projekt „XRTestbed". Danach Einbindung ins eigentliche Spielprojekt per UPM-Referenz.
2. **Spielcode, Meta SDK und HurricaneVR (offen, wichtig für Phase 4 und R17):** Tyroonis nutzt sonst HurricaneVR, eventuell das XR Interaction Toolkit (XRI) und das Meta SDK.
   - **Meta SDK:** Welche Teile genau (Interaction SDK, `OVRCameraRig`/`OVRManager`, `OVRInput`, Passthrough, Hand Tracking, Haptics, Platform SDK, …)? Komponenten, die OVRPlugin nutzen, bekommen im Linux-Editor keine Daten (R17). Empfehlung: Rig auf XR Origin / Unity-XR-Basis, Meta SDK nur für Android-Build-Features.
   - **HurricaneVR:** Welche Version, und welchen Input-Modus nutzt es im Projekt (XR Input über `InputDevices`/`CommonUsages`, Input System, OpenXR-Actions)? Das bestimmt die Priorität in Phase 4 (Layout-Kompatibilität, R6).
   - **XRI:** Wird es wirklich genutzt, oder nur HurricaneVR?
3. **Lizenz und Veröffentlichung:** Soll das Plugin privat bleiben oder veröffentlicht werden (GitHub/UPM)? Das betrifft die Unity Companion License der mitgeführten Header (R8) und die Wahl einer eigenen Lizenz (z. B. MIT). WiVRn (GPL-3.0) ist nur eine Laufzeit-Abhängigkeit über die OpenXR-Schnittstelle und überträgt die Lizenz nicht auf den Provider.
4. **Unity-Version:** *Beantwortet (2026-10-12):* 6000.6.5f1 mit Android-Modul. Offen ist nur, ob diese Version gepinnt bleiben soll (R14).
5. **Android-Tab:** Soll das Testbed auch einen Android-Build-Pfad (OpenXR + Meta Quest Support) enthalten, um die Parität „Spielcode identisch" automatisch zu prüfen? (Das Android-Modul ist installiert.)
6. **Container-Build:** Vorläufig wird der Host-Build genutzt (`scripts/build-native.sh`). Soll der reproduzierbare Podman-Build (`container/Containerfile`) später noch dazukommen?
7. **Modelle:** Passt die Zuordnung in Abschnitt 12, insbesondere Shell-Skripte und CMake auf Sonnet?

---

## 14. Quellen

Aus den Rechercheberichten übernommen (Stand 2026-10-12).

**WiVRn und Streaming**
- WiVRn-Repo, README, `docs/` (`building.md`, `configuration.md`, `debugging.md`, `nvenc.md`, `steamvr.md`): https://github.com/WiVRn/WiVRn
- WiVRn-Releases: https://github.com/WiVRn/WiVRn/releases
- Flathub: https://flathub.org/apps/io.github.wivrn.wivrn , https://github.com/flathub/io.github.wivrn.wivrn
- Launchpad-PPA (Veröffentlichungsliste): https://api.launchpad.net/devel/~lvra/+archive/ubuntu/wivrn?ws.op=getPublishedSources
- LVRA-Wiki WiVRn: https://vronlinux.org/docs/fossvr/wivrn/ ; Hardware: https://vronlinux.org/docs/hardware/
- Meta-Store-Eintrag WiVRn: https://www.meta.com/experiences/7959676140827574/
- Release-Artikel: https://www.gamingonlinux.com/2026/02/wireless-vr-streaming-levels-up-on-linux-with-the-latest-wivrn-release
- Monado-Extensionliste: https://monado.pages.freedesktop.org/monado/oxr__extension__support_8h_source.html
- Blender/Monado Wayland-GL: https://projects.blender.org/blender/blender/issues/119901

**Unity und OpenXR unter Linux**
- Unity-Foren: https://discussions.unity.com/t/linux-support-with-openxr/916945 , https://discussions.unity.com/t/openxr-linux-support/909788 , https://discussions.unity.com/t/openxr-support-for-linux/1693991 , https://discussions.unity.com/t/curious-what-the-current-state-of-wayland-support-for-the-linux-unity-editor/1696530 , https://discussions.unity.com/t/openxr-doesnt-work-for-dedicated-server-build/950594
- Stridemann-Hack: https://github.com/Stridemann/Unity-XR-on-Linux-for-Meta-Quest
- Paketspiegel (needle-mirror): https://github.com/needle-mirror/com.unity.xr.openxr , https://github.com/needle-mirror/com.unity.xr.management , https://github.com/needle-mirror/com.unity.xr.meta-openxr , https://github.com/needle-mirror/com.unity.xr.mock-hmd
- Valves Unity-XR-Plugin (Header, Provider-Vorlage): https://github.com/ValveSoftware/unity-xr-plugin
- Unity-Doku: https://docs.unity3d.com/2022.3/Documentation/Manual/xrsdk-provider-setup.html , https://docs.unity3d.com/6000.0/Documentation/Manual/system-requirements.html , https://docs.unity3d.com/6000.0/Documentation/Manual/EditorCommandLineArguments.html , https://docs.unity3d.com/6000.0/Documentation/Manual/PlayerCommandLineArguments.html , https://docs.unity3d.com/6000.6/Documentation/Manual/configure-graphicsAPIs.html , https://docs.unity3d.com/6000.0/Documentation/Manual/android-debugging-on-an-android-device.html , https://docs.unity3d.com/Packages/com.unity.mobile.android-logcat@1.4/manual/index.html , https://docs.unity3d.com/Packages/com.unity.xr.openxr@1.9/manual/project-configuration.html , https://docs.unity3d.com/Packages/com.unity.xr.interaction.toolkit@3.0/manual/xr-device-simulator-overview.html
- Unity-Roadmap-Artikel: https://digitalproduction.com/2026/03/17/official-steam-support-lands-on-unity-roadmap/
- Meta: https://developers.meta.com/horizon/documentation/unity/unity-development-requirements , https://developers.meta.com/horizon/documentation/unity/unity-req/ , https://developers.meta.com/horizon/blog/meta-xr-simulator-pc-mac-unity-unreal-developer-mixed-reality/ , https://developers.meta.com/horizon/documentation/native/android/ts-mqdh-getting-started/ , https://communityforums.atmeta.com/discussions/Questions_Discussions/fix-linux-development-versions-above-v74-0-3-do-not-work-on-unity-editor-linux/1348536

**Alternativen, Monado, Container**
- ALVR: https://github.com/alvr-org/ALVR , https://github.com/alvr-org/ALVR/wiki/Installation-guide , https://vronlinux.org/docs/steamvr/alvr/
- Monado: https://monado.freedesktop.org/getting-started.html , https://monado.pages.freedesktop.org/monado/group__drv__qwerty.html , https://monado.pages.freedesktop.org/monado/group__drv__simulated.html , https://monado.pages.freedesktop.org/monado/howto-remote-driver.html , https://gitlab.freedesktop.org/monado/monado/merge_requests/714 , https://developer.blender.org/docs/features/gpu/tools/monado/
- Envision: https://vronlinux.org/docs/fossvr/envision/
- Ubuntu-Archiv (Paketversionen): https://archive.ubuntu.com/ubuntu/dists/resolute/universe/binary-amd64/Packages.gz
- Steam/Virtual Desktop/CloudXR: https://www.gamingonlinux.com/2026/09/steamvr-2-17-arrives-ready-to-go-for-the-steam-frame/ , https://www.vrdesktop.net/ , https://docs.nvidia.com/cloudxr-sdk/latest/overview/overview.html
- Container: https://distrobox.it/usage/distrobox-create/ , https://wiki.alpinelinux.org/wiki/Using_Distrobox_For_VR_Gaming , https://universal-blue.discourse.group/t/running-unityhub-unity-editor/11406
- Unity Render Streaming: https://docs.unity3d.com/Packages/com.unity.renderstreaming@3.1/manual/index.html
