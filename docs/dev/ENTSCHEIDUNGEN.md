# Entscheidungen (ADR-Stil)

Stand: 2026-10-12. Kurzform je Entscheidung: Kontext, Entscheidung, Alternativen, Konsequenzen. Details im [PLAN.md](PLAN.md), Belege in `docs/research/`.

| Nr. | Titel | Status |
|---|---|---|
| ADR 1 | Eigener nativer Unity-XR-Provider „LinuxXR" | angenommen |
| ADR 2 | WiVRn als Flatpak als Streaming-Runtime | geändert am 2026-10-12 – vorher PPA |
| ADR 3 | Kein Container für Unity/WiVRn; Container nur optional für den Plugin-Build | angenommen |
| ADR 4 | Vulkan als einzige Grafik-API; Editor auf dem Host unter XWayland | angenommen |
| ADR 5 | Lazy-Verbindung zu OpenXR; PreInit mit Cache und fester Extension-Liste | angenommen; PreInit in Phase 1 umgesetzt und mit Monado geprüft, mit WiVRn/Unity zu verifizieren |
| ADR 6 | Unitys OpenXR-Paket bleibt nur für den Android-Build | angenommen |
| ADR 7 | Modellwahl: Code auf Opus, alles andere auf günstigeren Modellen | angenommen (User-Präferenz) |
| ADR 8 | Lizenz des Plugins und Umgang mit den Unity-Headern | offen |
| ADR 9 | Kopie statt Direkt-Rendering in Swapchain-Bilder | vorgeschlagen |
| ADR 10 | `XR_KHR_vulkan_enable` (v1) statt `enable2` im Editor | angenommen; in Phase 1 umgesetzt und mit Monado geprüft, mit WiVRn zu verifizieren |

---

## ADR 1 – Eigener nativer Unity-XR-Provider „LinuxXR"

**Kontext.** `com.unity.xr.openxr` hat keine Linux-Binärdatei, `OpenXRProjectValidation` blockiert den Play Mode bei `StandaloneLinux64`, `OpenXRLoader.cs` kennt nur Windows und macOS. Unity macht seit 2023 keine Zusage. Der Community-Hack (Stridemann: Android-x86_64-Binärdatei, bionic-Shims, Paket gepinnt ≤ 1.16.1) wurde nur mit SteamVR + ALVR getestet und bricht ab Paketversion 1.17.0-pre.2.

**Entscheidung.** Wir schreiben einen eigenen Provider: C++-Shared-Library `libLinuxXR.so` gegen die öffentlichen Unity-XR-SDK-Header (aus `ValveSoftware/unity-xr-plugin`, `CommonHeaders/ProviderInterface`, Unity Companion License), den Khronos-OpenXR-Loader (`libopenxr-loader1`, Ubuntu 26.04: 1.1.47, per `dlopen`) und Vulkan, dazu ein C#-Paket (`XRLoaderHelper`, Subsystem-Manifest, Settings, Editor-Tooling). Valves OpenVR-Provider dient als Strukturvorlage.

**Alternativen.**
- Stridemann-Hack dauerhaft nutzen: fragil, an Paketversion ≤ 1.16.1 gebunden, nie mit WiVRn getestet. Nur als optionaler Diagnose-Spike (Phase 0c, max. 1 Abend).
- Valves OpenVR-Plugin plus xrizer auf WiVRn: braucht OpenVR-Schicht, ungetestet, Multi-Pass/Single-Pass-Einschränkungen unter Linux.
- ALVR/Steam Link/SteamVR: verstößt gegen „ohne SteamVR".
- Auf Unity warten: keine Zusage, keine Frist.
- Nur Android-„Build and Run": bleibt Rückfalllinie, aber keine schnelle Iteration.

**Konsequenzen.**
- Größter Aufwand und größtes Risiko des Projekts (Abschnitt 8 im Plan: R2, R3, R4, R6, R14).
- Wir sind unabhängig von Unitys Plattformsperren und von Paketversionen des OpenXR-Pakets.
- Die Header sind undokumentiert; wir pinnen den Stand und prüfen bei Unity-Updates.
- Editor-Debugging läuft wie unter Windows mit Quest Link, weil das Spiel im Editor-Prozess läuft.

---

## ADR 2 – WiVRn als Flatpak als Streaming-Runtime

