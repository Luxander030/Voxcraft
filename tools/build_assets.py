#!/usr/bin/env python3
"""Validate and pack Voxcraft runtime assets into assets/generated/voxcraft.pak.

Checks:
  * every PNG under assets/textures/blocks is exactly 16x16 (unless listed in EXCEPTIONS)
  * every texture referenced by assets/data/blocks.json exists (error) and every PNG is used (warning)
Packs:
  * identical images are stored once (content-hash dedup); names alias to tile indices
  * rebuilds only when an input changed (input hash stored in assets/generated/pak.hash)

pak v1 layout (little endian):
  "VXPK" u32 version=1 u32 tileSize u32 tileCount u32 nameCount
  nameCount x { u16 len, utf8 name, u16 tile }
  tileCount x { u8 flags (1 = has transparent pixels, 2 = has partial alpha) }
  tileCount x tileSize*tileSize*4 RGBA8
  u32 crc32(everything above)
Usage: build_assets.py [--force] [--preview out.png]
"""
import hashlib, json, os, re, struct, sys, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEX = os.path.join(ROOT, "assets", "textures", "blocks")
ITEMS_TEX = os.path.join(ROOT, "assets", "textures", "items")
ENT_TEX = os.path.join(ROOT, "assets", "textures", "entities")
MODELS = os.path.join(ROOT, "assets", "models", "built")
ITEMS = os.path.join(ROOT, "assets", "data", "items.json")
GEN = os.path.join(ROOT, "assets", "generated")
BLOCKS = os.path.join(ROOT, "assets", "data", "blocks.json")
TILE = 16
EXCEPTIONS = {}   # name -> reason, for non-16x16 block textures (none so far)

def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n": raise ValueError("not a PNG")
    pos, idat, w = 8, b"", None
    while pos < len(data):
        ln, = struct.unpack(">I", data[pos:pos + 4]); t = data[pos + 4:pos + 8]; body = data[pos + 8:pos + 8 + ln]; pos += 12 + ln
        if t == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or inter: raise ValueError(f"unsupported PNG (depth {depth}, type {ctype}, interlace {inter}); save as 8-bit RGB/RGBA")
        elif t == b"IDAT": idat += body
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat); stride = w * bpp; out = bytearray(); prev = bytearray(stride); p = 0
    for _ in range(h):
        f = raw[p]; line = bytearray(raw[p + 1:p + 1 + stride]); p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0; b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += line; prev = line
    if bpp == 3: out = bytearray(b for i in range(0, len(out), 3) for b in (out[i], out[i + 1], out[i + 2], 255))
    return w, h, bytes(out)

def write_png(path, w, h, rgba):
    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

def referenced_textures():
    txt = re.sub(r"^\s*//.*$", "", open(BLOCKS).read(), flags=re.M)
    names = set()
    for v in json.loads(txt).values():
        t = v.get("tex")
        if isinstance(t, str): names.add(t)
        elif isinstance(t, dict): names.update(t.values())
    return names

