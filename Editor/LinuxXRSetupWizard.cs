// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;
using UnityEditor;
using UnityEditor.XR.Management;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.XR.Management;
using PackageInfo = UnityEditor.PackageManager.PackageInfo;
using PackageSource = UnityEditor.PackageManager.PackageSource;

namespace Tyroonis.XR.LinuxXR.Editor
{
    /// <summary>
    /// Window → XR → LinuxXR Setup. Opens automatically the first time LinuxXR is found in a project
    /// (Linux Editor only) and fixes everything that can be fixed from inside Unity with one click:
    /// Vulkan, XR Plug-in Management, Flatpak access to WiVRn, WiVRn itself, Editor restart.
    /// </summary>
    public class LinuxXRSetupWizard : EditorWindow
    {
        const string XRSettingsKey = "com.unity.xr.management.loader_settings";
        const string PrefAutoOpen = "LinuxXR.Setup.AutoOpen";
        const string SessionShown = "LinuxXR.Setup.ShownThisSession";

        enum State { Ok, Warn, Fail, Info, Pending }

        class Step
        {
            public State state;
            public string title, detail;
            public string fixLabel;
            public Action fix;
            public string fix2Label;
            public Action fix2;
            public bool required = true;
        }

        readonly List<Step> m_Steps = new List<Step>();
        Vector2 m_Scroll;
        double m_NextRefresh;
        GUIStyle m_Wrap, m_Title, m_Header;

        // Background job (WiVRn install, etc.)
        readonly StringBuilder m_JobLog = new StringBuilder();
        readonly object m_JobLock = new object();
        string m_JobName;
        int? m_JobExit;
        bool m_JobRunning;

        // Cached host checks (spawning processes is slow - refresh only on demand)
        bool? m_WiVRnInstalled;
        bool m_FlatpakOverrideApplied;

        [MenuItem("Window/XR/LinuxXR Setup")]
        public static void Open()
        {
            var w = GetWindow<LinuxXRSetupWizard>(true, "LinuxXR Setup");
            w.minSize = new Vector2(520, 520);
            w.Refresh(true);
        }

        // ---- automatic opening -----------------------------------------------------------------

        [InitializeOnLoadMethod]
        static void AutoOpen()
        {
            if (Application.platform != RuntimePlatform.LinuxEditor) return;
            if (Application.isBatchMode) return;
            if (SessionState.GetBool(SessionShown, false)) return;
            if (!EditorPrefs.GetBool(PrefAutoOpen + "." + ProjectKey, true)) return;
            EditorApplication.delayCall += () =>
            {
                if (EditorApplication.isPlayingOrWillChangePlaymode) return;
                SessionState.SetBool(SessionShown, true);
                if (!AllRequiredOk()) Open();
            };
        }

        // string.GetHashCode() is not guaranteed to be stable across processes - use the project's GUID.
        static string ProjectKey => PlayerSettings.productGUID.ToString("N");

        /// <summary>Quick check without host processes (used for auto-open and by the status window).</summary>
        public static bool AllRequiredOk()
        {
            return SystemInfo.graphicsDeviceType == GraphicsDeviceType.Vulkan
                   && LinuxXRNative.IsAvailable
                   && LinuxXRLoader.HasDescriptors()
                   && IsLoaderActive(BuildTargetGroup.Standalone)
                   && InitOnStart(BuildTargetGroup.Standalone)
                   && (!LinuxXRHost.InFlatpak || LinuxXRHost.WiVRnFolderVisible || HasWiVRnRuntime());
        }

        // ---- lifecycle --------------------------------------------------------------------------

        void OnEnable()
        {
            EditorApplication.update += Tick;
            Refresh(true);
        }

        void OnDisable() => EditorApplication.update -= Tick;

        void Tick()
        {
            if (m_JobRunning || EditorApplication.timeSinceStartup >= m_NextRefresh)
            {
                if (!m_JobRunning) Refresh(false);
                Repaint();
            }
        }

        void Refresh(bool hostChecks, bool force = false)
        {
            m_NextRefresh = EditorApplication.timeSinceStartup + 1.0;
            if (hostChecks || m_WiVRnInstalled == null)
                m_WiVRnInstalled = Application.platform == RuntimePlatform.LinuxEditor && LinuxXRHost.IsWiVRnInstalled(force);
            BuildSteps();
        }

        // ---- checks -----------------------------------------------------------------------------

