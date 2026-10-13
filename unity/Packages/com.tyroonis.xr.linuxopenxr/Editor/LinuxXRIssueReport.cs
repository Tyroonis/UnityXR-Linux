// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using UnityEditor;
using UnityEditor.XR.Management;
using UnityEngine;
using UnityEngine.Rendering;
using PackageInfo = UnityEditor.PackageManager.PackageInfo;

namespace Tyroonis.XR.LinuxXR.Editor
{
    /// <summary>
    /// Builds a Markdown report for GitHub/GitLab issues: system, Unity, LinuxXR settings, status,
    /// statistics, native diagnostics and the recent native log. The home directory is replaced by "~".
    /// </summary>
    static class LinuxXRIssueReport
    {
        public const int LogLines = 250;

        public static string Build()
        {
            var sb = new StringBuilder();
            sb.AppendLine("<!-- LinuxXR diagnostic report – please check it for private data before posting. -->");
            sb.AppendLine("## LinuxXR diagnostic report");
            sb.AppendLine();
            sb.AppendLine("**Describe the problem here:** what you did, what you expected, what happened.");
            sb.AppendLine();

            Section(sb, "System");
            bool flatpak = File.Exists("/.flatpak-info");
            Row(sb, "OS", OsName(flatpak));
            Row(sb, "Kernel", ReadFirstLine("/proc/sys/kernel/osrelease"));
            Row(sb, "Session", $"{Env("XDG_SESSION_TYPE")} / {Env("XDG_CURRENT_DESKTOP")}");
            Row(sb, "Unity in Flatpak", flatpak ? "yes (" + FlatpakAppId() + ")" : "no");
            Row(sb, "CPU", $"{SystemInfo.processorType} ({SystemInfo.processorCount} threads)");
            Row(sb, "RAM", $"{SystemInfo.systemMemorySize} MB");
            Row(sb, "GPU", $"{SystemInfo.graphicsDeviceName} ({SystemInfo.graphicsDeviceVendor})");
            Row(sb, "Graphics API", $"{SystemInfo.graphicsDeviceType} – {SystemInfo.graphicsDeviceVersion}");
            Row(sb, "WiVRn socket", WiVRnSocketState());
            sb.AppendLine();

            Section(sb, "Unity project");
            Row(sb, "Unity", Application.unityVersion);
            Row(sb, "Render pipeline", GraphicsSettings.currentRenderPipeline != null
                ? GraphicsSettings.currentRenderPipeline.GetType().Name : "Built-in");
            Row(sb, "Standalone XR loaders", ActiveLoaders(BuildTargetGroup.Standalone));
            Row(sb, "Init XR on startup", InitOnStartup(BuildTargetGroup.Standalone));
            Row(sb, "Android XR loaders", ActiveLoaders(BuildTargetGroup.Android));
            var pkg = PackageInfo.FindForAssembly(typeof(LinuxXRIssueReport).Assembly);
            Row(sb, "LinuxXR package", pkg != null ? $"{pkg.version} ({pkg.source}: {pkg.packageId})" : "?");
            Row(sb, "Subsystem manifest", SubsystemManifestLocation());
            sb.AppendLine();
            sb.AppendLine("<details><summary>XR-related packages</summary>");
            sb.AppendLine();
            foreach (var p in RelevantPackages()) sb.AppendLine($"- `{p.name}` {p.version} ({p.source})");
            sb.AppendLine();
            sb.AppendLine("</details>");
            sb.AppendLine();

            Section(sb, "LinuxXR settings");
            var s = LinuxXRSettings.GetSettings();
            if (s == null)
            {
                sb.AppendLine("No LinuxXR settings asset (loader never enabled?).");
            }
            else
            {
                Row(sb, "Runtime", s.RuntimeString);
                Row(sb, "Tracking origin", s.trackingOrigin.ToString());
                Row(sb, "Stereo mode", s.stereoMode.ToString());
                Row(sb, "Mirror", s.mirrorMode.ToString());
                Row(sb, "Render scale", s.renderScale.ToString("0.00", CultureInfo.InvariantCulture));
                Row(sb, "Verbose log", s.verboseLogging ? "on" : "off");
            }
            sb.AppendLine();

            string version = LinuxXRNative.GetVersion();
            Section(sb, "Native plugin");
            if (version == null)
            {
                sb.AppendLine("**libLinuxXR.so could not be loaded.**");
            }
            else
            {
                Row(sb, "Version", version);
                LinuxXRNative.GetStatus(out var st);
                Row(sb, "Headset reachable", st.runtimeReachable != 0 ? "yes" : "no");
                Row(sb, "Session", LinuxXRNative.SessionStateName(st.sessionState) +
                                   (st.refreshRate > 0 ? $", {st.refreshRate:0} Hz" : ""));
                Row(sb, "Display / Input subsystem", $"{st.displayState} / {st.inputState} (0 off, 1 initialized, 2 running)");
                if (LinuxXRNative.TryGetStats(out var stats))
                {
                    sb.AppendLine();
                    sb.AppendLine("**Statistics (current or last session)**");
                    sb.AppendLine();
                    foreach (var line in LinuxXRStatusWindow.StatsLines(stats)) Row(sb, line.Key, line.Value);
                }
                sb.AppendLine();
                sb.AppendLine("<details><summary>Diagnostics</summary>");
                sb.AppendLine();
                sb.AppendLine("```");
                sb.AppendLine(Anonymize(LinuxXRNative.GetDiagnostics().TrimEnd()));
                sb.AppendLine("```");
                sb.AppendLine();
                sb.AppendLine("Runtimes (kind, source, manifest, reachable):");
                sb.AppendLine("```");
                sb.AppendLine(Anonymize(LinuxXRNative.ListRuntimes().TrimEnd()));
                sb.AppendLine("```");
                sb.AppendLine("</details>");
                sb.AppendLine();
                sb.AppendLine($"<details><summary>Native log (last {LogLines} lines)</summary>");
                sb.AppendLine();
                sb.AppendLine("```");
                sb.AppendLine(Anonymize(LinuxXRNative.GetLogHistory(LogLines).TrimEnd()));
                sb.AppendLine("```");
                sb.AppendLine("</details>");
            }
            sb.AppendLine();
            sb.AppendLine($"_Generated {DateTime.Now:yyyy-MM-dd HH:mm} by Window → XR → LinuxXR Status._");
            return sb.ToString();
        }

