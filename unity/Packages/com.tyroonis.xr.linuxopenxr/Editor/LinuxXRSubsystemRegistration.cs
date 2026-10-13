// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
using System;
using System.IO;
using System.Linq;
using Tyroonis.XR.LinuxXR;
using UnityEditor;
using UnityEngine;
using PackageInfo = UnityEditor.PackageManager.PackageInfo;

namespace Tyroonis.XR.LinuxXR.Editor
{
    /// <summary>
    /// Unity registers native XR subsystems only from a UnitySubsystemsManifest.json in a DIRECT sub-folder of Assets/
    /// (Assets/<name>/UnitySubsystemsManifest.json - deeper folders are not scanned) or in a registry package, and only at
    /// Editor start. Local tarball, git and embedded packages are not scanned. So that every install method (git URL,
    /// tarball, .unitypackage) works the same, LinuxXR installs its native part on first load into
    ///     Assets/LinuxXR/UnitySubsystemsManifest.json
    ///     Assets/LinuxXR/Plugins/Linux/x86_64/{libLinuxXR.so, libopenxr_loader.so.1}
    /// The package keeps the binaries in Runtime/Native~ (ignored by Unity's importer, so no duplicate plug-in).
    /// After the first install or an update one Editor restart is needed.
    /// </summary>
    [InitializeOnLoad]
    static class LinuxXRSubsystemRegistration
    {
        public const string Folder = "Assets/LinuxXR";
        const string OldFolder = "Assets/XR/LinuxXR";   // location used by older installs (not scanned by Unity)
        public const string ManifestPath = Folder + "/UnitySubsystemsManifest.json";
        public const string PluginFolder = Folder + "/Plugins/Linux/x86_64";
        const string Version = "0.3.3";
        const string RestartKey = "LinuxXR.NativeChangedThisSession";

        static readonly string[] NativeFiles = { "libLinuxXR.so", "libopenxr_loader.so.1", "LICENSE-openxr-loader.txt" };

        // Must match LinuxXRLoader.DisplayId / InputId and the native plug-in name (libLinuxXR.so).
        static readonly string ManifestJson =
@"{
  ""name"": ""LinuxXR"",
  ""version"": """ + Version + @""",
  ""libraryName"": ""LinuxXR"",
  ""displays"": [
    {
      ""id"": ""LinuxXR Display"",
      ""supportedMirrorBlitReservedModes"": [""leftEye"", ""rightEye"", ""sideBySide""]
    }
  ],
  ""inputs"": [
    {
      ""id"": ""LinuxXR Input"",
      ""disablesLegacyInput"": true,
      ""disablesLegacyVr"": true
    }
  ]
}
";

        // Fixed GUIDs so a re-install or the .unitypackage produce the same assets.
        const string GuidFolder = "4c2f0b8e3d6a4f1b9e7a5c3d2b1a0f91";
        const string GuidManifest = "4c2f0b8e3d6a4f1b9e7a5c3d2b1a0f92";
        const string GuidPlugin = "759136dbc69642408859d85ca4fb0e39";
        const string GuidLoader = "d260fa2f0d1d3b87cc2ef135ca75284c";
        const string GuidLicense = "11902ba1408934be685cb84e792e8b31";

        const string PluginMeta = @"fileFormatVersion: 2
guid: {0}
PluginImporter:
  externalObjects: {{}}
  serializedVersion: 2
  iconMap: {{}}
  executionOrder: {{}}
  defineConstraints: []
  isPreloaded: 0
  isOverridable: 0
  isExplicitlyReferenced: 0
  validateReferences: 1
  platformData:
  - first:
      : Any
    second:
      enabled: 0
      settings:
        Exclude Android: 1
        Exclude Editor: 0
        Exclude Linux64: 1
        Exclude OSXUniversal: 1
        Exclude WebGL: 1
        Exclude Win: 1
        Exclude Win64: 1
  - first:
      Any:
    second:
      enabled: 0
      settings: {{}}
  - first:
      Editor: Editor
    second:
      enabled: 1
      settings:
        CPU: x86_64
        DefaultValueInitialized: true
        OS: Linux
  userData:
  assetBundleName:
  assetBundleVariant:
";
        const string DefaultMeta = "fileFormatVersion: 2\nguid: {0}\nDefaultImporter:\n  externalObjects: {{}}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n";
        const string TextMeta = "fileFormatVersion: 2\nguid: {0}\nTextScriptImporter:\n  externalObjects: {{}}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n";

        static LinuxXRSubsystemRegistration()
        {
            if (Application.platform != RuntimePlatform.LinuxEditor || Application.isBatchMode) return;
            EditorApplication.delayCall += () =>
            {
                if (Install())
                    Debug.Log("[LinuxXR] Native plug-in and XR subsystems installed to " + Folder +
                              ". Restart the Editor once to activate them (Window → XR → LinuxXR Setup).");
            };
        }

