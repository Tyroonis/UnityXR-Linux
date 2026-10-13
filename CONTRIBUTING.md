# Contributing to LinuxXR

Thanks for your interest. Bug reports, test results from other hardware, documentation fixes and code are all welcome.

## Reporting bugs

1. In the Unity Editor open **Window -> XR -> LinuxXR Status** and click **Create issue report**.
2. The Markdown report (system, Unity, packages, settings, status, statistics, diagnostics, last 250 native log lines; your home directory is replaced by `~`)
   is copied to the clipboard and saved as `Logs/LinuxXR-report-<timestamp>.md` in your project.
3. Open an issue using the bug report template and paste the report. Please look through it for anything private first.

See [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) for common problems. Reports from setups other than the tested one (see the README) are especially useful,
including "works for me" reports.

## Development setup

Users install LinuxXR through Unity's Package Manager and never run these scripts; this section is for working on LinuxXR itself.
The full walkthrough is in [docs/INSTALL.md](docs/INSTALL.md#building-from-source-developers).

```bash
./scripts/setup-host.sh --dev --monado     # build tools, WiVRn, and Monado for the integration tests (optional helper; you can install the packages by hand)
./scripts/fetch-unity-headers.sh           # Unity XR SDK headers (Unity Companion License); build-native.sh also does this
./scripts/build-native.sh                  # builds libLinuxXR.so and native/build/linuxxr-probe
./scripts/build-native.sh test             # build + ctest
./scripts/build-native.sh debug            # Debug build
```

`build-native.sh` copies `libLinuxXR.so` into the package folder
`unity/Packages/com.tyroonis.xr.linuxopenxr/Runtime/Native~/`. The Unity XR SDK headers must **not** be committed (they are in `.gitignore`).
`./scripts/build-openxr-loader.sh` rebuilds the bundled `libopenxr_loader.so.1` (Khronos OpenXR-SDK 1.1.47).

To work on the C# code with live edits, link the package into a test project:

```bash
./scripts/install-unity-package.sh /path/to/test-project --dev      # symlink <project>/Assets/LinuxXRDev -> the package folder
./scripts/install-unity-package.sh /path/to/test-project --tarball  # test the release tarball (what users get)
./scripts/install-unity-package.sh /path/to/test-project --remove
```

(`LINUXXR_UNITY_PROJECT=/path/to/test-project ./scripts/build-native.sh` installs after every build.) The package then installs its native part into `Assets/LinuxXR` itself, like for end users
(`Assets/LinuxXR` must not be a symlink or copy of the package). Restart the Editor after native changes and after switching install modes.

Every file in the package folder needs a `.meta` file. Run `./scripts/ensure-metas.py` after adding files, and commit the result.

`./scripts/pack-unity-package.sh` produces `dist/*.tgz`, `dist/*.unitypackage` and `dist/upm`, the same artifacts the release workflow publishes.

You can test the native chain without Unity at any time: `native/build/linuxxr-probe runtimes|info|preinit|run`.

## CI and workflows

- `.github/workflows/ci.yml` builds and runs `ctest` on ubuntu-24.04 for pushes to `main` and for pull requests.
- `.github/workflows/release.yml` runs on tags `vX.Y.Z` (see below).
- Issue templates: `.github/ISSUE_TEMPLATE/` (GitHub) and `.gitlab/issue_templates/` (GitLab).
- `docs/github/` holds a copy of the templates and workflows. If you change one of them, update the copy as well.

## Tests

`./scripts/build-native.sh test` runs `ctest`: core unit tests, a fake Unity host, and Monado integration tests (headless Monado + lavapipe, including runtime loss).
The Monado tests are skipped when Monado is not installed. Please add or update tests for changes in `native/src/core/`; tests use the small helper in `native/tests/check.h`.
Run the tests before opening a pull request.

## Code style

- C++20, 2-space indentation, no tabs. Keep the existing file layout: Unity-independent logic in `native/src/core/`, Unity-specific glue in `native/src/unity/`.
- No new compile warnings (the build uses `-Wall -Wextra`).
- **Comments, log messages and user-visible strings are in English.**
- Every source file starts with an SPDX header (`SPDX-License-Identifier: GPL-3.0-or-later` and `Copyright (C) 2026 Tyroonis`, in the file's comment syntax).
- C#: follow the style of the surrounding files in `unity/Packages/com.tyroonis.xr.linuxopenxr/`. Prefix messages shown in the Console with `[LinuxXR]`.
- Shell scripts: `bash` with `set -euo pipefail`, a header comment that documents all options, and `--help` or usage output where applicable.
- Do not add features to the docs that the code does not have. If behavior changes, update the English and the German documentation together.

## Submitting a pull request

1. Fork the repository and create a topic branch.
2. Keep the change focused; describe what and why. Mention the hardware/software you tested on (GPU, driver, Unity version, headset, WiVRn version).
3. Make sure `./scripts/build-native.sh test` passes.
4. Update `CHANGELOG.md` (the "Unreleased" section, [Keep a Changelog](https://keepachangelog.com/) style) and the docs where behavior changes.
5. If the change affects runtime behavior in the Editor, attach the issue report from the status window (see above) from a test run.
6. Open the pull request. By contributing you agree that your contribution is licensed under the GNU General Public License v3.0 or later (GPL-3.0-or-later, see [LICENSE](LICENSE)), including the additional permission described in the README.

## Release procedure

Releases are cut by pushing a tag; the release workflow does the rest. Every fix bumps the patch version (last digit); use `X.Y.Z` for the new version.

1. Bump the version everywhere it lives (they must all match, the workflow checks the tag against `package.json`):
   - `unity/Packages/com.tyroonis.xr.linuxopenxr/package.json` (`version`)
   - `native/CMakeLists.txt` (`project(LinuxXR VERSION X.Y.Z ...)`)
   - `LINUXXR_EXPECTED_VERSION` in `native/tests/fake_unity_host.cpp` and `native/tests/monado_integration.cpp` (CMake normally passes `PROJECT_VERSION`, but keep the defaults in sync)
   - the `Version` constant in `unity/Packages/com.tyroonis.xr.linuxopenxr/Editor/LinuxXRSubsystemRegistration.cs` (it is written into `Assets/LinuxXR/UnitySubsystemsManifest.json`)
2. Add a `## [X.Y.Z] - YYYY-MM-DD` section to `CHANGELOG.md` (move the "Unreleased" entries into it) and the link reference at the bottom. The release workflow fails without this section and uses it as the release notes.
   Update version numbers named in the docs (install examples such as `#upm/vX.Y.Z`, README status, package README).
3. Run `./scripts/build-native.sh test` and, for a local dry run, `./scripts/pack-unity-package.sh`; install the tarball into a test project with `install-unity-package.sh <project> --tarball` and check the setup wizard.
4. Commit, then tag and push: `git tag vX.Y.Z && git push origin main vX.Y.Z`.
5. `release.yml` (in `.github/workflows/`; copies are kept in `docs/github/`) then
   - runs the CI tests (`ci.yml` through `workflow_call`),
   - builds the binaries portably in an `ubuntu:22.04` container (static libstdc++, glibc 2.35+),
   - packs `com.tyroonis.xr.linuxopenxr-X.Y.Z.tgz` and `LinuxXR-X.Y.Z.unitypackage`,
   - signs the tarball if signing is configured (see below),
   - creates the GitHub Release with both files and the CHANGELOG section as notes,
   - force-pushes the `upm` branch and the tag `upm/vX.Y.Z`.
6. Verify: in a clean project, *Add package from git URL...* with `...#upm/vX.Y.Z`, restart the Editor, and check that the Console shows `[LinuxXR] Initialized (version X.Y.Z)`.

### Signing the tarball

Unity 6.3+ shows "package is not signed" for unsigned tarballs (only a warning; see [Unity's package signature documentation](https://docs.unity3d.com/Manual/upm-signature.html)).
The release workflow signs the tarball with Unity's `upm` CLI ([`upm pack`](https://docs.unity3d.com/6000.4/Documentation/Manual/upm-cli-pack.html)) when these are set up:

1. Create a Unity Cloud organization and a **service account** in it with the role **Package Manager Package Signer**.
2. Add three repository secrets (GitHub: *Settings -> Secrets and variables -> Actions*):
   - `UPM_SERVICE_ACCOUNT_KEY_ID` and `UPM_SERVICE_ACCOUNT_KEY_SECRET`: the service account's key ID and secret,
   - `UPM_ORG_ID`: the Organization ID from the Unity Cloud Dashboard (*Administration -> Settings*).

Without these secrets the workflow still succeeds and the tarball stays unsigned. Git URL installs (`#upm`) usually do not show the warning.

## Licensing notes

LinuxXR is GPL-3.0-or-later with an additional permission for the Unity Editor and Unity XR SDK headers (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)); code you add must be compatible with that.
Do not commit Unity XR SDK headers or other third-party code without discussing it first. Bundled binaries (the OpenXR loader in `Runtime/Native~/`) and their licenses are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
