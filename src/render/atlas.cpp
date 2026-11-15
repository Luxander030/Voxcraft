#include "render/atlas.h"
#include "core/util.h"

namespace vx {

bool TexturePack::load(const std::string& path, std::string* err) {
    auto bytes = readFile(pathFromUtf8(path));
    if (!bytes) { if (err) *err = "cannot read " + path + " (run tools/build_assets.py)"; return false; }
    return parse(*bytes, err);
}

bool TexturePack::parse(const std::vector<u8>& b, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = "pak: " + m; return false; };
    if (b.size() < 24 || std::memcmp(b.data(), "VXPK", 4) != 0) return fail("bad magic");
    u32 stored = (u32)b[b.size() - 4] | ((u32)b[b.size() - 3] << 8) | ((u32)b[b.size() - 2] << 16) | ((u32)b[b.size() - 1] << 24);
    if (crc32(b.data(), b.size() - 4) != stored) return fail("checksum mismatch");
    ByteReader r(b.data() + 4, b.size() - 8);
    if (r.u32_() != 1) return fail("unsupported version");
    tileSize = (int)r.u32_(); tileCount = (int)r.u32_(); u32 nameCount = r.u32_();
    if (tileSize != 16 || tileCount <= 0 || tileCount > 4096 || nameCount > 65536) return fail("bad header");
    names.clear();
    for (u32 i = 0; i < nameCount && r.ok(); i++) {
        u16 len = r.u16_(); std::string n(len, '\0'); r.bytes(n.data(), len);
        u16 t = r.u16_(); if (t >= tileCount) return fail("bad tile index");
        names[n] = t;
    }
    flags.resize(tileCount); r.bytes(flags.data(), flags.size());
    rgba.resize((size_t)tileCount * tileSize * tileSize * 4);
    if (!r.bytes(rgba.data(), rgba.size()) || !r.ok()) return fail("truncated");
    return true;
}

std::vector<u8> TexturePack::buildMips(int& levels) const {
    levels = 0; for (int s = tileSize; s >= 1; s >>= 1) levels++;
    std::vector<u8> out(rgba);
    std::vector<u8> prev(rgba);
    for (int lvl = 1, s = tileSize / 2; lvl < levels; lvl++, s /= 2) {
        int ps = s * 2;
        std::vector<u8> cur((size_t)tileCount * s * s * 4);
        for (int t = 0; t < tileCount; t++) {
            const u8* src = prev.data() + (size_t)t * ps * ps * 4; u8* dst = cur.data() + (size_t)t * s * s * 4;
            bool cutout = (flags[t] & 1) && !(flags[t] & 2);
            for (int y = 0; y < s; y++) for (int x = 0; x < s; x++) {
                int sum[3] = {0, 0, 0}, n = 0, amax = 0, asum = 0;
                for (int k = 0; k < 4; k++) {
                    const u8* p = src + (((y * 2 + (k >> 1)) * ps) + x * 2 + (k & 1)) * 4;
                    asum += p[3]; amax = std::max(amax, (int)p[3]);
                    if (p[3] > 0) { for (int c = 0; c < 3; c++) sum[c] += p[c]; n++; }
                }
                u8* d = dst + (y * s + x) * 4;
                for (int c = 0; c < 3; c++) d[c] = (u8)(n ? sum[c] / n : 0);
                // cutout: keep a texel if at least half its children were solid (keeps leaves from vanishing)
                d[3] = cutout ? (u8)(asum >= 2 * 255 ? 255 : 0) : (u8)(asum / 4);
                (void)amax;
            }
        }
        out.insert(out.end(), cur.begin(), cur.end());
        prev.swap(cur);
    }
    return out;
}