        void BuildSteps()
        {
            m_Steps.Clear();
            if (Application.platform != RuntimePlatform.LinuxEditor)
            {
                Add(State.Info, "Linux Editor only",
                    "LinuxXR streams Play Mode from the Linux Editor. On this platform nothing needs to be set up - " +
                    "Android builds keep using Unity OpenXR.").required = false;
                return;
            }

            // 1. Native plugin + subsystems
            string version = LinuxXRNative.GetVersion();
            bool descriptors = LinuxXRLoader.HasDescriptors();
            if (!LinuxXRSubsystemRegistration.IsInstalled)
            {
                var s = Add(State.Fail, "Native plugin",
                    "The native plug-in and XR subsystem registration are missing or outdated in " +
                    LinuxXRSubsystemRegistration.Folder + ".");
                s.fixLabel = "Install";
                s.fix = () => { LinuxXRSubsystemRegistration.Install(); };
            }
            else if (version == null && !LinuxXRSubsystemRegistration.RestartPending && descriptors)
            {
                Add(State.Fail, "Native plugin",
                    "libLinuxXR.so could not be loaded. Your Linux may be too old (glibc 2.35 / Ubuntu 22.04 or newer " +
                    "is required) - create an issue report from the status window.");
            }
            else if (version == null || !descriptors || LinuxXRSubsystemRegistration.RestartPending)
            {
                var s = Add(State.Fail, "Subsystems",
                    "Installed in " + LinuxXRSubsystemRegistration.Folder +
                    ". Unity loads XR plug-ins only while the Editor starts - restart the Editor once.");
                s.fixLabel = "Restart Editor";
                s.fix = LinuxXRHost.RestartEditor;
            }
            else
            {
                Add(State.Ok, "Native plugin", $"libLinuxXR.so {version} loaded, subsystems registered");
            }

            // 2. Vulkan
            var gfx = SystemInfo.graphicsDeviceType;
            var apis = PlayerSettings.GetGraphicsAPIs(BuildTarget.StandaloneLinux64);
            bool vulkanFirst = apis.Length > 0 && apis[0] == GraphicsDeviceType.Vulkan;
            if (gfx == GraphicsDeviceType.Vulkan)
            {
                Add(State.Ok, "Graphics API", "The Editor runs with Vulkan");
            }
            else if (vulkanFirst)
            {
                var s = Add(State.Fail, "Graphics API", $"Vulkan is selected, but the Editor still runs with {gfx}. Restart the Editor.");
                s.fixLabel = "Restart Editor";
                s.fix = LinuxXRHost.RestartEditor;
            }
            else
            {
                var s = Add(State.Fail, "Graphics API", $"The Editor runs with {gfx}. LinuxXR needs Vulkan (Player Settings → Linux → Graphics APIs).");
                s.fixLabel = "Use Vulkan + restart";
                s.fix = UseVulkan;
            }

            // 3. XR Plug-in Management
            bool active = IsLoaderActive(BuildTargetGroup.Standalone);
            {
                var s = Add(active ? State.Ok : State.Fail, "XR Plug-in Management",
                    active ? "LinuxXR is enabled on the 'Windows, Mac, Linux' tab"
                           : "Enable LinuxXR on the 'Windows, Mac, Linux' tab");
                if (!active) { s.fixLabel = "Enable LinuxXR"; s.fix = EnableLoader; }
            }
            var others = OtherLoaders(BuildTargetGroup.Standalone);
            if (others.Count > 0)
            {
                var s = Add(State.Warn, "Other XR loaders",
                    "Also enabled on 'Windows, Mac, Linux': " + string.Join(", ", others.Select(l => l.GetType().Name)) +
                    ". Unity uses the first one that starts; OpenXR cannot run in the Linux Editor. The Android tab is not touched.");
                s.fixLabel = "Disable them on this tab";
                s.fix = RemoveOtherLoaders;
                s.required = false;
            }
            bool initOnStart = InitOnStart(BuildTargetGroup.Standalone);
            if (!initOnStart)
            {
                var s = Add(State.Fail, "Initialize XR on Startup", "Must be on for the 'Windows, Mac, Linux' tab, otherwise Play Mode starts without XR.");
                s.fixLabel = "Turn on";
                s.fix = () => SetInitOnStart(BuildTargetGroup.Standalone, true);
            }

            // 4. Flatpak sandbox (Unity Hub as Flatpak)
            if (LinuxXRHost.InFlatpak)
            {
                string app = LinuxXRHost.FlatpakAppId ?? "com.unity.UnityHub";
                bool ok = LinuxXRHost.WiVRnFolderVisible && HasWiVRnRuntime();
                if (ok)
                {
                    Add(State.Ok, "Flatpak access", $"The Editor runs inside the {app} Flatpak and can reach WiVRn");
                }
                else if (m_FlatpakOverrideApplied)
                {
                    var s = Add(State.Fail, "Flatpak access",
                        "Access was granted. It takes effect after Unity AND Unity Hub were closed completely (also from the tray) " +
                        "and started again.");
                    s.fixLabel = "Close Unity Hub + Editor now";
                    s.fix = () => QuitFlatpak(app);
                }
                else
                {
                    var s = Add(State.Fail, "Flatpak access",
                        $"The Editor runs inside the {app} Flatpak and cannot see WiVRn yet " +
                        (LinuxXRHost.WiVRnFolderVisible ? "(runtime library not visible)." : "(socket folder not visible).") +
                        " LinuxXR can allow it (only for your user, only the WiVRn folders, read-only for the runtime):\n" +
                        LinuxXRHost.FlatpakOverrideCommand(app));
                    s.fixLabel = "Allow access to WiVRn";
                    s.fix = () => GrantFlatpakAccess(app);
                }
            }

            // 5. WiVRn
            if (m_WiVRnInstalled == true || HasWiVRnRuntime())
            {
                if (LinuxXRHost.WiVRnSocketPresent)
                {
                    Add(State.Ok, "WiVRn", "Running, headset connected - press Play").required = false;
                }
                else
                {
                    var s = Add(State.Warn, "WiVRn",
                        "Installed. Start the WiVRn server, then open the WiVRn app on the Quest (same version as the PC) " +
                        "and connect. The first time, confirm the pairing PIN.");
                    s.fixLabel = "Start WiVRn";
                    s.fix = LinuxXRHost.StartWiVRn;
                    s.fix2Label = "Quest app";
                    s.fix2 = () => Application.OpenURL(LinuxXRHost.WiVRnQuestUrl);
                    s.required = false;
                }
            }
            else
            {
                var s = Add(State.Fail, "WiVRn",
                    "WiVRn (the OpenXR streaming server for the Quest) is not installed. Install the Flatpak from Flathub - " +
                    "no other system changes needed.");
                s.fixLabel = "Install WiVRn (Flatpak)";
                s.fix = InstallWiVRn;
                s.fix2Label = "Flathub page";
                s.fix2 = () => Application.OpenURL(LinuxXRHost.WiVRnFlathubUrl);
            }

            // 6. Recommendations
            if (PlayerSettings.colorSpace != ColorSpace.Linear)
            {
                var s = Add(State.Info, "Color space", "Linear is recommended for VR (Gamma looks washed out on the headset).");
                s.fixLabel = "Use Linear";
                s.fix = () => PlayerSettings.colorSpace = ColorSpace.Linear;
                s.required = false;
            }
            var settings = LinuxXRSettings.GetSettings();
            if (settings != null && settings.renderScale > 0.85f)
            {
                var s = Add(State.Info, "Render scale",
                    $"Render scale is {settings.renderScale:0.00}. WiVRn asks for ~2500×2600 pixels per eye on a Quest 3; " +
                    "0.7 keeps the Editor at the headset's frame rate on most GPUs.");
                s.fixLabel = "Set 0.7";
                s.fix = () =>
                {
                    settings.renderScale = 0.7f;
                    EditorUtility.SetDirty(settings);
                    AssetDatabase.SaveAssets();
                    settings.WriteNativeSettingsFile();
                };
                s.required = false;
            }
        }

