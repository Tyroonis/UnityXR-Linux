# Unity OpenXR auf dem Linux-Editor: Recherchebericht

Stand: 2026-10-12. Umgebung der Recherche: Sandbox ohne Unity-Installation und ohne GPU. Alle Aussagen beruhen auf statischer Inspektion der Paketinhalte (git-Mirrors, `file`, `readelf`, `strings`) und auf öffentlichen Quellen. Es wurde nichts ausgeführt.

## 0. Fazit

| Frage | Antwort |
|---|---|
| Funktioniert das offizielle `com.unity.xr.openxr` im Linux-Editor (Play Mode)? | **Nein.** Es enthält in keiner geprüften Version ein Linux-Native-Plugin. Zusätzlich blockiert eine Validierungsregel den Play Mode, wenn das aktive Build-Target Linux ist. |
| Gibt es einen Weg, es doch zum Laufen zu bringen? | **Teilweise, per Community-Hack.** Das Repo von Stridemann lädt die Android-x86_64-Bibliothek des Pakets im Linux-Editor, mit glibc-Shims, gepatchtem und auf 1.16.1 gepinntem Paket und nur mit Vulkan. Getestet wurde es mit SteamVR + ALVR, **nicht** mit Monado oder WiVRn. |
| Grafik-API | Nur Vulkan. OpenGL Core kann nicht funktionieren: Die Android-Binärdatei nutzt für OpenGL den EGL/GLES-Pfad, die Windows-Binärdatei hat gar keinen OpenGL-Pfad. |
| Wayland oder X11 | Für XR unbekannt. Der Editor selbst gilt unter Wayland als "experimentell" und läuft vermutlich über XWayland. |
| Gilt der Hack auch für das neueste Paket? | **Nein.** Die Android-x64-Binärdatei wurde in 1.17.0-pre.2 (2026-03-03) entfernt. Es muss `com.unity.xr.openxr` 1.16.1 oder älter (bis 1.9.1) gepinnt werden. |

## 1. Wie die Pakete beschafft wurden

- Die Unity-Registry (`packages.unity.com`, `download.packages.unity.com`) und `docs.unity3d.com` liefern über den Sandbox-Proxy **HTTP 403**. `npm view` und `npm pack` mit `--registry https://packages.unity.com` scheiterten deshalb.
- `registry.npmjs.org/com.unity.xr.openxr` ist nur ein npm-"security holder" (Version 0.0.1-security), nicht das echte Paket.
- Die needle-mirror-Mirrors auf GitHub waren per anonymem git erreichbar. Sie enthalten den offiziellen Paketinhalt, ein Tag pro Release.
  - `needle-mirror/com.unity.xr.openxr` geklont: HEAD ist `1.19.0-pre.2` (2026-09-09, benötigt Unity 6000.0, Commit 5536b0c3).
  - Dateibäume per `git ls-tree` geprüft für die Tags 0.1.1-preview.1, 1.0.2, 1.9.1, 1.12.1, 1.14.0, 1.14.3, 1.15.1, 1.16.1, 1.17.0, 1.17.0-pre.1 und 1.18.0.
  - Außerdem geklont: `needle-mirror/com.unity.xr.management` 4.7.0, `com.unity.xr.meta-openxr` 2.6.1, `com.unity.xr.mock-hmd` 1.5.0-exp.3, `ValveSoftware/unity-xr-plugin` und `Stridemann/Unity-XR-on-Linux-for-Meta-Quest`.
- Stabile Releases reichen bis 1.18.0 (2026-08-04). 1.19.0-pre.2 ist die neueste Vorabversion.
- Lokale Kopien (nur lesend behandelt, nichts ausgeführt) liegen unter `/tmp/claude-0/-home-claude/f6526245-da37-529b-941a-ab31e1f3ac99/scratchpad/research/unity-openxr/`: `repo_openxr`, `repo_com.unity.xr.management`, `repo_com.unity.xr.meta-openxr`, `repo_com.unity.xr.mock-hmd`, `repo_valve`, `strid/repo`, `orig1161` (unverändertes 1.16.1) und `android_x64_libUnityOpenXR_1.16.1.so`.

## 2. Inhalt von com.unity.xr.openxr: was es für Linux gibt

### 2.1 Native Binärdateien (aktuell, 1.19.0-pre.2)

| Pfad im Paket | Format | Plattform |
|---|---|---|
| `Runtime/windows/x64/UnityOpenXR.dll` | PE32+ | Windows x64 |
| `Runtime/osx/UnityOpenXR.dylib` | Mach-O | macOS |
| `Runtime/android/arm64/libUnityOpenXR.so` | ELF aarch64 | Android arm64 |
| `RuntimeLoaders/windows/x64/openxr_loader.dll`, `RuntimeLoaders/osx/libopenxr_loader.dylib`, `RuntimeLoaders/android/openxr_loader.aar` | | Loader nur für diese drei |
| `APILayers~/WindowsLayers/x64/*` | | nur Windows |

