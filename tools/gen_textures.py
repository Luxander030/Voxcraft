#!/usr/bin/env python3
"""Generate Voxcraft's original 16x16 block textures procedurally.

Every texture is drawn from simple primitives (noise fills, boards, rings, specks, outlines)
with hand-picked palettes, seeded from the texture name, so output is deterministic and
reproducible. The resulting PNGs in assets/textures/blocks/ are the editable sources: they
can be hand-touched afterwards, and this script only (re)writes files listed in RECIPES when
run with --force or when the file does not exist yet.

Pure standard library (zlib + struct) so it runs anywhere Python 3 does.
"""
import os, struct, sys, zlib, hashlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "textures", "blocks")
N = 16

# ------------------------------------------------------------------ PNG I/O
def write_png(path, px):
    """px: list of 16*16 (r,g,b,a) tuples."""
    raw = b"".join(b"\x00" + bytes(c for p in px[y * N:(y + 1) * N] for c in p) for y in range(N))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 6, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f: f.write(data)

# ------------------------------------------------------------------ helpers
class Rng:
    def __init__(self, name): self.s = int(hashlib.sha256(name.encode()).hexdigest()[:16], 16)
    def next(self):
        self.s = (self.s * 6364136223846793005 + 1442695040888963407) & 0xFFFFFFFFFFFFFFFF
        return (self.s >> 33) / float(1 << 31)
    def int(self, n): return int(self.next() * n) % n

def hexc(h, a=255): h = h.lstrip("#"); return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)
def pal(*hs): return [hexc(h) for h in hs]
CLEAR = (0, 0, 0, 0)