**Status:** geändert am 2026-10-12 – vorher PPA (`ppa:lvra/wivrn`).

**Kontext.** Gesucht ist eine OpenXR-Runtime für Linux, die zur Quest 3 streamt, ohne SteamVR und ohne Meta Link. WiVRn ist ein Monado-Fork mit Streaming-Server (GPL-3.0), Server headless (kein Display/Compositor/DRM-Lease nötig), nahezu täglich gepflegt, Version 26.9. Es gibt zwei Installationswege: das Flatpak `io.github.wivrn.wivrn` (Flathub) und das PPA `ppa:lvra/wivrn` mit Builds für `resolute` (26.04). Pakete im PPA heißen `wivrn-server` und `wivrn-dashboard` (nicht „wivrn"). Ein Ubuntu-Archiv-Paket existiert nicht. Ursprünglich war das PPA gewählt; Tyroonis hat WiVRn inzwischen als Flatpak installiert.

**Entscheidung.** Installation als **Flatpak** `io.github.wivrn.wivrn`. Das Flatpak bringt xrizer und OpenComposite mit, aktualisiert sich selbst und ist mit Vulkan-Video-Encode gebaut (passend zur AMD Radeon RX 7600). `scripts/setup-host.sh` installiert standardmäßig das Flatpak; `--wivrn=ppa` und `--wivrn=none` sind Alternativen. Randdaten:

- Server/Dashboard starten: `flatpak run io.github.wivrn.wivrn`.
- Konfiguration: `~/.var/app/io.github.wivrn.wivrn/config/wivrn/config.json`.
- Runtime-Manifest: `/var/lib/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn.json` (bei `--user`-Installation `~/.local/share/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn.json`). LinuxXR findet beide Pfade im Modus *auto*.
- IPC-Socket weiterhin auf dem Host: `$XDG_RUNTIME_DIR/wivrn/comp_ipc`. Er existiert nur bei laufendem Server und verbundenem Headset; LinuxXR wertet das als „erreichbar".
- Port 9757 TCP+UDP, mDNS über avahi (5353/udp), Client `org.meumeu.wivrn` (Meta Store, App-ID 7959676140827574, oder passende APK sideloaden; Protokollversion muss exakt passen), PIN-Pairing.
- USB: „USB-Netzwerk" in WiVRn 26.9 (Quest 3, ohne ADB) oder `adb reverse tcp:9757 tcp:9757` mit `adb shell am start -a android.intent.action.VIEW -d "wivrn+tcp://127.0.0.1:9757" org.meumeu.wivrn`.

**Alternativen.**
- PPA `ppa:lvra/wivrn` (`./scripts/setup-host.sh --wivrn=ppa`): Pakete `wivrn-server` und `wivrn-dashboard`; Runtime-Manifest `/usr/share/openxr/1/openxr_wivrn*.json`, Bibliothek `/usr/lib/x86_64-linux-gnu/wivrn/libopenxr_wivrn.so` (Recherche 01). Bleibt unterstützt; LinuxXR erkennt beide Installationsarten. Das PPA baut laut Recherche voraussichtlich ohne Vulkan-Encode (VAAPI; zu verifizieren).
- Quellbau: nur bei Bedarf (Patches); verlangt Vulkan ≥ 1.4.304, CMake ≥ 3.28, C++23.
- ALVR (nur mit SteamVR), CloudXR (nur NVIDIA, proprietär), Monado allein (streamt nicht): verworfen.

**Konsequenzen.**
- Der Manifest-Pfad läuft über `current/active` und bleibt damit über Updates stabil, obwohl der tatsächliche Flatpak-Pfad einen Commit-Hash enthält.
- Die Client-Bibliothek der Runtime (`libopenxr_wivrn.so`) wird im Unity-Prozess auf dem Host geladen. Sie ist mit einer glibc-2.35-Toolchain gebaut und lädt deshalb auch im Host-Unity (glibc 2.43). Dass das mit dem echten Unity-Editor funktioniert, ist noch nicht getestet.
- Kein `setcap cap_sys_nice` nötig.
- Das Flatpak aktualisiert sich selbst; Server (Flatpak) und Quest-Client müssen weiter zusammenpassen (R11, R15). Updates bewusst durchführen und danach den Phase-0-Test (`hello_xr`) wiederholen.
- Vulkan-Video-Encode im Flatpak-Build ist für die RX 7600 vorteilhaft; welches Encoder-Backend WiVRn tatsächlich wählt, steht im Dashboard bzw. Serverlog (zu verifizieren).
- Wayland/X11 ist für den Server irrelevant.
- Die Runtime bedient Apps nur bei verbundenem Headset (Risiko R10, Lazy-Verbindung in ADR 5).
- `XR_KHR_composition_layer_depth` fehlt im WiVRn-Build, `XR_FB_passthrough`-Layer werden nicht gerendert; der Provider setzt beides nicht voraus.
- Unity-Prozess und Server müssen dieselbe GPU nutzen (R3).

---

## ADR 3 – Kein Container für Unity/WiVRn; Build-Container nur optional

**Kontext.** Die OpenXR-Client-Bibliothek der Runtime wird in den Unity-Prozess geladen und teilt GPU-Puffer per fd mit dem Server; Unix-Socket, Vulkan-Treiber, Netzwerk (avahi), D-Bus und PipeWire müssten durch einen Container gereicht werden. Ein Bericht über Unity in Distrobox (Bazzite, NVIDIA) zeigt ein leeres Editor-Fenster. Unity unterstützt offiziell nur Ubuntu 22.04/24.04.

**Entscheidung.** Unity-Editor und WiVRn laufen direkt auf dem Host. Ein Container (podman aus dem Ubuntu-Repo) ist nur **optional** für einen reproduzierbaren Plugin-Build: Image `ubuntu:24.04`, damit die gebaute `.so` auch mit der älteren glibc 2.39 kompatibel ist; ohne GPU, Socket oder Netzwerk. Ein Host-Build (`build-essential cmake ninja-build libvulkan-dev libopenxr-dev`) ist gleichwertig. Da der Loader per `dlopen` geladen wird, braucht der Build nur Header; falls die Header von `libopenxr-dev` 1.0.20 (24.04) zu alt sind, werden die Khronos-Header vendored.

**Alternativen.**
- Unity im Distrobox/Container: ungeklärte GPU/Display-Probleme, Mehraufwand, kein Nutzen für unser Ziel.
- WiVRn-Server im selbstgebauten Container: braucht Host-Netzwerk, `/dev/dri`, `XDG_RUNTIME_DIR`, D-Bus, Export von Manifest und Client-`.so` auf einen Host-Pfad; entfällt, weil das WiVRn-Flatpak (ADR 2) Socket und Manifest bereits auf dem Host bereitstellt.

**Konsequenzen.**
- Weniger bewegliche Teile im Alltag; die Debug-Kette entspricht dem Windows-Modell.
- Host-Pakete bleiben mit Flatpak + Ubuntu-Archiv überschaubar.
- ABI-Regel: Die Container-glibc darf nicht neuer sein als die der Host-Laufzeit; mit 24.04-Image gilt das für 26.04 immer.
- Ubuntu 26.04 für Unity ist nicht offiziell unterstützt (Risiko R1); Rückfalloptionen stehen im Plan.

---

## ADR 4 – Vulkan als einzige Grafik-API; Editor auf dem Host unter XWayland

**Kontext.** Unitys OpenXR-Plugin kennt OpenGL nie (keine Desktop-GL-Extension in den Binärdateien). WiVRn hat mit OpenGL-Clients Probleme (`XR_ERROR_GRAPHICS_DEVICE_INVALID`, Issues #526, #787, #1119); Vulkan ist der erstklassige Pfad. Der Editor läuft unter GNOME Wayland vermutlich über XWayland (nativ-Wayland laut Unity „experimentell"; ungeprüft).

**Entscheidung.** Nur Vulkan: Vulkan als erste Linux-Grafik-API im Projekt (oder `-force-vulkan`), `XR_KHR_vulkan_enable` (v1) im Provider (siehe ADR 10). Der Editor wird nicht gezwungen, nativ unter Wayland zu laufen; wir testen XWayland und X11 beide in Phase 3.

**Alternativen.** OpenGL Core (Provider und Runtime-Seite fragil, Unitys Android-Binärdatei nur mit EGL); D3D (nicht Linux).

**Konsequenzen.** Projekt-Validierung im Status-Fenster prüft die Grafik-API-Reihenfolge; keine GL-Sonderpfade; Wayland/X11 betreffen nur Unitys Fenster und Game View, nicht den XR-Pfad.

---

## ADR 5 – Lazy-Verbindung zu OpenXR; PreInit mit Cache und fester Extension-Liste

**Kontext.** `IUnityXRPreInit` wird beim Editor-Start aufgerufen, bevor Unity Vulkan initialisiert, und muss Vulkan-Instanz-/Geräte-Extensions und den Adapter liefern. Die WiVRn-Runtime ist aber nur bei verbundenem Headset erreichbar. Der Editor soll jederzeit startbar sein.

**Entscheidung.** (1) Runtime erreichbar → Extensions per `XR_KHR_vulkan_enable` abfragen und cachen (umgesetzt in `$XDG_CACHE_HOME/linuxxr/vulkan_requirements.txt`, Schlüssel ist das Runtime-Manifest). (2) Sonst Cache oder eine fest einkompilierte Liste für Monado/WiVRn (`external_memory_fd`, `external_semaphore_fd`, `timeline_semaphore`, `dedicated_allocation`, `get_memory_requirements2` u. a.; endgültige Liste zu verifizieren). (3) Instanz und Session erst bei Play; vor `xrCreateSession` das von OpenXR geforderte `VkPhysicalDevice` per UUID mit Unitys Gerät vergleichen. Runtime-Wahl per Projekteinstellung (Auto/Pfad) und `setenv("XR_RUNTIME_JSON", …)` vor `xrCreateInstance`. Unity erzeugt Instanz und Gerät selbst („Variante A"); `InterceptInitialization` mit `xrCreateVulkanInstanceKHR` ist nur der Rückfall („Variante B").

**Alternativen.** Headset zwingend vor Editor-Start (Variante B, schlechte Bedienung); Server mit `--early-active-runtime`; nur feste Liste ohne Abfrage (fragil bei Versionswechseln).

**Konsequenzen.** Stand Phase 1: umgesetzt; die Basisliste wird immer gegen das tatsächlich vom Treiber Unterstützte gefiltert und deckte sich in der Sandbox exakt mit Monados Anforderung. Editor startet unabhängig vom Headset. Die Extensions-Liste kann zur Laufzeit veralten (Cache-Schlüssel enthält Runtime-Pfad und Version). Ob eine Runtime die Kombination „Abfrage über `_enable`, Binding über Vulkan2-Struktur" akzeptiert, ist in Phase 2 zu verifizieren. Single-GPU-AMD macht den Geräteabgleich unkritisch; bei Multi-GPU gibt es eine klare Fehlermeldung.

---

## ADR 6 – Unitys OpenXR-Paket nur für den Android-Build

**Kontext.** Für echte Builds auf der Quest ist Unitys OpenXR + Meta Quest Support der unterstützte Weg und funktioniert unter Linux (Build and Run, logcat, Managed Debugger, Profiler). Im Linux-Editor ist es nutzlos und seine Validierung blockiert den Play Mode.

**Entscheidung.** Android-Tab: OpenXR + Meta Quest Support, unverändert. Standalone-Tab (Editor-Play): ausschließlich LinuxXR. Spielcode bleibt API-neutral (`UnityEngine.XR`, Input System, XRI), damit er in beiden Wegen identisch läuft. Das OpenXR-Paket wird nicht gepatcht.

**Alternativen.** Paket patchen (Stridemann): bricht bei Updates. Nur Android-Weg: langsame Iteration.

**Konsequenzen.** Zwei Konfigurationen im Projekt; Risiko, dass die Validierungsregel des OpenXR-Pakets trotz deaktiviertem Standalone-Tab feuert (R7 im Plan, zu verifizieren; Rückfall: Regel per Editor-Skript unterdrücken). „Build and Run" bleibt als dauerhafter Fallback.

---

## ADR 7 – Modellwahl nach User-Präferenz

**Kontext.** Präferenz von Tyroonis für Programmierprojekte: mit Subagenten arbeiten; nur reiner Code auf dem starken Modell (Opus), alles andere (Dokumentation, Tests, Recherche, Planung usw.) auf einfacheren Modellen (Sonnet/Haiku). Claude beurteilt selbst, was Code ist; Rückfrage zur Modellwahl nur am Anfang einer Aufgabe.

**Entscheidung.** C++- und C#-Produktionscode auf Opus. CMake, Shell-Skripte, Containerfile, Tests, Doku, Recherche, Planung auf Sonnet bzw. Haiku. Tabelle im Plan, Abschnitt 12.

**Alternativen.** Alles auf Opus (teurer, keine Qualitätssteigerung bei Doku); alles auf Sonnet (Risiko bei Threading/Vulkan-Interop).

**Konsequenzen.** Klare Zuständigkeiten; Grenzfälle (Skripte mit Logik, Unit-Tests) entscheidet Claude pro Aufgabe, im Zweifel das günstigere Modell, sofern per Test prüfbar.

---

## ADR 8 – Lizenz des Plugins und Umgang mit den Unity-Headern (offen)

**Kontext.** Die Unity-XR-SDK-Header stehen unter der Unity Companion License v1.2 (Nutzung nur für Inhalte, die eine gültige Unity-Engine-Lizenz voraussetzen; Wettbewerbsprodukte ausgeschlossen; „AS IS"). WiVRn ist GPL-3.0, wird aber nur über die OpenXR-Schnittstelle zur Laufzeit genutzt. Eine rechtliche Prüfung hat nicht stattgefunden.

**Entscheidung (vorläufig).** Header unverändert mit `LICENSE.md` unter `native/third_party/unity-xr-headers/` führen. Für privaten Gebrauch kein Problem. Eigene Lizenz des Providers und Veröffentlichung werden entschieden, wenn Phase 3 erreicht ist (Frage 3 im Plan, Abschnitt 13).

**Alternativen.** Header nicht ins Repo legen, sondern per Skript herunterladen (vermeidet Weitergabe); Provider nur privat halten.

**Stand Phase 1.** Umgesetzt ist die erste Alternative: `scripts/fetch-unity-headers.sh` holt die Header aus einem gepinnten Commit von `ValveSoftware/unity-xr-plugin` nach `native/third_party/unity-xr-headers/` (mit Lizenzdatei), der Ordner steht in `.gitignore`. `scripts/build-native.sh` ruft das Skript selbst auf. Eigene Lizenz und Veröffentlichung bleiben offen.

**Konsequenzen.** Vor einer Veröffentlichung ist die Lizenzlage zu klären; bis dahin gilt: kein öffentliches Repo mit Headern.

---

## ADR 9 – Kopie statt Direkt-Rendering in Swapchain-Bilder

**Status:** vorgeschlagen (Phase 2; wird mit dem ersten Bild auf der Quest bestätigt oder ersetzt).

**Kontext.** In Phase 2 müssen die von Unity gerenderten Bilder in die Swapchain-Bilder der OpenXR-Runtime gelangen. Der Plan (Abschnitt 6.4) sah vor, die `VkImage`s der Swapchain Unity als native Render-Texturen bekannt zu machen, sodass Unity direkt hineinrendert. Die Semantik der Vulkan-Zeiger in den Unity-XR-Strukturen (`nativePtr`) ist undokumentiert, ebenso wer Layout-Übergänge und Synchronisation übernimmt. Die Bilder gehören der Runtime (bei WiVRn werden sie per fd mit dem Server geteilt).

**Entscheidung.** Unity alloziert die Render-Texturen selbst. Das Plugin kopiert in `SubmitCurrentFrame` den Inhalt per `vkCmdCopyImage` (bei Format- oder Größenabweichung `vkCmdBlitImage`) in das erworbene Swapchain-Bild, danach `xrReleaseSwapchainImage` und `xrEndFrame`. Zugriff auf Unitys Texturen und Queue über `IUnityGraphicsVulkan::AccessTexture` und `AccessQueue` mit `flush`, damit Unitys ausstehende Arbeit vor der Kopie abgeschlossen ist. Layout-Übergänge der Swapchain-Bilder (laut OpenXR-Spec `COLOR_ATTACHMENT_OPTIMAL` bei Freigabe) liegen vollständig im Plugin.

**Alternativen.**
- Direkt-Rendering in die Swapchain-Bilder (Plan 6.4): spart die Kopie, hängt aber an der undokumentierten Zeigersemantik, an Unitys Annahmen über Layout und Besitz der Bilder und an der Synchronisation mit der Runtime. Höheres Risiko für schwer zu findende Fehler.
- Kopie per Compute-Shader oder Render-Pass: nur nötig, wenn Format- oder Farbraumumwandlung erforderlich ist.

**Konsequenzen.**
- Kosten: eine GPU-Kopie pro Frame, geschätzt unter 1 ms (noch nicht gemessen).
- Die Swapchain braucht `TRANSFER_DST` (die Probe nutzt bereits `COLOR_ATTACHMENT | TRANSFER_DST | SAMPLED`); bei Array-Swapchains werden beide Ebenen kopiert.
- Unity-Render-Textur und Swapchain-Bild müssen kompatible Formate haben; sonst Blit oder Formatauswahl in der Swapchain anpassen.
- Die Spiegelung in die Game View kann dieselbe Unity-Textur nutzen.
- Der Ansatz lässt sich später durch Direkt-Rendering ersetzen, ohne die Frame-Schleife zu ändern. Dann wird dieses ADR abgelöst.

---

## ADR 10 – `XR_KHR_vulkan_enable` (v1) statt `enable2` im Editor

**Status:** angenommen. In Phase 1 im Code umgesetzt (`XrContext`: `XR_KHR_vulkan_enable` ist Pflicht, Session-Binding über `XrGraphicsBindingVulkanKHR`) und mit Monado geprüft; mit WiVRn zu verifizieren.

**Kontext.** Unity erzeugt `VkInstance` und `VkDevice` selbst, beim Editor-Start, noch bevor eine OpenXR-Instanz nötig ist (PreInit, ADR 5). Mit `XR_KHR_vulkan_enable2` erzeugt die Runtime Instanz und Gerät (`xrCreateVulkanInstanceKHR`, `xrCreateVulkanDeviceKHR`). Das würde eine OpenXR-Instanz schon beim Editor-Start erzwingen, also ein verbundenes Headset (Risiko R10) und den Umbau von Unitys Vulkan-Initialisierung (Plan 6.2, „Variante B").

**Entscheidung.** Wir nutzen `XR_KHR_vulkan_enable` (v1): `xrGetVulkanInstanceExtensionsKHR`, `xrGetVulkanDeviceExtensionsKHR`, `xrGetVulkanGraphicsDeviceKHR` und `xrGetVulkanGraphicsRequirementsKHR` liefern die Anforderungen der Runtime; Unity erzeugt Instanz und Gerät mit diesen Extensions; die Session wird mit `XrGraphicsBindingVulkanKHR` an die von der Anwendung erzeugten Objekte gebunden. Die Abfrage und das Binding gehören damit zur selben Extension-Version. Das ist der Weg „Variante A" des Plans, ohne die ungeklärte Mischung „Abfrage über v1, Binding über die v2-Struktur".

**Alternativen.**
- `XR_KHR_vulkan_enable2` mit `InterceptInitialization` und Instanz-/Geräte-Erzeugung durch die Runtime („Variante B"): braucht beim Editor-Start eine erreichbare Runtime, widerspricht dem Ziel „Editor startet auch ohne Headset". Bleibt Rückfall, falls eine Runtime v1 nicht anbietet.
- Abfrage über v1, Binding über `XrGraphicsBindingVulkan2KHR` (Plan 6.2/6.3, ursprünglicher Entwurf): ob Runtimes diese Mischung akzeptieren, ist unklar; nicht nötig.

**Konsequenzen.**
- Die Runtime muss `XR_KHR_vulkan_enable` anbieten (Monado: ja, in der Sandbox geprüft; WiVRn: zu verifizieren). Fehlt sie, meldet `XrContext` „Runtime unterstützt XR_KHR_vulkan_enable nicht".
- Die Mindest- und Höchstversion von Vulkan kommt aus `xrGetVulkanGraphicsRequirementsKHR` und ist Pflichtaufruf vor `xrCreateSession`.
- Das von der Runtime gewünschte `VkPhysicalDevice` wird mit Unitys Gerät verglichen (R3); bei Abweichung gibt es eine Warnung mit Hinweis auf `LINUXXR_GPU` und `XRT_COMPOSITOR_FORCE_GPU_INDEX`.
- Die Extension-Liste wird beim Editor-Start festgelegt und lässt sich danach nicht mehr ändern. Stammt sie aus Cache oder Basisliste und die Runtime verlangt später mehr, hilft nur ein Editor-Neustart nach der ersten Verbindung (siehe [PHASE1.md](PHASE1.md), Fehlerbehebung).