- Es gibt **kein** Linux-x86_64-`UnityOpenXR`-Plugin und **keinen** Linux-`openxr_loader`.
- `git ls-tree` auf den Tags 0.1.1-preview.1, 1.0.2, 1.9.1, 1.14.0 und 1.18.0 findet keine Datei mit "linux" im Namen und keine `.so` außerhalb von Android. Linux wurde in den geprüften Versionen nie ausgeliefert.
- Universal Windows Platform wurde in 1.17.0-pre.2 entfernt.
- Die einzige Linux-Datei im Paket ist `Tests/Runtime/UbuntuTests/PlaceholderTests.cs`. Ihr Kommentar lautet "An always passing test for Linux platform to by pass Validation jobs requirements". Es ist ein CI-Platzhalter.
- `CHANGELOG.md` erwähnt Linux oder Ubuntu in keiner Version.
- Die `.meta`-Dateien der Windows-Binärdateien sind seltsam: `Runtime/windows/x64/UnityOpenXR.dll.meta` hat `Standalone: Linux64 enabled: 1`, `Exclude Linux64: 0` und `Editor ... OS: Windows`. Eine PE-DLL lässt sich unter Linux nicht laden, das hat also keine praktische Wirkung, verwirrt aber Unity.

### 2.2 Die Android-x86_64-Binärdatei (Schlüssel zum Community-Hack)

- `Runtime/android/x64/libUnityOpenXR.so` existiert in den Versionen **1.9.1 bis 1.17.0-pre.1** (einschließlich 1.16.1) und fehlt ab 1.17.0.
- `CHANGELOG.md`, Zeile 201, unter 1.17.0-pre.2: "Removed OpenXR support for Android x64 and Magic Leap 2."
- Die 1.16.1-Datei wurde extrahiert. `file`: ELF 64-bit x86-64. Sie ist gegen Androids NDK-libc++ gebaut (`std::__ndk1`) mit `NEEDED: libm.so, libdl.so, libc.so`.
- Gefundene Strings:
  - `XR_KHR_android_create_instance`, `XR_KHR_android_surface_swapchain`, `XR_KHR_android_thread_settings`.
  - Grafik: `XR_KHR_vulkan_enable`, `XR_KHR_vulkan_enable2`, `XR_KHR_opengl_es_enable`.
  - Vulkan-Aufrufe: `xrCreateVulkanInstanceKHR`, `xrCreateVulkanDeviceKHR` (enable2-Pfad) sowie die älteren `xrGetVulkan*`-Funktionen.
  - Die Meldung "Unsupported graphics API - falling back to NullGraphicsPlugin".
  - Fehlermeldungen, die erwarten, dass die Runtime Unitys `pfnGetInstanceProcAddr` für `vkCreateInstance` und `vkCreateDevice` benutzt und dass `VkDevice` und `VkPhysicalDevice` mit Unitys internen Handles übereinstimmen.
- Die Windows-DLL enthält `XR_KHR_D3D11_enable`, `XR_KHR_D3D12_enable` und die beiden Vulkan-Erweiterungen. **Keine der beiden Binärdateien enthält `XR_KHR_opengl_enable`** (Desktop-GL). OpenGL Core funktioniert mit Unitys OpenXR-Plugin daher nie.

### 2.3 Plattform-Sperren in C# und Metadaten

- **Der Play Mode wird unter Linux blockiert.** `Runtime/OpenXRProjectValidation.cs`, etwa Zeilen 199-206, enthält die Regel:
  - Meldung: "The only standalone targets supported are Windows x64 and OSX with OpenXR. Other architectures and operating systems are not supported at this time."
  - Prädikat: besteht nur, wenn das aktive Build-Target nicht zur Standalone-Gruppe gehört, oder `StandaloneWindows64` oder `StandaloneOSX` ist.
  - `error = true; errorEnteringPlaymode = true; buildTargetGroup = Standalone`.
  - Praktisch blockiert das Play Mode und Builds bei aktivem Target `StandaloneLinux64`. Forennutzer melden dieselbe Fehlermeldung bei Linux-Dedicated-Server-Builds (Juni 2024, Dezember 2025).
  - Stridemanns gepatchte Kopie setzt `checkPredicate = () => true`, `error = false`, `errorEnteringPlaymode = false`.
- `Runtime/OpenXRLoader.cs`, Zeilen 36-38: `[XRSupportedBuildTarget(BuildTargetGroup.Standalone, new[]{BuildTarget.StandaloneWindows64})]` und `[XRSupportedBuildTarget(BuildTargetGroup.Android)]`.
- `Runtime/OpenXRLoader.cs`, `LoadOpenXRSymbols()` (ca. Zeile 556): `#if UNITY_EDITOR_WIN` nutzt `..\\..\\..\\RuntimeLoaders\\windows\\x64\\openxr_loader`, `#elif UNITY_EDITOR_OSX` nutzt den Mock-Runtime-Loader. Unter Linux greift kein Zweig. Es bleibt `GetLoaderPath()` mit dem bloßen Namen `openxr_loader`; danach wird ein aktiver Loader-Pfad aus `EditorBuildSettings` abgefragt.
- `Editor/FeatureSupport/OpenXRChooseRuntimeLibraries.cs` (ca. Zeile 22): nur `#if UNITY_EDITOR_WIN` und `#elif UNITY_EDITOR_OSX`, kein Linux-Zweig.
- `Editor/OpenXRRuntimeSelector.cs`: Registry- und macOS-spezifische Erkennung, kein Linux-Zweig. Die Option "Other" existiert (Klasse `OtherRuntime`, Zeile ~104) und liest die Umgebungsvariable `OTHER_XR_RUNTIME_JSON`. Unter Linux würde man `XR_RUNTIME_JSON` selbst setzen oder `~/.config/openxr/1/active_runtime.json` nutzen.
- Eine Suche im ganzen Repo nach `UNITY_EDITOR_LINUX`, `UNITY_STANDALONE_LINUX`, `RuntimePlatform.LinuxEditor` und `BuildTarget.StandaloneLinux64` findet nur `Tests/Runtime/TestToolingTests.cs:238` (ein Test-Attribut, das `LinuxEditor` aufführt). Im Produktivcode gibt es nichts.
- `Documentation~/index.md` listet getestete Runtimes (Oculus PC + Link, Meta Quest, Android XR, "all other conformant runtimes (eg. SteamVR)" auf Windows 64-bit). **Es gibt keine Linux-Zeile.** `Documentation~/mock-environment.md` sagt, die Mock-Umgebung unterstütze "Standalone (Windows and MacOS) and Android".
- Die Dokumentation spricht mehrfach vom Tab "Mac, Windows, Linux settings (PC)". Das ist nur die Beschriftung von Unitys Standalone-Gruppe, der OpenXR-Text ist generisch.

