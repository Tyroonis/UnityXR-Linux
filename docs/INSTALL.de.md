# Installationsanleitung

[English](INSTALL.md) | **Deutsch**

Diese Anleitung führt dich von einem frisch eingerichteten Linux-Rechner bis zu dem Punkt, an dem du im Unity-Editor unter Linux auf **Play** drückst und die Szene auf einer Meta Quest siehst.
Teil 1 bis 8 richtet sich an **Anwender**: LinuxXR wird über Unitys Package Manager installiert, ohne Skripte und ohne Build-Werkzeuge.
[Aus dem Quellcode bauen](#aus-dem-quellcode-bauen-entwickler) am Ende ist für **Entwickler** von LinuxXR.

Inhalt: [1 Überblick](#1-voraussetzungen-und-überblick) - [2 WiVRn und Quest](#2-wivrn-und-die-quest-host-seite) -
[3 Paket installieren](#3-paket-installieren) - [4 Erster Start und Setup-Assistent](#4-erster-start-und-setup-assistent) -
[5 Unity-Einstellungen](#5-unity-einstellungen-was-der-assistent-erledigt) - [6 Szene einrichten](#6-szene-einrichten) - [7 Play drücken](#7-play-drücken) -
[8 Performance](#8-performance-tipps) - [9 Aktualisieren](#9-aktualisieren) - [10 Deinstallieren](#10-deinstallieren) -
[11 Optionale Hilfsskripte](#11-optionale-hilfsskripte) - [Aus dem Quellcode bauen](#aus-dem-quellcode-bauen-entwickler)

## 1. Voraussetzungen und Überblick

Du brauchst:

- Einen Linux-PC mit Vulkan-fähiger GPU und glibc 2.35 oder neuer. Getestet: Ubuntu 26.04 GNOME (Wayland, Editor über XWayland), AMD Radeon RX 7600 (RADV, Mesa 26).
  WiVRn kodiert Video mit VAAPI / Vulkan Video (AMD, Intel) oder NVENC (NVIDIA).
- Unity 6 (getestet: 6000.6.5f1 mit URP), installiert über den Unity Hub (nativ oder als Flatpak). Unity unterstützt offiziell nur Ubuntu 22.04 / 24.04.
- Eine Meta Quest (getestet: Quest 3).
- Netzwerk: der PC per **LAN-Kabel**, die Quest im **5-GHz-Band / Wi-Fi 6** - oder ein USB-Kabel. Firewall-Port 9757 TCP+UDP offen.

Was in welcher Reihenfolge passiert:

| Schritt | Was | Einmalig? |
|---------|-----|-----------|
| 2 | WiVRn auf dem PC und die WiVRn-App auf der Quest installieren, koppeln, Firewall-Port öffnen | ja |
| 3 | Das LinuxXR-Paket in dein Unity-Projekt einbauen (Package Manager) | pro Projekt |
| 4 | Editor neu starten, dem Setup-Assistenten folgen (Vulkan, XR Plug-in Management, Flatpak-Zugriff, ...) | pro Projekt |
| 5-6 | Unity-Einstellungen prüfen, Szene einrichten (XR Origin) | pro Projekt / Szene |
| 7 | WiVRn starten, Headset verbinden, Play drücken | jedes Mal |

Das native Plugin und der OpenXR-Loader gehören zum Paket. Du brauchst **keinen** Compiler, kein `libopenxr` und kein Skript.

## 2. WiVRn und die Quest (Host-Seite)

### 2.1 WiVRn auf dem PC installieren

Flatpak (empfohlen; der Setup-Assistent oder `setup-host.sh` erledigt das mit `flatpak install --user`):

```bash
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub io.github.wivrn.wivrn
flatpak run io.github.wivrn.wivrn          # startet das Dashboard
```

Eine systemweite Installation (`flatpak install flathub io.github.wivrn.wivrn`) funktioniert ebenfalls; siehe aber [Abschnitt 4.3](#43-unity-hub-als-flatpak),
falls dein Unity Hub ein Flatpak ist.

Alternative unter Ubuntu 26.04: das PPA.

```bash
./scripts/setup-host.sh --wivrn=ppa        # fügt ppa:lvra/wivrn hinzu und installiert wivrn-server wivrn-dashboard
wivrn-dashboard                            # oder: wivrn-server
```

### 2.2 WiVRn-App auf der Quest installieren

Installiere die App **"WiVRn"** aus dem Meta Horizon Store oder per Sideload. Das WiVRn-**Dashboard** bietet außerdem "Install on headset" (über `adb`;
`adb` installierst du mit `./scripts/setup-host.sh --adb`). **Client- und Server-Version müssen exakt übereinstimmen**; die Store-Version hinkt dem
Server manchmal hinterher. Eine Abweichung zeigt sich als "incompatible protocol version" (siehe [TROUBLESHOOTING.de](TROUBLESHOOTING.de.md)).

### 2.3 Koppeln

1. WiVRn-Server / Dashboard auf dem PC starten.
2. Auf der Quest die **WiVRn**-App öffnen und den PC auswählen.
3. Die am PC angezeigte PIN eingeben (nur beim ersten Mal).

Netzwerk: 5 GHz oder Wi-Fi 6 für die Quest, LAN-Kabel für den PC. Ports: 9757 TCP+UDP, 5353/UDP für die mDNS-Erkennung (avahi).

**USB statt WLAN** (Quest 3): In der WiVRn-App den Schalter **"USB networking"** aktivieren, oder den adb-Tunnel nutzen:

```bash
./scripts/setup-host.sh --adb              # einmalig, installiert adb
./scripts/quest-usb.sh                     # adb reverse tcp:9757, startet die WiVRn-App gegen 127.0.0.1
./scripts/quest-usb.sh logs                # logcat des WiVRn-Clients
./scripts/quest-usb.sh stop                # Tunnel entfernen
```

Der adb-Weg braucht den Entwicklermodus und erlaubtes USB-Debugging im Headset. Schalte das WLAN am Headset aus, damit nichts über die Luft läuft.

### 2.4 Firewall

Öffne Port **9757 TCP und UDP** (WiVRn) sowie **5353/UDP** (mDNS-Erkennung; braucht `avahi-daemon`). Mit `ufw`:

```bash
sudo ufw allow 9757/tcp && sudo ufw allow 9757/udp && sudo ufw allow 5353/udp
```

`./scripts/setup-host.sh` erledigt das (und mehr) für dich, ist aber optional.

## 3. Paket installieren

Wähle **einen** der drei Wege. Alle laufen über Unitys Package Manager bzw. den Import-Dialog; im Terminal wird nichts ausgeführt.
Das Paket heißt `com.tyroonis.xr.linuxopenxr` 0.3.3 und hängt von `com.unity.xr.management` 4.5.0 (XR Plug-in Management) ab.

### 3.1 Git-URL (empfohlen)

*Window -> Package Manager -> + -> Add package from git URL...* und eingeben:

```
https://github.com/tyroonis/UnityVR-Linux.git#upm
```

Um ein Release festzulegen, nimm stattdessen den Tag: `https://github.com/tyroonis/UnityVR-Linux.git#upm/v0.3.3`.
Den `upm`-Branch veröffentlicht die Release-CI (siehe [ARCHITECTURE](ARCHITECTURE.md#8-distribution-and-ci), englisch); er enthält das Paket **mit den fertigen Binärdateien**.
Ersetze `tyroonis` durch das GitHub-Konto, das das Repository hostet. Für diese Variante muss `git` installiert sein. Bei Git-URL-Installationen zeigt Unity in der Regel keine Signaturwarnung.

### 3.2 Tarball

Lade `com.tyroonis.xr.linuxopenxr-0.3.3.tgz` von der GitHub-Releases-Seite des Repositorys, dann
*Package Manager -> + -> Add package from tarball...* und die Datei wählen. Bewahre die `.tgz` an einem festen Ort auf (Unity verweist per Pfad darauf);
du kannst sie auch in dein Projekt einchecken.

Unity 6.3+ zeigt für den Tarball eventuell **"package is not signed"**. Das ist nur eine Warnung, das Paket funktioniert. Release-Tarballs sind signiert, wenn der Maintainer
die Signierung eingerichtet hat (siehe [CONTRIBUTING](../CONTRIBUTING.md#signing-the-tarball), englisch); sonst bleibt die Warnung.

### 3.3 .unitypackage

Lade `LinuxXR-0.3.3.unitypackage` (GitHub Releases oder Asset Store) und importiere es mit *Assets -> Import Package -> Custom Package...*.
Der C#-Code landet in `Assets/Tyroonis/LinuxXR`; die nativen Dateien kommen direkt nach `Assets/LinuxXR` (siehe 3.4).

### 3.4 Der native Teil in Assets/LinuxXR

Unity registriert native XR-Subsysteme nur aus einer `UnitySubsystemsManifest.json` in einem direkten Unterordner von `Assets/` (oder aus Registry-Paketen), und nur beim Start des Editors.
Tarball-, Git- und eingebettete Pakete werden nicht durchsucht. Damit alle Installationswege gleich funktionieren, installiert LinuxXR seinen nativen Teil beim ersten Laden (nur im Linux-Editor):

```
Assets/LinuxXR/UnitySubsystemsManifest.json
Assets/LinuxXR/Plugins/Linux/x86_64/libLinuxXR.so
Assets/LinuxXR/Plugins/Linux/x86_64/libopenxr_loader.so.1
```

(mit `.meta`-Dateien mit festen GUIDs). Das Paket selbst hält die Binärdateien in `Runtime/Native~/`; der Unity-Importer ignoriert Ordner, deren Name auf `~` endet.
Das funktioniert für Git-URL, Tarball, `.unitypackage` und *Add package from disk...*. `Assets/LinuxXR` kann wie jedes andere Asset in die Versionsverwaltung eingecheckt werden
oder weggelassen werden: Es wird automatisch neu erzeugt.

### 3.5 Editor neu starten

**Starte den Unity-Editor nach der ersten Installation und nach jedem Update, das die nativen Binärdateien ändert, einmal neu:** XR-Subsysteme werden nur beim Start des Editors registriert.
Der Setup-Assistent zeigt **Restart Editor**, wenn das nötig ist.

Bei einer Installation über den Package Manager wird `com.unity.xr.management` 4.5.0 als Abhängigkeit mitinstalliert. Fehlt es (zum Beispiel nach dem Import des `.unitypackage`), bietet ein kleiner Bootstrap die Installation an
(Menü *Window -> XR -> LinuxXR Install XR Plug-in Management*).

## 4. Erster Start und Setup-Assistent

### 4.1 Was der Assistent prüft

*Window -> XR -> LinuxXR Setup* öffnet sich **einmal pro Sitzung automatisch**, solange etwas Erforderliches fehlt, und lässt sich jederzeit über das Menü öffnen.
Jeder Schritt zeigt einen Status und, wo möglich, eine **Ein-Klick-Lösung**:

| Schritt | Ein-Klick-Lösung |
|---------|------------------|
| Natives Plugin in `Assets/LinuxXR` installiert und XR-Subsysteme gefunden | **Install**, danach **Restart Editor** (siehe 3.4) |
| Vulkan als Graphics API für Linux | **Use Vulkan + restart** |
| LinuxXR-Loader für Standalone ("Windows, Mac, Linux") aktiviert | **Enable LinuxXR** |
| Keine anderen Standalone-Loader aktiv (z. B. OpenXR) | **Disable them on this tab** |
| Initialize XR on Startup | **Turn on** |
| Flatpak-Sandbox-Zugriff auf WiVRn (nur wenn der Unity Hub das Flatpak `com.unity.UnityHub` ist) | **Allow access to WiVRn**, danach **Close Unity Hub + Editor now** |
| WiVRn installiert | **Install WiVRn (Flatpak)** (`flatpak install --user` von Flathub) oder die Flathub-Seite |
| WiVRn läuft | **Start WiVRn** oder der Link zur Quest-App |
| Empfohlen: Linear-Farbraum | **Use Linear** |
| Empfohlen: Render Scale 0,7 | **Set 0.7** |

Die Checkliste lässt sich pro Projekt abschalten (sie öffnet sich dann nicht mehr von selbst; der Menüeintrag funktioniert weiterhin).

### 4.2 Typischer erster Durchlauf

1. Editor nach dem Installieren des Pakets neu starten.
2. Den Assistenten von oben nach unten abarbeiten. Die Vulkan- und Flatpak-Schritte brauchen einen Editor-Neustart; der Assistent sagt das dazu.
3. WiVRn starten und das Headset verbinden (siehe 2.3), dann weiter mit [Abschnitt 6](#6-szene-einrichten) und [Play drücken](#7-play-drücken).

### 4.3 Unity Hub als Flatpak

Läuft der Unity Hub als Flatpak (`flatpak list | grep UnityHub` gibt `com.unity.UnityHub` aus), läuft der Editor **in der Sandbox des Hubs**
und sieht weder den WiVRn-Socket (`$XDG_RUNTIME_DIR/wivrn/comp_ipc`) noch das WiVRn-Flatpak. Der Assistent erledigt das:

1. **Allow access to WiVRn** klicken. Das führt `flatpak override --user --filesystem=xdg-run/wivrn` aus, dazu Lesezugriff auf die WiVRn-App-Verzeichnisse
   (Benutzer- und Systempfad). Sonst ändert sich nichts am System.
2. **Close Unity Hub + Editor now** klicken, danach Unity Hub und Projekt neu starten. Overrides gelten nur für neu gestartete Sandboxes.

Der Socket existiert nur, solange WiVRn läuft und die Quest verbunden ist; "socket missing" vor dem Verbinden des Headsets ist also normal.
Lässt sich ein systemweites WiVRn nicht freigeben, installiere es stattdessen pro Benutzer: `flatpak install --user flathub io.github.wivrn.wivrn` (die Systeminstallation optional entfernen).
Der Flatpak-Runtime fehlt ein OpenXR-Loader; LinuxXR bringt einen eigenen mit. Overrides zurücknehmen: `flatpak override --user --reset com.unity.UnityHub`
(oder das optionale `scripts/setup-unity-flatpak.sh --undo`).

## 5. Unity-Einstellungen (was der Assistent erledigt)

Der [Setup-Assistent](#4-erster-start-und-setup-assistent) setzt diese Einstellungen für dich; dieser Abschnitt ist die manuelle Referenz.

1. **Graphics API: Vulkan.** *Edit -> Project Settings -> Player -> Other Settings (Linux-Tab) -> Auto Graphics API for Linux* aus, und **Vulkan** an erste Stelle in
   *Graphics APIs for Linux* setzen. Oder der Assistenten-Schritt **Use Vulkan + restart** (ändert die Einstellung und startet den Editor neu).
2. **XR Plug-in Management.** *Edit -> Project Settings -> XR Plug-in Management*, Tab **"Windows, Mac, Linux"** (der Desktop-Tab):
   - **LinuxXR** aktivieren und **OpenXR auf diesem Tab deaktivieren** (nur LinuxXR darf angehakt sein);
   - **Initialize XR on Startup** eingeschaltet lassen;
   - der **Android**-Tab behält **OpenXR** mit der Feature-Gruppe *Meta Quest* für Geräte-Builds.
3. **LinuxXR-Einstellungen** (*Project Settings -> XR Plug-in Management -> LinuxXR*):

   | Einstellung | Werte / Standard | Bedeutung |
   |-------------|------------------|-----------|
   | Runtime | Automatic (prefer WiVRn) / WiVRn / Monado / Active runtime / Custom manifest path; Standard Automatic | Welche OpenXR-Runtime genutzt wird. Automatic bevorzugt WiVRn, dann `active_runtime.json`, dann Monado. |
   | Custom Runtime Manifest | Pfad zu einer `*.json` | Nur bei "Custom manifest path" wirksam. |
   | Tracking Origin | Floor (Standard) / Device (head) | Tracking-Raum. |
   | Stereo Mode | Single Pass Instanced (empfohlen, Standard) / Multi Pass | Wie beide Augen gerendert werden. |
   | Mirror Mode | Left eye (Standard) / Right eye / Both eyes / Off | Was die Game View im Play Mode zeigt. |
   | Render Scale | 0,5 - 1,5, Standard 1,0 | Render-Auflösung relativ zur Empfehlung der Runtime. Bei Ruckeln senken (z. B. 0,7). |
   | Verbose Logging | aus | Debug-Meldungen in der Console und in `linuxxr.log`. |
   | Forward Log To Console | an | Native `[LinuxXR]`-Meldungen in der Unity-Console anzeigen. |

   Änderungen wirken beim nächsten Druck auf Play.
4. **Farbraum:** **Linear** verwenden (Empfehlung des Assistenten) (*Player -> Other Settings -> Color Space*). Sonst wirkt das Bild im Headset zu dunkel oder zu hell.
5. **Editor neu starten** (auch nach dem Wechsel der Graphics API oder der Paketinstallation).

Umgebungsvariablen müssen gesetzt sein, **bevor** Hub / Editor starten (wann du sie brauchst, steht in [TROUBLESHOOTING.de](TROUBLESHOOTING.de.md)):

| Variable | Werte | Zweck |
|----------|-------|-------|
| `LINUXXR_RUNTIME` | `auto`, `wivrn`, `monado`, `active` oder `/pfad/zur/runtime.json` | Runtime-Präferenz. Die Einstellung Runtime hat Vorrang, außer sie steht auf "Automatic". |
| `LINUXXR_GPU` | GPU-Index oder Teil des Namens | GPU bei Mehr-GPU-Systemen wählen. |
| `LINUXXR_LOG` | `debug` | Ausführliches natives Log. |
| `LINUXXR_FLIP_Y` | `1` | Bild vertikal spiegeln (wenn es auf dem Kopf steht). |
| `LINUXXR_DIRECT_SUBMIT` | `1` | Ausweichweg bei eingefrorenem oder schwarzem Bild bzw. der Log-Meldung "Previous frame did not complete". |
| `LINUXXR_OPENXR_LOADER` | Pfad zu `libopenxr_loader.so.1` | Einen bestimmten OpenXR-Loader verwenden. |

## 6. Szene einrichten

Nutze Unitys XR-Input, keine Meta-spezifischen Rigs (`OVRCameraRig` / `OVRInput` / `OVRManager` bekommen im Linux-Editor keine Daten).

**Option A - XR Interaction Toolkit (empfohlen).** *XR Interaction Toolkit* im Package Manager installieren. In der Szene
*GameObject -> XR -> XR Origin (VR)* anlegen (plus *XR Interaction Setup*/Starter Assets, wenn du fertige Interactors willst). Den Tracking-Modus des Origins passend zur
Einstellung *Tracking Origin* wählen (Floor oder Device).

**Option B - einfache Kamera.** Einen **Tracked Pose Driver (Input System)** auf die Main Camera setzen und an das HMD binden.
Für das Input System diese Bindings verwenden (die Geräte heißen "LinuxXR HMD" und "LinuxXR Controller - Left/Right"):

| Verwendung | Binding |
|------------|---------|
| Kopfposition / -rotation | `<XRHMD>/centerEyePosition`, `<XRHMD>/centerEyeRotation` |
| Linker Controller (Pose) | `<XRController>{LeftHand}/devicePosition`, `<XRController>{LeftHand}/deviceRotation` |
| Rechter Controller (Pose) | `<XRController>{RightHand}/devicePosition`, `<XRController>{RightHand}/deviceRotation` |
| Linker Trigger / Grip | `<XRController>{LeftHand}/trigger`, `<XRController>{LeftHand}/grip` |
| Zeiger-Pose (Aim) | `<XRController>{RightHand}/pointerPosition`, `<XRController>{RightHand}/pointerRotation` |

Die Controller liefern Pose, Geschwindigkeit, Zeiger-Pose, Trigger, Grip, TriggerButton, GripButton, PrimaryButton / PrimaryTouch, SecondaryButton / SecondaryTouch, MenuButton
und mehr mit den Namen aus Unitys `CommonUsages`; im Input Debugger siehst du alles, was die Geräte bereitstellen.
Die Geräte funktionieren auch über `UnityEngine.XR.InputDevices`, das XR Interaction Toolkit und den XR-Input von HurricaneVR.

## 7. Play drücken

Reihenfolge: **WiVRn starten, Headset verbinden, dann Play drücken.** WiVRn bedient Anwendungen nur, solange ein Headset verbunden ist.

Ohne Headset (oder wenn WiVRn nicht läuft) läuft der Play Mode ohne VR weiter: LinuxXR schreibt eine gelbe Warnung (`No headset connected ... Play continues without VR`) und keine Fehler.
WiVRn blockiert OpenXR-Aufrufe, solange kein Headset verbunden ist; deshalb prüft LinuxXR zuerst mit einer Probe in einem Worker-Thread (Timeout 3 s), und der Editor friert nie ein.
Verbinde die Quest in WiVRn und drücke erneut Play.

Erwarteter Ablauf:

1. Die Console zeigt beim XR-Start `[LinuxXR] Initialized (version 0.3.3)`.
2. Danach folgen native Meldungen, zum Beispiel `Display subsystem initialized`, `Input subsystem initialized`,
   `Vulkan: Unity's device has all required extensions`, `Display subsystem started - image goes to <Systemname>`,
   `Input subsystem started: HMD + 2 controllers`, `First image: ...`.
3. Die Szene erscheint im Headset; die Game View zeigt die Spiegelung (je nach Mirror Mode); Kopf und Controller bewegen sich.

Öffne **Window -> XR -> LinuxXR Status** für Sitzungsstatus, Live-Statistik und Aktionen (die Einrichtungs-Checkliste steckt in **Window -> XR -> LinuxXR Setup**). Schaltflächen: **Start WiVRn**, **Use Vulkan**, **XR Settings**, **Open log**, **Copy diagnostics**, **Create issue report**.
Die Statistik umfasst (Headset-fps vs. Display-Hz, Unity-fps, `xrWaitFrame`-Zeit, Kopier- + Submit-Zeit, Frames begonnen / gezeigt /
leer, verpasste Display-Frames, von Unity verworfene Frames, Augen-Auflösung / Modus / Skalierung, Vulkan-Erweiterungs-Check, Sitzungsdauer, Warnungen und Fehler). **Create issue report** speichert einen Markdown-Bericht unter `Logs/LinuxXR-report-<Zeitstempel>.md` und kopiert ihn in die Zwischenablage; hänge ihn an Fehlermeldungen an.

Logs: Native Meldungen gehen in die Console und in die native Log-Datei `~/.cache/linuxxr/linuxxr.log` (mit Hub-Flatpak:
`~/.var/app/com.unity.UnityHub/cache/linuxxr/linuxxr.log`). Unitys eigenes Log ist `Logs/Editor.log` im Projekt.

Bei Problemen siehe [TROUBLESHOOTING.de](TROUBLESHOOTING.de.md).

## 8. Performance-Tipps

- Kabelgebundenes Netz für den PC, 5 GHz / Wi-Fi 6 für die Quest, oder USB (gutes Kabel, 5 Gb/s oder besser). Bei USB das WLAN am Headset ausschalten.
- In voller Auflösung (2476x2594 pro Auge bei der Quest 3) kamen im Testaufbau etwa 81 von 90 Frames an. **Render Scale auf etwa 0,7** setzen.
- **Single Pass Instanced** als Stereo-Modus bevorzugen.
- In der WiVRn-App / den Server-Einstellungen Bitrate oder Auflösung senken, wenn der Stream ruckelt.
- Die Statistik im Status-Fenster beobachten: "verpasste Display-Frames" und "von Unity verworfen" zeigen, ob GPU, Encoder oder Netzwerk der Engpass ist.

## 9. Aktualisieren

- **Git-URL:** Bei `#upm` zeigt der Package Manager ein Update an, sobald der `upm`-Branch weiterrückt (oder *Update* klicken); bei `#upm/vX.Y.Z` die URL auf den neuen Tag ändern.
  Falls Unity den alten Commit festhält, das Paket entfernen und neu hinzufügen.
- **Tarball:** die neue `.tgz` mit *Add package from tarball...* hinzufügen.
- **.unitypackage:** `Assets/Tyroonis/LinuxXR` löschen und die neue Datei importieren.

Das Paket aktualisiert `Assets/LinuxXR` beim Laden. Danach den **Editor neu starten** (der Assistent zeigt **Restart Editor**). WiVRn-Server und Quest-App auf derselben Version halten (`flatpak update io.github.wivrn.wivrn`).

## 10. Deinstallieren

1. LinuxXR im *Project Settings -> XR Plug-in Management* deaktivieren ("Windows, Mac, Linux").
2. Das Paket im *Package Manager* entfernen (beim `.unitypackage` `Assets/Tyroonis/LinuxXR` löschen).
3. `Assets/LinuxXR` löschen (der native Teil, den das Paket installiert hat).
4. Optional, falls benutzt: das Flatpak-Override mit `flatpak override --user --reset com.unity.UnityHub` zurücknehmen und den Hub neu starten;
   WiVRn mit `flatpak uninstall io.github.wivrn.wivrn` entfernen.

## 11. Optionale Hilfsskripte

Nichts davon ist erforderlich. Die Skripte liegen der Bequemlichkeit halber und zur Diagnose eines Hosts im Repository:

| Skript | Zweck |
|--------|-------|
| `scripts/check-system.sh` (`--report`) | Rein lesender Systemcheck; ändert nichts |
| `scripts/setup-host.sh` | Installiert WiVRn, OpenXR-Werkzeuge, Mesa-/VAAPI-Werkzeuge, avahi und Firewall-Regeln; `--uninstall` macht es rückgängig. Optionen: siehe unten |
| `scripts/setup-unity-flatpak.sh` (`--check`, `--undo`) | Kommandozeilen-Variante des Flatpak-Schritts im Assistenten |
| `scripts/quest-usb.sh` (`logs`, `stop`) | adb-Tunnel für USB-Streaming (siehe 2.3) |
| `scripts/test-openxr.sh` | `hello_xr` über WiVRn / Monado ohne Unity |

### Optionen von `setup-host.sh`

`scripts/setup-host.sh` zeigt vorab, was es vorhat, und fragt vor jeder Änderung nach. Es nutzt bei Bedarf `sudo`
(als normaler Benutzer starten, nicht als root).

```bash
./scripts/setup-host.sh
```

Optionen:

| Option | Wirkung |
|--------|---------|
| *(keine)* | Basis: WiVRn (Flatpak), OpenXR-Loader, `hello_xr`/`libopenxr-utils`, Vulkan- und VAAPI-Werkzeuge, Mesa-Vulkan-Treiber, avahi, Firewall-Regeln |
| `--dev` | Dazu Build-Werkzeuge: `build-essential cmake ninja-build pkg-config git libvulkan-dev libopenxr-dev` |
| `--debug-layers` | Dazu OpenXR-/Vulkan-Validierungs-Layer |
| `--monado` | Dazu Monado (`monado-service libopenxr1-monado monado-cli`) für Tests ohne Headset und die Integrationstests |
| `--adb` | Dazu `adb` (USB-Ausweichweg, logcat, Sideloading) |
| `--all` | `--dev`, `--debug-layers`, `--monado` und `--adb` |
| `--wivrn=flatpak` | WiVRn von Flathub (Standard) |
| `--wivrn=ppa` | WiVRn aus `ppa:lvra/wivrn` (Pakete `wivrn-server wivrn-dashboard`) |
| `--wivrn=none` | WiVRn nicht anfassen |
| `--dry-run` | Nur anzeigen, was getan würde |
| `--yes` / `-y` | Nicht nachfragen |

Das Skript aktiviert außerdem `avahi-daemon` (damit die Quest den PC per mDNS findet) und öffnet, falls `ufw` aktiv ist, die Ports
`5353/udp`, `9757/tcp` und `9757/udp`. Es warnt, wenn das System nicht Ubuntu 26.04 ist.

Manuelles Äquivalent (Ubuntu):

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  libopenxr-loader1 libopenxr-utils vulkan-tools vainfo mesa-vulkan-drivers avahi-daemon flatpak \
  build-essential cmake ninja-build pkg-config git libvulkan-dev libopenxr-dev
sudo systemctl enable --now avahi-daemon
# nur falls ufw aktiv ist:
sudo ufw allow 5353/udp && sudo ufw allow 9757/tcp && sudo ufw allow 9757/udp
```

Ohne Unity kannst du die ganze native Kette mit `linuxxr-probe` (aus dem Quellcode gebaut, siehe unten) oder mit `./scripts/test-openxr.sh wivrn` testen.

## Aus dem Quellcode bauen (Entwickler)

Nur nötig, wenn du an LinuxXR selbst arbeitest oder die Binärdateien selbst bauen willst. Alle Befehle im Hauptverzeichnis des Repositorys ausführen.
Den Ablauf für Beiträge und Releases beschreibt [CONTRIBUTING.md](../CONTRIBUTING.md) (englisch).

### Build-Werkzeuge

```bash
./scripts/setup-host.sh --dev              # oder von Hand:
sudo apt-get install -y build-essential cmake ninja-build pkg-config git libvulkan-dev libopenxr-dev python3
```

### Natives Plugin bauen

```bash
./scripts/fetch-unity-headers.sh     # Unity-XR-SDK-Header (Unity Companion License; nicht eingecheckt). build-native.sh ruft es ebenfalls auf
./scripts/build-native.sh            # Release (RelWithDebInfo) nach native/build
./scripts/build-native.sh test       # bauen + ctest
./scripts/build-native.sh debug      # Debug-Build
./scripts/build-native.sh clean      # native/build löschen
```

Der CMake-Build kopiert `libLinuxXR.so` in den Paketordner:
`unity/Packages/com.tyroonis.xr.linuxopenxr/Runtime/Native~/`. Außerdem entsteht `native/build/linuxxr-probe`.
`./scripts/build-openxr-loader.sh [--version 1.1.47]` baut den mitgelieferten `libopenxr_loader.so.1` aus dem Khronos OpenXR-SDK neu.

Die Tests (`ctest`) bestehen aus Core-Unit-Tests, einem Fake-Unity-Host, der `libLinuxXR.so` per `dlopen` lädt, und Monado-Integrationstests
(headless Monado + lavapipe, inklusive Runtime-Verlust ohne Hänger). Die Monado-Tests werden übersprungen, wenn Monado nicht installiert ist
(`./scripts/setup-host.sh --monado`).

### Arbeitskopie in einem Testprojekt nutzen (Live-Entwicklung)

```bash
./scripts/install-unity-package.sh /pfad/zum/testprojekt --dev
```

Das legt `<projekt>/Assets/LinuxXRDev` als Symlink auf den Paketordner des Repositorys an, sodass C#-Änderungen und jedes `./scripts/build-native.sh` direkt wirken.
Das Paket installiert dann `Assets/LinuxXR` selbst, wie bei Anwendern. Nach dem Umschalten den Editor neu starten. Weitere Modi:

```bash
./scripts/install-unity-package.sh /pfad/zum/testprojekt --tarball   # packt und installiert den Release-Tarball, um zu testen, was Anwender bekommen
./scripts/install-unity-package.sh /pfad/zum/testprojekt --remove    # entfernt LinuxXR wieder aus dem Projekt
```

### Release-Artefakte packen

```bash
./scripts/pack-unity-package.sh      # schreibt dist/*.tgz, dist/*.unitypackage und dist/upm
./scripts/ensure-metas.py            # legt fehlende .meta-Dateien an (jede Datei im Paket braucht eine)
```

### Kette ohne Unity testen

Lass das Headset mit WiVRn verbunden. Zuerst prüfen, ob OpenXR WiVRn sieht:

```bash
./scripts/test-openxr.sh list              # zeigt Runtime-/Manifest-Infos
./scripts/test-openxr.sh wivrn             # hello_xr (Vulkan) über WiVRn auf der Quest
```

Danach, nach dem Bauen des Plugins, die LinuxXR-Kette selbst mit dem Probe-Werkzeug testen:

```bash
native/build/linuxxr-probe runtimes        # gefundene Runtimes und ob sie erreichbar sind
native/build/linuxxr-probe info            # OpenXR-Instanz, System, Erweiterungen, Vulkan-Anforderungen
native/build/linuxxr-probe preinit         # Vulkan-Erweiterungen und GPU-Wahl
native/build/linuxxr-probe run             # Session: linkes Auge rot, rechtes Auge blau
```

Während `run` hellt der Trigger die Augenfarbe auf, A/X färbt grün, B/Y blauweiß, und der Grip vibriert.
Nützliche Optionen: `--runtime auto|wivrn|monado|active|/pfad/manifest.json`, `--seconds N` (Standard 30, `0` = bis Strg+C), `--origin floor|device`,
`--no-haptics`, `--quiet`, `--debug`. Siehst du Rot und Blau im Headset, funktioniert die gesamte native Kette.

Kein Headset zur Hand? Nimm Monado headless (braucht `--monado` bei `setup-host.sh`):

```bash
XRT_COMPOSITOR_NULL=1 SIMULATED_ENABLE=1 monado-service        # in einem eigenen Terminal
native/build/linuxxr-probe run --runtime monado --seconds 10
```

### CI und Releases

`.github/workflows/ci.yml` baut und führt `ctest` auf ubuntu-24.04 bei jedem Push und Pull Request aus. `.github/workflows/release.yml` läuft bei einem Tag `vX.Y.Z`,
führt die Tests aus, baut portabel, signiert optional den Tarball, erstellt das GitHub-Release und veröffentlicht den `upm`-Branch. Details: [ARCHITECTURE](ARCHITECTURE.md#8-distribution-and-ci) und [CONTRIBUTING](../CONTRIBUTING.md#release-procedure) (englisch).
