#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tyroonis
"""make-unitypackage.py - build a .unitypackage (Unity "Import Package" / Asset Store format) from a folder.

A .unitypackage is a gzip'ed tar with one directory per asset, named after the asset's GUID:
    <guid>/pathname     project-relative path, e.g. Assets/Tyroonis/LinuxXR/Runtime/LinuxXRLoader.cs
    <guid>/asset.meta   the .meta file
    <guid>/asset        the file content (not for folders)
GUIDs come from the existing .meta files, so the package imports with the same GUIDs as the UPM package.

    make-unitypackage.py <package folder> <target path, e.g. Assets/Tyroonis/LinuxXR> <out.unitypackage>

package.json is left out: inside Assets/ it has no function.
"""
import io
import os
import sys
import tarfile

SKIP_FILES = {"package.json", "package.json.meta"}

# Keep in sync with Editor/LinuxXRSubsystemRegistration.cs
_PD = "Assets/LinuxXR/Plugins/Linux/x86_64/"
NATIVE = [
    (_PD + "libLinuxXR.so", "759136dbc69642408859d85ca4fb0e39", "plugin"),
    (_PD + "libopenxr_loader.so.1", "d260fa2f0d1d3b87cc2ef135ca75284c", "default"),
    (_PD + "LICENSE-openxr-loader.txt", "11902ba1408934be685cb84e792e8b31", "text"),
]
_COMMON = "  externalObjects: {{}}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n"
META = {
    "default": "fileFormatVersion: 2\nguid: {g}\nDefaultImporter:\n" + _COMMON,
    "text": "fileFormatVersion: 2\nguid: {g}\nTextScriptImporter:\n" + _COMMON,
    "plugin": """fileFormatVersion: 2
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
""",
}


def guid_of(meta_path):
    with open(meta_path) as fh:
        for line in fh:
            if line.startswith("guid:"):
                return line.split()[1]
    raise SystemExit(f"no guid in {meta_path}")


def add_bytes(tar, name, data, mode=0o644):
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = mode
    info.mtime = 1767225600  # 2026-01-01, reproducible
    tar.addfile(info, io.BytesIO(data))


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    src, target, out = os.path.abspath(sys.argv[1]), sys.argv[2].rstrip("/"), sys.argv[3]
    if not target.startswith("Assets/"):
        raise SystemExit("target must start with Assets/")

    entries = []  # (rel path, is_dir)
    # Parent folders of the target (Assets/Tyroonis) need entries too
    parts = target.split("/")
    for i in range(2, len(parts) + 1):
        entries.append(("/".join(parts[:i]), None))
    for dirpath, dirnames, filenames in os.walk(src):
        # Folders ending in "~" (Runtime/Native~) are ignored by Unity; their binaries are mapped below.
        dirnames[:] = sorted(d for d in dirnames if not d.endswith("~") and not d.startswith("."))
        rel_dir = os.path.relpath(dirpath, src)
        for d in dirnames:
            entries.append((os.path.normpath(os.path.join(rel_dir, d)), True))
        for f in sorted(filenames):
            if f.endswith(".meta") or f in SKIP_FILES or f.startswith("."):
                continue
            entries.append((os.path.normpath(os.path.join(rel_dir, f)), False))

    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    count = 0
    with tarfile.open(out, "w:gz", format=tarfile.GNU_FORMAT) as tar:
        for rel, is_dir in entries:
            if is_dir is None:
                # synthetic parent folder (e.g. Assets/Tyroonis): GUID derived from the path
                import hashlib
                guid = hashlib.md5(f"unitypackage/{rel}".encode()).hexdigest()
                meta = (f"fileFormatVersion: 2\nguid: {guid}\nfolderAsset: yes\nDefaultImporter:\n"
                        "  externalObjects: {}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n").encode()
                path_in_project = rel
            else:
                full = os.path.join(src, rel)
                meta_path = full + ".meta"
                if not os.path.exists(meta_path):
                    raise SystemExit(f"missing .meta for {rel} - run ensure-metas.py first")
                guid = guid_of(meta_path)
                with open(meta_path, "rb") as fh:
                    meta = fh.read()
                path_in_project = f"{target}/{rel}"
            add_bytes(tar, f"{guid}/pathname", path_in_project.encode())
            add_bytes(tar, f"{guid}/asset.meta", meta)
            if is_dir is False:
                with open(os.path.join(src, rel), "rb") as fh:
                    add_bytes(tar, f"{guid}/asset", fh.read(),
                              0o755 if rel.endswith((".so", ".so.1")) else 0o644)
            count += 1
        # Native plug-in: straight to where the Editor code installs it (Assets/LinuxXR/Plugins/Linux/x86_64),
        # with the same GUIDs/metas as LinuxXRSubsystemRegistration.cs writes.
        native = os.path.join(src, "Runtime", "Native~")
        for rel, guid, kind in NATIVE:
            full = os.path.join(native, os.path.basename(rel))
            if not os.path.exists(full):
                raise SystemExit(f"missing {full}")
            add_bytes(tar, f"{guid}/pathname", rel.encode())
            add_bytes(tar, f"{guid}/asset.meta", META[kind].format(g=guid).encode())
            with open(full, "rb") as fh:
                add_bytes(tar, f"{guid}/asset", fh.read(), 0o755 if ".so" in rel else 0o644)
            count += 1
    print(f"✔ {out} ({count} assets, {os.path.getsize(out) // 1024} KiB) → {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
