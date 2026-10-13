# Third-party notices

## LinuxXR license

LinuxXR itself is licensed under the GNU General Public License, version 3 or (at your option) any later version
(**GPL-3.0-or-later**). The full GPLv3 text is in [LICENSE](LICENSE).

### Additional permission under GNU GPL version 3, section 7

LinuxXR may be linked and combined with the Unity Editor, the Unity engine runtime and the Unity XR SDK headers
(provided under the Unity Companion License), and the licensors of LinuxXR permit this linking and combining
and the conveying of the resulting work.

Using LinuxXR as a development tool does not put your own Unity project or game under the GPL: your project,
game and content are not a work based on LinuxXR and you may license them as you wish. LinuxXR is Editor-only and is
not included in player builds.

## Third-party components

This section lists third-party components that are bundled, fetched at build time, or required at runtime.

| Component | Role | Version / source | License | How it is used |
|-----------|------|------------------|---------|----------------|
| Khronos OpenXR-SDK loader (`libopenxr_loader.so.1`) | OpenXR loader | OpenXR-SDK 1.1.47, <https://github.com/KhronosGroup/OpenXR-SDK> | Apache-2.0 | **Bundled binary** in the Unity package at `unity/Packages/com.tyroonis.xr.linuxopenxr/Runtime/Native~/` (and therefore in the release tarball, the `.unitypackage` and the `upm` branch), together with its license text `LICENSE-openxr-loader.txt`. Built on Ubuntu 22.04 by `scripts/build-openxr-loader.sh`; needs glibc >= 2.35. Loaded with `dlopen` as a fallback when the system has no loader (e.g. inside the Unity Hub Flatpak). |
| Unity XR SDK provider headers (`IUnityXR*.h`, `IUnityGraphics*.h`, ...) | Build-time headers | `CommonHeaders/ProviderInterface` of <https://github.com/ValveSoftware/unity-xr-plugin>, pinned commit in `scripts/fetch-unity-headers.sh` | Unity Companion License (see `native/third_party/unity-xr-headers/LICENSE-Unity-Companion.md` after fetching) | **Not committed.** `scripts/fetch-unity-headers.sh` downloads them into `native/third_party/unity-xr-headers/` (`scripts/build-native.sh` calls it automatically). They are only needed to compile `libLinuxXR.so`, and are not part of the release package. |
| OpenXR headers (`libopenxr-dev`), Vulkan headers (`libvulkan-dev`) | Build-time headers | Ubuntu packages | Apache-2.0 / MIT (per upstream) | Installed from the distribution, not redistributed. |
| WiVRn | OpenXR runtime and streaming server, Quest client app | <https://github.com/WiVRn/WiVRn> | GPL-3.0 (per upstream) | **External, not bundled.** Installed separately (Flatpak `io.github.wivrn.wivrn` or the `ppa:lvra/wivrn` packages). LinuxXR talks to it only through the standard OpenXR interface. |
| Monado | OpenXR runtime (WiVRn is based on it); optional headless test runtime | <https://monado.freedesktop.org/> | BSL-1.0 (per upstream) | **External, not bundled.** Optional (`scripts/setup-host.sh --monado`), used for the integration tests and headset-less checks. |
| Unity Editor, Unity XR Management (`com.unity.xr.management`) | Host application and package dependency | Unity Technologies | Unity licenses | **External, not bundled.** `com.unity.xr.management` is resolved by Unity's Package Manager as a dependency of the LinuxXR package (version 4.5.0). |

`ValveSoftware/unity-xr-plugin` (Valve's OpenVR plugin for Unity) is also used as a structural reference for how a
native Unity XR provider is laid out. No code from it is included beyond the headers listed above.

License texts of the bundled and fetched components are included next to the respective files
(`LICENSE-openxr-loader.txt`, `LICENSE-Unity-Companion.md`). The licenses of external components are
stated here as described by their upstream projects; please check upstream for the authoritative text.

LinuxXR is not affiliated with or endorsed by Unity Technologies, Meta Platforms, Valve, Khronos, or the
WiVRn / Monado projects.
