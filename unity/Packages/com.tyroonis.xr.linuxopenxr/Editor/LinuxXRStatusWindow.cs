// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;
using Debug = UnityEngine.Debug;

namespace Tyroonis.XR.LinuxXR.Editor
{
    /// <summary>
    /// Window → XR → LinuxXR Status: setup checklist, session, live statistics, quick actions and a
    /// diagnostic report for bug reports.
    /// </summary>
    public class LinuxXRStatusWindow : EditorWindow
    {
        const string WiVRnFlatpak = "io.github.wivrn.wivrn";
        const double RefreshInterval = 0.5;

        enum Check { Ok, Warn, Fail, Info }

        struct Item
        {
            public Check state;
            public string label, value;
        }

        // Snapshot, refreshed every RefreshInterval (P/Invoke calls are cheap, runtime probing is cached natively)
        string m_Version;
        LinuxXRNative.Status m_Status;
        bool m_HasStats;
        LinuxXRNative.Stats m_Stats;
        readonly List<Item> m_Setup = new List<Item>();
        string m_Diagnostics = "";
        string m_LastReport;
        double m_NextRefresh;
        Vector2 m_Scroll;
        bool m_ShowDiagnostics;

        GUIStyle m_Header, m_Section, m_Wrap;

        [MenuItem("Window/XR/LinuxXR Status")]
        public static void Open() => GetWindow<LinuxXRStatusWindow>("LinuxXR");

        static string LogPath
        {
            get
            {
                var cache = Environment.GetEnvironmentVariable("XDG_CACHE_HOME");
                if (string.IsNullOrEmpty(cache))
                    cache = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".cache");
                return Path.Combine(cache, "linuxxr", "linuxxr.log");
            }
        }

        void OnEnable()
        {
            minSize = new Vector2(380, 320);
            EditorApplication.update += Tick;
            Refresh();
        }

        void OnDisable() => EditorApplication.update -= Tick;

        void Tick()
        {
            if (EditorApplication.timeSinceStartup < m_NextRefresh) return;
            Refresh();
            Repaint();
        }

        // ---- data ---------------------------------------------------------------------------

        void Refresh()
        {
            m_NextRefresh = EditorApplication.timeSinceStartup + RefreshInterval;
            m_Version = LinuxXRNative.GetVersion();
            m_HasStats = false;
            if (m_Version != null)
            {
                LinuxXRNative.GetStatus(out m_Status);
                m_HasStats = LinuxXRNative.TryGetStats(out m_Stats);
                if (m_ShowDiagnostics) m_Diagnostics = LinuxXRNative.GetDiagnostics();
            }
            BuildSetupList();
        }

        void BuildSetupList()
        {
            m_Setup.Clear();
            bool linux = Application.platform == RuntimePlatform.LinuxEditor;
            Add(linux ? Check.Ok : Check.Fail, "Linux Editor", linux ? "yes" : "LinuxXR only works in the Linux Editor");

            var gfx = SystemInfo.graphicsDeviceType;
            Add(gfx == GraphicsDeviceType.Vulkan ? Check.Ok : Check.Fail, "Graphics API",
                gfx == GraphicsDeviceType.Vulkan ? "Vulkan" : gfx + " – open the Setup wizard ('Use Vulkan')");

            Add(m_Version != null ? Check.Ok : Check.Fail, "Native plugin",
                m_Version != null ? "libLinuxXR.so " + m_Version : "libLinuxXR.so not loaded – reinstall the package (needs Ubuntu 22.04+ / glibc 2.35+)");

            string manifest = LinuxXRIssueReport.SubsystemManifestLocation();
            bool descriptors = LinuxXRLoaderHasDescriptors();
            Add(descriptors ? Check.Ok : Check.Fail, "Subsystems",
                descriptors ? "registered (" + manifest + ")"
                            : "not registered – " + manifest + ". Open the Setup wizard.");

            string loaders = LinuxXRIssueReport.ActiveLoaders(BuildTargetGroup.Standalone);
            bool enabled = loaders.Contains(nameof(LinuxXRLoader));
            Add(enabled ? Check.Ok : Check.Fail, "XR loader",
                enabled ? "LinuxXR enabled (Standalone)" : "enable LinuxXR under XR Plug-in Management → Windows, Mac, Linux");

            if (m_Version != null)
            {
                var runtimes = LinuxXRNative.ListRuntimes().Split('\n', StringSplitOptions.RemoveEmptyEntries)
                    .Select(l => l.Split('\t')).Where(p => p.Length >= 4).ToArray();
                Add(runtimes.Length > 0 ? Check.Ok : Check.Fail, "OpenXR runtime",
                    runtimes.Length > 0 ? $"{runtimes[0][0]} ({runtimes[0][1]})" + (runtimes.Length > 1 ? $" +{runtimes.Length - 1} more" : "")
                                        : "none found – install WiVRn" + (InFlatpak ? " and allow Flatpak access (Setup wizard)" : " (Setup wizard)"));
                Add(m_Status.runtimeReachable != 0 ? Check.Ok : Check.Warn, "Headset",
                    m_Status.runtimeReachable != 0 ? "connected" : "not connected – start WiVRn and open the WiVRn app on the Quest");
            }
            if (InFlatpak)
                Add(Check.Info, "Flatpak", "Editor runs in the Unity Hub Flatpak – WiVRn socket: " + LinuxXRIssueReport.WiVRnSocketState());
        }

