# Phase 2 – Bild auf der Brille

Stand: 2026-10-13 · Status: nativer Teil fertig und in der Sandbox getestet; **Test mit echtem Unity-Editor, WiVRn und Quest steht aus** (macht Tyroonis).

> **Wichtig – zuerst lesen (Erkenntnis aus dem ersten Unity-Test):**
>
> 1. **Plugin nach `Assets/` kopieren statt „Install package from disk“.** Unity 6.6 sucht `UnitySubsystemsManifest.json` beim Editor-Start nur in `Assets/` und `Library/PackageCache` – weder in per `file:` verlinkten noch in eingebetteten Paketen unter `Packages/` (im Editor.log verifiziert). Das Skript kopiert deshalb nach `Assets/LinuxXR/`. Ohne das gibt es weder PreInit noch die Subsysteme „LinuxXR Display/Input“.
>    ```bash
>    ./scripts/install-unity-package.sh "/pfad/zum/Unity-Projekt"
>    # dauerhaft nach jedem Build automatisch:
>    export LINUXXR_UNITY_PROJECT="/pfad/zum/Unity-Projekt"; ./scripts/build-native.sh
>    ```
> 2. **Unity Hub als Flatpak** (`com.unity.UnityHub`): Der Editor läuft dann in der Hub-Sandbox und sieht WiVRn nicht. Einmalig:
>    ```bash
>    ./scripts/setup-unity-flatpak.sh      # Overrides: xdg-run/wivrn + WiVRn-Runtime read-only
>    flatpak kill com.unity.UnityHub       # Hub neu starten
>    ```
>    Der OpenXR-Loader fehlt im Flatpak-Runtime; das Paket bringt deshalb `libopenxr_loader.so.1` (OpenXR-SDK 1.1.47, glibc ≥ 2.38) mit.
> 3. Auf dem Standalone-Tab von XR Plug-in Management **nur LinuxXR** aktivieren (OpenXR dort abwählen; OpenXR bleibt auf dem Android-Tab).
> 4. **Editor neu starten.**

Phase 2 baut auf [PHASE1.md](PHASE1.md) auf (Bauen, Paket, Fehlerbehebung der Grundlagen). Gesamtplan: [PLAN.md](PLAN.md), Begründungen: [ENTSCHEIDUNGEN.md](ENTSCHEIDUNGEN.md).

## Inhalt

