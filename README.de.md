# LinuxXR

[English](README.md) | **Deutsch**

**Im Unity-6-Editor unter Linux auf Play drücken, und die Szene läuft auf deiner Meta Quest.** LinuxXR ist ein nativer Unity-XR-Provider
(`libLinuxXR.so` + das UPM-Paket `com.tyroonis.xr.linuxopenxr`), der den Play Mode des Editors über
[WiVRn](https://github.com/WiVRn/WiVRn) per WLAN oder USB auf eine Quest streamt. Ohne SteamVR, ohne Meta Quest Link. Weil das Spiel im
Editor-Prozess läuft, funktioniert das Debugging wie unter Windows mit Quest Link: Console, Breakpoints, Profiler, Inspector und
ein Game-View-Spiegelbild.

Unitys eigenes `com.unity.xr.openxr` unterstützt Linux nicht (keine Linux-Binärdatei, Play Mode gesperrt), deshalb gibt es dieses Projekt.
Android-Builds für die Quest nutzen weiterhin Unity OpenXR (Android-Tab); dein Spielcode bleibt derselbe.

## Funktionen

- Play Mode auf einer Meta Quest aus dem **Linux-Editor**, über WiVRn (OpenXR-Runtime auf Monado-Basis).
- Vulkan-Rendering; Stereo-Modus **Single Pass Instanced** (empfohlen) oder **Multi Pass**.
- Input-Subsystem mit **"LinuxXR HMD"**, **"LinuxXR Controller - Left"** und **"LinuxXR Controller - Right"**,
  bereitgestellt mit Unity-`CommonUsages` (funktioniert mit dem Input System, dem XR Interaction Toolkit und dem XR-Input von HurricaneVR).
- Game-View-Spiegelung (linkes Auge / rechtes Auge / beide Augen / aus), Tracking Origin (Floor / Device), Render Scale (0,5-1,5).
- Status-Fenster (**Window -> XR -> LinuxXR Status**): Einrichtungs-Checkliste, Sitzungsstatus, Live-Statistik, Aktionen per Klick
  und **Create issue report** (Markdown-Bericht für Fehlermeldungen).
- Native `[LinuxXR]`-Logmeldungen erscheinen in der Unity-Console.
- Runtime-Auswahl: automatisch (WiVRn, dann `active_runtime.json`, dann Monado) oder per Einstellung / `LINUXXR_RUNTIME`.
- **Installation ohne Skripte** über Unitys Package Manager (Git-URL, Tarball oder `.unitypackage`) mit einem **Einrichtungsassistenten**, der Einstellungen per Klick behebt.
- Funktioniert mit dem **Unity Hub als Flatpak** (der Assistent öffnet die Sandbox für WiVRn) und bringt ein portables natives Plugin mit mitgeliefertem OpenXR-Loader mit.
- Testwerkzeuge: `linuxxr-probe` (prüft die ganze Kette ohne Unity) und eine `ctest`-Suite mit Fake-Unity-Host und
  Integrationstests gegen headless Monado.

## Voraussetzungen

| Was | Voraussetzung |
|-----|---------------|
| Betriebssystem | Linux mit Vulkan, glibc 2.35 oder neuer (die Release-Binärdatei ist portabel gebaut). Getestet: Ubuntu 26.04 GNOME (Wayland, Editor über XWayland). Unity unterstützt offiziell nur Ubuntu 22.04 / 24.04. |
| GPU | Vulkan-Treiber (z. B. Mesa RADV) plus Video-Encoding für WiVRn: VAAPI / Vulkan Video bei AMD und Intel, NVENC bei NVIDIA. |
| Unity | Unity 6 (das Paket verlangt `6000.0` oder neuer), Vulkan als Graphics API. Getestet: 6000.6.5f1 mit URP. |
| Headset | Meta Quest mit der WiVRn-App (Meta Horizon Store oder Sideload). Getestet: Quest 3. |
| Streaming | [WiVRn](https://github.com/WiVRn/WiVRn) auf dem PC (Flatpak `io.github.wivrn.wivrn`), in der Quest-App **exakt dieselbe Version**. |
| Netzwerk | PC per LAN-Kabel, Quest im 5-GHz-Band / Wi-Fi 6; oder USB. Firewall: Port 9757 TCP+UDP (und 5353/UDP für die mDNS-Erkennung). |
| Selbst bauen | Nur für Mitwirkende: siehe [CONTRIBUTING.md](CONTRIBUTING.md) und [Aus dem Quellcode bauen](docs/INSTALL.de.md#aus-dem-quellcode-bauen-entwickler). |

Zum Benutzen von LinuxXR sind weder Skripte noch Build-Werkzeuge noch ein separater OpenXR-Loader nötig: Das Paket enthält das native Plugin
und einen mitgelieferten OpenXR-Loader.

## Schnellstart

Der kürzeste funktionierende Weg. Jeder Schritt ist in [docs/INSTALL.de.md](docs/INSTALL.de.md) erklärt.

1. **WiVRn installieren** auf dem PC (`flatpak install --user flathub io.github.wivrn.wivrn`; der Einrichtungsassistent kann das auch),
   die **WiVRn-App** auf der Quest installieren, Port 9757 TCP+UDP in der Firewall öffnen und beide koppeln.
2. **Paket in Unity hinzufügen:** *Window -> Package Manager -> + -> Add package from git URL...* und eingeben:

   ```
   https://github.com/tyroonis/UnityVR-Linux.git#upm
   ```

   (mit `#upm/v0.3.3` wird genau dieses Release festgelegt). Alternativen: *Add package from tarball...* mit dem Release-`.tgz` oder das `.unitypackage`.
   Siehe [Installation](#installation) unten.
3. **Editor einmal neu starten.** Beim ersten Laden installiert das Paket sein natives Plugin nach `Assets/LinuxXR`, und Unity registriert XR-Subsysteme nur beim Start des Editors (der Setup-Assistent zeigt **Restart Editor**).
4. Dem **LinuxXR-Setup-Assistenten** folgen (*Window -> XR -> LinuxXR Setup*; er öffnet sich von selbst, solange etwas Erforderliches fehlt). Jeder Schritt hat eine Ein-Klick-Lösung.
5. Ein XR Origin in die Szene legen, WiVRn starten, das Headset verbinden und **Play** drücken.
   Die Console sollte `[LinuxXR] Initialized (version 0.3.3)` zeigen.

## Installation

LinuxXR wird über Unitys Package Manager installiert. Es sind keine Skripte beteiligt.

| # | Weg | So geht's |
|---|-----|-----------|
| 1 | **Git-URL** (empfohlen) | *Package Manager -> + -> Add package from git URL...* -> `https://github.com/tyroonis/UnityVR-Linux.git#upm`, oder `...#upm/v0.3.3`, um eine Version festzulegen. Der `upm`-Branch wird von der Release-CI veröffentlicht und enthält die fertigen Binärdateien. |
| 2 | **Tarball** | `com.tyroonis.xr.linuxopenxr-0.3.3.tgz` von der GitHub-Releases-Seite laden, dann *Package Manager -> + -> Add package from tarball...*. Unity 6.3+ zeigt eventuell "package is not signed"; das ist nur eine Warnung (siehe [Fehlerbehebung](docs/TROUBLESHOOTING.de.md)). |
| 3 | **.unitypackage** | `LinuxXR-0.3.3.unitypackage` laden (GitHub Releases oder Asset Store) und *Assets -> Import Package -> Custom Package...* verwenden. Der C#-Code landet in `Assets/Tyroonis/LinuxXR`, die nativen Dateien in `Assets/LinuxXR`. |

Alle Wege funktionieren gleich: Beim ersten Laden (nur im Linux-Editor) installiert das Paket seinen nativen Teil nach `Assets/LinuxXR`
(`UnitySubsystemsManifest.json` und `Plugins/Linux/x86_64/{libLinuxXR.so, libopenxr_loader.so.1}`), weil Unity native XR-Subsysteme nur aus einem
direkten Unterordner von `Assets/` registriert. `Assets/LinuxXR` kann wie jedes andere Asset eingecheckt werden oder weggelassen werden; es wird automatisch neu erzeugt.

Nach der Installation und nach jedem Update, das die nativen Binärdateien ändert, den **Editor einmal neu starten** (der Setup-Assistent zeigt **Restart Editor**). Fehlt `com.unity.xr.management` (XR Plug-in Management), bietet ein kleiner Bootstrap die Installation an
(Menü *Window -> XR -> LinuxXR Install XR Plug-in Management*).

### Setup-Assistent

*Window -> XR -> LinuxXR Setup* öffnet sich einmal pro Sitzung automatisch, solange etwas Erforderliches fehlt (die Checkliste lässt sich pro Projekt abschalten).
Schritte mit Ein-Klick-Lösung:

- natives Plugin nach `Assets/LinuxXR` installiert (**Install**, danach **Restart Editor**) und XR-Subsysteme gefunden
- Vulkan als Graphics API für Linux (startet den Editor neu)
- LinuxXR-Loader für Standalone aktiviert, andere Standalone-Loader entfernt, Initialize XR on Startup
- Flatpak-Sandbox-Zugriff auf WiVRn (nur wenn der Unity Hub als Flatpak `com.unity.UnityHub` läuft): **Allow access to WiVRn**, danach **Close Unity Hub + Editor now** und beides neu starten
- WiVRn installiert (von Flathub, `flatpak install --user`) und gestartet
- empfohlen: Linear-Farbraum, Render Scale 0,7

Die optionalen Host-Hilfsskripte (`scripts/check-system.sh`, `setup-host.sh`, `setup-unity-flatpak.sh`, `quest-usb.sh`) liegen weiter im Repository, nichts davon ist erforderlich.

## So funktioniert es

```
 Unity-Editor (Linux, Vulkan)                          PC (WiVRn)                    Quest
┌─────────────────────────────────────────┐   ┌──────────────────────────┐   ┌───────────────┐
│ Deine Szene (XR Origin, XRI, ...)        │   │ WiVRn-Server             │   │ WiVRn-App     │
│   rendert beide Augen in Unity-Texturen  │   │ (OpenXR-Runtime,         │   │               │
│                  │                       │   │  Monado-basiert)         │   │ dekodiert und │
│ libLinuxXR.so    ▼                       │   │                          │   │ zeigt Video   │
│   Kopie in die OpenXR-Swapchain          │──▶│ kodiert Video:           │──▶│               │
│   (in IUnityGraphicsVulkan::AccessQueue) │   │  VAAPI / Vulkan Video    │   │ sendet Kopf-  │
│   xrEndFrame (Projection Layer)          │   │  (AMD, Intel) oder NVENC │◀──│ und Controller│
│   Input: HMD + 2 Controller   ◀──────────│◀──│  (NVIDIA)                │   │ -Posen        │
└─────────────────────────────────────────┘   └──────────────────────────┘   └───────────────┘
                                                      WLAN oder USB
```

Unity rendert beide Augen (Single-Pass-Instanced-Texturarray oder Multi-Pass) in eigene Texturen. Das Plugin kopiert sie innerhalb von
`IUnityGraphicsVulkan::AccessQueue` in die OpenXR-Swapchain und reicht mit `xrEndFrame` einen Projection Layer ein. WiVRn kodiert das
Ergebnis und streamt es zur Quest. Kopf- und Controller-Posen kommen über OpenXR zurück und werden als Unity-XR-Eingabegeräte bereitgestellt.
Der OpenXR-Loader wird per `dlopen` geladen; eine portable Kopie (Khronos OpenXR-SDK 1.1.47) liegt neben dem Plugin, weil die
Laufzeitumgebung des Unity-Hub-Flatpaks keinen hat. Details: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (nur Englisch).

## Status

**0.3.3 - früh, aber funktionsfähig: Bild, Kopf-Tracking und Controller-Tracking, Installation ohne Skripte und ein Einrichtungsassistent (siehe [Changelog](CHANGELOG.md)).**

## Getestete Hardware

| Komponente | Getestet mit |
|------------|--------------|
| Betriebssystem | Ubuntu 26.04, GNOME, Wayland (Editor läuft über XWayland) |
| GPU / Treiber | AMD Radeon RX 7600, RADV, Mesa 26 |
| Unity | 6000.6.5f1, URP |
| Unity Hub | Flatpak (`com.unity.UnityHub`) |
| Headset | Meta Quest 3 |
| WiVRn | 26.9, Flatpak (`io.github.wivrn.wivrn`) |
| Ergebnis | Bild, Kopf- und Controller-Tracking funktionieren. Unity läuft mit etwa 90 fps; etwa 81 von 90 Frames erreichen das Headset in voller Auflösung (2476x2594 pro Auge). |

In voller Auflösung kommt nicht jeder Frame am Headset an. Setze die **Render Scale auf etwa 0,7** und nutze LAN-Kabel oder USB.
Andere Konfigurationen (NVIDIA, Intel, Quest 2 / Pro, Ubuntu 24.04, X11) sind ungetestet. Rückmeldungen sind willkommen.

## Bekannte Einschränkungen

- Es wird kein Depth Layer eingereicht, es gibt noch keine Occlusion Mesh, noch kein Hand-Tracking und kein Passthrough.
- Teile des Meta SDK, die auf OVRPlugin beruhen (`OVRCameraRig`, `OVRInput`, `OVRManager`), bekommen im Linux-Editor keine Daten.
  Nutze für das Rig ein XR Origin, das XR Interaction Toolkit oder Unity-XR-Input.
- Nur im Editor. Quest-Builds verwenden Unity OpenXR im Android-Tab.
- Unity unterstützt offiziell nur Ubuntu 22.04 und 24.04; andere Distributionen auf eigenes Risiko.
- WiVRn bedient Anwendungen nur, solange ein Headset verbunden ist: erst das Headset verbinden, dann Play drücken. Ohne Headset läuft der Play Mode ohne VR weiter und die Console zeigt eine gelbe Warnung (keine Fehler); der Editor friert nicht ein.

## Projektstruktur

```
.
├── native/                          C++20-Plugin
│   ├── src/core/                    Unity-unabhängige Logik (OpenXR, Vulkan, Runtime-Wahl, Input)
│   ├── src/unity/                   Unity-Glue (Plugin-Einstieg, Display, Input, Session, C-API)
│   ├── tools/probe/                 linuxxr-probe (Kette ohne Unity testen)
│   ├── tests/                       ctest-Suites, Fake-Unity-Host, Monado-Integrationstests
│   └── third_party/                 unity-xr-headers/ (per Skript geholt, nicht im Repo)
├── unity/
│   └── Packages/com.tyroonis.xr.linuxopenxr/   Das komplette UPM-Paket
│       ├── Runtime/                 Loader, Einstellungen, native Brücke (C#)
│       ├── Runtime/Native~/         libLinuxXR.so, libopenxr_loader.so.1, LICENSE-openxr-loader.txt (vom Unity-Importer ignoriert)
│       └── Editor/                  Registrierung (installiert Assets/LinuxXR), Setup-Assistent, Bootstrap, Status-Fenster, Issue-Bericht
├── scripts/                         Entwickler-Skripte: Build, Paketbau, Installation (dev), dazu optionale Host-Hilfen
├── .github/workflows/               CI (ci.yml) und Release (release.yml); docs/github/ enthält eine Kopie der Vorlagen und Workflows
└── docs/                            INSTALL, TROUBLESHOOTING (EN/DE), ARCHITECTURE (EN), dev/ (historische deutsche Notizen)
```

## Dokumentation

- [Installationsanleitung](docs/INSTALL.de.md) ([English](docs/INSTALL.md)) - zuerst die Installation für Anwender, am Ende "Aus dem Quellcode bauen"
- [Fehlerbehebung](docs/TROUBLESHOOTING.de.md) ([English](docs/TROUBLESHOOTING.md))
- [Architektur](docs/ARCHITECTURE.md) (nur Englisch)
- [Changelog](CHANGELOG.md) (Englisch)
- [Mitwirken und Release-Ablauf](CONTRIBUTING.md) (Englisch)
- [Historische Planungsnotizen (Deutsch)](docs/dev/README.md)

## Mitwirken und Fehler melden

Beiträge sind willkommen, siehe [CONTRIBUTING.md](CONTRIBUTING.md). Für Fehlermeldungen ein Issue öffnen und den Bericht aus
**Window -> XR -> LinuxXR Status -> Create issue report** einfügen. Er enthält Systeminfos, Versionen, Einstellungen, Statistik und die letzten 250
Zeilen des nativen Logs (dein Home-Verzeichnis wird durch `~` ersetzt).

## Danksagung

- [WiVRn](https://github.com/WiVRn/WiVRn) - die streamende OpenXR-Runtime, die das alles möglich macht.
- [Monado](https://monado.freedesktop.org/) - die quelloffene OpenXR-Runtime, auf der WiVRn aufbaut; auch für die Tests genutzt.
- [Khronos OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) - OpenXR-Header und Loader (mitgelieferter Loader: 1.1.47, Apache-2.0).
- [ValveSoftware/unity-xr-plugin](https://github.com/ValveSoftware/unity-xr-plugin) - strukturelle Referenz für einen nativen Unity-XR-Provider
  und Quelle der Unity-XR-SDK-Header (Unity Companion License), die beim Build geholt und nicht eingecheckt werden.

## Lizenz

LinuxXR steht unter der [GNU General Public License v3.0 oder später](LICENSE) (GPL-3.0-or-later), mit einer zusätzlichen Erlaubnis nach
GPLv3 Abschnitt 7: LinuxXR darf mit dem Unity-Editor, der Unity-Engine-Runtime und den Unity-XR-SDK-Headern (Unity Companion License) gelinkt und kombiniert werden,
und die Nutzung von LinuxXR als Entwicklungswerkzeug stellt dein eigenes Unity-Projekt oder Spiel nicht unter die GPL. LinuxXR läuft nur im Editor und ist nicht Teil von Player-Builds.
Der vollständige Wortlaut und die Lizenzen der Komponenten von Drittanbietern stehen in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Haftungsausschluss

LinuxXR ist ein unabhängiges Open-Source-Projekt. Es steht in keiner Verbindung zu Unity Technologies, Meta Platforms, Valve Corporation,
Khronos Group oder den Projekten WiVRn und Monado und wird von diesen weder unterstützt noch gefördert. Unity, Meta, Quest, SteamVR,
OpenXR und weitere Namen sind Marken ihrer jeweiligen Inhaber.