def main():
    force = "--force" in sys.argv
    preview = sys.argv[sys.argv.index("--preview") + 1] if "--preview" in sys.argv else None
    os.makedirs(GEN, exist_ok=True)
    files = sorted([os.path.join(TEX, f) for f in os.listdir(TEX) if f.endswith(".png")] +
                   [os.path.join(ITEMS_TEX, f) for f in os.listdir(ITEMS_TEX) if f.endswith(".png")] +
                   [os.path.join(ENT_TEX, f) for f in os.listdir(ENT_TEX) if f.endswith(".png")])
    h = hashlib.sha256(b"pak-v1")
    for f in files: h.update(os.path.basename(f).encode()); h.update(open(f, "rb").read())
    h.update(open(BLOCKS, "rb").read()); h.update(open(ITEMS, "rb").read())
    model_files = sorted(f for f in os.listdir(MODELS) if f.endswith(".json"))
    for f in model_files: h.update(open(os.path.join(MODELS, f), "rb").read())
    digest = h.hexdigest()
    pak = os.path.join(GEN, "voxcraft.pak"); hfile = os.path.join(GEN, "pak.hash")
    if not force and not preview and os.path.exists(pak) and os.path.exists(hfile) and open(hfile).read() == digest:
        print("build_assets: up to date"); return 0

    errors, warnings = [], []
    tiles, tile_index, names = [], {}, []
    seen = set()
    for path in files:
        f = os.path.basename(path); name = f[:-4]
        if name in seen: errors.append(f"duplicate texture name '{name}' (blocks and items share one namespace)"); continue
        seen.add(name)
        try: w, hh, px = read_png(path)
        except Exception as e: errors.append(f"{f}: {e}"); continue
        if (w, hh) != (TILE, TILE) and name not in EXCEPTIONS: errors.append(f"{f}: {w}x{hh}, block textures must be {TILE}x{TILE}"); continue
        key = hashlib.sha256(px).digest()
        if key not in tile_index: tile_index[key] = len(tiles); tiles.append(px)
        names.append((name, tile_index[key]))
    have = {n for n, _ in names}
    refs = referenced_textures()
    itxt = re.sub(r"^\s*//.*$", "", open(ITEMS).read(), flags=re.M)
    refs |= {v["tex"] for v in json.loads(itxt).values() if "tex" in v}
    for f in model_files:
        for b in json.load(open(os.path.join(MODELS, f)))["boxes"]: refs |= {b["tex"], b["front"]}
    import subprocess
    if subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "scad_export.py"), "--check"], capture_output=True).returncode != 0:
        warnings.append("built models are stale relative to assets/models/src (run tools/scad_export.py)")
    for m in sorted(refs - have): errors.append(f"missing texture '{m}' referenced by blocks.json")
    # implicit "lit" variants: "<x>_on" is used by blocks referencing "<x>_off" or "<x>"
    refs |= {n for n in have if n.endswith("_on") and (n[:-3] + "_off" in refs or n[:-3] in refs)}
    for u in sorted(have - refs): warnings.append(f"unused texture '{u}'")
    for wmsg in warnings: print("warning:", wmsg)
    if errors:
        for e in errors: print("error:", e)
        return 1

    out = bytearray(b"VXPK" + struct.pack("<IIII", 1, TILE, len(tiles), len(names)))
    for n, t in names: nb = n.encode(); out += struct.pack("<H", len(nb)) + nb + struct.pack("<H", t)
    for px in tiles:
        alphas = px[3::4]
        out.append((1 if min(alphas) == 0 else 0) | (2 if any(0 < a < 255 for a in alphas) else 0))
    for px in tiles: out += px
    out += struct.pack("<I", zlib.crc32(out) & 0xFFFFFFFF)
    tmp = pak + ".tmp"; open(tmp, "wb").write(out); os.replace(tmp, pak)
    open(hfile, "w").write(digest)
    print(f"build_assets: {len(names)} textures -> {len(tiles)} unique tiles, {len(out)} bytes")
    # all models in one runtime file (sorted, deterministic)
    models = {f[:-5]: json.load(open(os.path.join(MODELS, f))) for f in model_files}
    tmpm = os.path.join(GEN, "models.json.tmp"); json.dump(models, open(tmpm, "w"), separators=(",", ":")); os.replace(tmpm, os.path.join(GEN, "models.json"))

    if preview:
        cols, S = 10, 4; rows = (len(names) + cols - 1) // cols; W, H = cols * (TILE * S + 2), rows * (TILE * S + 2)
        img = bytearray([40, 40, 48, 255] * W * H)
        for i, (n, t) in enumerate(names):
            ox, oy = (i % cols) * (TILE * S + 2) + 1, (i // cols) * (TILE * S + 2) + 1; px = tiles[t]
            for y in range(TILE * S):
                for x in range(TILE * S):
                    s = ((y // S) * TILE + x // S) * 4; a = px[s + 3] / 255.0
                    bg = 90 if ((x // 8 + y // 8) % 2) else 60
                    d = ((oy + y) * W + ox + x) * 4
                    img[d:d + 3] = bytes(int(px[s + k] * a + bg * (1 - a)) for k in range(3))
        write_png(preview, W, H, bytes(img))
        print("preview:", preview, "order:", " ".join(n for n, _ in names))
    return 0

if __name__ == "__main__":
    sys.exit(main())
