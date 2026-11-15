#!/usr/bin/env python3
"""Export OpenSCAD box models (assets/models/src/*.scad) to runtime models (assets/models/built/*.json).

Steps per model:
  1. locate OpenSCAD (PATH, Homebrew, /Applications/OpenSCAD.app, %ProgramFiles%\\OpenSCAD)
  2. render it to STL (validates that the geometry builds and is non-empty)
  3. parse the VXBONE / VXBOX echo records into bones + boxes
  4. validate: unique bones, existing parents, every box on a known bone, positive sizes
  5. write JSON with the source hash, only if the source changed (incremental)

The built JSON files are committed, so building the game never requires OpenSCAD; the asset
packer warns if a built model is stale relative to its .scad source.
Usage: scad_export.py [--force] [--check]
"""
import glob, hashlib, json, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets", "models", "src")
OUT = os.path.join(ROOT, "assets", "models", "built")

def find_openscad():
    cands = [shutil.which("openscad"), "/opt/homebrew/bin/openscad", "/usr/local/bin/openscad",
             "/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD",
             os.path.join(os.environ.get("ProgramFiles", "C:\\\\Program Files"), "OpenSCAD", "openscad.exe")]
    for c in cands:
        if c and os.path.isfile(c): return c
    return None

def source_hash(path):
    h = hashlib.sha256()
    for p in [path, os.path.join(SRC, "lib.scad")]: h.update(open(p, "rb").read())
    return h.hexdigest()[:16]

def parse_value(s):
    s = s.strip()
    if s.startswith('"'): return s[1:-1]
    if s.startswith("["): return [float(x) for x in s[1:-1].split(",")]
    return float(s)

def split_args(s):
    out, depth, cur, q = [], 0, "", False
    for ch in s:
        if ch == '"': q = not q
        if not q and ch == "[": depth += 1
        if not q and ch == "]": depth -= 1
        if ch == "," and depth == 0 and not q: out.append(cur); cur = ""
        else: cur += ch
    out.append(cur)
    return [parse_value(x) for x in out]

def export(openscad, path, force):
    name = os.path.splitext(os.path.basename(path))[0]
    out = os.path.join(OUT, name + ".json")
    sh = source_hash(path)
    if not force and os.path.exists(out) and json.load(open(out)).get("source_hash") == sh: return "up to date"
    with tempfile.TemporaryDirectory() as td:
        stl = os.path.join(td, name + ".stl")
        r = subprocess.run([openscad, "-o", stl, path], capture_output=True, text=True)
        if r.returncode != 0: raise RuntimeError(f"{name}: OpenSCAD failed:\n{r.stderr}")
        if os.path.getsize(stl) < 100: raise RuntimeError(f"{name}: empty geometry")
        bones, boxes = [], []
        for line in r.stderr.splitlines():
            m = re.match(r'ECHO: "(VXBONE|VXBOX)", (.*)$', line.strip())
            if not m: continue
            a = split_args(m.group(2))
            if m.group(1) == "VXBONE": bones.append({"name": a[0], "parent": a[1], "pivot": a[2]})
            else: boxes.append({"bone": a[0], "min": a[1], "size": a[2], "tex": a[3], "front": a[4]})
    names = [b["name"] for b in bones]
    if len(set(names)) != len(names): raise RuntimeError(f"{name}: duplicate bone names")
    for b in bones:
        if b["parent"] and b["parent"] not in names[:names.index(b["name"])]: raise RuntimeError(f"{name}: bone {b['name']} parent must be declared before it")
    for bx in boxes:
        if bx["bone"] not in names: raise RuntimeError(f"{name}: box on unknown bone {bx['bone']}")
        if min(bx["size"]) <= 0: raise RuntimeError(f"{name}: non-positive box size")
    if not boxes: raise RuntimeError(f"{name}: no boxes")
    json.dump({"name": name, "source_hash": sh, "bones": bones, "boxes": boxes}, open(out, "w"), indent=1)
    return f"exported {len(bones)} bones, {len(boxes)} boxes"

def main():
    os.makedirs(OUT, exist_ok=True)
    srcs = sorted(p for p in glob.glob(os.path.join(SRC, "*.scad")) if not p.endswith("lib.scad"))
    if "--check" in sys.argv:   # CI / packer: verify built models match sources without OpenSCAD
        stale = [os.path.basename(p) for p in srcs if not os.path.exists(os.path.join(OUT, os.path.splitext(os.path.basename(p))[0] + ".json"))
                 or json.load(open(os.path.join(OUT, os.path.splitext(os.path.basename(p))[0] + ".json"))).get("source_hash") != source_hash(p)]
        if stale: print("stale models (run tools/scad_export.py):", " ".join(stale)); return 1
        print(f"scad_export: {len(srcs)} models up to date"); return 0
    oscad = find_openscad()
    if not oscad: print("error: OpenSCAD not found (install it or put it on PATH)"); return 1
    for p in srcs: print(f"{os.path.basename(p)}: {export(oscad, p, '--force' in sys.argv)}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
