# Architecture

This document describes how LinuxXR is put together. For installation see [INSTALL.md](INSTALL.md); for the release process see [CONTRIBUTING.md](../CONTRIBUTING.md); for the rationale and history see the
(German, partly outdated) [development notes](dev/README.md).

## 1. Goal and approach

Unity's `com.unity.xr.openxr` ships no Linux binary and blocks Play Mode on Linux. LinuxXR is therefore a **native Unity XR provider**
(a plug-in implementing Unity's XR SDK *Display* and *Input* subsystem interfaces) that talks OpenXR to an external runtime. The runtime used in practice is
[WiVRn](https://github.com/WiVRn/WiVRn) (Monado-based), which encodes the frames and streams them to a Meta Quest. Because Unity renders inside the
Editor process, Console, breakpoints, Profiler, Inspector and Game View keep working.

```
Unity Editor process
┌──────────────────────────────────────────────────────────────────────────┐
│ C# package com.tyroonis.xr.linuxopenxr                                    │
│   LinuxXRLoader (XRLoader) ── creates subsystems ──┐                      │
│   LinuxXRSettings ── settings.txt + P/Invoke ──────┤                      │
│   Editor: setup wizard, bootstrap, host helper, status window,            │
│           console forwarder, issue report                                 │
│                                                    ▼                      │
│ libLinuxXR.so  (src/unity = Unity glue, src/core = linuxxr_core)          │
│   Display subsystem  "LinuxXR Display"   Input subsystem "LinuxXR Input"  │
│   shared OpenXR session (session.cpp) ── XrContext ── dlopen OpenXR loader │
└──────────────────────────────────────────────────┬───────────────────────┘
                                                   │ OpenXR (XR_RUNTIME_JSON)
                                       WiVRn / Monado runtime ── stream ── Quest
```

## 2. Components

| Component | Where | Role |
|-----------|-------|------|
| `libLinuxXR.so` | `native/` (built by CMake) | The Unity plug-in: registers the display and input providers, owns the OpenXR session. |
| `linuxxr_core` | `native/src/core/` | Static library without Unity dependency: logging, OpenXR/Vulkan function tables, runtime selection, Vulkan requirements, OpenXR context, controller input, math. Shared with `linuxxr-probe` and the tests. |
| Unity glue | `native/src/unity/` | Plug-in entry points, display and input providers, shared session, C API for C#, log sink. |
| `linuxxr-probe` | `native/tools/probe/` | CLI that runs the same sequence as the Editor without Unity (`runtimes`, `info`, `preinit`, `run`). |
| UPM package | `unity/Packages/com.tyroonis.xr.linuxopenxr/` | The complete, self-contained package (see below): C# `Runtime/` and `Editor/` plus the native payload under `Runtime/Native~/`. |
| Native payload | `unity/Packages/com.tyroonis.xr.linuxopenxr/Runtime/Native~/` | `libLinuxXR.so`, `libopenxr_loader.so.1`, `LICENSE-openxr-loader.txt`. `libLinuxXR.so` is copied here by the CMake build / `build-native.sh` or CI; the loader is the bundled Khronos OpenXR-SDK 1.1.47. Folders ending in `~` are ignored by Unity's importer; the Editor code installs the files into `Assets/LinuxXR` (section 8.1). |
| Scripts | `scripts/` | Developer scripts: `fetch-unity-headers.sh`, `build-native.sh`, `build-openxr-loader.sh`, `pack-unity-package.sh`, `install-unity-package.sh` (`--dev`, `--tarball`, `--remove`), `ensure-metas.py`. Optional host helpers: `check-system.sh`, `setup-host.sh`, `setup-unity-flatpak.sh`, `quest-usb.sh`, `test-openxr.sh`. None is needed by end users. |
| CI | `.github/workflows/` | `ci.yml` (build + ctest) and `release.yml` (tests, portable build, optional tarball signing, GitHub Release, `upm` branch). `docs/github/` holds a copy of the templates and workflows. |

### Package layout

```
com.tyroonis.xr.linuxopenxr/
├── package.json                     name, version 0.3.3, depends on com.unity.xr.management 4.5.0
├── README.md                        shown in the Package Manager (CHANGELOG.md, LICENSE.md and THIRD_PARTY_NOTICES.md are added by pack-unity-package.sh)
├── Runtime/                         LinuxXRLoader, LinuxXRSettings, LinuxXRNative (P/Invoke)
│   └── Native~/                     libLinuxXR.so, libopenxr_loader.so.1, LICENSE-openxr-loader.txt (ignored by Unity's importer)
└── Editor/                          subsystem registration (installs Assets/LinuxXR), setup wizard, host helper, status window, console forwarder, issue report
    └── Bootstrap/                   separate assembly that can offer to install XR Plug-in Management
```

The package contains no `UnitySubsystemsManifest.json`; the Editor code writes it into `Assets/LinuxXR` (section 8.1).
Every file has a `.meta` file (`scripts/ensure-metas.py` creates missing ones), because packages from the Package Manager cache are immutable and Unity cannot create them there.

## 3. Frame flow

Per frame, on Unity's graphics thread (`native/src/unity/display_provider.cpp`):

1. **`PopulateNextFrameDesc`**: `pollEvents` -> `xrWaitFrame` -> `xrBeginFrame` -> `xrLocateViews` (view and app space). The eye poses (relative to the head) and
   field of view are handed to Unity, which then renders one pass (single-pass instanced texture array) or two (multi-pass) into **its own textures**.
2. **`SubmitCurrentFrame`**: the plug-in transitions the Unity textures with `AccessTexture`, then calls `IUnityGraphicsVulkan::AccessQueue(flush = true)`.
   Inside that callback it acquires and waits for the OpenXR swapchain image, records its own command buffer that **copies** the Unity eye textures into the
   swapchain image, releases the image and calls `xrEndFrame` with a **projection layer**.
3. WiVRn encodes the swapchain image (VAAPI / Vulkan Video on AMD and Intel, NVENC on NVIDIA) and streams it to the Quest.

Why copy instead of rendering into the swapchain? Wrapping foreign `VkImage`s as Unity render targets is undocumented for Vulkan, while the copy is robust and cheap on the GPU
(documented in the source as under 0.5 ms). Why `AccessQueue`? OpenXR runtimes submit work on the supplied queue during acquire/release, and nobody except Unity may touch
Unity's queue outside `AccessQueue`; `flush = true` guarantees Unity's render commands for the frame were submitted first. If the callback never arrives the plug-in waits at most 250 ms
and logs `Previous frame did not complete (AccessQueue callback missing?) - try LINUXXR_DIRECT_SUBMIT=1`.

No depth layer is submitted, and there is no occlusion mesh, hand tracking or passthrough yet.

Coordinate conversion (`core/xr_math.h`): OpenXR is right-handed (+Y up, -Z forward); Unity is left-handed (+Y up, +Z forward). Poses are converted by mirroring on the Z plane.
`LINUXXR_FLIP_Y=1` flips the image vertically if a setup needs it.

## 4. Input

`native/src/unity/input_provider.cpp` defines three devices with Unity `CommonUsages` feature names:

- `LinuxXR HMD` (HeadMounted | TrackedDevice): device pose and velocity, center/left/right eye pose, `IsTracked`, `TrackingState`, `UserPresence`.
- `LinuxXR Controller - Left` / `- Right` (HeldInHand | TrackedDevice | Controller | Left/Right): device pose and velocity, pointer (aim) pose, Trigger, Grip,
  TriggerButton, GripButton, PrimaryButton / PrimaryTouch, SecondaryButton / SecondaryTouch, MenuButton, TriggerTouch, ThumbrestTouch, haptics.

`core/xr_input.cpp` creates one shared set of OpenXR actions with left/right subaction paths and suggests bindings for
`/interaction_profiles/oculus/touch_controller` (Quest 2 and fallback for all Touch), `/interaction_profiles/meta/touch_controller_plus` (Quest 3, if the extension is enabled)
and `/interaction_profiles/khr/simple_controller` (minimal fallback).

## 5. Threading

- **Unity main thread**: C# loader and settings, subsystem creation, C API calls (`LinuxXR_*`), status window polling.
- **Unity graphics thread**: the display provider's frame functions (`PopulateNextFrameDesc`, `SubmitCurrentFrame`, `AccessQueue` callback). A mutex and condition
  variable (`endMutex`) serialize the end-of-frame hand-off between the submit function and the queue callback.
- **`XrContext`** (`core/xr_context.h`): instance/session creation and destruction on one thread; `pollEvents`, `waitFrame`, `beginFrame`, `endFrame` are serialized by an internal
  mutex; swapchain acquire/release is the caller's responsibility (one thread at a time). `xrWaitFrame` is deliberately called **without** the mutex because it blocks until the next frame time.
- **Runtime loss**: on `XR_ERROR_INSTANCE_LOST`, `XR_ERROR_SESSION_LOST` or `XR_ERROR_RUNTIME_FAILURE` (and on `LOSS_PENDING` events) the context logs the loss once and never enters `xrWaitFrame`
  again; a second call would block forever on Monado/WiVRn and freeze the Editor. This is covered by the `linuxxr_monado_instance_loss` test.
- **Reachability check**: `isRuntimeReachable` connects to the runtime's Unix socket (`$XDG_RUNTIME_DIR/wivrn/comp_ipc` or `monado_comp_ipc`) with a **non-blocking** `connect()` and a 50 ms poll,
  cached for 2 s. A blocking connect would hang once the WiVRn backlog is full.
- **Headset probe**: WiVRn blocks OpenXR calls (`xrCreateInstance`, `xrGetSystem`) while no headset is connected. Before creating the real session, `XrContext` therefore creates a throw-away
  OpenXR instance on a **worker thread** and waits at most 3 s (`probeHeadset`). If it does not answer, LinuxXR logs a yellow warning (`No headset connected ... Play continues without VR`),
  does not create a session and Play Mode continues without VR; the Editor thread never blocks. A probe that is still waiting inside the runtime keeps running detached
  (it owns its instance and destroys it when it returns); while it runs, further start attempts skip OpenXR and warn instead.
- **Logging**: native log lines are written to `linuxxr.log` and queued; `LinuxXRConsoleForwarder` (Editor) drains the queue on the main thread and writes `[LinuxXR] ...` to the Console.

## 6. Session lifecycle

`native/src/unity/session.cpp` owns the **shared OpenXR session**: it is created when the first subsystem starts (Play) and destroyed when the last one stops. On creation it checks
that Unity runs with Vulkan and that Unity's `VkDevice` has all extensions the runtime needs (logged as `Vulkan: Unity's device has all required extensions` or a warning listing the missing ones).
The Editor's XR PreInit stage does not run (only player builds use `IUnityXRPreInit`), so the extension set is whatever Unity's Editor device already enables; the check at Play time reports a mismatch.
The PreInit provider (`native/src/unity/preinit.cpp`, `core/vk_preinit.*`, `core/vk_requirements.*`) still exists and is exercised by `linuxxr-probe preinit` and the tests.

## 7. OpenXR runtime selection

`core/runtime_select.cpp` builds an ordered candidate list; the first one wins and is passed to the loader through `XR_RUNTIME_JSON`.

1. Preference: the **Runtime** setting (`wivrn`, `monado`, `active`, a path to a `.json`). If it is `auto` or empty, `LINUXXR_RUNTIME` is used when set.
2. A preference of `wivrn` / `monado` / `active` puts those candidates first; an explicit `.json` path is added first.
3. An already set `XR_RUNTIME_JSON` is respected.
4. Fallback order for `auto` and as remainder: **WiVRn** (`/usr/share/openxr/1/openxr_wivrn*.json`, `/usr/local/share/...`, user Flatpak, system Flatpak
   `.../io.github.wivrn.wivrn/current/active/files/share/openxr/1/openxr_wivrn.json`), then **active runtime** (`$XDG_CONFIG_HOME/openxr/1/active_runtime.json`,
   `~/.config/openxr/1/active_runtime.json`, `/etc/xdg/openxr/1/active_runtime.json`), then **Monado** (`/usr/share/openxr/1/openxr_monado.json`, `/usr/local/...`).

Reachability: WiVRn is "reachable" when `$XDG_RUNTIME_DIR/wivrn/comp_ipc` accepts connections (a headset is connected); Monado when `$XDG_RUNTIME_DIR/monado_comp_ipc` does.

**OpenXR loader**: loaded with `dlopen` (so `libLinuxXR.so` itself loads without a system loader). Search order: `LINUXXR_OPENXR_LOADER`, `libopenxr_loader.so.1`, `libopenxr_loader.so`,
then the copy next to the plug-in (`Assets/LinuxXR/Plugins/Linux/x86_64/`). The bundled copy (Khronos OpenXR-SDK 1.1.47, Apache-2.0, built on Ubuntu 22.04 so it needs only glibc 2.35+) exists because the Unity Hub Flatpak runtime has none.

## 8. Distribution and CI

### 8.1 Installing the native part into Assets/LinuxXR

Unity registers native XR subsystems only at Editor start, and only from a `UnitySubsystemsManifest.json` in a **direct sub-folder of `Assets/`** (deeper folders are not scanned) or in a
registry package. Local tarball, git and embedded packages are not scanned. So that every install method behaves the same, the package carries no manifest;
`Editor/LinuxXRSubsystemRegistration.cs` (`[InitializeOnLoad]`, Linux Editor only) installs the native part on first load:

```
Assets/LinuxXR/UnitySubsystemsManifest.json
Assets/LinuxXR/Plugins/Linux/x86_64/{libLinuxXR.so, libopenxr_loader.so.1, LICENSE-openxr-loader.txt}
```

The manifest is generated from the version constant in that file, and the `.meta` files use fixed GUIDs, so a re-install and the `.unitypackage` produce identical assets. The binaries stay in
`Runtime/Native~/` in the package (folders ending in `~` are ignored by Unity's importer, so there is no duplicate plug-in). The install is repeated whenever the files in `Assets/LinuxXR` are missing or differ from the package,
and one Editor restart is needed after the first install and after every update that changes the binaries; the setup wizard shows **Native plugin** with **Install** and then **Restart Editor**.
`Assets/LinuxXR` can be committed to version control or regenerated. To uninstall, remove the package **and** delete `Assets/LinuxXR`.

| Channel | Produced by | Lands in the project as |
|---------|-------------|-------------------------|
| Git URL `https://github.com/tyroonis/UnityVR-Linux.git#upm` (or `#upm/vX.Y.Z`) | `release.yml` publishes `dist/upm` (the package root with binaries) to the `upm` branch and the tag `upm/vX.Y.Z` | `Library/PackageCache`, plus `Assets/LinuxXR` |
| Tarball `com.tyroonis.xr.linuxopenxr-X.Y.Z.tgz` (GitHub Release) | `scripts/pack-unity-package.sh` | `Library/PackageCache`, plus `Assets/LinuxXR` |
| `LinuxXR-X.Y.Z.unitypackage` (GitHub Release / Asset Store) | `scripts/pack-unity-package.sh` (`make-unitypackage.py`) | C# in `Assets/Tyroonis/LinuxXR`, native files already in `Assets/LinuxXR` |
| Developer symlink | `scripts/install-unity-package.sh <project> --dev` | `Assets/LinuxXRDev` -> repository package folder, plus `Assets/LinuxXR` |

### 8.2 CI and release

- `.github/workflows/ci.yml`: on pushes to `main` and on pull requests, builds on ubuntu-24.04 and runs `ctest` (Monado is installed when available).
- `.github/workflows/release.yml`: on a tag `vX.Y.Z` (the tag must equal `package.json` `version`, and `CHANGELOG.md` needs a `## [X.Y.Z]` section, which becomes the release notes).
  It first runs the CI tests (`ci.yml` through `workflow_call`). The `build` job runs in an **`ubuntu:22.04` container** with g++-12,
  builds the OpenXR loader (`build-openxr-loader.sh`, OpenXR-SDK 1.1.47), fetches the Unity headers, builds `libLinuxXR.so` with `LINUXXR_STATIC_RUNTIME=ON`
  (static libstdc++, checked with `objdump` that `libstdc++` is not a `NEEDED` entry), then runs `pack-unity-package.sh` (`.tgz`, `.unitypackage`, `dist/upm`). The result runs on glibc 2.35 and newer, which includes
  the runtime inside the Unity Hub Flatpak. The `publish` job creates the GitHub Release with the `.tgz` and `.unitypackage`, and force-updates the `upm` branch and the `upm/vX.Y.Z` tag.
- **Tarball signing** (optional): when the repository secrets `UPM_SERVICE_ACCOUNT_KEY_ID`, `UPM_SERVICE_ACCOUNT_KEY_SECRET` and `UPM_ORG_ID` are set, the `build` job repacks `dist/upm` with Unity's `upm` CLI
  (`upm pack --organization-id ...`), checks that the tarball contains `package/.attestation.p7m` and replaces the unsigned `.tgz` with it. Without the secrets the tarball stays unsigned and Unity 6.3+ shows
  a harmless "package is not signed" warning. Setup: [CONTRIBUTING](../CONTRIBUTING.md#signing-the-tarball).
- `docs/github/` keeps a copy of the issue templates and workflows (for hosts where `.github/` is not used as is).

### 8.3 XR Plug-in Management metadata (packageId-aware)

`Editor/LinuxXRPackageMetadata.cs` registers the loader for XR Plug-in Management. XR Management checks the `packageId` of the metadata against the installed packages
before it assigns a loader, so the metadata is **packageId-aware**: it uses `com.tyroonis.xr.linuxopenxr` as `packageId` when the code runs from the installed package
(`InstalledAsPackage`) and falls back to `com.unity.xr.management` for the `Assets/` install (`.unitypackage`, dev symlink). The loader is listed **only in the Linux Editor**; on
other platforms and build targets the metadata lists nothing, so LinuxXR never appears for Android or Windows builds.

### 8.4 Wizard, bootstrap and host helper

- **Bootstrap** (`Editor/Bootstrap`, assembly `Tyroonis.XR.LinuxXR.Bootstrap`): a separate Editor assembly without any dependency. The main Editor assembly has a define constraint
  (`LINUXXR_XR_MANAGEMENT`, set when `com.unity.xr.management` 4.0.0+ is present), so when LinuxXR is imported into `Assets/` (`.unitypackage`) without XR Plug-in Management, the rest of LinuxXR is not compiled.
  The bootstrap then offers to install `com.unity.xr.management` (also via menu *Window -> XR -> LinuxXR Install XR Plug-in Management*). As a UPM package LinuxXR declares the dependency itself.
- **Setup wizard** (`LinuxXRSetupWizard`, *Window -> XR -> LinuxXR Setup*): evaluates a list of steps (native plug-in installed in `Assets/LinuxXR` and subsystems found, Vulkan, loader enabled for Standalone, other loaders removed,
  Initialize XR on Startup, Flatpak access, WiVRn installed / running, Linear color space, Render Scale 0.7). Each step has a status, a detail text and a fix action. It opens automatically once per
  session while a required step fails; the auto-open can be disabled per project.
- **Host helper** (`LinuxXRHost`): everything that touches the host. It detects the Unity Hub Flatpak (`com.unity.UnityHub`), runs `flatpak override --user --filesystem=xdg-run/wivrn` plus read-only
  overrides for the WiVRn app directories, installs WiVRn with `flatpak install --user` from Flathub, and starts it. The scripts `setup-unity-flatpak.sh` and `setup-host.sh` remain as optional command-line equivalents.
- **Runtime selection inside the sandbox** also tries the real home paths (`~/.local/share/flatpak/app/...`) because `XDG_DATA_HOME` points into `~/.var/app/...` there.

### 8.5 Settings flow

`LinuxXRSettings` (Project Settings -> XR Plug-in Management -> LinuxXR) is written to `Library/LinuxXR/settings.txt` (read by the native plug-in when it loads, before C# runs)
and also pushed through `LinuxXR_SetSettings` on XR start. `LinuxXRLoader.Initialize` checks the Linux Editor, the native library, Vulkan and the subsystem descriptors before creating
the display and input subsystems and then logs `[LinuxXR] Initialized (version <ver>)`. Settings and status in the native plug-in are protected against concurrent access.

## 9. File map

### `native/src/core/` (no Unity dependency)

| File | Purpose |
|------|---------|
| `log.h/.cpp` | Logging with replaceable sink (stderr, Unity trace, file); `LINUXXR_LOG=debug` |
| `xr_api.h/.cpp` | OpenXR function table; loads the loader with `dlopen` |
| `vk_api.h/.cpp` | Minimal Vulkan function table via `dlopen("libvulkan.so.1")`; works with Unity's `VkInstance`/`VkDevice` |
| `runtime_select.h/.cpp` | Runtime candidates, selection, reachability, Flatpak hints |
| `vk_requirements.h/.cpp` | Vulkan extensions the runtime requires (runtime query, cache, base list) |
| `vk_preinit.h/.cpp` | Logic behind `IUnityXRPreInit` (instance/device extensions, GPU choice; `LINUXXR_GPU`) |
| `xr_context.h/.cpp` | OpenXR instance, system, session, spaces, swapchains, frame loop, loss handling |
| `xr_input.h/.cpp` | Controller actions and interaction-profile bindings |
| `xr_math.h` | Pose composition and OpenXR -> Unity conversion |

### `native/src/unity/` (Unity glue)

| File | Purpose |
|------|---------|
| `plugin_main.cpp` | Entry points Unity looks for in `libLinuxXR.so` |
| `preinit.cpp` | `IUnityXRPreInit` provider (extensions and GPU before Vulkan initialization; player builds) |
| `display_provider.cpp` | Display subsystem "LinuxXR Display": frame loop, texture copy, statistics |
| `input_provider.cpp` | Input subsystem "LinuxXR Input": HMD and controllers |
| `session.cpp` | Shared OpenXR session for display and input; Vulkan extension check |
| `native_api.cpp` | C API for P/Invoke (`LinuxXR_GetVersion`, `LinuxXR_GetStats`, settings, diagnostics) |
| `unity_log.cpp` | Log sink: file under `$XDG_CACHE_HOME/linuxxr/linuxxr.log` (default `~/.cache/linuxxr/`), console queue |
| `provider_context.h` | Process-wide plug-in state |

### C# (`unity/Packages/com.tyroonis.xr.linuxopenxr/`)

| File | Purpose |
|------|---------|
| `Runtime/LinuxXRLoader.cs` | `XRLoader`: preconditions, creates display and input subsystems |
| `Runtime/LinuxXRSettings.cs` | Settings asset; writes `Library/LinuxXR/settings.txt`; applies settings to the native plug-in |
| `Runtime/LinuxXRNative.cs` | P/Invoke bridge to `libLinuxXR.so` |
| `Editor/LinuxXRSubsystemRegistration.cs` | Installs the native part into `Assets/LinuxXR` on first load (section 8.1) |
| `Editor/LinuxXRPackageMetadata.cs` | Registers LinuxXR in XR Plug-in Management ("Windows, Mac, Linux"); packageId-aware, see section 8.3 |
| `Editor/LinuxXRSetupWizard.cs` | Window -> XR -> LinuxXR Setup: checklist with one-click fixes, auto-open once per session |
| `Editor/LinuxXRHost.cs` | Host helper: Flatpak detection and overrides, WiVRn install/start (see section 8.4) |
| `Editor/Bootstrap/LinuxXRBootstrap.cs` | Offers to install `com.unity.xr.management` if it is missing (own assembly, no dependency on XR Management) |
| `Editor/LinuxXRStatusWindow.cs` | Window -> XR -> LinuxXR Status: session state, statistics, actions |
| `Editor/LinuxXRConsoleForwarder.cs` | Forwards native log lines to the Unity Console |
| `Editor/LinuxXRIssueReport.cs` | Builds the Markdown issue report (last 250 log lines; `~` for the home directory) |

### `scripts/`

Developer: `fetch-unity-headers.sh`, `build-native.sh`, `build-openxr-loader.sh`, `pack-unity-package.sh`, `install-unity-package.sh`, `ensure-metas.py`, `make-unitypackage.py`.
Optional host helpers: `setup-host.sh`, `check-system.sh`, `setup-unity-flatpak.sh`, `test-openxr.sh`, `quest-usb.sh`.

## 10. Tests

`./scripts/build-native.sh test` builds and runs `ctest`:

| Test | What it checks |
|------|----------------|
| `linuxxr_core_tests` | Unit tests of `linuxxr_core` (runtime selection, requirements, math, ...); no external framework (`native/tests/check.h`) |
| `linuxxr_fake_unity_host` | A fake Unity host that loads `libLinuxXR.so` with `dlopen` and drives the plug-in entry points in a hermetic environment (own `XDG_*` directories, dummy runtime manifest) |
| `linuxxr_monado_integration` | Headless Monado + lavapipe: full session and frame loop against a real runtime (label `integration`; skipped with code 77 if Monado is missing) |
| `linuxxr_monado_instance_loss` | Kills Monado mid-operation; the plug-in must not hang (watchdog in the test) |

The tests need no headset and are suitable for CI (install Monado with `scripts/setup-host.sh --monado` for the integration tests).
