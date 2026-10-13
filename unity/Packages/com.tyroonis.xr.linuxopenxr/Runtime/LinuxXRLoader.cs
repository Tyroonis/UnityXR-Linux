// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.XR;
using UnityEngine.XR.Management;

namespace Tyroonis.XR.LinuxXR
{
    /// <summary>
    /// XR loader for the Linux Editor. Enable it in Project Settings → XR Plug-in Management on the
    /// "Windows, Mac, Linux" (Standalone) tab. The Android tab keeps Unity OpenXR for Quest builds,
    /// so the game code stays the same.
    /// </summary>
    public class LinuxXRLoader : XRLoaderHelper
    {
        public const string DisplayId = "LinuxXR Display";
        public const string InputId = "LinuxXR Input";
        public const string NativeLibrary = "LinuxXR";

        static readonly List<XRDisplaySubsystemDescriptor> s_DisplayDescriptors = new List<XRDisplaySubsystemDescriptor>();
        static readonly List<XRInputSubsystemDescriptor> s_InputDescriptors = new List<XRInputSubsystemDescriptor>();

        public XRDisplaySubsystem displaySubsystem => GetLoadedSubsystem<XRDisplaySubsystem>();
        public XRInputSubsystem inputSubsystem => GetLoadedSubsystem<XRInputSubsystem>();

        public override bool Initialize()
        {
            if (Application.platform != RuntimePlatform.LinuxEditor)
            {
                Debug.LogWarning("[LinuxXR] Only available in the Linux Editor - loader skipped.");
                return false;
            }
            if (!LinuxXRNative.IsAvailable)
            {
                Debug.LogError("[LinuxXR] libLinuxXR.so could not be loaded. Install the LinuxXR package " +
                               "(see docs/INSTALL.md) and restart the Editor.");
                return false;
            }

            var settings = LinuxXRSettings.GetSettings();
            if (settings != null)
            {
                settings.WriteNativeSettingsFile();
                settings.ApplyToNative();
            }

            if (SystemInfo.graphicsDeviceType != UnityEngine.Rendering.GraphicsDeviceType.Vulkan)
            {
                Debug.LogError("[LinuxXR] The Editor runs with " + SystemInfo.graphicsDeviceType +
                               ", LinuxXR needs Vulkan. Window → XR → LinuxXR Status → 'Use Vulkan', then restart the Editor.");
                return false;
            }

            // Unity reads UnitySubsystemsManifest.json only at Editor start. LinuxXR writes it to
            // Assets/LinuxXR on first load (Editor/LinuxXRSubsystemRegistration.cs).
            if (!HasDescriptors())
            {
                Debug.LogError("[LinuxXR] Unity does not know the LinuxXR subsystems yet. They are registered through " +
                               "Assets/LinuxXR/UnitySubsystemsManifest.json, which Unity reads only at Editor start - " +
                               "restart the Editor once (Window → XR → LinuxXR Setup). XR displays found: " + ListDisplayDescriptors());
                return false;
            }

            CreateSubsystem<XRDisplaySubsystemDescriptor, XRDisplaySubsystem>(s_DisplayDescriptors, DisplayId);
            CreateSubsystem<XRInputSubsystemDescriptor, XRInputSubsystem>(s_InputDescriptors, InputId);

            if (displaySubsystem == null || inputSubsystem == null)
            {
                Debug.LogError("[LinuxXR] The subsystems could not be created - native initialization failed. " +
                               "See the [LinuxXR] messages above and Window → XR → LinuxXR Status.");
                Deinitialize();
                return false;
            }
            Debug.Log("[LinuxXR] Initialized (version " + LinuxXRNative.GetVersion() + ")");
            return true;
        }

        /// <summary>true if Unity registered the LinuxXR display and input subsystems.</summary>
        public static bool HasDescriptors()
        {
            var displays = new List<XRDisplaySubsystemDescriptor>();
            var inputs = new List<XRInputSubsystemDescriptor>();
            SubsystemManager.GetSubsystemDescriptors(displays);
            SubsystemManager.GetSubsystemDescriptors(inputs);
            return displays.Exists(d => d.id == DisplayId) && inputs.Exists(d => d.id == InputId);
        }

        static string ListDisplayDescriptors()
        {
            var displays = new List<XRDisplaySubsystemDescriptor>();
            SubsystemManager.GetSubsystemDescriptors(displays);
            var names = new List<string>();
            foreach (var d in displays) names.Add(d.id);
            return names.Count == 0 ? "(none)" : string.Join(", ", names);
        }

        public override bool Start()
        {
            StartSubsystem<XRDisplaySubsystem>();
            StartSubsystem<XRInputSubsystem>();
            return true;
        }

        public override bool Stop()
        {
            StopSubsystem<XRInputSubsystem>();
            StopSubsystem<XRDisplaySubsystem>();
            return true;
        }

        public override bool Deinitialize()
        {
            DestroySubsystem<XRInputSubsystem>();
            DestroySubsystem<XRDisplaySubsystem>();
            return base.Deinitialize();
        }

        // No IXRLoaderPreInit: XR PreInit only runs in player builds (boot.config), never in the Editor.
        // Unity's Editor Vulkan device already enables every extension WiVRn/Monado need; the plugin
        // checks this when Play starts and reports missing ones in the status window.
    }
}