        void Add(Check s, string label, string value) => m_Setup.Add(new Item { state = s, label = label, value = value });

        static bool InFlatpak => File.Exists("/.flatpak-info");

        static bool LinuxXRLoaderHasDescriptors()
        {
            var displays = new List<UnityEngine.XR.XRDisplaySubsystemDescriptor>();
            SubsystemManager.GetSubsystemDescriptors(displays);
            return displays.Exists(d => d.id == LinuxXRLoader.DisplayId);
        }

        /// <summary>Human-readable statistics, shared with the diagnostic report.</summary>
        internal static List<KeyValuePair<string, string>> StatsLines(LinuxXRNative.Stats s)
        {
            var l = new List<KeyValuePair<string, string>>();
            void Line(string k, string v) => l.Add(new KeyValuePair<string, string>(k, v));
            float displayHz = s.displayPeriodMs > 0 ? 1000f / s.displayPeriodMs : 0f;
            Line("Headset frame rate", s.headsetFps > 0 ? $"{s.headsetFps:0.0} fps" + (displayHz > 0 ? $" of {displayHz:0} Hz" : "") : "-");
            Line("Unity frame rate", s.appFps > 0 ? $"{s.appFps:0.0} fps" : "-");
            Line("Wait for headset", $"{s.waitFrameMs:0.00} ms (xrWaitFrame)");
            Line("Copy + submit", $"{s.submitMs:0.00} ms (CPU)");
            ulong total = Math.Max(1, s.framesBegun);
            Line("Frames", $"{s.framesBegun} begun, {s.framesRendered} shown, {s.framesEmpty} empty");
            Line("Missed display frames", $"{s.framesMissed} ({100.0 * s.framesMissed / (s.framesBegun + s.framesMissed + 1e-9):0.0} %)");
            Line("Dropped by Unity", $"{s.framesDropped} ({100.0 * s.framesDropped / total:0.0} %)");
            if (s.framesNoRender > 0) Line("Headset idle (no render)", s.framesNoRender.ToString());
            Line("Eye resolution", s.eyeWidth > 0
                ? $"{s.eyeWidth} × {s.eyeHeight}, {(s.singlePass != 0 ? "single-pass instanced" : "multi-pass")}, scale {s.renderScale.ToString("0.00", CultureInfo.InvariantCulture)}"
                : "-");
            Line("Vulkan device", s.vulkanChecked == 0 ? "checked when Play starts"
                : s.vulkanComplete != 0 ? "all required extensions" : "extensions missing – see diagnostics");
            Line("Session time", s.sessionActive != 0 ? TimeSpan.FromSeconds(s.sessionSeconds).ToString(@"hh\:mm\:ss") : "no session");
            Line("Log", $"{s.logWarnings} warnings, {s.logErrors} errors");
            return l;
        }

        // ---- GUI ----------------------------------------------------------------------------

        void InitStyles()
        {
            if (m_Header != null) return;
            m_Header = new GUIStyle(EditorStyles.boldLabel) { fontSize = 15 };
            m_Section = new GUIStyle(EditorStyles.boldLabel) { margin = new RectOffset(4, 4, 10, 2) };
            m_Wrap = new GUIStyle(EditorStyles.label) { wordWrap = true, richText = true };
        }

