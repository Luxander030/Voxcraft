#!/usr/bin/env python3
"""Package a built game into dist/Voxcraft-<version>-<platform>.zip.

macOS: the self-contained Voxcraft.app bundle (assets in Contents/Resources).
Windows: voxcraft.exe + packed assets + data files.
Both include README.md, THIRD_PARTY.md and licences of vendored libraries.
Usage: package.py <build-dir> [--config Release]
"""
import os, platform, re, shutil, sys, tempfile, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def version():
    m = re.search(r"project\(Voxcraft VERSION ([0-9.]+)", open(os.path.join(ROOT, "CMakeLists.txt")).read())
    return m.group(1) if m else "0.0.0"

def main():
    if len(sys.argv) < 2: print(__doc__); return 2
    build = os.path.abspath(sys.argv[1])
    config = sys.argv[sys.argv.index("--config") + 1] if "--config" in sys.argv else "Release"
    system = platform.system()
    arch = platform.machine().lower().replace("amd64", "x64").replace("x86_64", "x64")
    plat = {"Darwin": "macos", "Windows": "windows"}.get(system, system.lower()) + "-" + arch
    os.makedirs(os.path.join(ROOT, "dist"), exist_ok=True)
    out = os.path.join(ROOT, "dist", f"Voxcraft-{version()}-{plat}.zip")
    with tempfile.TemporaryDirectory() as td:
        stage = os.path.join(td, "Voxcraft")
        os.makedirs(stage)
        if system == "Darwin":
            app = os.path.join(build, "Voxcraft.app")
            if not os.path.isdir(app): print("missing", app); return 1
            shutil.copytree(app, os.path.join(stage, "Voxcraft.app"), symlinks=True)
        else:
            exe_dir = os.path.join(build, config) if os.path.isdir(os.path.join(build, config)) else build
            exe = os.path.join(exe_dir, "voxcraft.exe" if system == "Windows" else "voxcraft")
            if not os.path.isfile(exe): print("missing", exe); return 1
            shutil.copy2(exe, stage)
            for f in ("voxcraft.pak", "models.json"):
                shutil.copy2(os.path.join(ROOT, "assets", "generated", f), stage)
            for f in ("blocks.json", "items.json", "recipes.json", "mobs.json", "animations.json"):
                shutil.copy2(os.path.join(ROOT, "assets", "data", f), stage)
        for f in ("README.md", "THIRD_PARTY.md", "CHANGELOG.md"):
            shutil.copy2(os.path.join(ROOT, f), stage)
        lic = os.path.join(stage, "licenses"); os.makedirs(lic)
        for lib in ("sokol", "lz4"):
            shutil.copy2(os.path.join(ROOT, "third_party", lib, "LICENSE"), os.path.join(lic, f"{lib}-LICENSE.txt"))
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for dp, dn, fn in os.walk(stage):
                for f in fn:
                    full = os.path.join(dp, f)
                    info = zipfile.ZipInfo.from_file(full, os.path.relpath(full, td))
                    if os.access(full, os.X_OK): info.external_attr = 0o755 << 16
                    with open(full, "rb") as fh: z.writestr(info, fh.read(), zipfile.ZIP_DEFLATED)
    print(f"packaged {out} ({os.path.getsize(out) / 1024 / 1024:.1f} MiB)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