### 2.4 XR Plug-in Management (com.unity.xr.management 4.7.0)

- Der Tab "Windows, Mac, Linux" in den Project Settings ist `BuildTargetGroup.Standalone`. OpenXR ist für diese Gruppe registriert (`com.unity.xr.openxr/Editor/OpenXRMangementSettings.cs`, Zeile ~44: `supportedBuildTargets = { Standalone, Android }`). Im Linux-Editor lässt sich OpenXR dort also **ankreuzen**.
- `com.unity.xr.management/Editor/XRLoaderInfoManager.cs`, `FilteredLoaderInfos` (Zeile ~179), filtert nur nach `BuildTargetGroup`, nicht nach einzelnen `BuildTarget`. Linux wird in der UI also nicht ausgeschlossen.
- Das Management-Paket enthält keine Linux-Logik; `grep -ri linux` findet nur eine Changelog-Zeile "Fix Linux tests in CI."
- Ergebnis: Das Häkchen ist sichtbar, aber dahinter gibt es unter Linux keinen funktionierenden Loader.

### 2.5 Fehlerbilder unter Linux (Unity-Foren und GitHub)

- Ohne Hack: `DllNotFoundException: UnityOpenXR` im Editor. Stridemann-Issue #2 bestätigt das und sagt, man müsse `libUnityOpenXR.so.meta` anpassen, damit Linux-Editor und Linux64 aktiv sind.
- Antwort eines Unity-Mitarbeiters (SamZ_XR, Mai 2023): "We're currently investigating". Eine spätere offizielle Aussage wurde nicht gefunden.

## 3. Community-Weg: Stridemann/Unity-XR-on-Linux-for-Meta-Quest

Quelle: https://github.com/Stridemann/Unity-XR-on-Linux-for-Meta-Quest (MIT, "as-is", Proof of Concept). Issues #1 bis #4 sind offen; Commit `cfc8ede`.

