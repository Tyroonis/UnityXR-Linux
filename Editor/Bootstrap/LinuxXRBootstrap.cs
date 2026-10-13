// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
using System;
using UnityEditor;
using UnityEditor.PackageManager;
using UnityEditor.PackageManager.Requests;
using UnityEngine;

namespace Tyroonis.XR.LinuxXR.Bootstrap
{
    /// <summary>
    /// Runs without any dependency. When LinuxXR was imported into Assets/ (e.g. from the Asset Store)
    /// and XR Plug-in Management is missing, the rest of LinuxXR is not compiled (asmdef define
    /// constraint) - this offers to install com.unity.xr.management. As a package, LinuxXR declares the
    /// dependency itself and this never triggers.
    /// </summary>
    static class LinuxXRBootstrap
    {
        const string XRManagement = "com.unity.xr.management";
        const string AskedKey = "LinuxXR.Bootstrap.Asked";
        static AddRequest s_Request;

        [InitializeOnLoadMethod]
        static void Check()
        {
            if (Application.isBatchMode || Application.platform != RuntimePlatform.LinuxEditor) return;
            if (Type.GetType("UnityEngine.XR.Management.XRGeneralSettings, Unity.XR.Management") != null) return;
            if (SessionState.GetBool(AskedKey, false)) return;
            SessionState.SetBool(AskedKey, true);
            EditorApplication.delayCall += Ask;
        }

        [MenuItem("Window/XR/LinuxXR Install XR Plug-in Management", true)]
        static bool CanInstall() =>
            Type.GetType("UnityEngine.XR.Management.XRGeneralSettings, Unity.XR.Management") == null;

        [MenuItem("Window/XR/LinuxXR Install XR Plug-in Management")]
        static void Ask()
        {
            if (!EditorUtility.DisplayDialog("LinuxXR",
                    "LinuxXR needs Unity's XR Plug-in Management package (com.unity.xr.management). Install it now?",
                    "Install", "Not now"))
                return;
            s_Request = Client.Add(XRManagement);
            EditorApplication.update += Progress;
        }

        static void Progress()
        {
            if (s_Request == null || !s_Request.IsCompleted) return;
            EditorApplication.update -= Progress;
            if (s_Request.Status == StatusCode.Success)
                Debug.Log("[LinuxXR] XR Plug-in Management installed. The LinuxXR Setup window opens after the next compile.");
            else
                Debug.LogError("[LinuxXR] Could not install XR Plug-in Management: " + s_Request.Error?.message +
                               " - install it from Window → Package Manager.");
            s_Request = null;
        }
    }
}
