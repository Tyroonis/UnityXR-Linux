# Fehlerbehebung

[English](TROUBLESHOOTING.md) | **Deutsch**

Beginne mit **Window -> XR -> LinuxXR Setup** (der Setup-Assistent: natives Plugin und Subsysteme, Vulkan, Loader, Flatpak-Zugriff, WiVRn) und
**Window -> XR -> LinuxXR Status** (Sitzungsstatus und Statistik). Sie zeigen meistens schon auf das Problem, und die meisten Schritte haben eine Ein-Klick-Lösung.
Native `[LinuxXR]`-Meldungen erscheinen in der Unity-Console. Für eine Fehlermeldung nutze **Create issue report** im Status-Fenster (siehe [Ein Issue melden](#ein-issue-melden)).

Unten genannte Umgebungsvariablen müssen gesetzt sein, **bevor** Unity Hub / Editor starten (zum Beispiel `LINUXXR_FLIP_Y=1 unityhub`, oder
`flatpak override --user --env=LINUXXR_FLIP_Y=1 com.unity.UnityHub` beim Hub-Flatpak). Danach den Hub vollständig neu starten.

## Symptom, Ursache, Lösung

### Einrichtung und Start

| Symptom | Ursache | Lösung |
|---------|---------|--------|
| Console: `[LinuxXR] Unity does not know the LinuxXR subsystems ...`; Assistent / Status: **Subsystems** oder **Native plugin** rot ("Subsysteme nicht gefunden") | Unity registriert native XR-Subsysteme nur beim Start des Editors und nur aus `Assets/LinuxXR/UnitySubsystemsManifest.json`, die das Paket beim ersten Laden installiert. Typische Ursachen: der Editor wurde nach der ersten Installation oder einem Update **nicht neu gestartet**, oder `Assets/LinuxXR` wurde gelöscht bzw. ist unvollständig. | **Editor neu starten** (der Assistent zeigt **Restart Editor**). Bietet der Schritt **Native plugin** im Assistenten **Install** an, klicken und neu starten. Prüfen, ob `Assets/LinuxXR/UnitySubsystemsManifest.json` und `Assets/LinuxXR/Plugins/Linux/x86_64/libLinuxXR.so` existieren. Bleibt es dabei, mit einem der drei Wege aus [INSTALL.de](INSTALL.de.md#3-paket-installieren) neu installieren. |
| XR Plug-in Management: LinuxXR fehlt, oder "Unable to add package ... cannot be found" beim Anhaken | `com.unity.xr.management` ist nicht installiert, oder das Paket ist noch nicht aufgelöst. | *Window -> XR -> LinuxXR Install XR Plug-in Management* ausführen (der Bootstrap bietet die Installation von `com.unity.xr.management` an), das Kompilieren abwarten, Editor neu starten. In `Packages/manifest.json` auf `com.tyroonis.xr.linuxopenxr` prüfen. |
| Console: `[LinuxXR] libLinuxXR.so could not be loaded ...`; Assistent: **Native plugin** rot | Die native Bibliothek fehlt im Paket (zum Beispiel eine Git-URL auf `main` statt auf den `upm`-Branch, der keine Binärdateien enthält), oder ihre Abhängigkeiten lassen sich nicht laden. | Von `...git#upm` (oder `#upm/v0.3.3`), dem Release-Tarball oder dem `.unitypackage` installieren, nicht vom Quell-Branch. Prüfen, ob `libLinuxXR.so` unter `Library/PackageCache/com.tyroonis.xr.linuxopenxr@.../Runtime/Native~/` und nach der Installation unter `Assets/LinuxXR/Plugins/Linux/x86_64/` liegt. Die Release-Binärdatei braucht glibc 2.35 oder neuer. Editor neu starten. |
| Console: `[LinuxXR] The Editor runs with <API>, LinuxXR needs Vulkan ...`; Assistent: **Graphics API** rot | Unity nutzt OpenGL Core oder eine andere API. | Im Assistenten **Use Vulkan + restart** klicken (oder im Status-Fenster **Use Vulkan**), oder unter Player Settings -> Linux -> Graphics APIs **Vulkan an erster Stelle** setzen (Auto Graphics API aus) und neu starten. |
| LinuxXR erscheint nicht im XR Plug-in Management | Falscher Tab, oder das Paket wurde nicht erkannt. | Den Tab **"Windows, Mac, Linux"** nutzen. Fehlt es dort, siehe die ersten beiden Zeilen und den Editor neu starten. |
| Package Manager (Unity 6.3+): "package is not signed" | Release-Tarballs sind nur signiert, wenn der Maintainer die Signierung eingerichtet hat; bei Git-URL-Installationen erscheint die Meldung in der Regel nicht. | Ignorieren: Es ist nur eine Warnung, das Paket funktioniert. |
| Console: `[LinuxXR] Only available in the Linux Editor - loader skipped.` | LinuxXR ist für ein Build-Target / eine Plattform aktiviert, auf der es nicht funktioniert. | LinuxXR nur im Tab "Windows, Mac, Linux" aktivieren; der Android-Tab behält OpenXR. |
| XR startet nicht, im Play Mode passiert nichts | LinuxXR nicht angehakt, gleichzeitig OpenXR angehakt, oder "Initialize XR on Startup" aus. | Im Assistenten **Enable LinuxXR**, **Disable them on this tab** und **Turn on** (Initialize XR on Startup) nutzen, oder das im Tab "Windows, Mac, Linux" von Hand einstellen. |
| Der Setup-Assistent öffnet sich nicht mehr | Die Checkliste ist für dieses Projekt abgeschaltet, oder alles Erforderliche ist schon in Ordnung. | Über *Window -> XR -> LinuxXR Setup* öffnen (dort lässt sich die Checkliste wieder einschalten). |
| Console: `The subsystems could not be created - native initialization failed` | Das native Plugin konnte ein Subsystem nicht starten. | Die `[LinuxXR]`-Zeilen davor und die Logdatei lesen (siehe unten); typische Gründe: keine Runtime, kein Headset, nicht Vulkan. |

### Runtime und Headset

| Symptom | Ursache | Lösung |
|---------|---------|--------|
| `No OpenXR runtime found. Install WiVRn (wivrn-server) or set LINUXXR_RUNTIME.`; Status: **OpenXR runtime** rot | Kein WiVRn-/Monado-Manifest gefunden. | WiVRn installieren ([INSTALL.de](INSTALL.de.md#2-wivrn-und-die-quest-host-seite); der Assistent hat eine Schaltfläche **Install WiVRn (Flatpak)**), oder die Einstellung **Runtime** auf *Custom manifest path* setzen, oder `LINUXXR_RUNTIME=/pfad/zu/openxr_wivrn.json` setzen. Diagnose mit `native/build/linuxxr-probe runtimes`. |
| Unity Hub ist ein Flatpak und WiVRn (Flatpak) wird nicht gefunden; die Meldung erwähnt "running inside a Flatpak sandbox"; Assistent: Schritt **Flatpak** offen | Der Editor läuft in der Sandbox des Hubs und sieht weder WiVRns Runtime noch den Socket. | Im Assistenten **Allow access to WiVRn** klicken (führt `flatpak override --user --filesystem=xdg-run/wivrn` plus Lesezugriff auf die WiVRn-App-Verzeichnisse aus), dann **Close Unity Hub + Editor now** und Hub und Projekt neu starten. Hilft das nicht, WiVRn pro Benutzer installieren (`flatpak install --user flathub io.github.wivrn.wivrn`, der Assistent hat dafür eine Schaltfläche) und die Systeminstallation entfernen. Optionales Kommandozeilen-Äquivalent: `scripts/setup-unity-flatpak.sh`. |
| `WiVRn: no headset connected (open the WiVRn app on the Quest).`; Status: **Headset** gelb | WiVRn bedient Anwendungen nur, solange ein Headset verbunden ist; der Socket `$XDG_RUNTIME_DIR/wivrn/comp_ipc` fehlt oder nimmt keine Verbindungen an. | WiVRn-Server / Dashboard starten, auf der Quest die WiVRn-App öffnen, mit dem PC verbinden und **dann** Play drücken. |
| Headset verbindet nur manchmal oder gar nicht | Firewall / mDNS / Netzwerk. | Ports 9757 TCP+UDP und 5353/UDP offen (`ufw allow ...`), `avahi-daemon` läuft, PC und Quest im selben Netz, Quest im 5-GHz-Band / Wi-Fi 6. Oder USB nutzen ("USB networking" in der WiVRn-App oder `./scripts/quest-usb.sh`). |
| Die WiVRn-App auf der Quest meldet unverträgliche Versionen (z. B. "incompatible protocol version") | WiVRn-Client und -Server müssen exakt dieselbe Version haben. | Beide aktualisieren: `flatpak update io.github.wivrn.wivrn` am PC und die WiVRn-App auf der Quest (über "Install on headset" im Dashboard lässt sich die passende Version installieren). |
| `Runtime wants a different VkPhysicalDevice than the application uses ...` | Mehrere GPUs; WiVRn und Unity haben unterschiedliche gewählt. | `LINUXXR_GPU=<Index oder Teil des GPU-Namens>` und/oder `XRT_COMPOSITOR_FORCE_GPU_INDEX` setzen, dann Hub/Editor neu starten. |
| `Unity's Vulkan device lacks extensions the runtime needs: ... - the image may stay black.` | Unitys Vulkan-Gerät aktiviert eine Erweiterung nicht, die WiVRn braucht. | Sicherstellen, dass Unity wirklich mit Vulkan auf der GPU läuft, die WiVRn nutzt (`LINUXXR_GPU`). Den Bericht aus dem Status-Fenster in ein Issue packen. |
| `Connection to the OpenXR runtime lost (...)` / `OpenXR instance is about to be lost (runtime exited?)` | WiVRn wurde beendet oder das Headset hat sich während Play getrennt. | Play Mode beenden, WiVRn neu starten, Headset neu verbinden, erneut Play drücken. Der Editor hängt bei Runtime-Verlust nicht. |

### Verhalten des Editors

| Symptom | Ursache | Lösung |
|---------|---------|--------|
| Editor friert beim Drücken von Play oder beim Öffnen des Status-Fensters ein | Ein OpenXR-Aufruf hat blockiert. WiVRn blockiert OpenXR-Aufrufe, solange kein Headset verbunden ist; LinuxXR vermeidet das mit einer Probe in einem Worker-Thread (Timeout 3 s). | Zuerst die Quest in WiVRn verbinden. Friert der Editor trotzdem ein, ihn neu starten, das Ende des nativen Logs ansehen (`~/.cache/linuxxr/linuxxr.log`, siehe [Wo liegen die Logs?](#wo-liegen-die-logs)) und ein Issue mit diesem Log und dem Issue-Bericht melden. |
| Play Mode startet ohne VR; Console: gelbes `No headset connected ... Play continues without VR` | WiVRn läuft nicht oder es ist kein Headset verbunden. Das ist eine Warnung, kein Fehler. | WiVRn starten, die Quest in der WiVRn-App verbinden, erneut Play drücken. |

### Bild

| Symptom | Ursache | Lösung |
|---------|---------|--------|
| Bild steht auf dem Kopf | Orientierungsabweichung beim Kopieren in die Swapchain auf diesem System. | `LINUXXR_FLIP_Y=1` setzen, bevor der Editor startet. Bitte dein Setup melden. |
| Bild schwarz oder eingefroren; Console: `Previous frame did not complete (AccessQueue callback missing?) - try LINUXXR_DIRECT_SUBMIT=1` | Das Einreichen des Frames innerhalb von `AccessQueue` wurde nicht abgeschlossen. | `LINUXXR_DIRECT_SUBMIT=1` setzen. Außerdem die Vulkan-Erweiterungszeile in Console / Status-Fenster prüfen und ob das Headset noch verbunden ist. |
| Bild schwarz, keine Fehler | Headset nicht wirklich verbunden, oder `Display subsystem started` erschien nie. | Status-Fenster prüfen; in der Console nach `Display subsystem started - image goes to ...` suchen. Die Kette mit `native/build/linuxxr-probe run` testen. |
| Ruckeln, "missed display frames" steigen, weniger Headset-Frames als Unity-Frames | Nicht alle Frames in voller Auflösung (2476x2594 pro Auge bei der Quest 3) schaffen es durch Encoder / Netzwerk. Im Testaufbau kamen etwa 81 von 90 Frames an. | **Render Scale etwa 0,7**, LAN-Kabel am PC oder USB am Headset, 5 GHz / Wi-Fi 6, Bitrate in WiVRn senken, Single Pass Instanced bevorzugen. |
| Farben zu dunkel oder zu hell | Gamma-Farbraum. | Player Settings -> Other Settings -> **Color Space: Linear**. |
| Game View leer / nur ein Auge | Mirror Mode. | Project Settings -> XR Plug-in Management -> LinuxXR -> **Mirror Mode** (Left eye / Right eye / Both eyes / Off). |

### Eingabe und Rig

| Symptom | Ursache | Lösung |
|---------|---------|--------|
| Kopf oder Controller bewegen sich nicht | Rig nicht an XR-Input gebunden. | Ein XR Origin (XR Interaction Toolkit) oder einen Tracked Pose Driver mit den Bindings für "LinuxXR HMD" / "LinuxXR Controller" nutzen, siehe [INSTALL.de](INSTALL.de.md#6-szene-einrichten). |
| `OVRCameraRig` / `OVRInput` / `OVRManager` bekommen keine Daten | Teile des Meta SDK, die auf OVRPlugin beruhen, funktionieren im Linux-Editor nicht. | XR Origin / XR Interaction Toolkit / Unity-XR-Input für das Rig nutzen. Quest-Builds können das Meta SDK weiter verwenden. |
| Hände, Passthrough, tiefenbasierte Effekte fehlen | Noch nicht implementiert (kein Hand-Tracking, kein Passthrough, kein Depth Layer, keine Occlusion Mesh). | Siehe "Bekannte Einschränkungen" in der [README](../README.de.md). |
| Boden auf Kopfhöhe oder umgekehrt | Tracking Origin. | **Tracking Origin** zwischen Floor und Device umschalten. Bei WiVRn/Quest zum Zentrieren die Meta-Taste lang drücken. |

## Wo liegen die Logs?

| Log | Ort |
|-----|-----|
| Natives LinuxXR-Log | `~/.cache/linuxxr/linuxxr.log` (beachtet `XDG_CACHE_HOME`) |
| Dasselbe mit Unity Hub als Flatpak | `~/.var/app/com.unity.UnityHub/cache/linuxxr/linuxxr.log` |
| Unity-Console | `[LinuxXR]`-Zeilen (Einstellung **Forward Log To Console**, standardmäßig an) |
| Unity-Editor-Log | `Logs/Editor.log` im Projektordner |
| Issue-Berichte | `Logs/LinuxXR-report-<Zeitstempel>.md` im Projektordner |
| Logs von `test-openxr.sh` | `scripts/logs/` |

Für mehr Details **Verbose Logging** in den LinuxXR-Einstellungen aktivieren oder mit `LINUXXR_LOG=debug` starten. Das Status-Fenster hat **Open log**, **Copy diagnostics** und **Create issue report**.
Zum Debuggen der OpenXR-Kette ohne Unity: `./scripts/test-openxr.sh wivrn --debug` und `native/build/linuxxr-probe run --debug`.

## Ein Issue melden

1. Das Problem wenn möglich mit eingeschaltetem **Verbose Logging** nachstellen.
2. Im Editor **Window -> XR -> LinuxXR Status** öffnen und **Create issue report** klicken. Der Markdown-Bericht (System, Unity, Pakete, Einstellungen, Status,
   Statistik, Diagnose, letzte 250 Zeilen des nativen Logs; dein Home-Verzeichnis wird durch `~` ersetzt) landet in der Zwischenablage und unter `Logs/LinuxXR-report-<Zeitstempel>.md`.
3. Ein Issue auf GitHub oder GitLab öffnen (die Fehlerbericht-Vorlage fragt nach dem Bericht) und den Bericht **anhängen oder einfügen**, zusammen mit den Schritten zum Nachstellen und dem erwarteten und tatsächlichen Verhalten.
   Vorher auf Privates prüfen.

Kein Editor-Zugriff (z. B. der Editor startet gar nicht)? Dann die Ausgabe des optionalen `./scripts/check-system.sh` und den Inhalt von `~/.cache/linuxxr/linuxxr.log` anhängen.