        Step Add(State s, string title, string detail)
        {
            var step = new Step { state = s, title = title, detail = detail };
            m_Steps.Add(step);
            return step;
        }

        static bool HasWiVRnRuntime()
        {
            if (!LinuxXRNative.IsAvailable) return false;
            return LinuxXRNative.ListRuntimes().Split('\n').Any(l => l.StartsWith("WiVRn\t"));
        }

        // ---- XR Plug-in Management --------------------------------------------------------------

        static XRGeneralSettingsPerBuildTarget PerTargetSettings(bool create)
        {
            EditorBuildSettings.TryGetConfigObject(XRSettingsKey, out XRGeneralSettingsPerBuildTarget s);
            if (s != null || !create) return s;
            // XRGeneralSettingsPerBuildTarget.GetOrCreate() is internal - call it via reflection.
            var m = typeof(XRGeneralSettingsPerBuildTarget).GetMethod("GetOrCreate", BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public);
            if (m != null && m.GetParameters().Length == 0) return m.Invoke(null, null) as XRGeneralSettingsPerBuildTarget;
            SettingsService.OpenProjectSettings("Project/XR Plug-in Management");
            EditorBuildSettings.TryGetConfigObject(XRSettingsKey, out s);
            return s;
        }

        static XRManagerSettings Manager(BuildTargetGroup group, bool create)
        {
            var per = PerTargetSettings(create);
            if (per == null) return null;
            if (create && !per.HasManagerSettingsForBuildTarget(group))
            {
                per.CreateDefaultManagerSettingsForBuildTarget(group);
                EditorUtility.SetDirty(per);
            }
            var gs = per.SettingsForBuildTarget(group);
            return gs != null ? gs.Manager : null;
        }

