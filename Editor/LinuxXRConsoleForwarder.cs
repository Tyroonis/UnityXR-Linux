// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
using System;
using UnityEditor;
using UnityEngine;

namespace Tyroonis.XR.LinuxXR.Editor
{
    /// <summary>
    /// Forwards native [LinuxXR] messages to the Unity Console.
    ///
    /// The native plugin writes through IUnityXRTrace, which only ends up in Editor.log. Instead it keeps
    /// a queue of messages; this class drains it on the main thread (EditorApplication.update) and logs
    /// each message with Debug.Log/LogWarning/LogError. Messages from the graphics thread and from editor
    /// start-up (before C# is loaded) are queued natively and show up as soon as this runs.
    /// While forwarding is active the native side skips IUnityXRTrace, so nothing appears twice in Editor.log.
    /// </summary>
    [InitializeOnLoad]
    static class LinuxXRConsoleForwarder
    {
        const int MaxPerTick = 200;
        static bool s_Disabled;
        static bool s_Active;
        static double s_NextSettingsCheck;

        static LinuxXRConsoleForwarder()
        {
            if (Application.platform != RuntimePlatform.LinuxEditor) return;
            EditorApplication.update += Pump;
            EditorApplication.quitting += () => SetActive(false);
            AssemblyReloadEvents.beforeAssemblyReload += () =>
            {
                // Drain what we have; messages during the reload stay queued natively and follow afterwards.
                Pump();
            };
        }

        static bool Wanted()
        {
            var settings = LinuxXRSettings.GetSettings();
            return settings == null || settings.forwardLogToConsole;
        }

        static void SetActive(bool active)
        {
            if (s_Disabled || s_Active == active) return;
            try
            {
                LinuxXRNative.SetConsoleForwarding(active);
                s_Active = active;
            }
            catch (DllNotFoundException) { s_Disabled = true; }       // LinuxXR not installed in this project
            catch (EntryPointNotFoundException) { s_Disabled = true; } // older libLinuxXR.so without queue
        }

        static void Pump()
        {
            if (s_Disabled) return;

            double now = EditorApplication.timeSinceStartup;
            if (now >= s_NextSettingsCheck)
            {
                s_NextSettingsCheck = now + 1.0;
                SetActive(Wanted());
            }
            if (!s_Active) return;

            try
            {
                for (int i = 0; i < MaxPerTick; ++i)
                {
                    string text = LinuxXRNative.PopLog(out int level);
                    if (text == null) break;
                    LogType type = level >= 3 ? LogType.Error : level == 2 ? LogType.Warning : LogType.Log;
                    Debug.LogFormat(type, LogOption.NoStacktrace, null, "[LinuxXR] {0}", text);
                }
            }
            catch (DllNotFoundException) { s_Disabled = true; }
            catch (EntryPointNotFoundException) { s_Disabled = true; }
        }
    }
}