        /// <summary>Folder with the binaries shipped by the package (Runtime/Native~), or null.</summary>
        static string SourceFolder()
        {
            string root = null;
            var pkg = PackageInfo.FindForAssembly(typeof(LinuxXRSubsystemRegistration).Assembly);
            if (pkg != null) root = pkg.resolvedPath;
            else
            {
                // Installed under Assets/ (dev symlink or a copy): <root>/Editor/LinuxXRSubsystemRegistration.cs
                string script = AssetDatabase.FindAssets("LinuxXRSubsystemRegistration t:MonoScript")
                    .Select(AssetDatabase.GUIDToAssetPath)
                    .FirstOrDefault(p => p.EndsWith("/LinuxXRSubsystemRegistration.cs"));
                if (script != null) root = Path.GetDirectoryName(Path.GetDirectoryName(Path.GetFullPath(script)));
            }
            if (root == null) return null;
            string dir = Path.Combine(root, "Runtime", "Native~");
            return Directory.Exists(dir) ? dir : null;
        }

        static bool SameContent(string a, string b)
        {
            if (!File.Exists(a) || !File.Exists(b)) return false;
            if (new FileInfo(a).Length != new FileInfo(b).Length) return false;
            byte[] x = File.ReadAllBytes(a), y = File.ReadAllBytes(b);
            for (int i = 0; i < x.Length; i++)
                if (x[i] != y[i]) return false;
            return true;
        }

        static bool WriteTextIfDifferent(string path, string text)
        {
            if (File.Exists(path) && File.ReadAllText(path).Replace("\r", "") == text) return false;
            File.WriteAllText(path, text);
            return true;
        }

        /// <summary>true if manifest and native plug-in in Assets/ are present and up to date.</summary>
        public static bool IsInstalled
        {
            get
            {
                if (!File.Exists(ManifestPath) || File.ReadAllText(ManifestPath).Replace("\r", "") != ManifestJson) return false;
                if (!File.Exists(Path.Combine(PluginFolder, "libLinuxXR.so"))) return false;
                string src = SourceFolder();
                if (src == null) return true; // .unitypackage install: binaries came with the package itself
                return NativeFiles.All(f => !File.Exists(Path.Combine(src, f)) || SameContent(Path.Combine(src, f), Path.Combine(PluginFolder, f)));
            }
        }

        /// <summary>true if binaries were replaced in this Editor session (a restart is needed to load them).</summary>
        public static bool RestartPending => SessionState.GetBool(RestartKey, false);

        /// <summary>Installs/updates manifest and plug-in in Assets/LinuxXR. Returns true if anything changed.</summary>
        public static bool Install()
        {
            try
            {
                bool changed = false, binariesChanged = false;
                // Remove leftovers of older installs (a second libLinuxXR.so would clash with the plug-in name).
                if (Directory.Exists(OldFolder))
                {
                    AssetDatabase.DeleteAsset(OldFolder);
                    if (Directory.Exists(OldFolder)) Directory.Delete(OldFolder, true);
                    if (File.Exists(OldFolder + ".meta")) File.Delete(OldFolder + ".meta");
                    changed = binariesChanged = true;
                }
                // Assets/LinuxXR must not be a symlink/copy of the package itself (left over from an older dev setup).
                if (Directory.Exists(Folder) && File.Exists(Path.Combine(Folder, "package.json")))
                {
                    Debug.LogError("[LinuxXR] " + Folder + " contains a copy/symlink of the LinuxXR package itself. " +
                                   "Remove it (developers: ./scripts/install-unity-package.sh <project> --dev).");
                    return false;
                }
                Directory.CreateDirectory(PluginFolder);
                changed |= WriteTextIfDifferent(ManifestPath, ManifestJson);
                changed |= WriteTextIfDifferent(ManifestPath + ".meta", string.Format(TextMeta, GuidManifest));

                string src = SourceFolder();
                if (src != null)
                {
                    foreach (var f in NativeFiles)
                    {
                        string from = Path.Combine(src, f), to = Path.Combine(PluginFolder, f);
                        if (!File.Exists(from) || SameContent(from, to)) continue;
                        // Write to a temp file and rename: replaces a .so the Editor has loaded without corrupting it.
                        string tmp = to + ".tmp~";
                        File.Copy(from, tmp, true);
                        if (File.Exists(to)) File.Delete(to);   // unlink only - a loaded copy stays valid until restart
                        File.Move(tmp, to);
                        changed = binariesChanged = true;
                    }
                }
                changed |= WriteTextIfDifferent(Path.Combine(PluginFolder, "libLinuxXR.so.meta"), string.Format(PluginMeta, GuidPlugin));
                changed |= WriteTextIfDifferent(Path.Combine(PluginFolder, "libopenxr_loader.so.1.meta"), string.Format(DefaultMeta, GuidLoader));
                changed |= WriteTextIfDifferent(Path.Combine(PluginFolder, "LICENSE-openxr-loader.txt.meta"), string.Format(TextMeta, GuidLicense));
                changed |= WriteTextIfDifferent(Folder + ".meta", string.Format(FolderMeta, GuidFolder));

                if (changed) AssetDatabase.Refresh();
                if (binariesChanged) SessionState.SetBool(RestartKey, true);
                return changed;
            }
            catch (Exception e)
            {
                Debug.LogError("[LinuxXR] Could not install the native plug-in to " + Folder + ": " + e.Message);
                return false;
            }
        }

        const string FolderMeta = "fileFormatVersion: 2\nguid: {0}\nfolderAsset: yes\nDefaultImporter:\n  externalObjects: {{}}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n";
    }
}