        public static bool IsLoaderActive(BuildTargetGroup group)
        {
            var m = Manager(group, false);
            return m != null && m.activeLoaders.Any(l => l is LinuxXRLoader);
        }

        static List<XRLoader> OtherLoaders(BuildTargetGroup group)
        {
            var m = Manager(group, false);
            return m == null ? new List<XRLoader>() : m.activeLoaders.Where(l => l != null && !(l is LinuxXRLoader)).ToList();
        }

        static bool InitOnStart(BuildTargetGroup group)
        {
            var per = PerTargetSettings(false);
            var gs = per != null ? per.SettingsForBuildTarget(group) : null;
            return gs == null || gs.InitManagerOnStart;  // no settings yet -> reported by the loader step
        }

        static void SetInitOnStart(BuildTargetGroup group, bool on)
        {
            var per = PerTargetSettings(true);
            var gs = per != null ? per.SettingsForBuildTarget(group) : null;
            if (gs == null) return;
            gs.InitManagerOnStart = on;
            EditorUtility.SetDirty(gs);
            AssetDatabase.SaveAssets();
        }

        static void EnableLoader()
        {
            if (EditorApplication.isPlayingOrWillChangePlaymode)
            {
                Debug.LogWarning("[LinuxXR] Exit Play Mode first.");
                return;
            }
            var manager = Manager(BuildTargetGroup.Standalone, true);
            if (manager == null)
            {
                Debug.LogError("[LinuxXR] XR Plug-in Management settings could not be created - open Project Settings → XR Plug-in Management once.");
                return;
            }
            if (!AssignLoader(manager, BuildTargetGroup.Standalone))
                Debug.LogError("[LinuxXR] Could not enable the LinuxXR loader - enable it in Project Settings → XR Plug-in Management.");
            SetInitOnStart(BuildTargetGroup.Standalone, true);
            AssetDatabase.SaveAssets();
        }

        static void RemoveOtherLoaders()
        {
            var manager = Manager(BuildTargetGroup.Standalone, false);
            if (manager == null) return;
            foreach (var l in OtherLoaders(BuildTargetGroup.Standalone))
            {
                if (!InvokeMetadataStore("RemoveLoader", manager, l.GetType().FullName, BuildTargetGroup.Standalone))
                    manager.TryRemoveLoader(l);
            }
            EditorUtility.SetDirty(manager);
            AssetDatabase.SaveAssets();
        }

        // XRPackageMetadataStore is not visible to package code in every XR Plug-in Management version,
        // so it is called via reflection; the fallback uses the public XRManagerSettings API.
        static bool InvokeMetadataStore(string method, XRManagerSettings manager, string loaderType, BuildTargetGroup group)
        {
            try
            {
                var store = AppDomain.CurrentDomain.GetAssemblies()
                    .Where(a => a.GetName().Name == "Unity.XR.Management.Editor")
                    .Select(a => a.GetType("UnityEditor.XR.Management.Metadata.XRPackageMetadataStore", false))
                    .FirstOrDefault(t => t != null);
                var m = store?.GetMethod(method, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static,
                    null, new[] { typeof(XRManagerSettings), typeof(string), typeof(BuildTargetGroup) }, null);
                if (m == null) return false;
                return m.Invoke(null, new object[] { manager, loaderType, group }) is bool ok && ok;
            }
            catch (Exception e)
            {
                Debug.LogWarning($"[LinuxXR] XRPackageMetadataStore.{method} failed: {e.GetBaseException().Message}");
                return false;
            }
        }

