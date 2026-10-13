#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tyroonis
"""ensure-metas.py - create missing Unity .meta files for a package folder.

Packages installed through the Package Manager (git URL, tarball, registry) are immutable: Unity
ignores every file without a .meta there. This script gives every file and folder one, with a GUID
derived from the package-relative path (stable across runs and machines). Existing .meta files are
kept (their GUIDs are referenced by assets in user projects). Stray .meta files without a file are
removed. Fails on duplicate GUIDs.

    scripts/ensure-metas.py unity/Packages/com.tyroonis.xr.linuxopenxr [--check]
"""
import hashlib
import os
import sys

PLUGIN_EDITOR_LINUX = """fileFormatVersion: 2
guid: {g}
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
"""
FOLDER = "fileFormatVersion: 2\nguid: {g}\nfolderAsset: yes\nDefaultImporter:\n  externalObjects: {{}}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n"
TEXT = "fileFormatVersion: 2\nguid: {g}\nTextScriptImporter:\n  externalObjects: {{}}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n"
DEFAULT = "fileFormatVersion: 2\nguid: {g}\nDefaultImporter:\n  externalObjects: {{}}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n"
SCRIPT = "fileFormatVersion: 2\nguid: {g}\nMonoImporter:\n  externalObjects: {{}}\n  serializedVersion: 2\n  defaultReferences: []\n  executionOrder: 0\n  icon: {{instanceID: 0}}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n"
ASMDEF = "fileFormatVersion: 2\nguid: {g}\nAssemblyDefinitionImporter:\n  externalObjects: {{}}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n"
PKGJSON = "fileFormatVersion: 2\nguid: {g}\nPackageManifestImporter:\n  externalObjects: {{}}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n"

SKIP_DIRS = {".git"}


def template_for(name, is_dir):
    if is_dir:
        return FOLDER
    if name == "package.json":
        return PKGJSON
    if name.endswith(".so"):
        return PLUGIN_EDITOR_LINUX
    if name.endswith(".cs"):
        return SCRIPT
    if name.endswith(".asmdef"):
        return ASMDEF
    if name.endswith((".json", ".md", ".txt")):
        return TEXT
    return DEFAULT


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    root = os.path.abspath(sys.argv[1])
    check_only = "--check" in sys.argv[2:]
    name = os.path.basename(root)
    created, removed, missing = [], [], []

    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS and not d.startswith(".") and not d.endswith("~"))
        for f in sorted(filenames):
            # Keep metas of build outputs (*.so): the binary is created by the build, its GUID must stay stable.
            if f.endswith(".meta") and not f.endswith(".so.meta") and not os.path.exists(os.path.join(dirpath, f[:-5])):
                if check_only:
                    missing.append("stray " + os.path.relpath(os.path.join(dirpath, f), root))
                else:
                    os.remove(os.path.join(dirpath, f))
                    removed.append(os.path.relpath(os.path.join(dirpath, f), root))
        entries = [(d, True) for d in dirnames] + [
            (f, False) for f in sorted(filenames) if not f.endswith(".meta") and not f.startswith(".")]
        for entry, is_dir in entries:
            path = os.path.join(dirpath, entry)
            meta = path + ".meta"
            if os.path.exists(meta):
                continue
            rel = os.path.relpath(path, root)
            if check_only:
                missing.append(rel)
                continue
            guid = hashlib.md5(f"{name}/{rel}".encode()).hexdigest()
            with open(meta, "w") as fh:
                fh.write(template_for(entry, is_dir).format(g=guid))
            created.append(rel)

    seen = {}
    for dirpath, _, filenames in os.walk(root):
        for f in filenames:
            if not f.endswith(".meta"):
                continue
            with open(os.path.join(dirpath, f)) as fh:
                for line in fh:
                    if line.startswith("guid:"):
                        g = line.split()[1]
                        rel = os.path.relpath(os.path.join(dirpath, f), root)
                        if g in seen:
                            print(f"duplicate GUID {g}: {seen[g]} and {rel}", file=sys.stderr)
                            return 1
                        seen[g] = rel

    if check_only:
        for m in missing:
            print("missing .meta:", m)
        return 1 if missing else 0
    for c in created:
        print("  + " + c + ".meta")
    for r in removed:
        print("  - " + r)
    return 0


if __name__ == "__main__":
    sys.exit(main())
