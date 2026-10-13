// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
using System.Collections.Generic;
using UnityEditor;
using UnityEditor.XR.Management.Metadata;
using UnityEngine;
using PackageInfo = UnityEditor.PackageManager.PackageInfo;

namespace Tyroonis.XR.LinuxXR.Editor
{
    /// <summary>
    /// Shows LinuxXR in Project Settings → XR Plug-in Management (tab "Windows, Mac, Linux"), only in the
    /// Linux Editor.
    ///
    /// XR Plug-in Management checks <c>packageId</c> before assigning a loader: if no installed package
    /// has that id it tries to download it from the Unity registry ("Unable to add package … cannot be
    /// found"). That works when LinuxXR is installed as a package (git URL, tarball, registry), but not
    /// when it was imported into Assets/ (Asset Store .unitypackage). In that case the id of XR Plug-in
    /// Management itself is reported - it is always installed, so the loader is simply assigned.
    /// </summary>
    class LinuxXRPackage : IXRPackage
    {
        public const string PackageName = "com.tyroonis.xr.linuxopenxr";
        const string XRManagementPackage = "com.unity.xr.management";

        class LoaderMetadata : IXRLoaderMetadata
        {
            public string loaderName { get; set; }
            public string loaderType { get; set; }
            public List<BuildTargetGroup> supportedBuildTargets { get; set; }
        }

        class PackageMetadata : IXRPackageMetadata
        {
            public string packageName { get; set; }
            public string packageId { get; set; }
            public string settingsType { get; set; }
            public List<IXRLoaderMetadata> loaderMetadata { get; set; }
        }

        static IXRPackageMetadata s_Metadata;

        /// <summary>true if LinuxXR is installed as a UPM package, false if it lives under Assets/.</summary>
        public static bool InstalledAsPackage =>
            PackageInfo.FindForAssembly(typeof(LinuxXRPackage).Assembly) != null;

        public IXRPackageMetadata metadata
        {
            get
            {
                if (s_Metadata != null) return s_Metadata;
                var loaders = new List<IXRLoaderMetadata>();
                if (Application.platform == RuntimePlatform.LinuxEditor)
                {
                    loaders.Add(new LoaderMetadata
                    {
                        loaderName = "LinuxXR",
                        loaderType = typeof(LinuxXRLoader).FullName,
                        supportedBuildTargets = new List<BuildTargetGroup> { BuildTargetGroup.Standalone }
                    });
                }
                s_Metadata = new PackageMetadata
                {
                    packageName = "LinuxXR (Linux Editor → WiVRn)",
                    packageId = InstalledAsPackage ? PackageName : XRManagementPackage,
                    settingsType = typeof(LinuxXRSettings).FullName,
                    loaderMetadata = loaders
                };
                return s_Metadata;
            }
        }

        public bool PopulateNewSettingsInstance(ScriptableObject obj)
        {
            if (obj is LinuxXRSettings s)
            {
                s.WriteNativeSettingsFile();
                return true;
            }
            return false;
        }
    }
}