        static bool AssignLoader(XRManagerSettings manager, BuildTargetGroup group)
        {
            if (InvokeMetadataStore("AssignLoader", manager, typeof(LinuxXRLoader).FullName, group)) return true;
            if (manager.activeLoaders.Any(l => l is LinuxXRLoader)) return true;

            // Fallback: find or create the loader asset and add it directly.
            LinuxXRLoader loader = AssetDatabase.FindAssets("t:" + nameof(LinuxXRLoader))
                .Select(g => AssetDatabase.LoadAssetAtPath<LinuxXRLoader>(AssetDatabase.GUIDToAssetPath(g)))
                .FirstOrDefault(l => l != null);
            if (loader == null)
            {
                const string dir = "Assets/XR/Loaders";
                if (!AssetDatabase.IsValidFolder("Assets/XR")) AssetDatabase.CreateFolder("Assets", "XR");
                if (!AssetDatabase.IsValidFolder(dir)) AssetDatabase.CreateFolder("Assets/XR", "Loaders");
                loader = ScriptableObject.CreateInstance<LinuxXRLoader>();
                AssetDatabase.CreateAsset(loader, dir + "/LinuxXRLoader.asset");
            }
            bool added = manager.TryAddLoader(loader);
            EditorUtility.SetDirty(manager);
            return added;
        }

        static void UseVulkan()
        {
            PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.StandaloneLinux64, false);
            var apis = new List<GraphicsDeviceType> { GraphicsDeviceType.Vulkan };
            apis.AddRange(PlayerSettings.GetGraphicsAPIs(BuildTarget.StandaloneLinux64).Where(a => a != GraphicsDeviceType.Vulkan));
            PlayerSettings.SetGraphicsAPIs(BuildTarget.StandaloneLinux64, apis.ToArray());
            AssetDatabase.SaveAssets();
            if (EditorUtility.DisplayDialog("LinuxXR", "Vulkan is now the first graphics API for Linux. The Editor has to restart to use it.",
                    "Restart now", "Later"))
                LinuxXRHost.RestartEditor();
        }

        // ---- host actions -----------------------------------------------------------------------

        void GrantFlatpakAccess(string app)
        {
            var r = LinuxXRHost.RunOnHost("flatpak", LinuxXRHost.FlatpakOverrideArgs(app));
            if (!r.Ok)
            {
                Debug.LogError("[LinuxXR] Could not set the Flatpak override (" + r.output + "). Run this in a terminal:\n" +
                               LinuxXRHost.FlatpakOverrideCommand(app));
                return;
            }
            m_FlatpakOverrideApplied = true;
            Debug.Log("[LinuxXR] Flatpak access to WiVRn granted for " + app + ". Close Unity and Unity Hub completely and start them again.");
            BuildSteps();
        }

