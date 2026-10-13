// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using UnityEditor;
using UnityEngine;
using Debug = UnityEngine.Debug;

namespace Tyroonis.XR.LinuxXR.Editor
{
    /// <summary>
    /// Helpers for things outside Unity: Flatpak sandbox detection, running commands on the host
    /// (through flatpak-spawn when the Editor runs inside the Unity Hub Flatpak) and WiVRn.
    /// </summary>
    static class LinuxXRHost
    {
        public const string WiVRnFlatpak = "io.github.wivrn.wivrn";
        public const string WiVRnFlathubUrl = "https://flathub.org/apps/io.github.wivrn.wivrn";
        public const string WiVRnQuestUrl = "https://github.com/WiVRn/WiVRn#installation";

        public static bool InFlatpak => File.Exists("/.flatpak-info");

        /// <summary>Flatpak app id of the sandbox the Editor runs in (e.g. com.unity.UnityHub), or null.</summary>
        public static string FlatpakAppId
        {
            get
            {
                if (!InFlatpak) return null;
                try
                {
                    var line = File.ReadLines("/.flatpak-info").FirstOrDefault(l => l.StartsWith("name="));
                    return line?.Substring(5).Trim();
                }
                catch (Exception) { return null; }
            }
        }

        public static string Home => Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);

        public static string RuntimeDir => Environment.GetEnvironmentVariable("XDG_RUNTIME_DIR") ?? "";

        /// <summary>WiVRn's socket folder is visible from this process.</summary>
        public static bool WiVRnFolderVisible =>
            !string.IsNullOrEmpty(RuntimeDir) && Directory.Exists(Path.Combine(RuntimeDir, "wivrn"));

        /// <summary>WiVRn's IPC socket exists (server running with a headset connected).</summary>
        public static bool WiVRnSocketPresent =>
            !string.IsNullOrEmpty(RuntimeDir) && File.Exists(Path.Combine(RuntimeDir, "wivrn", "comp_ipc"));

        /// <summary>Host paths where the WiVRn Flatpak keeps its OpenXR runtime.</summary>
        public static readonly string[] WiVRnFlatpakDirs =
        {
            "/var/lib/flatpak/app/" + WiVRnFlatpak,
            "~/.local/share/flatpak/app/" + WiVRnFlatpak,
        };

        /// <summary>
        /// Flatpak overrides that let the sandboxed Editor reach WiVRn: the socket folder and the
        /// runtime library (read-only). Applied to the Flatpak the Editor runs in.
        /// </summary>
        public static string[] FlatpakOverrideArgs(string appId) =>
            new[] { "override", "--user", "--filesystem=xdg-run/wivrn" }
                .Concat(WiVRnFlatpakDirs.Select(d => "--filesystem=" + d + ":ro"))
                .Concat(new[] { appId })
                .ToArray();

        public static string FlatpakOverrideCommand(string appId) =>
            "flatpak " + string.Join(" ", FlatpakOverrideArgs(appId));

        // ---- processes -------------------------------------------------------------------------

        public struct Result
        {
            public bool started;
            public int exitCode;
            public string output;
            public bool Ok => started && exitCode == 0;
        }

        /// <summary>Runs a command on the host (via flatpak-spawn --host inside Flatpak) and waits.</summary>
        public static Result RunOnHost(string file, string[] args, int timeoutMs = 15000)
        {
            var r = new Result();
            try
            {
                using (var p = Process.Start(HostStartInfo(file, args)))
                {
                    if (p == null) return r;
                    r.started = true;
                    var stdout = p.StandardOutput.ReadToEndAsync();
                    var stderr = p.StandardError.ReadToEndAsync();
                    if (!p.WaitForExit(timeoutMs))
                    {
                        try { p.Kill(); } catch (Exception) { }
                        r.exitCode = -1;
                        r.output = "timed out";
                        return r;
                    }
                    r.exitCode = p.ExitCode;
                    r.output = (stdout.Result + stderr.Result).Trim();
                }
            }
            catch (Exception e)
            {
                r.output = e.Message;
            }
            return r;
        }

