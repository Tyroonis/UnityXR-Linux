# Phase 1 – „Hello Provider“ (nativer Teil fertig)

Stand: 2026-10-12 · Status: nativer Teil fertig und in der Sandbox getestet; **Unity-Test und WiVRn/Quest-Test stehen aus** (beides macht Tyroonis).

Dieses Dokument beschreibt, was Phase 1 geliefert hat, wie man es baut und ohne Unity testet, wie man das Paket in ein Unity-Projekt einbindet und was wir danach vom Test brauchen. Der Gesamtplan steht in [PLAN.md](PLAN.md), Begründungen in [ENTSCHEIDUNGEN.md](ENTSCHEIDUNGEN.md).

## Inhalt

1. [Was ist fertig](#1-was-ist-fertig)
2. [Was ist verifiziert, was nicht](#2-was-ist-verifiziert-was-nicht)
3. [Zielsystem](#3-zielsystem)
4. [Bauen](#4-bauen)
5. [Testen ohne Unity (linuxxr-probe)](#5-testen-ohne-unity-linuxxr-probe)
6. [Optional: headless mit Monado](#6-optional-headless-mit-monado)
7. [Unity-Einbindung](#7-unity-einbindung)
8. [Konfiguration und Umgebungsvariablen](#8-konfiguration-und-umgebungsvariablen)
9. [Rückmeldung an Claude](#9-rückmeldung-an-claude)
10. [Fehlerbehebung](#10-fehlerbehebung)
11. [Hinweis zu HurricaneVR, XRI und Meta SDK](#11-hinweis-zu-hurricanevr-xri-und-meta-sdk)
12. [Ausblick Phase 2](#12-ausblick-phase-2)

---

## 1. Was ist fertig

| Baustein | Pfad | Inhalt |
|---|---|---|
| `libLinuxXR.so` | `native/` (Target `LinuxXR`) | Unity-Plugin: `linuxxr_core` + Unity-Glue (`native/src/unity/`): `UnityPluginLoad`, `XRSDKPreInit`, Display- und Input-Lifecycle, C-API für P/Invoke |
| `linuxxr_core` | `native/src/core/` | Unity-unabhängige Logik: Logging, `dlopen` von OpenXR-Loader und Vulkan, Runtime-Wahl, Vulkan-Anforderungen (Runtime → Cache → Basisliste), PreInit-Logik, `XrContext` (Instanz, Session, Spaces, Swapchains, Frame-Loop, Refresh-Rate), `XrInput` (Actions, Haptik), Koordinatenumrechnung (`xr_math.h`) |
| `linuxxr-probe` | `native/tools/probe/` | Kommandozeilen-Werkzeug: testet die gesamte Kette ohne Unity |
| Tests | `native/tests/` | 2 ctest-Suites (`linuxxr_core_tests`, `linuxxr_fake_unity_host`), etwa 780 Checks, grün |
| UPM-Paket | `unity/Packages/com.tyroonis.xr.linuxopenxr/` | `LinuxXRLoader` (mit `IXRLoaderPreInit`), `LinuxXRSettings`, Status-Fenster **Window → XR → LinuxXR Status**, P/Invoke-Brücke, `UnitySubsystemsManifest.json` |
| Skripte | `scripts/` | `setup-host.sh`, `fetch-unity-headers.sh`, `build-native.sh` (neu in Phase 1) |

Verhalten in Phase 1:

- **PreInit** liefert Unity vor der Vulkan-Initialisierung die Instanz-/Geräte-Extensions und die GPU (siehe [PLAN.md](PLAN.md), Abschnitt 6.2 und ADR 5).
- **Display und Input sind Stubs.** Das Display-Subsystem meldet `renderPassesCount = 0`, Unity rendert also normal weiter, **nichts geht an die Brille**. Das Input-Subsystem registriert keine Geräte.
- OpenXR wird im Unity-Plugin in Phase 1 nur für die PreInit-Abfrage benutzt (kurzlebige Instanz). Session, Swapchains und Frame-Loop sind in `XrContext` fertig und werden von `linuxxr-probe` genutzt, aber noch nicht vom Display-Subsystem.

Technische Eckpunkte des nativen Codes:

- Der OpenXR-Loader (`libopenxr_loader.so.1`) und Vulkan (`libvulkan.so.1`) werden per `dlopen` geladen, es gibt keine Link-Abhängigkeit. Zur Build-Zeit genügen Header (`libvulkan-dev`, `libopenxr-dev`, Unity-XR-Header).
- Vulkan-Extensions für PreInit: Runtime-Abfrage (falls erreichbar) → Cache (`$XDG_CACHE_HOME/linuxxr/vulkan_requirements.txt`) → Basisliste. Das Ergebnis wird **immer gegen das tatsächlich vom Treiber Unterstützte gefiltert**, damit `vkCreateInstance`/`vkCreateDevice` nicht an einer fehlenden Extension scheitern.
- Runtime-Wahl: Projekteinstellung bzw. `Library/LinuxXR/settings.txt` (vom C#-Teil geschrieben, vom PreInit gelesen, weil der Editor mit dem Projektordner als Arbeitsverzeichnis läuft), Umgebungsvariable `LINUXXR_RUNTIME`, sonst *auto*: WiVRn (PPA oder Flatpak) > `active_runtime.json` > Monado. Symlinks von `active_runtime.json` werden aufgelöst.
- Logdatei: `~/.cache/linuxxr/linuxxr.log`.

## 2. Was ist verifiziert, was nicht

**Verifiziert (Sandbox, ohne GPU-Hardware und ohne Headset):** headless Monado 21 mit lavapipe (`XRT_COMPOSITOR_NULL=1 SIMULATED_ENABLE=1`).

- PreInit-Pfad **mit erreichbarer Runtime**: Extension-Liste kommt von der Runtime und wird gecacht.
- PreInit-Pfad **ohne Runtime**: Basisliste. Die Basisliste deckte sich exakt mit Monados Anforderung.
- `linuxxr-probe run`: kompletter Session-Zyklus `IDLE → READY → SYNCHRONIZED → VISIBLE → FOCUSED → … → EXITING`, Swapchain-Array mit 2 Ebenen, Frames werden übertragen, Kopf-Tracking liefert Werte.
- Beide ctest-Suites grün.

**Nicht verifiziert:**

- **WiVRn und Quest** (echtes Headset, Encoder, Netzwerk, Controller-Eingaben, Haptik). Das macht Tyroonis, siehe Abschnitt 5.
- **Unity selbst** (Laden des Plugins im Editor, Aufruf von PreInit, Subsystem-Start im Play Mode, Status-Fenster). Das macht Tyroonis, siehe Abschnitt 7.
- Verhalten auf AMD/RADV mit realer GPU.

## 3. Zielsystem

| Punkt | Wert |
|---|---|
| GPU | AMD Radeon RX 7600 (RADV) |
| OS | Ubuntu 26.04.1, Kernel 7.0, glibc 2.43, GNOME Wayland |
| Unity | 6000.6.5f1 (mit Android-Modul), URP-Testprojekt „XRTestbed“ |
| OpenXR-Loader | 1.1.47 (`libopenxr-loader1`) |
| WiVRn | Flatpak `io.github.wivrn.wivrn` (siehe ADR 2); baut mit Vulkan-Video-Encode, passend zur RX 7600 |
| Netz | PC hängt per USB-WLAN-Stick im Netz |

**Empfehlung Netzwerk:** Der USB-WLAN-Stick ist für Streaming die schwächste Stelle. Besser ist ein **LAN-Kabel** zum Router oder, falls das nicht geht, **USB direkt zur Quest** (`./scripts/quest-usb.sh` oder in der WiVRn-App „USB-Netzwerk“). Details in [PLAN.md](PLAN.md), Abschnitt 9.

## 4. Bauen

```bash
# Build-Werkzeuge und Basis (WiVRn-Flatpak, OpenXR-Loader, Vulkan-/OpenXR-Header, cmake, ninja)
./scripts/setup-host.sh --dev

# Bauen (Release/RelWithDebInfo) – holt die Unity-XR-Header automatisch
./scripts/build-native.sh

# Bauen und alle Tests ausführen
./scripts/build-native.sh test

# Weitere Modi
./scripts/build-native.sh debug     # Debug-Build
./scripts/build-native.sh clean     # native/build löschen
```

Ergebnis:

- `native/build/libLinuxXR.so`
- `native/build/linuxxr-probe`
- `libLinuxXR.so` wird automatisch nach `unity/Packages/com.tyroonis.xr.linuxopenxr/Runtime/Plugins/Linux/x86_64/` kopiert.

Die Unity-XR-Header holt `scripts/fetch-unity-headers.sh` (gepinnter Commit aus `ValveSoftware/unity-xr-plugin`) nach `native/third_party/unity-xr-headers/`. Sie sind absichtlich **nicht** im Repo (Unity Companion License, siehe ADR 8). Ohne Header baut CMake nur `linuxxr_core`, `linuxxr-probe` und die Core-Tests, aber **nicht** `libLinuxXR.so`.

Nur die Header neu holen:

```bash
./scripts/fetch-unity-headers.sh --force
```

Erwartung bei `./scripts/build-native.sh test`: beide Suites (`linuxxr_core_tests`, `linuxxr_fake_unity_host`) grün. Der Fake-Host lädt `libLinuxXR.so` per `dlopen` und simuliert den Unity-Editor, ohne echte Runtime.

## 5. Testen ohne Unity (linuxxr-probe)

`linuxxr-probe` bildet den Ablauf des Editors nach: PreInit (Vulkan-Extensions, GPU) → eigene `VkInstance`/`VkDevice` wie Unity → OpenXR-Instanz und Session erst danach („Play“) → Frame-Loop mit Farbflächen pro Auge.

### 5.1 Befehle

```bash
native/build/linuxxr-probe runtimes   # gefundene OpenXR-Runtimes und Erreichbarkeit
native/build/linuxxr-probe info       # Instanz, System, Extensions, Vulkan-Anforderungen der Runtime
native/build/linuxxr-probe preinit    # Unitys PreInit simulieren (Extensions + GPU-Wahl, vkCreateDevice)
native/build/linuxxr-probe run        # Session starten, Farbflächen, Posen/Tasten ausgeben
```

Optionen:

| Option | Wirkung |
|---|---|
| `--runtime X` | `auto` \| `wivrn` \| `monado` \| `active` \| `/pfad/manifest.json` (Standard `auto`) |
| `--seconds N` | Laufzeit für `run` (Standard 30, `0` = bis Strg+C) |
| `--origin O` | `floor` \| `device` (Standard `floor`) |
| `--no-haptics` | keine Vibration |
| `--quiet` | keine Statuszeile pro Sekunde |
| `--debug` | ausführliches Log (entspricht `LINUXXR_LOG=debug`) |

### 5.2 Ablauf mit WiVRn und Quest

1. WiVRn-Dashboard starten (Flatpak):

   ```bash
   flatpak run io.github.wivrn.wivrn
   ```

2. Quest: WiVRn-App öffnen, den PC wählen, beim ersten Mal per PIN koppeln. Die **Version von Server und Client muss exakt zusammenpassen** (siehe Fehlerbehebung).
3. Headset verbunden lassen und erst dann prüfen, ob die Runtime erreichbar ist:

   ```bash
   native/build/linuxxr-probe runtimes
   ```

   Erwartet: ein WiVRn-Eintrag (Quelle `wivrn-flatpak` bzw. `wivrn-flatpak-user`) mit dem Hinweis „WiVRn: Headset verbunden.“ Ohne verbundenes Headset fehlt der Socket `$XDG_RUNTIME_DIR/wivrn/comp_ipc` und der Eintrag gilt als nicht erreichbar.

4. Details der Runtime und der Vulkan-Anforderungen ansehen:

   ```bash
   native/build/linuxxr-probe info
   native/build/linuxxr-probe preinit
   ```

   `preinit` soll mit `vkCreateDevice: ok` enden und als GPU die Radeon RX 7600 nennen. Bei „Quelle: runtime“ stammt die Extension-Liste von WiVRn, bei „cache“/„fallback“ nicht.

5. Die eigentliche Probe:

   ```bash
   native/build/linuxxr-probe run
   ```

   Headset aufsetzen. Strg+C beendet sauber (ein zweites Strg+C bricht hart ab).

### 5.3 Was man sehen soll

Auf der Quest:

| Beobachtung | Bedeutung |
|---|---|
| **Links rot, rechts blau** | Beide Augen bekommen unterschiedliche Ebenen der Array-Swapchain, Seiten sind nicht vertauscht |
| **Trigger** (links bzw. rechts) hellt die Farbe des jeweiligen Auges auf | Analog-Eingabe funktioniert; ohne Trigger ist die Fläche dunkler |
| **A bzw. X** färbt das jeweilige Auge grün | Digital-Eingabe `primaryButton` |
| **B bzw. Y** färbt das jeweilige Auge hellblau | Digital-Eingabe `secondaryButton` |
| **Grip** (fest drücken) löst einen kurzen Vibrationsimpuls aus | Haptik funktioniert |

Dabei wirken die Eingaben der linken Hand auf das linke, die der rechten Hand auf das rechte Auge.

Im Terminal erscheint **einmal pro Sekunde** eine Statuszeile, etwa in dieser Form:

```
[FOCUSED     ]  90.0 fps | Kopf +0.01 +1.62 +0.03 | L touch_controller_plus T0.00 G0.00 S+0.0/+0.0 · | R touch_controller_plus T0.00 G0.00 S+0.0/+0.0 ·
```

(Werte beispielhaft.) Bedeutung: Session-Zustand, Bildrate, Kopfposition in Unity-Koordinaten (Meter), je Hand das aktive Interaktionsprofil, Trigger `T`, Grip `G`, Thumbstick `S x/y` und gedrückte Tasten (`P` = A/X, `S` = B/Y, `K` = Stick-Klick, `M` = Menü, `t` = Trigger-Touch, `r` = Daumenablage; `·` = nichts).

### 5.4 Erfolgskriterien

- [ ] `runtimes` zeigt WiVRn als erreichbar, solange das Headset verbunden ist.
- [ ] `preinit` endet mit `vkCreateDevice: ok` auf der RX 7600.
- [ ] `run` erreicht `FOCUSED` (Statuszeile), ohne Fehlermeldung im Terminal.
- [ ] Bildrate entspricht der Display-Rate des Headsets (72, 90 oder 120 Hz) und bleibt stabil.
- [ ] Links rot, rechts blau; Kopfbewegung ändert die Kopfposition und das Bild bleibt stabil im Raum.
- [ ] Trigger, A/X, B/Y und Grip-Vibration reagieren wie oben beschrieben, in der Statuszeile ist das Interaktionsprofil beider Hände eingetragen.
- [ ] Strg+C beendet die Session sauber (`Frames übertragen: N`, Rückkehr zur Shell), der WiVRn-Server läuft weiter, ein erneuter Start von `run` funktioniert ohne Neustart von Server oder App.

Falls etwas nicht stimmt: Abschnitt 10 und die Rückmeldung in Abschnitt 9.

## 6. Optional: headless mit Monado

Für Tests ohne Headset, zum Beispiel um Änderungen am Code zu prüfen. So wurde die Sandbox-Verifikation gemacht.

```bash
./scripts/setup-host.sh --monado
```

Terminal 1 (headless, simuliertes HMD und Controller):

```bash
XRT_COMPOSITOR_NULL=1 SIMULATED_ENABLE=1 monado-service
```

Terminal 2:

```bash
native/build/linuxxr-probe runtimes --runtime monado
native/build/linuxxr-probe run --runtime monado --seconds 10
```

Mit einer echten GPU kann statt des Null-Compositors ein Fenster genutzt werden (siehe [PLAN.md](PLAN.md), Phase 0b: `QWERTY_ENABLE=1 XRT_DEBUG_GUI=1 monado-service`); das ist hier nicht getestet. `monado-service` registriert sich systemweit per `update-alternatives`; LinuxXR wählt die Runtime pro Prozess und ändert diese Auswahl nicht.

## 7. Unity-Einbindung

Voraussetzung: `./scripts/build-native.sh` ist gelaufen, `libLinuxXR.so` liegt im Paket.

### 7.1 Paket einbinden

1. In einem **frischen Unity-6-URP-Projekt** (Testprojekt „XRTestbed“, Build-Target Linux belassen): **Window → Package Manager → „+“ → Add package from disk…**
2. Datei wählen:

   ```
   unity/Packages/com.tyroonis.xr.linuxopenxr/package.json
   ```

   Das Paket hängt von `com.unity.xr.management` 4.5.0 ab; Unity löst das automatisch auf.

### 7.2 XR Plug-in Management

1. **Edit → Project Settings → XR Plug-in Management**; falls angeboten, **Install XR Plug-in Management** klicken.
2. Tab **Windows, Mac, Linux** (Standalone):
   - **Initialize XR on Startup** aktivieren.
   - **LinuxXR** anhaken.
   - **OpenXR dort nicht anhaken.** Unitys OpenXR-Paket blockiert den Play Mode auf Linux (siehe [PLAN.md](PLAN.md), Abschnitt 2 und R7).
3. Tab **Android** (nur für echte Builds auf der Quest, nicht für Phase 1 nötig): **OpenXR** aktivieren und das Feature **Meta Quest Support**. Der Spielcode bleibt für beide Wege derselbe (ADR 6).
4. Einstellungen von LinuxXR (Runtime-Wahl, Tracking-Origin, Stereo-Modus, Mirror, Render Scale, ausführliches Log) stehen unter **Project Settings → XR Plug-in Management → LinuxXR**. Standard ist *Automatisch (WiVRn bevorzugt)*.

### 7.3 Vulkan als erste Grafik-API

LinuxXR funktioniert nur mit Vulkan (ADR 4). Am einfachsten über das Status-Fenster:

1. **Window → XR → LinuxXR Status** öffnen.
2. **Vulkan setzen** klicken. Das setzt für Linux die Grafik-APIs auf Vulkan, dann OpenGLCore, und bietet den Neustart an.

Alternativ von Hand: Player Settings → Other Settings → Auto Graphics API for Linux aus, Vulkan an erste Stelle. Oder den Editor einmalig mit `-force-vulkan` starten.

### 7.4 Editor neu starten

**Der PreInit läuft nur beim Editor-Start.** Nach dem erstmaligen Aktivieren von LinuxXR, nach dem Setzen von Vulkan und nach Änderungen an der Runtime-Wahl den Editor komplett neu starten. Beim Neustart wird die Grafik-API und die Extension-Liste festgelegt; ein späteres Aktivieren im laufenden Editor reicht nicht.

### 7.5 Erwartete Console-Zeilen

Alle Zeilen tragen das Präfix `[LinuxXR]`. Die Version in der ersten Zeile ist die aus `CMakeLists.txt` (derzeit 0.1.0).

| Zeitpunkt | Zeile (sinngemäß) |
|---|---|
| Editor-Start | `LinuxXR 0.1.0 PreInit (Runtime-Präferenz: auto)` |
| Editor-Start | `PreInit-Provider registriert (Quelle der Vulkan-Liste: runtime\|cache\|fallback)` |
| Editor-Start / Plugin-Laden | `LinuxXR 0.1.0 geladen (PreInit ausgeführt)` |
| Play | `Initialisiert (Version 0.1.0)` (aus dem C#-Loader) |
| Play | `Display-Subsystem initialisiert (PreInit ok, Vulkan-Liste: …, GPU: AMD Radeon RX 7600 …)` |
| Play | `Input-Subsystem initialisiert (Phase 1: noch keine Geräte)` |
| Play | `Display-Subsystem gestartet` |
| Play | `Display (Gfx-Thread) gestartet – Phase-1-Stub: noch keine Ausgabe an die Brille` |
| Play, erster Frame | `Erster PopulateNextFrameDesc: singlePass=… sRGB=… zNear=… zFar=… scale=…` |
| Stop | `Display-Subsystem gestoppt`, `Display (Gfx-Thread) gestoppt nach N Frames`, `Display-Subsystem beendet` |

Die genaue Reihenfolge der Zeilen beim Play-Start legt Unity fest; sie ist nicht zugesichert.

Das Status-Fenster zeigt Runtime, Headset, Grafik-API, PreInit und Session. In Phase 1 ist „Session“ immer „keine Session“, denn das Display-Subsystem öffnet noch keine OpenXR-Session.

### 7.6 Erfolgskriterien Unity (Phase 1)

- [ ] Editor startet ohne Absturz und ohne `DllNotFoundException`.
- [ ] „LinuxXR“ erscheint unter XR Plug-in Management auf dem Standalone-Tab.
- [ ] Status-Fenster: Grafik-API „Vulkan ✔“, PreInit „ok“.
- [ ] Die Console-Zeilen aus 7.5 erscheinen.
- [ ] Play und Stop mehrfach hintereinander ohne Fehler und ohne Absturz. Die Game View rendert wie gewohnt (die Brille bleibt in Phase 1 unberührt).

Das Ergebnis beantwortet nebenbei offene Punkte aus dem Plan: R1 (läuft der Editor auf 26.04 stabil?), R2 (laden die gepinnten Header-Stände in Unity 6000.6?) und R7 (nutzt der Play Mode die Standalone-Einstellungen?).

## 8. Konfiguration und Umgebungsvariablen

| Variable | Wirkung |
|---|---|
| `LINUXXR_RUNTIME` | Runtime-Wahl, wenn die Projekteinstellung auf *auto* steht: `wivrn` \| `monado` \| `active` \| Pfad zu einem Manifest |
| `LINUXXR_GPU` | GPU-Wahl für PreInit und Session (Index oder Name); nur bei mehreren GPUs nötig |
| `LINUXXR_LOG=debug` | ausführliches Log |
| `LINUXXR_OPENXR_LOADER` | Pfad zu einer anderen `libopenxr_loader.so.1` |
| `XR_RUNTIME_JSON` | wird vom Plugin respektiert, wenn schon gesetzt; steht vor den Fallbacks |

Wichtige Dateien:

| Datei | Zweck |
|---|---|
| `~/.cache/linuxxr/linuxxr.log` | Logdatei des Plugins (auch ohne Unity-Console) |
| `~/.cache/linuxxr/vulkan_requirements.txt` | Cache der Vulkan-Extension-Liste der Runtime |
| `<Projekt>/Library/LinuxXR/settings.txt` | vom C#-Teil geschrieben, vom PreInit gelesen |
| `~/.var/app/io.github.wivrn.wivrn/config/wivrn/config.json` | Konfiguration des WiVRn-Flatpaks (Encoder usw.) |
| `~/.config/unity3d/Editor.log` | Log des Unity-Editors |

Variablen für den Editor müssen **vor dem Editor-Start** gesetzt sein, zum Beispiel:

```bash
LINUXXR_LOG=debug XR_LOADER_DEBUG=all ~/Unity/Hub/Editor/6000.6.5f1/Editor/Unity -projectPath <Projektordner>
```

(Pfad zum Editor ist ein Beispiel; je nach Installation anpassen.)

Pfade des Runtime-Manifests im Flatpak (LinuxXR findet sie im Modus *auto* selbst):

```
/var/lib/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn.json
~/.local/share/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn.json   (bei --user)
```

Der Socket liegt weiterhin auf dem Host unter `$XDG_RUNTIME_DIR/wivrn/comp_ipc`.

## 9. Rückmeldung an Claude

Bitte schicken, sobald getestet:

1. **Ausgabe von `probe run` mit WiVRn**, inklusive `runtimes`, `info` und `preinit`:

   ```bash
   native/build/linuxxr-probe runtimes
   native/build/linuxxr-probe info
   native/build/linuxxr-probe preinit
   native/build/linuxxr-probe run --seconds 20
   ```

   Dazu in Worten: Bild auf der Quest (links rot, rechts blau?), reagieren Trigger, A/X, B/Y, Grip? Wie ist die Bildrate in der Statuszeile?
2. **Console-Zeilen aus Unity** (alles mit `[LinuxXR]`), vom Editor-Start bis nach Play/Stop. Das Status-Fenster hat dafür den Button **Diagnose kopieren**.
3. **`~/.cache/linuxxr/linuxxr.log`**, bei Problemen mit `LINUXXR_LOG=debug` erzeugt.
4. Nur bei Abstürzen oder Fehlern im Editor: die letzten Zeilen von `~/.config/unity3d/Editor.log`.
5. Antworten auf die offenen Fragen in [PLAN.md](PLAN.md), Abschnitt 13 (insbesondere zu Meta SDK und HurricaneVR, siehe Abschnitt 11).

## 10. Fehlerbehebung

| Symptom | Ursache | Lösung |
|---|---|---|
| Status-Fenster: „PreInit: nicht gelaufen“; Log: `geladen (PreInit NICHT ausgeführt – …)` | LinuxXR wurde erst nach dem Editor-Start aktiviert, oder der Loader ist nicht auf dem Standalone-Tab angehakt, oder `libLinuxXR.so` fehlte beim Start | Loader auf dem Tab *Windows, Mac, Linux* anhaken, `./scripts/build-native.sh` prüfen, Editor **komplett neu starten** |
| Console: „Der Editor läuft mit … LinuxXR braucht Vulkan“ bzw. PreInit-Warnung „Grafik-API ist nicht Vulkan“ | Linux-Grafik-API ist nicht Vulkan oder der Editor wurde nach der Umstellung nicht neu gestartet | Status-Fenster → **Vulkan setzen**, Editor neu starten; alternativ Editor mit `-force-vulkan` starten |
| Console: „libLinuxXR.so nicht gefunden“, `DllNotFoundException` | Plugin nicht gebaut oder nicht ins Paket kopiert | `./scripts/build-native.sh`; prüfen, dass `unity/Packages/com.tyroonis.xr.linuxopenxr/Runtime/Plugins/Linux/x86_64/libLinuxXR.so` existiert; Editor neu starten |
| `build-native.sh`: „… fehlt“ oder CMake-Warnung „Unity-XR-Header fehlen“ | Build-Werkzeuge oder Header nicht vorhanden | `./scripts/setup-host.sh --dev`, `./scripts/fetch-unity-headers.sh`; ohne Header wird `libLinuxXR.so` nicht gebaut |
| Console: „Subsysteme konnten nicht erstellt werden“ | `UnitySubsystemsManifest.json` wurde nicht gefunden oder passt nicht zu den registrierten IDs | Paket vollständig eingebunden? Console auf frühere Fehler ab Editor-Start prüfen; Log-Datei ansehen |
| `runtimes`: „Keine OpenXR-Runtime gefunden“ | WiVRn-Flatpak nicht installiert, oder Manifest nicht am erwarteten Pfad | `flatpak info io.github.wivrn.wivrn`; `./scripts/setup-host.sh`; Pfad notfalls per `--runtime /pfad/openxr_wivrn.json` bzw. `LINUXXR_RUNTIME` setzen |
| Runtime gefunden, aber „nicht erreichbar“ / Status „Headset: nicht verbunden“ | Der Socket `$XDG_RUNTIME_DIR/wivrn/comp_ipc` existiert erst, wenn der Server läuft **und** ein Headset verbunden ist | Dashboard starten (`flatpak run io.github.wivrn.wivrn`), WiVRn-App auf der Quest öffnen und verbinden, **dann** Probe bzw. Play starten; Kontrolle: `ls $XDG_RUNTIME_DIR/wivrn/` |
| Quest: „Incompatible protocol version“ | Client (Quest-App) und Server (Flatpak) haben unterschiedliche Versionen; der Meta Store kann hinterherhinken | Flatpak aktualisieren (`flatpak update io.github.wivrn.wivrn`) und passenden Client installieren: im Dashboard „Install on headset“ oder passende APK von den WiVRn-Releases sideloaden |
| Quest findet den PC nicht / Verbindung bricht ab | Firewall oder mDNS blockiert, falsches Netz, schwaches WLAN | `5353/udp` und `9757/tcp+udp` freigeben (macht `setup-host.sh` bei aktivem ufw); `systemctl status avahi-daemon`; PC und Quest im selben Netz; besser LAN-Kabel am PC oder USB-Verbindung (`./scripts/quest-usb.sh`), siehe [PLAN.md](PLAN.md), Abschnitt 9 |
| `xrCreateSession` oder Geräteerzeugung scheitert mit fehlender Vulkan-Extension; im Status „PreInit: ok (Cache/Basisliste)“ | Beim Editor-Start war die Runtime nicht erreichbar, die Extension-Liste stammt aus Cache oder Basisliste und deckt eine Anforderung der Runtime nicht ab | Headset verbinden und **Editor neu starten**, damit die Liste von der Runtime abgefragt und gecacht wird (Status: „ok (Liste von der Runtime)“). Bei Verdacht Cache löschen: `rm ~/.cache/linuxxr/vulkan_requirements.txt` |
| Warnung „Runtime möchte ein anderes VkPhysicalDevice“ | Mehrere GPUs, Unity und Runtime wählen unterschiedlich | `LINUXXR_GPU` (Index oder Name) und `XRT_COMPOSITOR_FORCE_GPU_INDEX` auf dieselbe GPU setzen; bei der Single-GPU-RX-7600 unkritisch |
| Log leer oder zu knapp | Standard-Loglevel ist Info | `LINUXXR_LOG=debug` setzen oder in den LinuxXR-Einstellungen „Verbose Logging“ aktivieren; Datei: `~/.cache/linuxxr/linuxxr.log` |
| Editor stürzt ab oder zeigt Grafikfehler | Unity 6 auf Ubuntu 26.04 ist nicht offiziell unterstützt (R1) | Zuerst ohne LinuxXR testen (Loader abwählen). Tritt der Fehler auch ohne LinuxXR auf, liegt es an Unity/System. Sonst `~/.config/unity3d/Editor.log` und die LinuxXR-Logdatei schicken |
| `probe run`: Bild ruckelt, Bildrate unter der Display-Rate | Netz (USB-WLAN-Stick), Encoder, Bitrate | LAN-Kabel oder USB zur Quest, Bitrate/Auflösung in der WiVRn-Client-App senken, siehe [PLAN.md](PLAN.md), Abschnitt 9 |

Allgemeine Debug-Werkzeuge (`XR_LOADER_DEBUG=all`, API-Dump-Layer, Vulkan-Validation) stehen in [PLAN.md](PLAN.md), Abschnitt 10. Die Reihenfolge bei Fehlern: erst `./scripts/test-openxr.sh wivrn` (`hello_xr`) prüfen, dann `linuxxr-probe`, dann Unity.

## 11. Hinweis zu HurricaneVR, XRI und Meta SDK

Kurzfassung (Begründung und Empfehlung in [PLAN.md](PLAN.md), Abschnitt 8, Risiko R17):

- **HurricaneVR** und das **XR Interaction Toolkit (XRI)** lesen Unity-XR-Input (`InputDevices`/`CommonUsages` bzw. Input-System-`XRController`). Sie funktionieren mit LinuxXR, sobald die Phasen 3 und 4 die Geräte liefern. In Phase 1 gibt es noch keine Geräte.
- **Meta-SDK-Komponenten, die OVRPlugin nutzen** (`OVRCameraRig`, `OVRInput`, `OVRManager`), bekommen im Linux-Editor **keine Daten**: OVRPlugin gibt es nicht für Linux. Außerdem kompilieren Meta-XR-SDK-Versionen über v74.0.3 laut Meta-Forum im Linux-Editor nicht ([Recherche 02](research/02-unity-openxr-linux.md)).
- Empfehlung: Rig auf **XR Origin / Unity-XR-Basis** bauen, das Meta SDK nur für Android-Build-Features nutzen und gegebenenfalls per Assembly-Define oder Plattformabfrage vom Rest trennen.

Offene Frage an Tyroonis: Welche Teile des Meta SDK nutzt du (Interaction SDK, Passthrough, Hand Tracking, OVRInput, Haptics, Platform SDK, …), und welche HurricaneVR-Version mit welchem Input-Modus (XR Input / Input System / OpenXR-Actions)?

## 12. Ausblick Phase 2

Ziel (siehe [PLAN.md](PLAN.md), Phase 2): echte OpenXR-Session im Editor, pro Auge eine Clear-Color auf der Quest, Session erreicht `FOCUSED`, Play-Stop beendet sauber.

Geplanter Ablauf in `native/src/unity/display_provider.cpp`:

| Callback | Aufgabe |
|---|---|
| Start (Gfx-Thread) | OpenXR-Instanz und Session mit Unitys Vulkan-Objekten (`IUnityGraphicsVulkan`), Swapchains anlegen (Array mit 2 Ebenen für Single-Pass) |
| `PopulateNextFrameDesc` | `pollEvents`, `xrWaitFrame`/`xrBeginFrame`, `xrLocateViews`; Render-Pass(es) mit Augenposen und FOV füllen |
| `SubmitCurrentFrame` | **Unity-Textur in das Swapchain-Bild kopieren** (`IUnityGraphicsVulkan::AccessTexture` und `AccessQueue` mit `flush`), `xrReleaseSwapchainImage`, `xrEndFrame` mit Projection-Layer |

Zwei Designentscheidungen dazu stehen als Vorschlag in [ENTSCHEIDUNGEN.md](ENTSCHEIDUNGEN.md): ADR 9 („Kopie statt Direkt-Rendering in Swapchain-Bilder“) und ADR 10 („`XR_KHR_vulkan_enable` (v1) statt `enable2` im Editor“). Die Haupt-Unbekannten bleiben die undokumentierte Semantik der Vulkan-Zeiger in `IUnityGraphicsVulkan`, Layout-Übergänge und die Synchronisation zwischen Unitys Queue und der OpenXR-Runtime.