        static void QuitFlatpak(string app)
        {
            if (!EditorUtility.DisplayDialog("LinuxXR",
                    "Unity Hub and all Unity Editors will be closed (unsaved changes are offered for saving first). " +
                    "Start Unity Hub again afterwards.", "Close now", "Cancel"))
                return;
            if (!UnityEditor.SceneManagement.EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return;
            AssetDatabase.SaveAssets();
            LinuxXRHost.LaunchOnHost("flatpak", "kill", app);
        }

        void InstallWiVRn()
        {
            if (m_JobRunning) return;
            // Per-user install: no root password, and the files live in the home folder where a sandboxed
            // Editor can be given read access.
            const string script =
                "flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo && " +
                "flatpak install -y --noninteractive --user flathub " + LinuxXRHost.WiVRnFlatpak;
            StartJob("Installing WiVRn from Flathub (this can take a few minutes) …", "sh", new[] { "-c", script },
                exit =>
                {
                    m_WiVRnInstalled = null;  // re-check
                    if (exit == 0 && LinuxXRHost.InFlatpak)
                        GrantFlatpakAccess(LinuxXRHost.FlatpakAppId ?? "com.unity.UnityHub");
                });
        }

        void StartJob(string name, string file, string[] args, Action<int> done)
        {
            lock (m_JobLock)
            {
                m_JobLog.Clear();
                m_JobLog.AppendLine("$ " + file + " " + string.Join(" ", args));
            }
            m_JobName = name;
            m_JobExit = null;
            m_JobRunning = true;
            LinuxXRHost.RunOnHostAsync(file, args,
                line => { lock (m_JobLock) m_JobLog.AppendLine(line); },
                exit =>
                {
                    EditorApplication.delayCall += () =>
                    {
                        if (this == null) return;  // window was closed while the job ran
                        m_JobRunning = false;
                        m_JobExit = exit;
                        done?.Invoke(exit);
                        Refresh(true, true);
                        Repaint();
                    };
                });
        }

        // ---- GUI --------------------------------------------------------------------------------

        void OnGUI()
        {
            if (m_Wrap == null)
            {
                m_Wrap = new GUIStyle(EditorStyles.label) { wordWrap = true, richText = true };
                m_Title = new GUIStyle(EditorStyles.boldLabel) { fontSize = 16 };
                m_Header = new GUIStyle(EditorStyles.boldLabel) { fontSize = 12 };
            }

            EditorGUILayout.LabelField("LinuxXR Setup", m_Title);
            EditorGUILayout.LabelField(
                "Press Play in the Linux Editor and play on a Meta Quest through WiVRn. Fix the red items, " +
                "then connect the headset and press Play.", m_Wrap);
            EditorGUILayout.Space(6);

            int fails = m_Steps.Count(s => s.required && s.state == State.Fail);
            EditorGUILayout.HelpBox(fails == 0 ? "Everything required is set up." : $"{fails} item{(fails == 1 ? "" : "s")} left.",
                fails == 0 ? MessageType.Info : MessageType.Warning);

            m_Scroll = EditorGUILayout.BeginScrollView(m_Scroll);
            foreach (var s in m_Steps) DrawStep(s);

            if (m_JobName != null)
            {
                EditorGUILayout.Space(8);
                EditorGUILayout.LabelField(m_JobRunning ? m_JobName : $"{m_JobName} – finished (exit {m_JobExit})", m_Header);
                string log;
                lock (m_JobLock) log = m_JobLog.ToString();
                EditorGUILayout.SelectableLabel(log, EditorStyles.textArea, GUILayout.MinHeight(90));
            }
            EditorGUILayout.EndScrollView();

            EditorGUILayout.Space(4);
            using (new EditorGUILayout.HorizontalScope())
            {
                bool auto = EditorPrefs.GetBool(PrefAutoOpen + "." + ProjectKey, true);
                bool newAuto = EditorGUILayout.ToggleLeft("Open automatically when something is missing", auto);
                if (newAuto != auto) EditorPrefs.SetBool(PrefAutoOpen + "." + ProjectKey, newAuto);
                GUILayout.FlexibleSpace();
                if (GUILayout.Button("Re-check", GUILayout.Width(80))) Refresh(true, true);
                if (GUILayout.Button("Status window", GUILayout.Width(110))) LinuxXRStatusWindow.Open();
            }
        }

        void DrawStep(Step s)
        {
            using (new EditorGUILayout.VerticalScope(EditorStyles.helpBox))
            {
                using (new EditorGUILayout.HorizontalScope())
                {
                    string icon = s.state == State.Ok ? "TestPassed" : s.state == State.Fail ? (s.required ? "TestFailed" : "console.warnicon.sml")
                        : s.state == State.Warn ? "console.warnicon.sml" : "console.infoicon.sml";
                    GUILayout.Label(EditorGUIUtility.IconContent(icon), GUILayout.Width(18), GUILayout.Height(18));
                    GUILayout.Label(s.title, m_Header);
                    GUILayout.FlexibleSpace();
                    using (new EditorGUI.DisabledScope(m_JobRunning))
                    {
                        if (s.fix2 != null && GUILayout.Button(s.fix2Label, GUILayout.ExpandWidth(false))) RunFix(s.fix2);
                        if (s.fix != null && GUILayout.Button(s.fixLabel, GUILayout.ExpandWidth(false))) RunFix(s.fix);
                    }
                }
                EditorGUILayout.LabelField(s.detail, m_Wrap);
            }
        }

        void RunFix(Action a)
        {
            try { a(); }
            catch (Exception e) { Debug.LogError("[LinuxXR] Setup step failed: " + e); }
            Refresh(false);
            GUIUtility.ExitGUI();
        }
    }
}