def value_noise(rng, scale=4):
    g = [[rng.next() for _ in range(N // scale + 2)] for _ in range(N // scale + 2)]
    out = []
    for y in range(N):
        for x in range(N):
            fx, fy = x / scale, y / scale; ix, iy = int(fx), int(fy); tx, ty = fx - ix, fy - iy
            a = g[iy][ix] * (1 - tx) + g[iy][ix + 1] * tx; b = g[iy + 1][ix] * (1 - tx) + g[iy + 1][ix + 1] * tx
            out.append(a * (1 - ty) + b * ty)
    return out

def noisy(rng, colors, blend=0.55, scale=4):
    """Fill with palette indexed by mixed smooth + white noise."""
    vn = value_noise(rng, scale)
    return [colors[min(len(colors) - 1, int((vn[i] * blend + rng.next() * (1 - blend)) * len(colors)))] for i in range(N * N)]

def put(px, x, y, c):
    if 0 <= x < N and 0 <= y < N: px[y * N + x] = c

def specks(px, rng, colors, count, size=2):
    for _ in range(count):
        cx, cy = rng.int(N - 2) + 1, rng.int(N - 2) + 1
        for dy in range(size):
            for dx in range(size):
                if rng.next() < 0.8: put(px, cx + dx, cy + dy, colors[rng.int(len(colors))])

def ore(base_name, colors, count=8):
    def f(rng):
        px = RECIPES[base_name](Rng(base_name))
        for _ in range(count):
            cx, cy = rng.int(12) + 2, rng.int(12) + 2
            for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1), (-1, 0), (0, -1)):
                if rng.next() < 0.75: put(px, cx + dx, cy + dy, colors[rng.int(len(colors))])
            put(px, cx, cy, colors[0])
        return px
    return f

def planks(colors):
    def f(rng):
        px = noisy(rng, colors[1:], 0.3, 8)
        for y in range(N):
            for x in range(N):
                if y % 4 == 3: px[y * N + x] = colors[0]
        for row in range(4):
            sx = (rng.int(8) + row * 5) % N
            for y in range(row * 4, row * 4 + 3): put(px, sx, y, colors[0])
        return px
    return f

def log_side(colors):
    def f(rng):
        px = []
        cols = [rng.int(len(colors)) for _ in range(N)]
        for y in range(N):
            for x in range(N):
                c = cols[x] if rng.next() > 0.15 else rng.int(len(colors))
                px.append(colors[c])
        return px
    return f

def log_top(bark, rings):
    def f(rng):
        px = []
        for y in range(N):
            for x in range(N):
                if x in (0, 15) or y in (0, 15): px.append(bark[rng.int(len(bark))]); continue
                d = max(abs(x - 7.5), abs(y - 7.5))
                px.append(rings[int(d + rng.next() * 0.6) % 2 + (1 if d > 5 else 0)])
        return px
    return f

def leaves(colors, holes=0.12):
    def f(rng):
        px = noisy(rng, colors, 0.4, 3)
        return [CLEAR if rng.next() < holes else p for p in px]
    return f

def bricks(mortar, colors, h=4, w=8):
    def f(rng):
        px = []
        for y in range(N):
            off = (w // 2) if (y // h) % 2 else 0
            for x in range(N):
                if y % h == h - 1 or (x + off) % w == w - 1: px.append(mortar)
                else: px.append(colors[rng.int(len(colors))] if rng.next() < 0.35 else colors[0])
        return px
    return f

def cobble(dark, colors):
    def f(rng):
        # voronoi stones with dark outlines
        pts = [(rng.next() * N, rng.next() * N) for _ in range(9)]
        shade = [colors[rng.int(len(colors))] for _ in pts]
        px = []
        for y in range(N):
            for x in range(N):
                ds = sorted(((min(abs(x - px_), N - abs(x - px_)) ** 2 + min(abs(y - py), N - abs(y - py)) ** 2, i) for i, (px_, py) in enumerate(pts)))
                px.append(dark if ds[1][0] - ds[0][0] < 5 else shade[ds[0][1]])
        return px
    return f

def plant(draw):
    def f(rng):
        px = [CLEAR] * (N * N); draw(px, rng); return px
    return f

def stem(px, x, y0, y1, c):
    for y in range(y0, y1 + 1): put(px, x, y, c)

def flower(head, center):
    def d(px, rng):
        g = hexc("4f8a3a"); gd = hexc("3a6b2b")
        stem(px, 8, 7, 15, g); put(px, 7, 11, gd); put(px, 6, 10, gd); put(px, 9, 12, gd); put(px, 10, 11, gd)
        for dx, dy in ((0, -1), (-1, 0), (1, 0), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1)):
            put(px, 8 + dx, 5 + dy, head[(dx + dy) % len(head)])
        put(px, 8, 5, center)
    return plant(d)

def grass_blades(colors, n=9):
    def d(px, rng):
        for i in range(n):
            x = 1 + rng.int(14); h = 5 + rng.int(9); lean = rng.int(3) - 1
            for k in range(h):
                put(px, x + (lean if k > h // 2 else 0), 15 - k, colors[rng.int(len(colors))])
    return plant(d)

def framed(inner, frame):
    def f(rng):
        px = inner(rng)
        for i in range(N):
            for (x, y) in ((i, 0), (i, 15), (0, i), (15, i)): put(px, x, y, frame)
        return px
    return f

def solid_block(colors, edge):
    def f(rng):
        px = noisy(rng, colors, 0.7, 8)
        for i in range(N):
            put(px, i, 0, colors[-1]); put(px, 0, i, colors[-1]); put(px, i, 15, edge); put(px, 15, i, edge)
        return px
    return f

def overlay(base, draw):
    def f(rng):
        px = RECIPES[base](Rng(base)) if isinstance(base, str) else base(rng)
        draw(px, rng); return px
    return f

# ------------------------------------------------------------------ palettes
STONE = pal("7d7f84", "74767b", "86888c", "6c6e73", "8f9195")
SLATE = pal("4a4d57", "42454f", "53565f", "3c3e47", "5a5d66")
DIRT = pal("7a5537", "6e4b30", "86603f", "5f412a", "8e6846")
GREY = pal("bdbdbd", "b0b0b0", "c8c8c8", "a6a6a6", "d2d2d2")   # tinted at render time
SAND = pal("dcd1a0", "d3c794", "e4daad", "cbbf8b", "e9e0b8")
RSAND = pal("c0703c", "b56836", "ca7a45", "a85f31", "d1844e")
OAK = pal("6e5131", "a8854f", "b48f58", "9c7a47", "bf9a62")
BIRCH = pal("8a7a52", "d6c690", "dccd9a", "cbbb84", "e2d4a4")
PINE = pal("3e2a18", "6e4c2c", "785433", "634426", "82603b")

def stone_f(rng): return noisy(rng, STONE, 0.5, 4)

RECIPES = {
    "stone": stone_f,
    "cobblestone": cobble(hexc("4e5054"), pal("7a7c80", "8a8c90", "6e7074", "959799")),
    "slate": lambda r: [p if (i // N) % 5 else SLATE[3] for i, p in enumerate(noisy(r, SLATE, 0.5, 4))],
    "slate_cobble": cobble(hexc("2c2e35"), pal("4a4d57", "555862", "40434c")),
    "bedrock": lambda r: noisy(r, pal("2a2a2a", "4a4a4a", "1c1c1c", "5c5c5c", "3a3a3a"), 0.2, 2),
    "dirt": lambda r: noisy(r, DIRT, 0.4, 4),
    "turf_top": lambda r: noisy(r, GREY, 0.35, 3),
    "turf_side": overlay("dirt", lambda px, r: [put(px, x, y, hexc(["5d9b3f", "53903a", "68a647"][r.int(3)]))
                                                for x in range(N) for y in range(0, 3 + (r.int(3) if x % 3 else 0))]),
    "podzol_top": lambda r: noisy(r, pal("6b4a24", "7a5630", "5c3f1e", "8a6438", "4f361a"), 0.3, 3),
    "podzol_side": overlay("dirt", lambda px, r: [put(px, x, y, hexc(["6b4a24", "5c3f1e"][r.int(2)])) for x in range(N) for y in range(0, 2 + r.int(2))]),
    "sand": lambda r: noisy(r, SAND, 0.3, 4),
    "red_sand": lambda r: noisy(r, RSAND, 0.3, 4),
    "gravel": cobble(hexc("5e5a58"), pal("8b8580", "9c9690", "7a7470", "aaa49e")),
    "sandstone": lambda r: [SAND[3] if (i // N) in (3, 9, 13) else p for i, p in enumerate(noisy(r, SAND, 0.6, 8))],
    "sandstone_top": lambda r: noisy(r, SAND, 0.7, 8),
    "clay": lambda r: noisy(r, pal("a3a8b8", "9aa0b0", "acb1c0", "9298a8"), 0.7, 4),
    "snow": lambda r: noisy(r, pal("f2f6fa", "e8eef4", "fafcff", "dfe7ef"), 0.6, 4),
    "ice": lambda r: [(*p[:3], 190) for p in noisy(r, pal("9cc4f0", "a8ccf4", "8fb8e8", "b8d6f8"), 0.8, 8)],
    "water": lambda r: [(*p[:3], 170) for p in noisy(r, GREY, 0.9, 4)],
    "lava": lambda r: noisy(r, pal("d4501a", "e8691e", "f08a28", "c03c14", "f8b040"), 0.85, 4),
    "oak_log": log_side(pal("5c4326", "4e381f", "6a4e2e", "443019")),
    "oak_log_top": log_top(pal("5c4326", "4e381f"), pal("b08c56", "9c7a47", "8a6a3c")),
    "birch_log": overlay(log_side(pal("e6e2d6", "dcd8cc", "f0ece2")), lambda px, r: specks(px, r, pal("2e2a26", "3e3a34"), 6, 2)),
    "birch_log_top": log_top(pal("e6e2d6", "2e2a26"), pal("d6c690", "c4b37c", "b4a46e")),
    "pine_log": log_side(pal("3e2a18", "33220f", "4a3320", "2b1d0e")),
    "pine_log_top": log_top(pal("3e2a18", "33220f"), pal("7a5636", "6a4a2c", "5a3e24")),
    "oak_planks": planks(OAK), "birch_planks": planks(BIRCH), "pine_planks": planks(PINE),
    "oak_leaves": leaves(GREY), "birch_leaves": leaves(GREY, 0.1), "pine_leaves": leaves(pal("3a6a44", "33603c", "42764c", "2c5434"), 0.08),
    "cactus_side": lambda r: [hexc("d8e0a0") if (i % N) in (3, 8, 12) and r.next() < 0.2 else p
                              for i, p in enumerate([pal("4f8a34", "457c2e", "5a983c")[int(((i % N) % 5) / 2)] for i in range(N * N)])],
    "cactus_top": framed(lambda r: noisy(r, pal("5a983c", "4f8a34", "68a648"), 0.6, 4), hexc("3e6e28")),
    "tall_grass": grass_blades(GREY), "fern": grass_blades(pal("9a9a9a", "8a8a8a", "aaaaaa"), 6),
    "dead_bush": grass_blades(pal("8a6a3c", "7a5a30", "9c7a47"), 7),
    "sunpetal": flower(pal("f4c430", "e8b020", "fad050"), hexc("8a5a10")),
    "bluebell": flower(pal("4a6ad8", "3a58c0", "5c7ce8"), hexc("e8e8f8")),
    "emberbloom": flower(pal("e04a2a", "c83a1e", "f0603a"), hexc("ffd040")),
    "sapling": plant(lambda px, r: (stem(px, 8, 8, 15, hexc("5c4326")), [put(px, 8 + dx, 6 + dy, hexc(["4f8a3a", "5d9b3f", "3a6b2b"][r.int(3)]))
                                    for dx in range(-3, 4) for dy in range(-3, 3) if abs(dx) + abs(dy) < 4 and r.next() < 0.85])),
    "coal_ore": ore("stone", pal("2a2a2c", "3a3a3e", "1e1e20")),
    "copper_ore": ore("stone", pal("c87a4a", "e09060", "4a9a7a")),
    "iron_ore": ore("stone", pal("d8b494", "c8a080", "e4c4a8")),
    "gold_ore": ore("stone", pal("f0d040", "e0b828", "fae878")),
    "glimmer_ore": ore("stone", pal("e83070", "c8205a", "ff6a9a"), 8),
    "azurite_ore": ore("slate", pal("38b8e0", "2a90c8", "8ae0f8"), 7),
    "iron_block": solid_block(pal("d6d6d6", "cacaca", "e0e0e0", "f0f0f0"), hexc("9a9a9a")),
    "gold_block": solid_block(pal("f0c838", "e6b828", "f8d850", "fff090"), hexc("b08a18")),
    "azurite_block": solid_block(pal("3ab4e0", "2aa0cc", "50c4ee", "a0e8fa"), hexc("1a6a98")),
    "glass": lambda r: [(220, 240, 250, 255) if (x in (0, 15) or y in (0, 15)) or (x - y in (3, 4) and 4 < x < 12) else CLEAR
                        for y in range(N) for x in range(N)],
    "bricks": bricks(hexc("c8bca8"), pal("9a4a36", "8a3e2c", "a8543e", "7c3626")),
    "stone_bricks": bricks(hexc("55575b"), pal("7d7f84", "74767b", "86888c"), 8, 8),
    "wool": lambda r: noisy(r, pal("ecebe6", "e2e1dc", "f4f3ee", "d8d7d2"), 0.2, 2),
    "bookshelf": overlay(planks(OAK), lambda px, r: [put(px, x, y, hexc(["8a2a2a", "2a4a8a", "2a7a3a", "8a7a2a", "5a2a7a"][(x // 2 + y // 8) % 5]))
                                                     for y in range(N) for x in range(1, 15) if y % 8 in (1, 2, 3, 4, 5) and x % 2 == 0 or (y % 8 in (2, 3, 4) and x % 2)]),
    "workbench_top": framed(planks(OAK), hexc("4e381f")),
    "workbench_side": overlay(planks(OAK), lambda px, r: [put(px, x, y, hexc("7a7c80")) for x in range(3, 7) for y in range(2, 6)] +
                              [put(px, x, y, hexc("5c4326")) for x in (10, 11) for y in range(2, 9)]),
    "workbench_front": overlay(planks(OAK), lambda px, r: [put(px, x, y, hexc("4e381f")) for x in range(2, 14) for y in (2, 3)]),
    "kiln_side": bricks(hexc("3a3230"), pal("8a5040", "7a4436", "9a5a48"), 4, 8),
    "kiln_top": framed(lambda r: noisy(r, pal("6a5048", "5c443c", "785a50"), 0.5, 4), hexc("3a3230")),
    "kiln_front": overlay(bricks(hexc("3a3230"), pal("8a5040", "7a4436", "9a5a48"), 4, 8),
                          lambda px, r: [put(px, x, y, hexc("1a1210")) for x in range(4, 12) for y in range(7, 13)]),
    "chest_top": framed(planks(pal("5a3e22", "9a6a38", "a8783e", "8a5e30", "b4844a")), hexc("3a2814")),
    "chest_side": framed(planks(pal("5a3e22", "9a6a38", "a8783e", "8a5e30", "b4844a")), hexc("3a2814")),
    "chest_front": overlay(framed(planks(pal("5a3e22", "9a6a38", "a8783e", "8a5e30", "b4844a")), hexc("3a2814")),
                           lambda px, r: [put(px, x, y, hexc("c8c8c8") if y != 7 else hexc("8a8a8a")) for x in (7, 8) for y in (6, 7, 8)]),
    "torch": plant(lambda px, r: (stem(px, 7, 6, 15, hexc("6e5131")), stem(px, 8, 6, 15, hexc("5c4326")),
                                  [put(px, x, y, hexc(c)) for (x, y, c) in ((7, 5, "ffd040"), (8, 5, "ffb020"), (7, 4, "fff0a0"), (8, 4, "ffd040"))])),
    "ladder": plant(lambda px, r: [put(px, x, y, OAK[0 if x in (2, 13) else 3]) for y in range(N) for x in range(2, 14)
                                   if x in (2, 3, 12, 13) or y % 4 == 1]),
    "glowcrystal": lambda r: [hexc(["f8e080", "f0c860", "fff4c0", "e0a840"][(x * 3 + y * 5 + r.int(2)) % 4]) for y in range(N) for x in range(N)],
    "glimmer_wire": plant(lambda px, r: [put(px, x, y, (255, 255, 255, 255)) for y in range(N) for x in range(N) if 6 <= x <= 9 or 6 <= y <= 9]),
    "spark_torch_off": plant(lambda px, r: (stem(px, 7, 6, 15, hexc("6e5131")), stem(px, 8, 6, 15, hexc("5c4326")),
                                            [put(px, x, y, hexc("5a1a2a")) for x in (7, 8) for y in (4, 5)])),
    "spark_torch_on": plant(lambda px, r: (stem(px, 7, 6, 15, hexc("6e5131")), stem(px, 8, 6, 15, hexc("5c4326")),
                                           [put(px, x, y, hexc(c)) for (x, y, c) in ((7, 5, "ff3a6a"), (8, 5, "e82858"), (7, 4, "ff9ab8"), (8, 4, "ff3a6a"))])),
    "lever": plant(lambda px, r: [put(px, 8 + k // 3, 14 - k, OAK[0]) for k in range(9)]),
    "lamp_off": framed(lambda r: noisy(r, pal("6a4a30", "5a3e26", "7a5638"), 0.5, 4), hexc("3a2814")),
    "lamp_on": framed(lambda r: [hexc(["fff0b0", "ffe080", "fff8d8", "f8d060"][(x * 5 + y * 3 + r.int(2)) % 4]) for y in range(N) for x in range(N)], hexc("8a6a2c")),
    "kiln_front_on": overlay(bricks(hexc("3a3230"), pal("8a5040", "7a4436", "9a5a48"), 4, 8),
                             lambda px, r: [put(px, x, y, hexc(["ff8a20", "ffc040", "e05010"][r.int(3)])) for x in range(4, 12) for y in range(7, 13)]),
    "hollowrock": lambda r: noisy(r, pal("5a2a2a", "4e2424", "662f2f", "442020", "703636"), 0.55, 3),
    "ember_ore": ore("hollowrock", pal("ff9a30", "ffd060", "e86a10"), 8),
    "ashsoil": lambda r: noisy(r, pal("4a4440", "3e3834", "56504a", "34302c"), 0.4, 3),
    "glowcap": plant(lambda px, r: (stem(px, 8, 9, 15, hexc("c8b8a0")), stem(px, 7, 11, 15, hexc("b8a890")),
                                    [put(px, 8 + dx, 7 + dy, hexc(["60f0d0", "40d8b8", "a0fff0"][(dx * dx + dy) % 3])) for dx in range(-3, 4) for dy in range(-2, 2) if abs(dx) + max(0, dy) < 4])),
    "rift": lambda r: [(*p[:3], 200) for p in [hexc(["5a2a9a", "7a3ac8", "3a1a6a", "9a60e8"][int((abs(((x - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5 * 1.3 + r.next() * 1.5)) % 4)]) for y in range(N) for x in range(N)]],
    "sticky_piston_front": overlay(framed(planks(OAK), hexc("4e381f")), lambda px, r: [put(px, x, y, hexc(["5a3a78", "7a5a98", "4a2a68"][r.int(3)])) for x in range(2, 14) for y in range(2, 14) if r.next() < 0.85]),
    "hopper_top": lambda r: [hexc("2a2a2e") if 3 <= x <= 12 and 3 <= y <= 12 else p for i, p in enumerate(noisy(r, pal("6a6a72", "5a5a62", "7a7a82"), 0.5, 4)) for x, y in [(i % N, i // N)]],
    "hopper_side": lambda r: [hexc("3a3a42") if (y > 9 and (x < 4 or x > 11)) or y == 9 else p for i, p in enumerate(noisy(r, pal("6a6a72", "5a5a62", "7a7a82"), 0.5, 4)) for x, y in [(i % N, i // N)]],
    "hopper_bottom": framed(lambda r: noisy(r, pal("4a4a52", "3a3a42"), 0.5, 4), hexc("2a2a2e")),
    "dropper_top": framed(lambda r: noisy(r, pal("6a5048", "5c443c", "785a50"), 0.5, 4), hexc("3a3230")),
    "dropper_front": overlay(bricks(hexc("3a3230"), pal("8a5040", "7a4436", "9a5a48"), 4, 8), lambda px, r: [put(px, x, y, hexc("141010")) for x in range(5, 11) for y in range(6, 11) if (x - 7.5) ** 2 + (y - 8) ** 2 < 8]),
    "dispenser_front": overlay(bricks(hexc("3a3230"), pal("8a5040", "7a4436", "9a5a48"), 4, 8), lambda px, r: [put(px, x, y, hexc("141010") if abs(x - 7.5) < 2 else hexc("c8c8c8")) for x in range(4, 12) for y in range(6, 11)]),
    "comparator_top": overlay("slate", lambda px, r: [put(px, x, y, hexc("8a2a3a")) for x, y in ((3, 3), (12, 3), (7, 12), (8, 12), (7, 11), (8, 11))]),
    "relay_top": overlay("slate", lambda px, r: [put(px, x, y, hexc("8a2a3a")) for x in (4, 11) for y in (3, 4, 11, 12)]),
    "watcher_face": overlay("cobblestone", lambda px, r: [put(px, x, y, hexc("1a1a1a")) for x in range(3, 13) for y in (4, 5, 6)] +
                            [put(px, x, y, hexc("e83070")) for x in (5, 10) for y in (5,)]),
    "watcher_side": overlay("cobblestone", lambda px, r: [put(px, x, 8, hexc("8a2a3a")) for x in range(4, 12)]),
    "piston_side": lambda r: [OAK[3] if y < 4 else STONE[1 + (x + y) % 3] for y in range(N) for x in range(N)],
    "piston_front": framed(planks(OAK), hexc("4e381f")),
    "piston_back": framed(stone_f, hexc("4e5054")),
}

# ------------------------------------------------------------------ items
def item(draw):
    def f(rng):
        px = [CLEAR] * (N * N); draw(px, rng)
        # 1px dark outline around opaque pixels for readability on any background
        out = list(px)
        for y in range(N):
            for x in range(N):
                if px[y * N + x][3] == 0 and any(0 <= x + dx < N and 0 <= y + dy < N and px[(y + dy) * N + x + dx][3] > 0
                                                  for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    out[y * N + x] = (24, 20, 18, 255)
        return out
    return f

def blob(cols, cx=8, cy=8, r=4.2, squash=0.85):
    def d(px, rng):
        for y in range(N):
            for x in range(N):
                dd = ((x - cx) / r) ** 2 + ((y - cy) / (r * squash)) ** 2 + (rng.next() - 0.5) * 0.35
                if dd < 1: put(px, x, y, cols[min(len(cols) - 1, int((dd + (x - cx) * 0.05) * len(cols)) % len(cols))])
    return item(d)

def ingot(cols):
    def d(px, rng):
        for y in range(6, 11):
            for x in range(3 + (10 - y) // 2, 13 - (10 - y) // 2):
                put(px, x, y, cols[0] if y == 6 else cols[1] if y < 9 else cols[2])
    return item(d)

TIER = {"wooden": pal("b48f58", "9c7a47", "6e5131"), "stone": pal("8a8c90", "74767b", "55575b"),
        "iron": pal("e8e8e8", "c8c8c8", "8a8a8a"), "azurite": pal("8ae0f8", "38b8e0", "1a6a98")}
HANDLE = pal("8a6a3c", "5c4326")

def tool(kind, tier):
    c = TIER[tier]
    def d(px, rng):
        reach = 7 if kind == "sword" else 9
        for k in range(reach):   # handle: bottom-left to centre
            put(px, 2 + k, 13 - k, HANDLE[k % 2] if kind != "sword" or k < 3 else c[1])
        if kind == "pickaxe":
            for k in range(-5, 6):
                x, y = 9 + k // 2 + (k > 0) * 0, 5 - abs(k) // 2
                put(px, 8 + k, 3 + abs(k) // 2 + (1 if abs(k) > 3 else 0), c[0 if k < 0 else 1])
                put(px, 8 + k, 4 + abs(k) // 2 + (1 if abs(k) > 3 else 0), c[2])
        elif kind == "axe":
            for y in range(2, 9):
                for x in range(9, 9 + max(1, 4 - abs(y - 5) // 2)):
                    put(px, x, y, c[0] if x == 9 else c[1] if y < 6 else c[2])
        elif kind == "shovel":
            for y in range(2, 7):
                for x in range(9, 14):
                    if abs(x - 11) + abs(y - 4) < 4: put(px, x, y, c[0] if y < 4 else c[1])
        elif kind == "sword":
            for k in range(9):
                put(px, 6 + k, 9 - k, c[0]); put(px, 7 + k, 9 - k, c[1]); put(px, 6 + k, 10 - k, c[2])
            for k in range(-2, 3): put(px, 5 + k, 9 + k, HANDLE[1])   # crossguard
    return item(d)

def gem(cols):
    def d(px, rng):
        for y in range(3, 13):
            w = (y - 2) if y < 7 else (13 - y) * 0.9
            for x in range(N):
                if abs(x - 7.5) < w: put(px, x, y, cols[0] if y < 6 else cols[1] if x < 8 else cols[2])
    return item(d)

def bucket(fill):
    def d(px, rng):
        for y in range(4, 14):
            w = 5 - (y - 4) // 4
            for x in range(8 - w, 8 + w):
                edge = x in (8 - w, 8 + w - 1) or y == 13
                put(px, x, y, hexc("c8c8c8") if edge else (fill[(x + y) % len(fill)] if fill and y >= 5 else hexc("5a5a5a") if y >= 5 else hexc("e0e0e0")))
        for x in range(3, 13): put(px, x, 4, hexc("e8e8e8"))
        for k in range(3): put(px, 4 + k, 3 - k, hexc("9a9a9a")); put(px, 11 - k, 3 - k, hexc("9a9a9a"))
    return item(d)

ITEM_RECIPES = {
    "bucket": bucket(None), "water_bucket": bucket(pal("3a68d8", "4a78e8")), "lava_bucket": bucket(pal("e8691e", "f8b040")),
    "stick": item(lambda px, r: [put(px, 3 + k, 12 - k, HANDLE[k % 2]) for k in range(10)]),
    "coal": blob(pal("2e2e30", "1e1e20", "3c3c40")),
    "raw_iron": blob(pal("d8b494", "b89474", "e4c4a8")),
    "raw_gold": blob(pal("f0d040", "d0a820", "fae878")),
    "raw_copper": blob(pal("c87a4a", "a85e34", "4a9a7a")),
    "iron_ingot": ingot(pal("f0f0f0", "c8c8c8", "9a9a9a")),
    "gold_ingot": ingot(pal("fff090", "f0c838", "b08a18")),
    "copper_ingot": ingot(pal("f0a070", "c87a4a", "8a4a2a")),
    "brick": ingot(pal("c06a50", "9a4a36", "7c3626")),
    "azurite": gem(pal("bff0fc", "38b8e0", "1a6a98")),
    "glimmer_dust": item(lambda px, r: [put(px, 4 + r.int(9), 6 + r.int(7), hexc(["e83070", "ff6a9a", "c8205a"][r.int(3)])) for _ in range(30)]),
    "clay_lump": blob(pal("a3a8b8", "8a90a0", "b8bdcc")),
    "apple": item(lambda px, r: ([put(px, x, y, hexc("c8202a") if (x - 7) ** 2 + (y - 9) ** 2 < 14 else CLEAR) for y in range(4, 14) for x in range(3, 13)
                                  if (x - 7.5) ** 2 + (y - 9) ** 2 < 18], put(px, 8, 3, hexc("5c4326")), put(px, 8, 4, hexc("5c4326")), put(px, 9, 3, hexc("4f8a3a")), put(px, 6, 7, hexc("ff8080")))),
    "wild_root": blob(pal("d8c8a0", "b8a478", "e8dcbc"), r=4.6, squash=0.6),
    "baked_root": blob(pal("c87a3a", "a85e24", "e09858"), r=4.6, squash=0.6),
}
# ------------------------------------------------------------------ creature skins (16x16 tiles)
def face(base, eye, extra=None):
    def f(rng):
        px = noisy(rng, base, 0.5, 4)
        for (x, y) in ((4, 6), (5, 6), (10, 6), (11, 6)): put(px, x, y, eye[0])
        for (x, y) in ((4, 5), (11, 5)): put(px, x, y, eye[1])
        if extra: extra(px, rng)
        return px
    return f

ENTITY_RECIPES = {
    "grazer_hide": lambda r: [p if r.next() > 0.06 else hexc("e8dcc0") for p in noisy(r, pal("9a6a3a", "8a5e32", "a87844", "7c5430"), 0.6, 4)],
    "grazer_face": face(pal("9a6a3a", "8a5e32", "a87844"), pal("1a1210", "f0e8d8"), lambda px, r: [put(px, x, y, hexc("3a2418")) for x in (7, 8) for y in (11, 12)]),
    "grazer_horn": lambda r: noisy(r, pal("e8dcc0", "d0c4a4", "f4ecd8"), 0.4, 4),
    "grazer_leg": lambda r: [hexc("3a2418") if (i // N) > 12 else p for i, p in enumerate(noisy(r, pal("8a5e32", "7c5430", "9a6a3a"), 0.6, 4))],
    "puffbird_feathers": lambda r: noisy(r, pal("b8d8f0", "a8c8e8", "c8e4f8", "98bce0"), 0.5, 3),
    "puffbird_face": face(pal("b8d8f0", "a8c8e8", "c8e4f8"), pal("101820", "ffffff")),
    "puffbird_beak": lambda r: noisy(r, pal("f0a830", "e09020", "f8c050"), 0.5, 4),
    "puffbird_wing": lambda r: [hexc("6890c0") if (i % N) % 4 == 0 else p for i, p in enumerate(noisy(r, pal("88b0d8", "78a0c8"), 0.5, 4))],
    "gloomling_skin": lambda r: noisy(r, pal("3a2a48", "322440", "443254", "2a1e36"), 0.5, 4),
    "gloomling_face": face(pal("3a2a48", "322440", "443254"), pal("ffd040", "fff4a0"), lambda px, r: [put(px, x, 11, hexc("1a1020")) for x in range(5, 11)]),
    "gloomling_leg": lambda r: noisy(r, pal("2a1e36", "241a30", "322440"), 0.5, 4),
    "cinderwisp_core": lambda r: noisy(r, pal("ff8a20", "ffb040", "e86010", "ffd070"), 0.6, 3),
    "cinderwisp_face": face(pal("ff8a20", "ffb040", "e86010"), pal("3a0a00", "fff4c0"), lambda px, r: [put(px, x, 11, hexc("6a1a00")) for x in range(6, 10)]),
    "cinderwisp_shard": lambda r: noisy(r, pal("5a2a2a", "ff6a10", "442020"), 0.4, 2),
    "rockmite_shell": lambda r: [hexc("4a4844") if (i % N) % 5 == 0 or (i // N) % 6 == 0 else p for i, p in enumerate(noisy(r, pal("7a766e", "6c6862", "88847a"), 0.5, 4))],
    "rockmite_face": face(pal("7a766e", "6c6862"), pal("c82020", "ff6060")),
    "rockmite_leg": lambda r: noisy(r, pal("4a4844", "3c3a36"), 0.5, 4),
}

def crack(stage):
    def f(rng):
        px = [CLEAR] * (N * N)
        r = Rng("crack")   # same base pattern for every stage, revealed progressively
        segs = []
        for _ in range(10):
            x, y = 7 + r.int(3) - 1, 7 + r.int(3) - 1
            path = []
            for _ in range(9):
                x = max(0, min(15, x + r.int(3) - 1)); y = max(0, min(15, y + r.int(3) - 1)); path.append((x, y))
            segs.append(path)
        show = (stage + 1) * 10 // 8
        for i in range(show):
            for k, (x, y) in enumerate(segs[i]):
                if k <= 3 + stage: put(px, x, y, (20, 16, 14, 255))
        return px
    return f

for _s in range(8): ENTITY_RECIPES[f"crack_{_s}"] = crack(_s)

ITEM_RECIPES.update({
    "raw_venison": blob(pal("c84a4a", "b03a3a", "e07070"), r=4.6, squash=0.7),
    "cooked_venison": blob(pal("8a4a2a", "6e3a20", "a86040"), r=4.6, squash=0.7),
    "raw_fowl": blob(pal("f0c0b0", "e0a898", "f8d8c8"), r=4.0, squash=0.8),
    "cooked_fowl": blob(pal("c88a4a", "a86e34", "e0a868"), r=4.0, squash=0.8),
    "feather": item(lambda px, r: ([put(px, 4 + k, 12 - k, hexc("e8e8f0")) for k in range(9)], [put(px, 4 + k + d, 12 - k - d2, hexc("c8d8f0")) for k in range(2, 9) for d, d2 in ((1, 0), (0, -1)) if r.next() < 0.8])),
    "hide": blob(pal("9a6a3a", "8a5e32", "a87844"), r=5.0, squash=0.8),
    "shadow_thread": item(lambda px, r: [put(px, x, int(8 + 3 * ((x * 7 % 5) - 2) / 2), hexc(["5a3a78", "7a5a98"][x % 2])) for x in range(2, 14)]),
    "flint": gem(pal("5a5a60", "3a3a40", "2a2a30")),
    "ember_shard": gem(pal("ffd060", "ff9a30", "c85a10")),
    "bow": item(lambda px, r: ([put(px, int(4 + 7 * (1 - ((k - 6.5) / 6.5) ** 2) * 0.6) + k // 3, 2 + k, hexc(["8a6a3c", "6e5131"][k % 2])) for k in range(13)],
                               [put(px, 5 + k // 4, 2 + k, hexc("e8e8f0")) for k in range(13)])),
    "arrow": item(lambda px, r: ([put(px, 3 + k, 12 - k, hexc("8a6a3c")) for k in range(8)], [put(px, 11 + dx, 4 + dy, hexc("6a6a70")) for dx, dy in ((0, 0), (1, 0), (0, -1), (1, -1), (2, -2))],
                                 [put(px, 2 + dx, 13 + dy, hexc("e8e8f0")) for dx, dy in ((0, 0), (1, 0), (0, -1), (-1, 1), (1, 1))])),
    "ember_striker": item(lambda px, r: ([put(px, 3 + k, 12 - k, hexc(["9a9a9a", "c8c8c8"][k % 2])) for k in range(6)],
                                         [put(px, 9 + dx, 6 + dy, hexc(["ff9a30", "ffd060"][(dx + dy) & 1])) for dx in range(-1, 3) for dy in range(-2, 2)])),
})

for t in TIER:
    for k in ("pickaxe", "axe", "shovel", "sword"):
        ITEM_RECIPES[f"{t}_{k}"] = tool(k, t)

def main():
    force = "--force" in sys.argv
    os.makedirs(OUT, exist_ok=True)
    made = 0
    for name, recipe in sorted(RECIPES.items()):
        path = os.path.join(OUT, name + ".png")
        if os.path.exists(path) and not force: continue
        px = recipe(Rng(name))
        assert len(px) == N * N, name
        write_png(path, [tuple(int(c) for c in p) for p in px]); made += 1
    items_dir = os.path.join(ROOT, "assets", "textures", "items"); os.makedirs(items_dir, exist_ok=True)
    for name, recipe in sorted(ITEM_RECIPES.items()):
        path = os.path.join(items_dir, name + ".png")
        if os.path.exists(path) and not force: continue
        px = recipe(Rng(name)); write_png(path, [tuple(int(c) for c in p) for p in px]); made += 1
    ent_dir = os.path.join(ROOT, "assets", "textures", "entities"); os.makedirs(ent_dir, exist_ok=True)
    for name, recipe in sorted(ENTITY_RECIPES.items()):
        path = os.path.join(ent_dir, name + ".png")
        if os.path.exists(path) and not force: continue
        px = recipe(Rng(name)); write_png(path, [tuple(int(c) for c in p) for p in px]); made += 1
    print(f"gen_textures: {made} written ({OUT}, {items_dir}, {ent_dir})")

if __name__ == "__main__":
    main()
