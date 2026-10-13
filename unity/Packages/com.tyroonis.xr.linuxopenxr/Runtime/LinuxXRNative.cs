// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Tyroonis.XR.LinuxXR
{
    /// <summary>P/Invoke bridge to libLinuxXR.so (native/src/unity/native_api.cpp).</summary>
    internal static class LinuxXRNative
    {
        const string Lib = "LinuxXR";

        [StructLayout(LayoutKind.Sequential)]
        public struct Status
        {
            public int preinitRan;
            public int preinitRuntimeQueried;
            public int displayState;
            public int inputState;
            public int sessionState;
            public int runtimeReachable;
            public float refreshRate;
            public ulong framesSubmitted;
        }

        /// <summary>Mirror of LinuxXRStats in native_api.cpp. Append new fields at the end only.</summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct Stats
        {
            public int structSize;
            public int sessionActive;
            public ulong framesBegun;
            public ulong framesRendered;
            public ulong framesEmpty;
            public ulong framesNoRender;
            public ulong framesMissed;
            public ulong framesDropped;
            public ulong logWarnings;
            public ulong logErrors;
            public double sessionSeconds;
            public float headsetFps;
            public float appFps;
            public float waitFrameMs;
            public float submitMs;
            public float displayPeriodMs;
            public float renderScale;
            public int eyeWidth;
            public int eyeHeight;
            public int singlePass;
            public int vulkanChecked;
            public int vulkanComplete;
            public long swapchainFormat;
        }

        [DllImport(Lib, EntryPoint = "LinuxXR_GetStats")]
        static extern void GetStatsNative(ref Stats stats, int size);

        [DllImport(Lib, EntryPoint = "LinuxXR_PopLog")]
        static extern int PopLogNative(out int level, byte[] buffer, int capacity);

        [DllImport(Lib, EntryPoint = "LinuxXR_SetConsoleForwarding")]
        static extern void SetConsoleForwardingNative(int enabled);

        [DllImport(Lib, EntryPoint = "LinuxXR_GetLogHistory")]
        static extern int GetLogHistoryNative(int maxLines, byte[] buffer, int capacity);

        /// <summary>Frame statistics; false if the library is missing or too old.</summary>
        public static bool TryGetStats(out Stats stats)
        {
            stats = default;
            try
            {
                GetStatsNative(ref stats, Marshal.SizeOf<Stats>());
                return stats.structSize > 0;
            }
            catch (DllNotFoundException) { return false; }
            catch (EntryPointNotFoundException) { return false; }
        }

        static byte[] s_LogBuffer = new byte[4096];

        /// <summary>Next queued native log message (level 0 debug, 1 info, 2 warning, 3 error). null = none.</summary>
        public static string PopLog(out int level)
        {
            int needed = PopLogNative(out level, s_LogBuffer, s_LogBuffer.Length);
            if (needed <= 0) return null;
            int len = Math.Min(needed, s_LogBuffer.Length) - 1;
            return Encoding.UTF8.GetString(s_LogBuffer, 0, Math.Max(0, len));
        }

        public static void SetConsoleForwarding(bool enabled) => SetConsoleForwardingNative(enabled ? 1 : 0);

        /// <summary>Last maxLines native log lines ("HH:MM:SS.mmm [level] text").</summary>
        public static string GetLogHistory(int maxLines) =>
            ReadString((buf, cap) => GetLogHistoryNative(maxLines, buf, cap));

        [DllImport(Lib, EntryPoint = "LinuxXR_GetVersion")]
        static extern IntPtr GetVersionNative();

        [DllImport(Lib, EntryPoint = "LinuxXR_GetStatus")]
        public static extern void GetStatus(out Status status);

        [DllImport(Lib, EntryPoint = "LinuxXR_GetDiagnostics")]
        static extern int GetDiagnosticsNative(byte[] buffer, int capacity);

        [DllImport(Lib, EntryPoint = "LinuxXR_ListRuntimes")]
        static extern int ListRuntimesNative(byte[] buffer, int capacity);

        [DllImport(Lib, EntryPoint = "LinuxXR_SetSettings")]
        static extern void SetSettingsNative(byte[] runtimeUtf8, int originFloor, int singlePass, int mirrorMode,
            float renderScale, int verboseLog);

        public static void SetSettings(string runtime, bool originFloor, bool singlePass, int mirrorMode,
            float renderScale, bool verboseLog)
        {
            var bytes = Encoding.UTF8.GetBytes((runtime ?? "auto") + "\0");
            SetSettingsNative(bytes, originFloor ? 1 : 0, singlePass ? 1 : 0, mirrorMode, renderScale, verboseLog ? 1 : 0);
        }

        public static string GetVersion()
        {
            try { return Marshal.PtrToStringUTF8(GetVersionNative()); }
            catch (DllNotFoundException) { return null; }
            catch (EntryPointNotFoundException) { return null; }
        }

        public static string GetDiagnostics() => ReadString(GetDiagnosticsNative);
        public static string ListRuntimes() => ReadString(ListRuntimesNative);

        /// <summary>true if libLinuxXR.so can be loaded.</summary>
        public static bool IsAvailable => GetVersion() != null;

        static string ReadString(Func<byte[], int, int> fn)
        {
            try
            {
                int needed = fn(null, 0);
                if (needed <= 1) return string.Empty;
                var buf = new byte[needed];
                fn(buf, buf.Length);
                return Encoding.UTF8.GetString(buf, 0, needed - 1);
            }
            catch (DllNotFoundException) { return "libLinuxXR.so not found – see docs/INSTALL.md."; }
            catch (EntryPointNotFoundException e) { return "Outdated libLinuxXR.so: " + e.Message; }
        }

        static readonly string[] s_StateNames =
            { "no session", "IDLE", "READY", "SYNCHRONIZED", "VISIBLE", "FOCUSED", "STOPPING", "LOSS_PENDING", "EXITING" };

        public static string SessionStateName(int s) =>
            s >= 0 && s < s_StateNames.Length ? s_StateNames[s] : s.ToString();
    }
}