1. [Was ist neu](#1-was-ist-neu)
2. [Verifiziert und nicht verifiziert](#2-verifiziert-und-nicht-verifiziert)
3. [Test Schritt für Schritt](#3-test-schritt-für-schritt)
4. [Erwartete Console-Zeilen](#4-erwartete-console-zeilen)
5. [Fehlerbehebung](#5-fehlerbehebung)
6. [Rückmeldung an Claude](#6-rückmeldung-an-claude)
7. [Bekannte Grenzen und nächste Schritte](#7-bekannte-grenzen-und-nächste-schritte)

---

## 1. Was ist neu

### Display: Unity rendert auf die Brille

- Unity rendert die Augentexturen selbst. Bei **Single Pass Instanced** ist das ein Texture-Array mit 2 Ebenen (1 Render-Pass, 2 Parameter-Sätze), bei **Multi Pass** sind es 2 Passes.
- Pro Frame (Gfx-Thread): `PopulateNextFrameDesc` ruft `xrWaitFrame`/`xrBeginFrame`/`xrLocateViews` und liefert Unity Augenpose relativ zum Kopf plus FOV. `SubmitCurrentFrame` kopiert die Unity-Textur in das OpenXR-Swapchain-Bild, und zwar **innerhalb von `IUnityGraphicsVulkan::AccessQueue(flush)`**. `flush` stellt sicher, dass Unitys Render-Befehle dieses Frames vorher eingereicht sind; nur dort darf außer Unity jemand auf Unitys Queue zugreifen. Danach folgen `xrReleaseSwapchainImage` und `xrEndFrame` mit einem **Projection-Layer**.
- Die Layer-Pose ist die Kopfpose aus dem Input-Subsystem (in `BeforeRender` gemeldet) × Augenpose-im-Kopf.
- Warum Kopie statt direkt in die Swapchain zu rendern: siehe Kopfkommentar in `native/src/unity/display_provider.cpp` (Unity-Texturen mit fremden `VkImage`s zu verknüpfen ist undokumentiert; ADR 9).
- **Mirror-Modi in der Game View** (Einstellung `mirrorMode`): linkes Auge (Standard), rechtes Auge, beide Augen nebeneinander, aus.
- **Render Scale** (`renderScale`, 0.5 bis 1.5) skaliert relativ zur Empfehlung der Runtime. **sRGB** folgt dem Color Space des Projekts (die Swapchain-Formatwahl richtet sich danach).

### Input: Geräte und Features

Das Input-Subsystem registriert drei Geräte. Die Feature-Namen entsprechen Unitys `CommonUsages`, funktionieren also mit `InputDevices.TryGetFeatureValue`, dem Input System (`<XRController>{LeftHand}/…`), XRI und HurricaneVR (XR-Input).

| Gerät | Features |
|---|---|
| **LinuxXR HMD** (HeadMounted, TrackedDevice) | `IsTracked`, `TrackingState`, `DevicePosition`, `DeviceRotation`, `DeviceVelocity`, `DeviceAngularVelocity`, `CenterEyePosition`, `CenterEyeRotation`, `LeftEyePosition`, `LeftEyeRotation`, `RightEyePosition`, `RightEyeRotation`, `UserPresence` |
| **LinuxXR Controller - Left** / **- Right** (HeldInHand, TrackedDevice, Controller, Left bzw. Right) | `IsTracked`, `TrackingState`, `DevicePosition`, `DeviceRotation`, `DeviceVelocity`, `DeviceAngularVelocity`, `PointerPosition`, `PointerRotation`, `Primary2DAxis`, `Trigger`, `Grip`, `TriggerButton` (Trigger > 0.5), `GripButton` (Grip > 0.5), `PrimaryButton`/`PrimaryTouch` (A bzw. X), `SecondaryButton`/`SecondaryTouch` (B bzw. Y), `Primary2DAxisClick`, `Primary2DAxisTouch`, `MenuButton`, `TriggerTouch`, `ThumbrestTouch` |

- **Haptik** über Impulse (`SendHapticImpulse`); der Controller meldet `supportsImpulse`.
- **Tracking-Origin**: *Floor* nutzt `LOCAL_FLOOR` (sonst `STAGE`, sonst `LOCAL` mit Warnung, dann ist die Kopfhöhe 0), *Device* nutzt `LOCAL`. Standard und Einstellung: Project Settings → XR Plug-in Management → LinuxXR → Tracking Origin; Unity kann den Modus auch zur Laufzeit setzen (Console: `Tracking-Origin: Floor|Device`).
- Kopf-Tracking der Kamera kommt in Unity immer aus dem Input-Subsystem (`CenterEyePosition/Rotation`).

### Verhalten bei Runtime-Verlust

Beendet sich WiVRn oder trennt das Headset, wird die Verbindung als verloren markiert (`XR_ERROR_INSTANCE_LOST`, `SESSION_LOST`, `RUNTIME_FAILURE` bzw. das Event „Instanz geht verloren“). Danach ruft das Plugin **nie wieder `xrWaitFrame`** auf, weil das bei WiVRn/Monado sonst dauerhaft blockiert und den Editor einfrieren würde. Das Status-Fenster zeigt den Fehler „Verbindung zur Brille verloren – Play beenden, WiVRn/Quest prüfen, neu starten“. Dann: Play beenden, WiVRn starten, Quest verbinden, erneut Play. Unity rendert in der Zwischenzeit normal weiter.

---

## 2. Verifiziert und nicht verifiziert

**Verifiziert (Sandbox, ohne Headset, ohne echte GPU):** `./scripts/build-native.sh test` mit **4 ctest-Suites, alle grün**:

| Suite | Inhalt |
|---|---|
| `linuxxr_core_tests` | Unit-Tests der Core-Logik |
| `linuxxr_fake_unity_host` | simulierter Editor ohne Runtime (PreInit, Lifecycle) |
| `linuxxr_monado_integration` | **Fake-Unity-Host mit echtem Vulkan-Gerät (lavapipe) und headless Monado**: Session erreicht `FOCUSED`; Single-Pass: 1 Pass / 2 Parameter-Sätze; Multi-Pass: 2 Passes; Frames werden übertragen; Geräte (HMD, 2 Controller) vorhanden; Mirror-Modi; Wechsel der Render-Skalierung; Umschalten sRGB |
| `linuxxr_monado_instance_loss` | Monado wird **mitten in der Session beendet**; das Plugin hängt nicht (Watchdog) |

Die Monado-Tests enden mit „SKIP“ (ctest „Skipped“), wenn Monado, Vulkan oder der OpenXR-Loader fehlen.

**Nicht verifiziert:**

- **Echter Unity-Editor** (Unitys tatsächliches Verhalten bei `AccessQueue`, Layouts, Mirror-Blit, Play/Stop-Zyklen).
- **Echtes WiVRn und Quest 3** (Bildqualität, Orientierung, Latenz, Controller, Haptik).
- AMD/RADV mit realer GPU.

Der Fake-Host bildet Unity nach; Abweichungen sind möglich, besonders bei Bildorientierung, Farben und Synchronisation. Genau dafür sind die Schalter in Abschnitt 5.

---

## 3. Test Schritt für Schritt

Zielsystem wie in PHASE1.md, Abschnitt 3. Empfohlener Ort des Testprojekts: `~/Projekte/UnityVR-Linux/unity/XRTestbed`.

### 3.1 Nativ bauen

```bash
cd ~/Projekte/UnityVR-Linux
./scripts/build-native.sh test
```

Erwartung: alle 4 Suites grün (bzw. die Monado-Tests „Skipped“, falls Monado nicht installiert ist). `libLinuxXR.so` liegt danach im Paket unter `unity/Packages/com.tyroonis.xr.linuxopenxr/Runtime/Plugins/Linux/x86_64/`.

### 3.2 Frisches Unity-Projekt

1. Unity Hub: neues Projekt **Unity 6 (6000.6.5f1)**, Vorlage **3D (URP)**, Name z. B. `XRTestbed` unter `~/Projekte/UnityVR-Linux/unity/`.
2. **Window → Package Manager → „+“ → Install package from disk…** → `unity/Packages/com.tyroonis.xr.linuxopenxr/package.json`.
3. **Edit → Project Settings → XR Plug-in Management → Install XR Plug-in Management**.
4. Tab **Windows, Mac, Linux** (Standalone): **Initialize XR on Startup** an, **LinuxXR** anhaken. **OpenXR dort nicht anhaken** (blockiert den Play Mode auf Linux, siehe PHASE1.md Abschnitt 7.2).
5. Optional für ein fertiges Rig: Package Manager → **XR Interaction Toolkit** installieren (bringt auch das Input System mit; bei der Rückfrage zum Backend „Yes“ und Editor-Neustart).
6. Empfohlen: **Edit → Project Settings → Player → Other Settings → Color Space = Linear** (siehe Abschnitt 5).
7. **Window → XR → LinuxXR Status** öffnen → **Vulkan setzen** → Neustart bestätigen. Die URP-Vorlage hat meist schon Vulkan; der Button stellt sicher, dass es die **erste** Linux-Grafik-API ist.
8. **Editor komplett neu starten** (PreInit läuft nur beim Editor-Start). Am besten mit WiVRn und Quest bereits verbunden, damit die Vulkan-Extension-Liste von der Runtime kommt (Status: „PreInit: ok (Liste von der Runtime)“).
9. Console prüfen (Abschnitt 4: Editor-Start).
10. Optional unter **XR Plug-in Management → LinuxXR**: Stereo-Modus, Mirror-Modus, Render Scale (z. B. 0.7 zum Testen, falls die GPU-Last hoch ist), Tracking Origin.

### 3.3 Szene

**Variante A (einfach, mit XRI):** In der Hierarchie **XR → XR Origin (XR Rig)** hinzufügen (die vorhandene Main Camera löschen). Ein paar Würfel auf Armlänge platzieren (Rechtsklick → 3D Object → Cube, Position z. B. (0, 1.2, 1.5), Scale 0.3).

**Variante B (ohne XRI, Input System):**

1. Main Camera mit **Tracked Pose Driver (Input System)** versehen (Add Component → Tracked Pose Driver (Input System)). Position Input Action: `<XRHMD>/centerEyePosition`, Rotation: `<XRHMD>/centerEyeRotation`. Alternativ den alten **Tracked Pose Driver**, falls das Legacy-Input-Paket aktiv ist (Pose Source: Center Eye – Head Mounted Display Reference).
2. Zwei leere GameObjects „LeftHand“/„RightHand“ mit je einem Tracked Pose Driver (Input System) und je einem kleinen Würfel (Scale 0.1) als Kind. Bindings: `<XRController>{LeftHand}/devicePosition` und `<XRController>{LeftHand}/deviceRotation` bzw. `{RightHand}` für rechts. Tracking State: `<XRController>{LeftHand}/trackingState`.
3. Würfel (siehe oben) in die Szene.

Die Kamera sollte sich als Kind eines Rig-Objekts in Bodenhöhe 0 befinden (Tracking Origin *Floor*), sonst stimmt die Augenhöhe nicht.

### 3.4 Starten

1. WiVRn-Dashboard: `flatpak run io.github.wivrn.wivrn` (oder Status-Fenster → **WiVRn starten**).
2. Quest: WiVRn-App öffnen, PC wählen und verbinden. **Erst danach** Play.
3. Status-Fenster: „Headset: verbunden ✔“.
4. **Play** drücken.

### 3.5 Erwartung

- [ ] Bild in der Brille: Würfel stehen stabil im Raum, richtige Seite oben, Farben plausibel.
- [ ] Kopfdrehung und Kopfbewegung werden korrekt umgesetzt (Kamera folgt, Bild bleibt im Raum stabil).
- [ ] Game View zeigt das Spiegelbild (Standard: linkes Auge).
- [ ] Controller bewegen sich (Variante A: Controller-Visuals des Rigs; Variante B: die Würfel). Trigger/Grip/Sticks liefern Werte, z. B. im Input Debugger (**Window → Analysis → Input Debugger → Devices → LinuxXR Controller - …**).
- [ ] `Debug.Log`, Pausieren und Breakpoints im Editor funktionieren wie gewohnt (Brille zeigt bei Pause das letzte Bild).
- [ ] Status-Fenster: Session `FOCUSED`, Hz-Wert, wachsender Frames-Zähler.
- [ ] **Play/Stop mehrfach hintereinander** (mindestens 5-mal): jedes Mal Bild, kein Absturz, kein Hänger. Beim Stop erscheint in der Console `OpenXR-Session beendet`.
- [ ] Optional: Mirror-Modus und Render Scale ändern (wirkt bei der nächsten Session, also nach Stop/Play), Multi-Pass testen.

Falls etwas nicht stimmt: Abschnitt 5 und Rückmeldung in Abschnitt 6.

---

## 4. Erwartete Console-Zeilen

Alle Zeilen tragen das Präfix `[LinuxXR]`. Die Reihenfolge beim Play-Start legt Unity fest und ist nicht zugesichert. Zusätzlich gelten die Zeilen aus PHASE1.md, Abschnitt 7.5 (Editor-Start: PreInit; Play: `Initialisiert (Version …)`).

| Zeitpunkt | Zeile |
|---|---|
| Play | `Display-Subsystem initialisiert (PreInit ok, Vulkan-Liste: runtime, GPU: …)` (statt `runtime` ggf. `cache`/`fallback`) |
| Play | `Runtime: <Manifest> [<Typ>, Quelle: …]` |
| Play | `OpenXR bereit: <Runtime> <Version>, System "…", BxH pro Auge` (Breite x Höhe) |
| Play | `Session erstellt (Queue-Familie N, Index 0), App-Space LOCAL_FLOOR` (bzw. `STAGE`/`LOCAL`) |
| Play | `Controller-Actions bereit` |
| Play | `Session-Status: IDLE -> READY`, danach `SYNCHRONIZED`, `VISIBLE`, `FOCUSED` (jeweils `Session-Status: A -> B`) |
| Play | `Input-Subsystem initialisiert`, dann `Input-Subsystem gestartet: HMD + 2 Controller` |
| Play | `Display-Subsystem gestartet – Bild geht an <Systemname>` |
| Play | `Display (Gfx-Thread) gestartet` (ggf. mit Zusatz `, Y gespiegelt` und/oder `, Direct-Submit`) |
| Play, erster Frame | `Erster Frame: singlePass=1 sRGB=1 zNear=… zFar=… Skalierung=1.00` |
| Play | `Augen-Texturen: BxH, <Format>, <Layout> (Renderskalierung 1.00)` |
| Play | `Swapchain BxH x2, Format N, M Bilder` |
| Play, erstes Bild | `Erstes Bild: Unity-Format N BxH, Swapchain-Format N BxH → Kopie` (oder `→ Blit`, wenn die Formate verschieden sind) |
| Laufzeit | `Bildwiederholrate: A -> B Hz`, `Referenzraum ändert sich (Recenter/Guardian)`, `Tracking-Origin: Floor` bzw. `Device` |
| Stop | `Display-Subsystem gestoppt`, `Display (Gfx-Thread) gestoppt: N Frames angefragt, M an die Brille`, `Display-Subsystem beendet`, `OpenXR-Session beendet` |

Wichtige Warnungen/Fehler:

| Zeile | Bedeutung |
|---|---|
| `Unitys VkDevice wurde ohne … erstellt – Editor neu starten (Liste ist jetzt gecacht).` | PreInit kannte die Extension nicht, siehe Abschnitt 5 |
| `PreInit lief nicht beim Editor-Start – Vulkan-Extensions fehlen evtl. Editor neu starten.` | siehe PHASE1.md Abschnitt 10 |
| `Vorheriger Frame wurde nicht abgeschlossen (AccessQueue-Callback ausgeblieben?) – LINUXXR_DIRECT_SUBMIT=1 testen` | Abschnitt 5 |
| `Verbindung zur OpenXR-Runtime verloren (XR_ERROR_…)` bzw. `OpenXR-Instanz wird verloren gehen (Runtime beendet?)` | Runtime-Verlust, Play beenden und neu starten |
| `Display konnte nicht starten – siehe vorherige Meldung (Vulkan? WiVRn läuft? Quest verbunden?).` | Start fehlgeschlagen |
| `Unity-Augentextur N nicht zugreifbar (AccessTexture)`, `CreateTexture fehlgeschlagen (…)`, `Kopier-Fence hängt` | Fehler im Bildpfad, bitte melden |

Mit `LINUXXR_LOG=debug` kommen weitere Zeilen hinzu.

---

## 5. Fehlerbehebung

Grundlegendes (Plugin lädt nicht, Runtime nicht erreichbar, Versionskonflikt Quest/Server, Firewall usw.) steht in PHASE1.md, Abschnitt 10. Neu in Phase 2:

| Symptom | Ursache / Lösung |
|---|---|
| **Bild steht auf dem Kopf** | Orientierung der Unity-Textur gegenüber OpenXR. Editor mit `LINUXXR_FLIP_Y=1` starten (Console: `Display (Gfx-Thread) gestartet, Y gespiegelt`) **und bitte melden**, damit wir den Standard korrigieren. Die Variable muss **vor** dem Start von Unity Hub bzw. Editor gesetzt sein, am einfachsten per Startskript, z. B. `LINUXXR_FLIP_Y=1 ~/Unity/Hub/Editor/6000.6.5f1/Editor/Unity -projectPath ~/Projekte/UnityVR-Linux/unity/XRTestbed` (Editor-Pfad anpassen). Wird der Hub benutzt, die Variable vor dem Hub-Start exportieren. |
| **Bild eingefroren oder schwarz**, oder Warnung `Vorheriger Frame wurde nicht abgeschlossen …` | Der `AccessQueue`-Callback läuft nicht wie erwartet. Mit `LINUXXR_DIRECT_SUBMIT=1` starten (Console: `…, Direct-Submit`, setzt wie `LINUXXR_FLIP_Y` einen Editor-Neustart voraus) **und bitte melden**. |
| **Zu dunkel oder zu hell / ausgewaschen** | sRGB-/Linear-Problem. In den Player Settings **Color Space = Linear** setzen (empfohlen). Die Zeile `Erster Frame: … sRGB=…` und `Erstes Bild: …` melden. |
| `Unitys VkDevice wurde ohne … erstellt` | Die Runtime verlangt eine Device-Extension, die beim Editor-Start nicht angefordert wurde. Headset verbinden und **Editor neu starten** (die Liste ist jetzt gecacht); ggf. `rm ~/.cache/linuxxr/vulkan_requirements.txt`. |
| **Play startet, aber kein Bild; `Display konnte nicht starten …`** | WiVRn nicht gestartet oder Quest nicht verbunden. Dashboard starten, WiVRn-App verbinden, `ls $XDG_RUNTIME_DIR/wivrn/` muss den Socket zeigen, dann Play. |
| **GPU-Last hoch, Ruckeln, Bildrate fällt** | WiVRn empfiehlt ca. 2476x2594 pro Auge. In den LinuxXR-Einstellungen `renderScale` senken (z. B. 0.7), Session neu starten; zusätzlich Bitrate/Auflösung in der WiVRn-Client-App prüfen. |
| **Hohe Latenz, Bild „schwimmt“** | Netzwerk: LAN-Kabel am PC statt USB-WLAN-Stick, oder USB-Netzwerk zur Quest (`./scripts/quest-usb.sh` bzw. Schalter in der WiVRn-App). |
| **Editor friert nach WiVRn-Ende ein** | Soll nicht passieren (siehe Abschnitt 1). Falls doch: bitte Logdatei und `~/.config/unity3d/Editor.log` schicken. |
| **Status zeigt „Verbindung zur Brille verloren …“** | Play beenden, WiVRn/Quest prüfen, neu starten. |
| **Controller bewegen sich nicht** | Tracked Pose Driver mit den richtigen Bindings? Im Input Debugger prüfen, ob die Geräte `LinuxXR Controller - Left/Right` existieren. Wenn ja und Werte fehlen: Log mit `LINUXXR_LOG=debug`. |
| Augenhöhe falsch (Kamera am Boden oder zu hoch) | Tracking Origin passt nicht zum Rig: *Floor* (Rig-Höhe 0) oder *Device* (Rig-Höhe = Kopfhöhe). Bei XR Origin den Tracking Origin Mode setzen. |

Logs:

- Plugin: `~/.cache/linuxxr/linuxxr.log` (ausführlich mit `LINUXXR_LOG=debug` oder „Verbose Logging“ in den LinuxXR-Einstellungen).
- Editor: `~/.config/unity3d/Editor.log`.
- WiVRn (Flatpak): im Dashboard bzw. mit `flatpak run io.github.wivrn.wivrn` im Terminal gestartet; alternativ `flatpak run --command=sh io.github.wivrn.wivrn` und dort `wivrn-server` direkt ausführen. Auf dem Host reicht `journalctl --user -b | grep -i wivrn`, sofern der Server als Dienst läuft.

---

## 6. Rückmeldung an Claude

Bitte schicken, sobald getestet:

1. **Console-Zeilen mit `[LinuxXR]`** vom Editor-Start bis nach Play/Stop (Status-Fenster → **Diagnose kopieren**).
2. **`~/.cache/linuxxr/linuxxr.log`** (bei Problemen mit `LINUXXR_LOG=debug` erzeugt).
3. In Worten:
   - Bild korrekt, **auf dem Kopf** oder gespiegelt? Farben/Helligkeit in Ordnung (Color Space Linear oder Gamma)?
   - Bildrate: Hz-Wert und Frames-Zähler im Status-Fenster (Zähler zweimal mit Zeitabstand ablesen, daraus ergibt sich die Rate); Eindruck von Latenz und Ruckeln.
   - Kopf-Tracking korrekt (Richtung, Höhe)?
   - Controller-Test: Position/Rotation, Trigger, Grip, Stick, A/B/X/Y, Menü, Haptik (zum Beispiel per Skript mit `SendHapticImpulse`).
   - Mehrfaches Play/Stop ohne Probleme? Verhalten, wenn WiVRn beendet wird.
   - Ob `LINUXXR_FLIP_Y` bzw. `LINUXXR_DIRECT_SUBMIT` nötig waren.
4. Bei Absturz oder Fehler im Editor: die letzten Zeilen von `~/.config/unity3d/Editor.log`.

---

## 7. Bekannte Grenzen und nächste Schritte

Bekannte Grenzen:

- **Keine Tiefenschicht:** Es wird kein Depth-Layer übermittelt, weil WiVRn `XR_KHR_composition_layer_depth` nicht anbietet. Reprojektion nutzt daher keine Tiefe.
- **Kein Occlusion Mesh** (`occlusionMeshId = 0`).
- **Tracking-Origin zur Laufzeit wechseln** ist nicht vollständig thread-sicher (der Wechsel wird im Gfx-Thread vor dem nächsten Frame angewendet). Zuverlässig ist die Einstellung vor Play.
- **Kopfpose und Zeitpunkt:** Die Kamerapose kommt aus der `BeforeRender`-Extrapolation des Input-Subsystems; die im Frame an die Runtime übergebene Pose kann leicht von der vorhergesagten Pose abweichen. Das Verhalten im echten Unity ist ungetestet.
- **Meta-SDK / HurricaneVR:** wie PHASE1.md, Abschnitt 11. XRI und HurricaneVR (XR-Input) sollten jetzt Geräte sehen. Meta-SDK-Teile, die OVRPlugin nutzen (`OVRCameraRig`, `OVRInput`, `OVRManager`), bekommen im Linux-Editor keine Daten.
- **Hand-Tracking:** noch nicht enthalten.

Nächste Schritte (siehe [PLAN.md](PLAN.md)):

- **Phase 3–5:** Feinschliff nach dem Praxistest (Pose-Timing, Eingabe-Latenz, Robustheit bei Verbindungsabbruch), Hand-Tracking, Kompatibilitätstests mit XRI, HurricaneVR und den vom Projekt genutzten Meta-SDK-Teilen. Die genaue Aufteilung auf die Phasen steht im Plan.