- **Idee:** Die Android-x86_64-Datei `Runtime/android/x64/libUnityOpenXR.so` des Pakets wird im Linux-Editor geladen. Laut README stürzt sie auf glibc ohne Korrekturen ab.
- **Getestet mit:** Arch Linux, Unity 6000.3.11f1 (Bazzite mit 6000.4.2f1 in Issue #2), openxr 1.16.1, Quest 3 über ALVR 20.x + **SteamVR**, Vulkan, Start mit `-force-vulkan`.
- **Bausteine:**
  - `NativeFix/compat_libc.c`, `compat_libm.c`, `compat_libdl.c` mit `version_*.map`: Shim-Bibliotheken, die bionics "LIBC"-versionierte Symbole exportieren und an glibc weiterreichen (vorn in `LD_LIBRARY_PATH`).
  - `NativeFix/fake_jvm.c`: `LD_PRELOAD`-Stub für `JavaVM`/`JNIEnv`, damit der JNI-Code nicht abstürzt.
  - `NativeFix/openxr_layer/strip_android_layer.c`: OpenXR-API-Layer, der `XR_KHR_android_create_instance` und `XrInstanceCreateInfoAndroidKHR` aus `xrCreateInstance` entfernt.
  - `NativeFix/vulkan_layer/force_timeline_sem.c`: implizierter Vulkan-Layer, der `VK_KHR_timeline_semaphore` erzwingt (SteamVR verlangt das, Unity aktiviert es nicht).
  - `launch_unity_vr.sh`: setzt die Umgebungsvariablen, baut die Shims beim ersten Lauf und startet `Unity -projectPath ... -force-vulkan`.
  - Das eingebettete Paket `Packages/com.unity.xr.openxr` (1.16.1) mit drei Änderungen (per Diff gegen das unveränderte Tag geprüft):
    1. `Runtime/OpenXRLoader.cs`: neuer Zweig `#elif UNITY_EDITOR_LINUX` mit `loaderPath = "/usr/lib/libopenxr_loader.so"`; der Loader-Override aus `EditorBuildSettings` wird mit `&& !UNITY_EDITOR_LINUX` übersprungen.
    2. `Runtime/OpenXRProjectValidation.cs`: Regel "nur Windows x64 und OSX" deaktiviert, dazu ein Null-Check.
    3. `.meta` von `Runtime/android/x64/libUnityOpenXR.so`: Editor an, Linux64 an.
- **Projekteinstellungen:** OpenXR für Standalone aktivieren, "Vulkan Additional Graphics Queue" an, das **Oculus Touch Controller Profile** verwenden (SteamVR lehnt Meta Touch Plus ab).
- **Warum `-force-vulkan`:** Laut Kommentar im Skript startet der Editor unter Linux standardmäßig mit OpenGL Core, aber der GL-Pfad der Android-Binärdatei braucht EGL.
- **Konsolenrauschen:** "Callback unregistration failed. Callback not registered." gilt als harmlos.
- **Grenzen:**
  - Funktioniert nur mit gepinntem 1.16.1 (oder älter bis 1.9.1).
  - Getestet mit SteamVR und ALVR, nicht mit Monado oder WiVRn.
  - Der Timeline-Semaphore-Layer und der Android-Extension-Stripper sind runtime-spezifische Workarounds. Auf Monado/WiVRn sind sie vielleicht unnötig oder nicht ausreichend.
  - Unity kann abstürzen; dann die erzeugten `.so`-Dateien löschen und neu bauen lassen.
  - Keine Aussage zu X11 oder Wayland.
- **WiVRn:** Issue #1 ("Try WiVRn && Arch Linux packaging considerations", 2026-03-23) bittet den Autor, WiVRn auszuprobieren. Es ist offen, ohne sichtbare Antwort und ohne Ergebnis. **Es gibt keinen Beleg, dass WiVRn je getestet wurde.**
- Der Unity-Forenthread "Linux support with OpenXR" (Seite 2) nennt dieses Repo als ersten funktionierenden Bericht (März 2026). Unity-Mitarbeiter haben nicht geantwortet. https://discussions.unity.com/t/linux-support-with-openxr/916945?page=2

## 4. Offizielle Position von Unity

- Keine Roadmap-Zusage für OpenXR unter Linux. Foren:
  - https://discussions.unity.com/t/openxr-linux-support/909788 (2023-02 bis 2026-08, 41 Antworten): "Right now Unity's OpenXR doesn't support Linux, any version of it."
  - https://discussions.unity.com/t/openxr-support-for-linux/1693991 (November 2025).
- GDC 2026 (laut DigitalProduction, 2026-03-17): Unity plant "official Steam support" und "targeted enhancements to the engine Linux runtime" für SteamOS-artige Ziele. Zu XR oder OpenXR steht dort nichts. https://digitalproduction.com/2026/03/17/official-steam-support-lands-on-unity-roadmap/
- Editor-Anforderungen (Unity 6000.0): Ubuntu 22.04 und 24.04, "Gnome desktop environment running on top of X11 or Wayland windowing system", OpenGL 3.2+ oder Vulkan, proprietärer Nvidia-Treiber oder AMD-Mesa. https://docs.unity3d.com/6000.0/Documentation/Manual/system-requirements.html

## 5. Grafik-API im Linux-Editor und Wayland

- Die Grafik-API des Editors folgt dem **Linux-Abschnitt der Graphics-API-Liste in den Player Settings**: "The graphics API at the top of the Graphics API list is the default API". Wird sie nicht unterstützt, probiert der Editor den nächsten Eintrag. https://docs.unity3d.com/6000.6/Documentation/Manual/configure-graphicsAPIs.html
- `-force-vulkan`: "Make the Editor use Vulkan for rendering. Normally the graphics API depends on Player Settings." `-force-glcore`: "Make the Editor use OpenGL 3/4 core profile for rendering." https://docs.unity3d.com/6000.0/Documentation/Manual/EditorCommandLineArguments.html
- Die Standardreihenfolge in einem frischen Unity-6-Linux-Projekt ließ sich aus der Dokumentation nicht bestätigen. Stridemann berichtet, der Editor sei standardmäßig bei OpenGL Core gelandet.
- **Wayland:**
  - Die Dokumentation sagt: "Wayland support for Desktop Linux is currently in experimental stage." Ubuntu 22.04: Wayland mit AMD; Ubuntu 24.04: AMD sowie Nvidia-Treiber 550 und neuer.
  - Das Flag `-force-wayland` ist nur für den **Linux-Player** dokumentiert ("Activate experimental Wayland support when running a Linux player"). https://docs.unity3d.com/6000.0/Documentation/Manual/PlayerCommandLineArguments.html
  - Ein Forennutzer betreibt den Editor unter XWayland (Bug IN-121338). Zur Frage, ob der Editor nativ unter Wayland laufen kann, gibt es keine Antwort. Annahme: XWayland. https://discussions.unity.com/t/curious-what-the-current-state-of-wayland-support-for-the-linux-unity-editor/1696530
  - OpenXR und Vulkan sind vom Fenstersystem weitgehend unabhängig, aber es ist **ungetestet**, wie sich Unitys Vulkan-Swapchain für die Spiel-/Mirror-Ansicht mit dem XR-Plugin unter XWayland gegenüber X11 verhält.
  - Die Strings für Wayland- und Xlib-Surfaces in der Android-Binärdatei stammen nur aus den Vulkan-Headern. Nichts darin zielt auf ein Fenstersystem.

## 6. Alternativen

1. **Valves OpenVR-XR-Plugin** (`ValveSoftware/unity-xr-plugin`, `com.valve.openvr` 1.2.4, 2025-10-29). Es liefert `Runtime/x64/libXRSDKOpenVR.so` und `libopenvr_api.so` für **Linux**, nutzt SteamVR/OpenVR und Vulkan. Die README nennt "Linux - Vulkan - Multipass will crash" und "Linux - OpenGL - Single Pass Instanced will crash." Es braucht SteamVR (läuft mit ALVR) und ist kein Monado/WiVRn-Weg. Die Kombination mit einem OpenVR-zu-OpenXR-Layer wie xrizer (https://github.com/Supreeeme/xrizer) auf WiVRn ist spekulativ; es wurde kein Bericht dazu gefunden. Ältere Versionen hatten Bibliothekspfad-Probleme (https://github.com/ValveSoftware/unity-xr-plugin/issues/3).
2. **Eigener XR-Provider über Unitys XR-SDK-Native-API:** siehe Abschnitt 9.
3. **Mock HMD / Simulatoren.** `com.unity.xr.mock-hmd` enthält keine Linux-Binärdatei und hilft unter Linux nicht.
4. **APK-Build** direkt auf die Quest funktioniert vom Linux-Editor aus (das Stridemann-README beschreibt das samt Korrektur für kaputte NDK-Symlinks), ist aber kein Editor-Streaming.

## 7. Meta-Pakete unter Linux

- `com.unity.xr.meta-openxr` 2.6.1 hängt von `com.unity.xr.openxr` 1.18.0 ab (ohne Android-x64-Binärdatei). Das asmdef `Runtime/Unity.XR.MetaOpenXR.asmdef` schließt nur Android, Editor, macOSStandalone und WindowsStandalone64 ein. Die nativen Plugins `Runtime/Plugins/*/libUnityARFoundationMeta` gibt es nur für Windows, macOS und Android. Die Doku sagt: "Meta Horizon Link is only supported on Windows." Für Linux bringt das Paket also nichts.
- Die Meta-XR-Core-SDK-Doku nennt nur Windows und macOS (https://developers.meta.com/horizon/documentation/unity/unity-development-requirements). Im Meta-Forum steht: "Linux isn't officially supported or tested by Meta"; Versionen über v74.0.3 lassen sich im Linux-Editor wegen der XR-Simulator-Abhängigkeit nicht kompilieren; Bauen und Testen auf dem Gerät ist möglich. https://communityforums.atmeta.com/discussions/Questions_Discussions/fix-linux-development-versions-above-v74-0-3-do-not-work-on-unity-editor-linux/1348536
- Für WiVRn/Monado werden die Meta-Pakete im PC-Editor nicht gebraucht. "Meta Quest Support" ist nur für APK-Builds relevant.

## 8. Was ein Test auf dem Rechner des Nutzers klären muss

1. `com.unity.xr.openxr` auf 1.16.1 pinnen und die Stridemann-Patches auf eine Kopie anwenden, Start mit `-force-vulkan`.
2. Prüfen, ob `xrCreateInstance` auf WiVRn gelingt, nachdem die Android-Extension entfernt wurde, und ob der Timeline-Semaphore-Layer dort nötig oder schädlich ist.
3. Das `XR_KHR_vulkan_enable2`-Handshake im Editor-Play-Mode prüfen. Die Binärdatei meldet einen Fehler, wenn die Runtime ein anderes `VkDevice` oder `VkPhysicalDevice` zurückgibt als Unity intern nutzt.
4. Kopf- und Controller-Pose sowie Interaction Profiles auf WiVRn prüfen.
5. X11 gegen XWayland vergleichen. Mit Abstürzen und Brüchen bei Unity-Updates ist zu rechnen.

## 9. Bausteine für unseren eigenen XR-Provider

Unitys Doku (https://docs.unity3d.com/2022.3/Documentation/Manual/xrsdk-provider-setup.html) sagt: "The Unity XR SDK is no longer available as we have focused our investments towards OpenXR." Die Native-Header sind aber weiterhin öffentlich, weil Valve sie in seinem Plugin mitliefert. Geprüft im Klon `repo_valve` (Commit a30a0100, 2025-10-29, Valves `com.valve.openvr` 1.2.4).

### 9.1 Öffentliche Header

Pfad: `CommonHeaders/ProviderInterface/` in https://github.com/ValveSoftware/unity-xr-plugin. Dateien: `IUnityInterface.h`, `IUnityGraphics.h`, `IUnityGraphicsD3D11.h`, `IUnityGraphicsD3D12.h`, `IUnityGraphicsMetal.h`, `IUnityGraphicsVulkan.h`, `IUnityEventQueue.h`, `IUnityProfilerCallbacks.h`, `IUnityRenderingExtensions.h`, `IUnityShaderCompilerAccess.h`, `IUnityXRAudio.h`, `IUnityXRDisplay.h`, `IUnityXRDisplayShadingRateExt.h`, `IUnityXRInput.h`, `IUnityXRMeshing.h`, `IUnityXRPreInit.h`, `IUnityXRStats.h`, `IUnityXRTrace.h`, `UnitySubsystemTypes.h`, `UnityTypes.h`, `UnityXRDisplayStats.h`, `UnityXRSubsystemTypes.h`, `UnityXRTypes.h`, `XRMath.h`, `XRMath.cpp`, `LICENSE.md`.

### 9.2 IUnityXRPreInit.h (Vorab-Initialisierung des Grafikgeräts)

- Struktur `UnityXRPreInitProvider` mit den Callbacks `GetPreInitFlags`, `GetGraphicsAdapterId`, `GetVulkanInstanceExtensions`, `GetVulkanDeviceExtensions`. Registriert wird sie über `IUnityXRPreInit::RegisterPreInitProvider`. Es gibt genau einen PreInit-Provider; ein neuer Aufruf überschreibt den alten. Interface-GUID: `0x4E5EB567159F4848ULL, 0x9969601F505A455EULL`.
- `GetGraphicsAdapterId(userData, renderer, rendererData, &adapterId)`: Unity fragt, welcher Grafik-Adapter zum XR-Gerät gehört. Für Vulkan ist `rendererData` die `VkInstance` und `adapterId` bekommt den Zeiger auf das `VkPhysicalDevice`. Die Header-Doku nennt "LUID/VkPhysicalDevice/id[MTLDevice]". In `Providers/OpenVRSystem.cpp` (ca. Zeilen 116-150) ruft Valve dafür `m_VRSystem->GetOutputDevice(&graphicsAdapterId, vr::TextureType_Vulkan, (VkInstance_T*)rendererData)` auf und schreibt `*adapterId = (uint64_t)(&graphicsAdapterId)`. Der Renderer-Enum `UnityXRPreInitRenderer` unterscheidet D3D11, D3D12, GLES20, GLES3x, Metal, GLCore und Vulkan.
- `GetVulkanInstanceExtensions` und `GetVulkanDeviceExtensions`: Zweiphasig. Zuerst mit `namesCapacityIn = 0` die nötige Größe erfragen, dann den Puffer füllen. Ausgabe ist ein einziger, durch Leerzeichen (0x20) getrennter String mit Erweiterungsnamen. Für OpenXR würde man hier `xrGetVulkanInstanceExtensionsKHR` und `xrGetVulkanDeviceExtensionsKHR` weiterreichen (oder bei `XR_KHR_vulkan_enable2` die Anforderungen der Runtime ermitteln).
- `GetPreInitFlags` liefert eine 64-Bit-Maske aus `UnityXRPreInitFlags`:
  - `kUnityXRPreInitFlagsEGLUsePBuffer` (1<<0)
  - `kUnityXRPreInitFlagsEGLUseNoErrorContext` (1<<1)
  - `kUnityXRPreInitFlagsDisableMainDisplayFramebuffer` (1<<2)
  - `kUnityXRPreInitFlagsRequestAdditionalVulkanGraphicsQueue` (1<<3), im Header kommentiert als "Plugin requests an additional Vulkan graphics queue for its own rendering. Plugin should check for the additional queue before using it."
  - `kUnityXRPreInitFlagsUseVulkanOffscreenSwapchain` (1<<4)
- Valve selbst setzt in `Providers/dllmain.cpp` (Zeilen 48-53) `*flags = 0`; das Queue-Flag nutzt Valve also nicht. In Unitys OpenXR-Paket entspricht ihm die Einstellung "Vulkan Additional Graphics Queue" (`OpenXRSettings.VulkanAdditionalGraphicsQueue`, siehe `CHANGELOG.md`).
- Registrierung bei Valve: Die exportierte Funktion `XRSDKPreInit(IUnityInterfaces*)` (`Providers/dllmain.cpp`, Zeilen 79-92) holt `IUnityXRPreInit`, füllt `UnityXRPreInitProvider` und ruft `RegisterPreInitProvider`. Auf der C#-Seite liefert `IXRLoaderPreInit.GetPreInitLibraryName(...)` den Namen der Bibliothek. Valve gibt im Editor `null` zurück (`com.valve.openvr/Runtime/OpenVRLoader.cs`, Zeile ~444). Unitys OpenXR gibt `"UnityOpenXR"` zurück (`Runtime/OpenXRLoader.cs`, Zeile ~48).

### 9.3 IUnityGraphicsVulkan.h

- `InterceptInitialization(UnityVulkanInitCallback func, void* userdata)`: Muss aufgerufen werden, bevor `kUnityGfxDeviceEventInitialize` eintritt (Plugin vorladen). Unity ruft dann `func` bei der Vulkan-Initialisierung auf. Der Callback bekommt Unitys `vkGetInstanceProcAddr` vom Vulkan-Loader und darf eine andere Implementierung zurückgeben. Damit lassen sich alle Vulkan-Aufrufe abfangen (wie bei einem Vulkan-Layer). Rückgabe `false`, wenn es zu spät aufgerufen wird.
- Weitere Funktionen im Header: `InterceptVulkanAPI` (jederzeit nutzbar, aber ohne Wrapping bestehender Objekte), `ConfigureEvent`, `AccessTexture`, `Instance()` (liefert `instance`, `physicalDevice`, `device`, `getInstanceProcAddr` und die Queue). Der Header enthält außerdem eine Swapchain-Konfiguration (`kUnityVulkanSwapchainMode_Offscreen`).
- Valve benutzt `InterceptInitialization` in `Providers/` nicht (kein Treffer im Quelltext), sondern nur `Instance()` und `AccessTexture` (`Providers/Display/Display.cpp`, Zeilen ~725-742). Der Hook ist trotzdem die Stelle, um für `XR_KHR_vulkan_enable2` Unitys `vkCreateInstance`/`vkCreateDevice` durch `xrCreateVulkanInstanceKHR`/`xrCreateVulkanDeviceKHR` zu ersetzen. Unitys Android-Binärdatei prüft genau dieses Verhalten (siehe 2.2).

### 9.4 IUnityXRDisplay.h und IUnityXRInput.h

- `IUnityXRDisplay::RegisterLifecycleProvider(pluginName, id, UnityLifecycleProvider*)` (Zeile 613) registriert das Subsystem. `RegisterProvider(handle, UnityXRDisplayProvider*)` (Zeile 622) und `RegisterProviderForGraphicsThread` (Zeile 631) hängen die Callbacks an.
- `UnityXRDisplayProvider`-Callbacks (Auswahl): `Start`, `Stop`, `PopulateNextFrameDesc` (Zeile 509), `SubmitCurrentFrame` (498), `UpdateDisplayState` (562), `BlitToMirrorViewRenderTarget` (522), `QueryMirrorViewBlitDesc` (575). Für die Grafik: `CreateTexture`, `QueryTextureDesc`, `DestroyTexture`, `GetPlatformData`; dazu Occlusion-Mesh-Funktionen.
- `IUnityXRInput::RegisterLifecycleProvider(...)` (Zeile 657). `UnityXRInputProvider`-Callbacks: `Tick`, `FillDeviceDefinition`, `UpdateDeviceConfig`, `UpdateDeviceState`, `HandleEvent`, `HandleRecenter`, Haptik (`HandleHapticImpulse`, `HandleHapticBuffer`, `QueryHapticCapabilities`, `HandleHapticStop`), Tracking-Origin (`QueryTrackingOriginMode`, `QuerySupportedTrackingOriginModes`, `HandleSetTrackingOriginMode`) und `TryGetDeviceStateAtTime`.
- Der Einstieg ist `UnityPluginLoad(IUnityInterfaces*)` (Valve: `Providers/dllmain.cpp`, Zeile 31). Dort werden die Interfaces geholt und `RegisterDisplayLifecycleProvider` sowie `RegisterInputLifecycleProvider` aufgerufen.

### 9.5 Vorlagen: Valves Display.cpp und Input.cpp

- `Providers/Display/Display.cpp`: 1551 Zeilen. Enthält Lifecycle-Handler und Display-Provider, Erzeugung der Eye-Texturen, den Vulkan-Pfad (`AccessTexture`, `m_vrVulkanTexture` mit Instance, Device, PhysicalDevice), D3D11/D3D12/OpenGL/Metal-Pfade und `RegisterDisplayLifecycleProvider` (Zeile 1526; Aufruf `RegisterLifecycleProvider("XRSDKOpenVR", "OpenVR Display", ...)` bei Zeile 1543).
- `Providers/Input/Input.cpp`: 1077 Zeilen. Geräte-Definitionen, Tick und Zustandsupdate, `RegisterInputLifecycleProvider` (Zeile 1059; Aufruf `RegisterLifecycleProvider("XRSDKOpenVR", "OpenVR Input", ...)` bei Zeile 1074).
- Weitere Dateien: `Providers/OpenVRSystem.cpp` (209 Zeilen, Verbindung zur Runtime und PreInit-Callbacks), `Providers/UserProjectSettings.cpp` (589 Zeilen), `Providers/dllmain.cpp` (95 Zeilen), `CommonHeaders/UnityInterfaces.cpp`.
- Build: `CMakeLists.txt` hat einen Linux-Zweig (`-DLINUX -DPOSIX`, `ARCH_TARGET linux64`, rpath `$ORIGIN`). Das fertige Linux-Binary liegt unter `com.valve.openvr/Runtime/x64/libXRSDKOpenVR.so`.
- Für einen OpenXR-Provider müssten die OpenVR-Aufrufe durch OpenXR ersetzt werden: Instanz und Session, Vulkan-Binding, Swapchains (die Unity-Texturen sind Swapchain-Images), Frame-Schleife (`xrWaitFrame`, `xrBeginFrame`, `xrEndFrame`), Pose-Abfrage und die Abbildung von Actions auf Unitys Geräte-Layouts.

### 9.6 Lizenz

Die Header stehen unter der **Unity Companion License v1.2** (`CommonHeaders/ProviderInterface/LICENSE.md`, Copyright 2019 Unity Technologies ApS). Die Nutzung ist auf die Erstellung, Nutzung und Verbreitung von Inhalten beschränkt, die eine gültige Unity-Engine-Lizenz voraussetzen. Wettbewerbsanalyse oder die Entwicklung eines konkurrierenden Produkts sind ausgeschlossen. Die Software wird "AS IS" geliefert. Für ein Unity-VR-Spiel als Anwendungsfall ist das nutzbar; eine Weitergabe der Header außerhalb eines Unity-bezogenen Projekts wäre es nicht. Der Rest von Valves Repo (`ThirdParty/vulkan` usw.) hat eigene Lizenzen und müsste getrennt geprüft werden.

### 9.7 XR-Management-Integration

- **`UnitySubsystemsManifest.json`** liegt im Paket neben dem Runtime-Code (Valve: `com.valve.openvr/Runtime/UnitySubsystemsManifest.json`). Inhalt: `"name": "XRSDKOpenVR"`, `"version": "1.0.0-preview.1"`, `"libraryName": "XRSDKOpenVR"`, ein `displays`-Eintrag (`"id": "OpenVR Display"`) und ein `inputs`-Eintrag (`"id": "OpenVR Input"`, `"disablesLegacyInput": true`, `"disablesLegacyVr": true`). Zum Vergleich hat Unitys OpenXR `libraryName: "UnityOpenXR"` mit den IDs "OpenXR Display" und "OpenXR Input" (`Runtime/UnitySubsystemsManifest.json`). Die IDs im Manifest müssen mit den IDs übereinstimmen, die der native Code in `RegisterLifecycleProvider` angibt. Der `libraryName` bestimmt, welche native Bibliothek Unity lädt, also `libXRSDKOpenVR.so` bzw. `libUnityOpenXR.so` unter Linux.
- **Loader-Klasse:** `OpenVRLoader : XRLoaderHelper` (Valve, `com.valve.openvr/Runtime/OpenVRLoader.cs`, Zeile 124) mit `IXRLoaderPreInit` nur im Editor. `XRLoaderHelper` (aus `com.unity.xr.management`) liefert `CreateSubsystem<TDescriptor, TSubsystem>` und `StartSubsystem`/`StopSubsystem`/`DestroySubsystem` für die Display- und Input-Subsysteme. Die Loader-Klasse trägt `[XRSupportedBuildTarget(...)]` (siehe 2.3) und wird über `IXRPackage` samt Metadaten bei XR Plug-in Management registriert (Beispiel: `Editor/OpenXRMangementSettings.cs` im OpenXR-Paket, Klassen `MyLoaderMetadata` und `MyPackageMetadata`). Hinweis: Der Filter in `XRLoaderInfoManager.FilteredLoaderInfos` vergleicht nur die `BuildTargetGroup`. Ein Loader mit `[XRSupportedBuildTarget(BuildTargetGroup.Standalone)]` erscheint also auch im Linux-Editor.
- **Native Plugin-Metadaten:** Die `.so` braucht eine `.meta` mit Editor und Linux64 aktiviert (CPU x86_64). Genau das hat Valve für `libXRSDKOpenVR.so` getan und das OpenXR-Paket für Linux nie.
- **Wirkung im Editor:** Steht der Loader unter "Windows, Mac, Linux" in XR Plug-in Management, startet `XRGeneralSettings` ihn beim Play Mode. Unity lädt die Bibliothek aus dem Manifest, ruft `UnityPluginLoad` auf und startet die Subsysteme.

### 9.8 Einschätzung

Ein eigener Provider ist der einzige vollständig legitime Weg. Er ist aber ein großes Projekt: OpenXR-Sitzung, Vulkan-Interop über `IUnityGraphicsVulkan` und `IUnityXRPreInit`, Swapchain-Verwaltung, Input-Mapping. Valves Repo ist eine gute, aktuell gepflegte Linux-fähige Vorlage für die Unity-Seite. Es wurde kein fertiger Community-Provider für OpenXR unter Linux gefunden. Nicht geprüft wurde, ob die Unity-Header in Version 6.x gegenüber dem Stand von Valves Repo (Oktober 2025) neue Pflichtfelder haben.

## 10. Quellen

- needle-mirror: https://github.com/needle-mirror/com.unity.xr.openxr , https://github.com/needle-mirror/com.unity.xr.management , https://github.com/needle-mirror/com.unity.xr.meta-openxr , https://github.com/needle-mirror/com.unity.xr.mock-hmd
- https://github.com/Stridemann/Unity-XR-on-Linux-for-Meta-Quest (und Issues #1, #2)
- https://github.com/ValveSoftware/unity-xr-plugin (und Issue #3)
- https://discussions.unity.com/t/linux-support-with-openxr/916945
- https://discussions.unity.com/t/openxr-linux-support/909788
- https://discussions.unity.com/t/openxr-support-for-linux/1693991
- https://discussions.unity.com/t/curious-what-the-current-state-of-wayland-support-for-the-linux-unity-editor/1696530
- https://docs.unity3d.com/6000.0/Documentation/Manual/system-requirements.html
- https://docs.unity3d.com/6000.0/Documentation/Manual/EditorCommandLineArguments.html
- https://docs.unity3d.com/6000.0/Documentation/Manual/PlayerCommandLineArguments.html
- https://docs.unity3d.com/6000.6/Documentation/Manual/configure-graphicsAPIs.html
- https://docs.unity3d.com/2022.3/Documentation/Manual/xrsdk-provider-setup.html
- https://developers.meta.com/horizon/documentation/unity/unity-development-requirements
- https://communityforums.atmeta.com/discussions/Questions_Discussions/fix-linux-development-versions-above-v74-0-3-do-not-work-on-unity-editor-linux/1348536
- https://digitalproduction.com/2026/03/17/official-steam-support-lands-on-unity-roadmap/
- https://vronlinux.org/docs/fossvr/wivrn/ (WiVRn und xrizer; nichts Unity-Spezifisches)