        /// <summary>Writes the report to Logs/, copies it to the clipboard and returns the path.</summary>
        public static string CreateAndSave()
        {
            string report = Build();
            Directory.CreateDirectory("Logs");
            string path = Path.GetFullPath(Path.Combine("Logs", $"LinuxXR-report-{DateTime.Now:yyyyMMdd-HHmmss}.md"));
            File.WriteAllText(path, report);
            EditorGUIUtility.systemCopyBuffer = report;
            return path;
        }

        // ---- helpers --------------------------------------------------------------------------

        static void Section(StringBuilder sb, string title)
        {
            sb.AppendLine("### " + title);
            sb.AppendLine();
            sb.AppendLine("| | |");
            sb.AppendLine("|---|---|");
        }

        static void Row(StringBuilder sb, string key, string value) =>
            sb.AppendLine($"| {key} | {Anonymize(value ?? "").Replace("|", "\\|").Replace("\n", "<br>")} |");

        static string Env(string name) => Environment.GetEnvironmentVariable(name) ?? "-";

        public static string Anonymize(string text)
        {
            string home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            if (string.IsNullOrEmpty(home) || home == "/") return text;
            return text.Replace(home, "~");
        }

        static string ReadFirstLine(string path)
        {
            try { return File.ReadLines(path).FirstOrDefault() ?? "-"; }
            catch (Exception) { return "-"; }
        }

        static string OsName(bool flatpak)
        {
            // Inside Flatpak /etc/os-release describes the Flatpak runtime; the host is in /run/host/os-release.
            foreach (var path in flatpak ? new[] { "/run/host/os-release", "/etc/os-release" } : new[] { "/etc/os-release" })
            {
                try
                {
                    if (!File.Exists(path)) continue;
                    var line = File.ReadLines(path).FirstOrDefault(l => l.StartsWith("PRETTY_NAME="));
                    if (line != null) return line.Substring(12).Trim('"') + (path.StartsWith("/run/host") ? " (host)" : "");
                }
                catch (Exception) { }
            }
            return SystemInfo.operatingSystem;
        }

        static string FlatpakAppId()
        {
            try
            {
                var line = File.ReadLines("/.flatpak-info").FirstOrDefault(l => l.StartsWith("name="));
                return line != null ? line.Substring(5) : "?";
            }
            catch (Exception) { return "?"; }
        }

        public static string WiVRnSocketState()
        {
            string run = Environment.GetEnvironmentVariable("XDG_RUNTIME_DIR");
            if (string.IsNullOrEmpty(run)) return "unknown (XDG_RUNTIME_DIR not set)";
            string dir = Path.Combine(run, "wivrn");
            if (!Directory.Exists(dir)) return "folder not visible (WiVRn never started, or Flatpak override missing)";
            return File.Exists(Path.Combine(dir, "comp_ipc")) ? "present (headset connected)" : "missing (no headset connected)";
        }

        public static string ActiveLoaders(BuildTargetGroup group)
        {
            var gs = XRGeneralSettingsPerBuildTarget.XRGeneralSettingsForBuildTarget(group);
            var loaders = gs != null && gs.Manager != null ? gs.Manager.activeLoaders : null;
            if (loaders == null || loaders.Count == 0) return "(none)";
            return string.Join(", ", loaders.Where(l => l != null).Select(l => l.GetType().Name));
        }

        static string InitOnStartup(BuildTargetGroup group)
        {
            var gs = XRGeneralSettingsPerBuildTarget.XRGeneralSettingsForBuildTarget(group);
            return gs == null ? "-" : gs.InitManagerOnStart ? "yes" : "no";
        }

        /// <summary>Where LinuxXR's UnitySubsystemsManifest.json lives and whether Unity scans that place.</summary>
        public static string SubsystemManifestLocation()
        {
            // LinuxXR writes its manifest to Assets/LinuxXR (see LinuxXRSubsystemRegistration).
            var pkg = PackageInfo.FindForAssembly(typeof(LinuxXRIssueReport).Assembly);
            string source = pkg != null ? pkg.source + " package" : "Assets";
            if (File.Exists(LinuxXRSubsystemRegistration.ManifestPath))
                return LinuxXRSubsystemRegistration.ManifestPath +
                       (LinuxXRSubsystemRegistration.IsInstalled ? "" : " (outdated)") + $" - LinuxXR installed as {source}";
            var guids = AssetDatabase.FindAssets("UnitySubsystemsManifest");
            foreach (var g in guids)
            {
                string p = AssetDatabase.GUIDToAssetPath(g);
                if (p.StartsWith("Assets/") && File.Exists(p) && File.ReadAllText(p).Contains("LinuxXR Display"))
                    return p;
            }
            return "not found";
        }

        static IEnumerable<PackageInfo> RelevantPackages()
        {
            string[] keys = { "xr", "openxr", "oculus", "meta", "hurricane", "inputsystem", "render-pipelines" };
            return PackageInfo.GetAllRegisteredPackages()
                .Where(p => keys.Any(k => p.name.Contains(k)))
                .OrderBy(p => p.name);
        }
    }
}