bool BlockTextures::build(const BlockRegistry& reg, const TexturePack& pack, std::string* err) {
    int miss = pack.layer("missing");
    missing_ = (u16)(miss >= 0 ? miss : 0);
    faces_.assign(reg.stateCount() * 6, missing_);
    std::string missingNames;
    for (const BlockDef& d : reg.defs()) {
        u16 base[6];
        for (int f = 0; f < 6; f++) {
            int l = d.tex[f].empty() ? -1 : pack.layer(d.tex[f]);
            if (l < 0 && !d.tex[f].empty() && missingNames.find(d.tex[f]) == std::string::npos) missingNames += d.tex[f] + " ";
            base[f] = l < 0 ? missing_ : (u16)l;
        }
        int front = d.texFront.empty() ? -1 : pack.layer(d.texFront);
        int pFacing = d.propIndex("facing"), pLit = d.propIndex("lit");
        for (u16 i = 0; i < d.stateCount; i++) {
            BlockState s = (BlockState)(d.base + i);
            u16 t[6]; std::copy(base, base + 6, t);
            if (front >= 0 && pFacing >= 0) {
                int fv = reg.getProp(s, pFacing), cnt = d.props[pFacing].count;
                // 4-way: 0 N(-Z) 1 S(+Z) 2 W(-X) 3 E(+X);  6-way: 0 down 1 up 2 N 3 S 4 W 5 E
                static const int four[4] = {FACE_NZ, FACE_PZ, FACE_NX, FACE_PX};
                static const int six[6] = {FACE_NY, FACE_PY, FACE_NZ, FACE_PZ, FACE_NX, FACE_PX};
                int ff = cnt == 6 ? six[fv] : four[fv & 3];
                if (cnt == 6) {   // directional block: front on facing side, "top" texture on the back
                    int back = ff ^ 1;
                    for (int f = 0; f < 6; f++) t[f] = base[FACE_NX];
                    t[back] = base[FACE_PY];
                }
                t[ff] = (u16)front;
            }
            if (pLit >= 0 && d.shape != BlockShape::Torch && reg.getProp(s, pLit)) {
                // lit variant: "<name>_off" -> "<name>_on" (or "<name>" -> "<name>_on") when that texture exists
                for (int f = 0; f < 6; f++) {
                    std::string n = d.tex[f];
                    if (front >= 0 && t[f] == (u16)front) n = d.texFront;
                    std::string on = n.size() > 4 && n.compare(n.size() - 4, 4, "_off") == 0 ? n.substr(0, n.size() - 4) + "_on" : n + "_on";
                    int l = pack.layer(on);
                    if (l >= 0) t[f] = (u16)l;
                }
            }
            if (int pAge = d.propIndex("age"); pAge >= 0) {   // growth stages: "<name>_0" -> "<name>_<age>"
                const std::string& n = d.tex[FACE_NX];
                if (n.size() > 2 && n.compare(n.size() - 2, 2, "_0") == 0) {
                    int l = pack.layer(n.substr(0, n.size() - 1) + std::to_string(reg.getProp(s, pAge)));
                    if (l >= 0) for (int f = 0; f < 6; f++) t[f] = (u16)l;
                }
            }
            if (d.shape == BlockShape::Rail) {   // rails: "top" straight; "side" = curve (rail) or powered look (powered rail)
                bool alt = d.propIndex("powered") >= 0 ? reg.getProp(s, "powered") != 0 : reg.getProp(s, "shape") >= 6;
                for (int f = 0; f < 6; f++) t[f] = alt ? base[FACE_NX] : base[FACE_PY];
            }
            if (d.shape == BlockShape::Door) {   // doors: "top" texture on the upper half, "side" on the lower
                u16 l = reg.getProp(s, "upper") ? base[FACE_PY] : base[FACE_NX];
                for (int f = 0; f < 6; f++) t[f] = l;
            }
            if (pLit >= 0 && d.shape == BlockShape::Torch) {   // torch-shaped: "top" = lit look, "side" = unlit
                u16 l = reg.getProp(s, pLit) ? base[FACE_PY] : base[FACE_NX];
                for (int f = 0; f < 6; f++) t[f] = l;
            }
            for (int f = 0; f < 6; f++) faces_[(size_t)s * 6 + f] = t[f];
        }
    }
    if (!missingNames.empty()) { if (err) *err = "missing textures: " + missingNames; return false; }
    return true;
}

} // namespace vx