        /// <summary>Starts a long-running command on the host; output lines go to onOutput (any thread).</summary>
        public static Process RunOnHostAsync(string file, string[] args, Action<string> onOutput, Action<int> onExit)
        {
            try
            {
                var p = new Process { StartInfo = HostStartInfo(file, args), EnableRaisingEvents = true };
                p.OutputDataReceived += (_, e) => { if (e.Data != null) onOutput?.Invoke(e.Data); };
                p.ErrorDataReceived += (_, e) => { if (e.Data != null) onOutput?.Invoke(e.Data); };
                p.Exited += (_, __) => onExit?.Invoke(p.ExitCode);
                p.Start();
                p.BeginOutputReadLine();
                p.BeginErrorReadLine();
                return p;
            }
            catch (Exception e)
            {
                onOutput?.Invoke("Could not start " + file + ": " + e.Message);
                onExit?.Invoke(-1);
                return null;
            }
        }

        /// <summary>Starts a GUI program on the host without waiting for it.</summary>
        public static bool LaunchOnHost(string file, params string[] args)
        {
            try
            {
                var psi = HostStartInfo(file, args);
                psi.RedirectStandardOutput = psi.RedirectStandardError = false;
                Process.Start(psi);
                return true;
            }
            catch (Exception) { return false; }
        }

        static ProcessStartInfo HostStartInfo(string file, string[] args)
        {
            var all = InFlatpak ? new[] { "--host", file }.Concat(args) : args;
            var psi = new ProcessStartInfo(InFlatpak ? "flatpak-spawn" : file)
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                Arguments = string.Join(" ", all.Select(Quote)),
            };
            return psi;
        }

        static string Quote(string a)
        {
            if (a.Length > 0 && a.All(c => char.IsLetterOrDigit(c) || "-_./:=~@+,".IndexOf(c) >= 0)) return a;
            var sb = new StringBuilder("\"");
            foreach (char c in a)
            {
                if (c == '"' || c == '\\') sb.Append('\\');
                sb.Append(c);
            }
            return sb.Append('"').ToString();
        }

        // ---- WiVRn -----------------------------------------------------------------------------

        /// <summary>Is the WiVRn Flatpak or the wivrn-server package installed on the host?</summary>
        const string WiVRnInstalledKey = "LinuxXR.WiVRnInstalled";

        /// <summary>
        /// True if WiVRn is installed on the host. Checks the file system first (no process); the slower
        /// host query runs only when that is inconclusive and its result is cached for the Editor session
        /// (pass force = true to re-check, e.g. after installing).
        /// </summary>
        public static bool IsWiVRnInstalled(bool force = false)
        {
            string home = Environment.GetEnvironmentVariable("HOME") ?? "";
            if (Directory.Exists("/var/lib/flatpak/app/" + WiVRnFlatpak) ||
                Directory.Exists(Path.Combine(home, ".local/share/flatpak/app", WiVRnFlatpak)) ||
                File.Exists("/usr/bin/wivrn-server"))
                return true;
            if (!force)
            {
                int cached = SessionState.GetInt(WiVRnInstalledKey, -1);
                if (cached >= 0) return cached == 1;
            }
            bool ok = RunOnHost("flatpak", new[] { "info", WiVRnFlatpak }, 4000).Ok ||
                      RunOnHost("sh", new[] { "-c", "command -v wivrn-server" }, 3000).Ok;
            SessionState.SetInt(WiVRnInstalledKey, ok ? 1 : 0);
            return ok;
        }

        public static void StartWiVRn()
        {
            if (RunOnHost("flatpak", new[] { "info", WiVRnFlatpak }, 8000).Ok)
            {
                if (LaunchOnHost("flatpak", "run", WiVRnFlatpak)) return;
            }
            if (LaunchOnHost("wivrn-dashboard")) return;
            Debug.LogError("[LinuxXR] Could not start WiVRn - start it from your application menu.");
        }

        /// <summary>Restarts the Editor with the current project.</summary>
        public static void RestartEditor()
        {
            if (!UnityEditor.SceneManagement.EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return;
            AssetDatabase.SaveAssets();
            EditorApplication.OpenProject(Directory.GetCurrentDirectory());
        }
    }
}