        void OnGUI()
        {
            InitStyles();
            m_Scroll = EditorGUILayout.BeginScrollView(m_Scroll);

            EditorGUILayout.LabelField(m_Version != null ? "LinuxXR " + m_Version : "LinuxXR", m_Header);
            EditorGUILayout.LabelField(Summary(), m_Wrap);

            GUILayout.Label("Setup", m_Section);
            foreach (var it in m_Setup) DrawItem(it);

            GUILayout.Label("Session", m_Section);
            if (m_Version == null)
            {
                EditorGUILayout.LabelField("–");
            }
            else
            {
                DrawKV("State", LinuxXRNative.SessionStateName(m_Status.sessionState) +
                                (m_Status.refreshRate > 0 ? $" · {m_Status.refreshRate:0} Hz" : ""));
                DrawKV("Subsystems", $"Display {StateName(m_Status.displayState)}, Input {StateName(m_Status.inputState)}");
            }

            GUILayout.Label(m_HasStats && m_Stats.sessionActive != 0 ? "Statistics (live)" : "Statistics (last session)", m_Section);
            if (!m_HasStats)
                EditorGUILayout.LabelField(m_Version == null ? "–" : "Not available – libLinuxXR.so is outdated, rebuild it.", m_Wrap);
            else if (m_Stats.framesBegun == 0)
                EditorGUILayout.LabelField("No frames yet – press Play with the headset connected.", m_Wrap);
            else
                foreach (var kv in StatsLines(m_Stats)) DrawKV(kv.Key, kv.Value);

            GUILayout.Label("Actions", m_Section);
            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("Start WiVRn")) StartWiVRn();
                using (new EditorGUI.DisabledScope(SystemInfo.graphicsDeviceType == GraphicsDeviceType.Vulkan))
                    if (GUILayout.Button("Use Vulkan")) SetVulkan();
                if (GUILayout.Button("Setup wizard")) LinuxXRSetupWizard.Open();
            }
            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("Open log")) OpenLog();
                if (GUILayout.Button("Copy diagnostics"))
                    EditorGUIUtility.systemCopyBuffer = m_Version != null
                        ? LinuxXRIssueReport.Anonymize(LinuxXRNative.GetDiagnostics()) : "libLinuxXR.so not loaded";
            }
            if (GUILayout.Button(new GUIContent("Create issue report",
                    "Collects system, Unity, settings, status, statistics and the recent native log as Markdown, " +
                    "copies it to the clipboard and saves it under Logs/."), GUILayout.Height(26)))
            {
                CreateReport();
            }
            if (m_LastReport != null)
            {
                EditorGUILayout.HelpBox("Report copied to the clipboard and saved:\n" + m_LastReport +
                                        "\nPaste it into a new GitHub/GitLab issue. Check it for private data first.", MessageType.Info);
                using (new EditorGUILayout.HorizontalScope())
                {
                    if (GUILayout.Button("Open report")) EditorUtility.OpenWithDefaultApp(m_LastReport);
                    if (GUILayout.Button("Show in folder")) EditorUtility.RevealInFinder(m_LastReport);
                }
            }

            bool show = EditorGUILayout.Foldout(m_ShowDiagnostics, "Diagnostics", true);
            if (show != m_ShowDiagnostics)
            {
                m_ShowDiagnostics = show;
                Refresh();
            }
            if (m_ShowDiagnostics)
                EditorGUILayout.SelectableLabel(LinuxXRIssueReport.Anonymize(m_Diagnostics), EditorStyles.textArea,
                    GUILayout.MinHeight(160), GUILayout.ExpandHeight(true));

            EditorGUILayout.EndScrollView();
        }

        string Summary()
        {
            int fails = m_Setup.Count(i => i.state == Check.Fail);
            if (fails > 0) return $"<color=#e06c6c>{fails} setup problem{(fails == 1 ? "" : "s")} – see below.</color>";
            if (m_HasStats && m_Stats.sessionActive != 0) return "<color=#6cc070>Streaming to the headset.</color>";
            if (m_Status.runtimeReachable != 0) return "<color=#6cc070>Ready – press Play.</color>";
            return "<color=#d8b04c>Ready once the headset is connected through WiVRn.</color>";
        }

        static string StateName(int s) => s == 2 ? "running" : s == 1 ? "initialized" : "off";

        void DrawItem(Item it)
        {
            using (new EditorGUILayout.HorizontalScope())
            {
                string icon = it.state == Check.Ok ? "TestPassed" : it.state == Check.Warn ? "console.warnicon.sml"
                    : it.state == Check.Fail ? "TestFailed" : "console.infoicon.sml";
                GUILayout.Label(EditorGUIUtility.IconContent(icon), GUILayout.Width(18), GUILayout.Height(18));
                GUILayout.Label(it.label, EditorStyles.boldLabel, GUILayout.Width(110));
                GUILayout.Label(it.value, m_Wrap);
            }
        }

        void DrawKV(string key, string value)
        {
            using (new EditorGUILayout.HorizontalScope())
            {
                GUILayout.Space(22);
                GUILayout.Label(key, GUILayout.Width(150));
                GUILayout.Label(value, m_Wrap);
            }
        }

        // ---- actions ------------------------------------------------------------------------

        void CreateReport()
        {
            try
            {
                m_LastReport = LinuxXRIssueReport.CreateAndSave();
                Debug.Log("[LinuxXR] Diagnostic report saved to " + m_LastReport + " and copied to the clipboard.");
            }
            catch (Exception e)
            {
                Debug.LogError("[LinuxXR] Could not create the report: " + e);
            }
        }

        static void StartWiVRn() => LinuxXRHost.StartWiVRn();

        static void SetVulkan()
        {
            PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.StandaloneLinux64, false);
            PlayerSettings.SetGraphicsAPIs(BuildTarget.StandaloneLinux64,
                new[] { GraphicsDeviceType.Vulkan, GraphicsDeviceType.OpenGLCore });
            if (EditorUtility.DisplayDialog("LinuxXR",
                    "Vulkan is now the first graphics API for Linux. The Editor has to restart to use it.",
                    "Restart now", "Later"))
            {
                EditorApplication.OpenProject(Directory.GetCurrentDirectory());
            }
        }

        static void OpenLog()
        {
            if (File.Exists(LogPath)) EditorUtility.OpenWithDefaultApp(LogPath);
            else Debug.Log("[LinuxXR] No log file yet: " + LogPath);
        }
    }
}
