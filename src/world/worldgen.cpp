#include "world/worldgen.h"
#include "core/profiler.h"

namespace vx {

const char* biomeName(Biome b) {
    static const char* n[] = {"ocean", "deep_ocean", "beach", "plains", "forest", "birch_forest", "taiga", "tundra", "desert",
                              "badlands", "savanna", "swamp", "mountains", "snowy_peaks", "river", "frozen_ocean", "hollow"};
    return (int)b < (int)Biome::Count ? n[(int)b] : "unknown";
}

BiomeColors biomeColors(Biome b) {
    switch (b) {
        case Biome::Plains: return {0x8DB360, 0x6FA33C, 0x3F76E4};
        case Biome::Forest: return {0x6FAE45, 0x4F9A2A, 0x3F76E4};
        case Biome::BirchForest: return {0x88BB67, 0x6BA941, 0x3F76E4};
        case Biome::Taiga: return {0x7FA67A, 0x5E8F5A, 0x3D57D6};
        case Biome::Tundra: case Biome::SnowyPeaks: case Biome::FrozenOcean: return {0x80B497, 0x60A17B, 0x3938C9};
        case Biome::Desert: case Biome::Badlands: return {0xBFB755, 0xAEA42A, 0x32A598};
        case Biome::Savanna: return {0xBFB755, 0xAEA42A, 0x2C8B9C};
        case Biome::Swamp: return {0x6A7039, 0x5B6A2B, 0x4C6559};
        case Biome::Mountains: return {0x8AB689, 0x6DA36B, 0x3F76E4};
        case Biome::Ocean: case Biome::DeepOcean: case Biome::River: return {0x8DB360, 0x6FA33C, 0x3F76E4};
        case Biome::Hollow: return {0x8A6A50, 0x7A5A40, 0x5A4A8A};
        default: return {0x8DB360, 0x6FA33C, 0x3F76E4};
    }
}

namespace {

inline float smoothstep(float a, float b, float x) { float t = clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline int ifloor(float v) { int i = (int)v; return v < (float)i ? i - 1 : i; }

enum class TreeKind : u8 { None, Oak, BigOak, Birch, Pine, Cactus, Acacia };

class FlatGenerator final : public WorldGenerator {
public:
    explicit FlatGenerator(const BlockRegistry& r) { cb.resolve(r); }
    void generate(Chunk& c) const override {
        for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            c.setGen(x, 0, z, cb.bedrock);
            for (int y = 1; y < 4; y++) c.setGen(x, y, z, cb.dirt);
            c.setGen(x, 4, z, cb.turf);
            c.biome[x + z * 16] = (u8)Biome::Plains;
        }
        c.compact();
    }
    ColumnInfo column(int, int) const override { return {5, Biome::Plains, false}; }
    u32 version() const override { return 1; }
    CoreBlocks cb;
};

class GeneratorV1 final : public WorldGenerator {
public:
    GeneratorV1(const GenSettings& s, const BlockRegistry& r) : st(s), reg(r),
        contN(mix64(s.seed ^ 0xC0A7)), eroN(mix64(s.seed ^ 0xE205)), ridN(mix64(s.seed ^ 0x41D6E)), hillN(mix64(s.seed ^ 0x4111)),
        tempN(mix64(s.seed ^ 0x7E3A)), humN(mix64(s.seed ^ 0x4C3D)), rivN(mix64(s.seed ^ 0x51FE)), detN(mix64(s.seed ^ 0xDE7A)),
        caveA(mix64(s.seed ^ 0xCA0E1)), caveB(mix64(s.seed ^ 0xCA0E2)), caveC(mix64(s.seed ^ 0xCA0E3)) { cb.resolve(r); }

    u32 version() const override { return 1; }

    struct Climate { float cont, ero, ridge, temp, hum, river; };
    Climate climate(int x, int z) const {
        float k = 1.0f / st.terrainScale;
        float fx = (float)x * k, fz = (float)z * k;
        Climate c;
        c.cont = clamp(contN.fbm2(fx / 900.0f, fz / 900.0f, 5) * 2.4f + 0.12f, -1.0f, 1.0f);
        c.ero = clamp(eroN.fbm2(fx / 520.0f, fz / 520.0f, 4) * 2.2f, -1.0f, 1.0f);
        float r = ridN.fbm2(fx / 240.0f, fz / 240.0f, 4) * 2.0f;
        c.ridge = clamp(1.0f - (r < 0 ? -r : r), 0.0f, 1.0f);
        c.temp = clamp(tempN.fbm2(fx / 1400.0f, fz / 1400.0f, 3) * 2.4f, -1.0f, 1.0f);
        c.hum = clamp(humN.fbm2(fx / 1100.0f, fz / 1100.0f, 3) * 2.4f, -1.0f, 1.0f);
        float rv = rivN.fbm2(fx / 650.0f, fz / 650.0f, 4) * 2.0f;
        c.river = rv < 0 ? -rv : rv;
        return c;
    }

    ColumnInfo column(int x, int z) const override {
        Climate c = climate(x, z);
        float h; float landT = 0;
        if (c.cont < -0.45f) h = 30.0f + (c.cont + 1.0f) / 0.55f * 15.0f;
        else if (c.cont < -0.15f) h = 45.0f + (c.cont + 0.45f) / 0.30f * 13.0f;
        else if (c.cont < -0.02f) h = 58.0f + (c.cont + 0.15f) / 0.13f * 5.5f;
        else { landT = clamp(c.cont / 0.6f, 0.0f, 1.0f); h = 64.0f + landT * 12.0f; }
        float hills = hillN.fbm2((float)x / 110.0f, (float)z / 110.0f, 4) * 2.0f;
        h += hills * (2.5f + 7.0f * landT);
        float mnt = smoothstep(0.05f, 0.5f, c.cont) * smoothstep(0.15f, -0.45f, c.ero);
        h += mnt * c.ridge * c.ridge * 100.0f;
        bool river = false;
        if (h > 60.0f && c.river < 0.045f && mnt < 0.35f) {
            float t = smoothstep(0.0f, 0.045f, c.river);
            float nh = lerpf(58.0f, h, t);
            if (nh < (float)SEA_LEVEL + 0.5f) river = true;
            h = nh;
        }
        int hi = clamp(ifloor(h), 4, WORLD_H - 20);
        Biome b;
        if (river) b = Biome::River;
        else if (hi < SEA_LEVEL - 1) b = c.temp < -0.5f ? Biome::FrozenOcean : (c.cont < -0.45f ? Biome::DeepOcean : Biome::Ocean);
        else if (hi > 135 || (hi > 105 && c.temp < -0.2f)) b = Biome::SnowyPeaks;
        else if (hi > 100 && mnt > 0.3f) b = Biome::Mountains;
        else if (hi <= SEA_LEVEL + 2 && c.cont < 0.06f) b = c.temp < -0.5f ? Biome::Tundra : Biome::Beach;
        else if (c.temp < -0.5f) b = Biome::Tundra;
        else if (c.temp < -0.18f) b = Biome::Taiga;
        else if (c.temp > 0.45f) b = (c.hum < -0.35f && c.cont > 0.2f) ? Biome::Badlands : (c.hum < 0.0f ? Biome::Desert : Biome::Savanna);
        else if (c.hum > 0.4f && hi < 70) b = Biome::Swamp;
        else if (c.hum > 0.05f) b = c.temp > 0.15f ? Biome::Forest : Biome::BirchForest;
        else b = Biome::Plains;
        return {hi, b, river};
    }

    void generate(Chunk& ch) const override {
        VX_TIME_US(Stat::GenTimeUs);
        const int bx = ch.pos.x * 16, bz = ch.pos.z * 16;
        ColumnInfo cols[256];
        int maxH = 0;
        for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            cols[x + z * 16] = column(bx + x, bz + z);
            ch.biome[x + z * 16] = (u8)cols[x + z * 16].biome;
            maxH = std::max(maxH, cols[x + z * 16].height);
        }
        // --- base terrain
        for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            const ColumnInfo& ci = cols[x + z * 16];
            int wx = bx + x, wz = bz + z;
            u64 hcol = hashCoords(st.seed ^ 0xBED, wx, wz);
            ch.setGen(x, 0, z, cb.bedrock);
            for (int y = 1; y < 4; y++) if ((int)((hcol >> (y * 8)) & 3) >= y) ch.setGen(x, y, z, cb.bedrock);
            int slateTop = 12 + (int)((hcol >> 40) & 3);
            int depth = 3 + (int)((hcol >> 44) & 1);
            BlockState top, under, deep = cb.stone;
            surfaceFor(ci, wx, wz, top, under);
            bool underwater = ci.height < SEA_LEVEL;
            for (int y = 1; y <= ci.height; y++) {
                if (ch.get(x, y, z) == cb.bedrock) continue;
                BlockState s;
                if (y == ci.height) s = top;
                else if (y > ci.height - depth) s = under;
                else s = y < slateTop ? cb.slate : deep;
                ch.setGen(x, y, z, s);
            }
            if (underwater || ci.biome == Biome::River)
                for (int y = ci.height + 1; y <= SEA_LEVEL; y++) ch.setGen(x, y, z, cb.water);
            if ((ci.biome == Biome::FrozenOcean || (ci.biome == Biome::River && climate(wx, wz).temp < -0.5f)) && ci.height < SEA_LEVEL)
                ch.setGen(x, SEA_LEVEL, z, cb.ice);
        }
        if (st.caves) carveCaves(ch, cols);
        if (st.ores) placeOres(ch);
        if (st.trees || st.structures) placeFeatures(ch);
        placeVegetation(ch, cols);
        ch.compact();
        Profiler::add(Stat::ChunksGenerated);
    }

private:
    void surfaceFor(const ColumnInfo& ci, int wx, int wz, BlockState& top, BlockState& under) const {
        u64 h = hashCoords(st.seed ^ 0x5F, wx, wz);
        float d = detN.noise2((float)wx / 12.0f, (float)wz / 12.0f);
        top = cb.turf; under = cb.dirt;
        if (ci.height < SEA_LEVEL) {   // sea / lake floor
            if (ci.height < SEA_LEVEL - 12) { top = under = cb.gravel; }
            else { top = under = (d > 0.35f ? cb.clay : (d < -0.3f ? cb.gravel : cb.sand)); }
            return;
        }
        switch (ci.biome) {
            case Biome::River: top = under = d > 0.2f ? cb.gravel : cb.sand; break;
            case Biome::Beach: case Biome::Desert: top = under = cb.sand; if (ci.biome == Biome::Desert && (h & 0xF) == 0) under = cb.sandstone; break;
            case Biome::Badlands: top = cb.red_sand; under = cb.sandstone; break;
            case Biome::Tundra: top = cb.snow; break;
            case Biome::SnowyPeaks: top = ci.height > 150 || d > -0.2f ? cb.snow : cb.stone; under = cb.stone; break;
            case Biome::Mountains: if (d > 0.25f) { top = cb.stone; under = cb.stone; } else if (d < -0.45f) { top = cb.gravel; under = cb.stone; } break;
            case Biome::Taiga: if (d > 0.3f) top = cb.podzol; break;
            default: break;
        }
    }

    // Cave densities are sampled on a 4x4x4 lattice and trilinearly interpolated (~64x fewer noise calls).
    void carveCaves(Chunk& ch, const ColumnInfo* cols) const {
        const int bx = ch.pos.x * 16, bz = ch.pos.z * 16;
        constexpr int NY = 33;   // y = 0..128 step 4
        static thread_local float lat[5][NY][5];
        for (int lz = 0; lz < 5; lz++) for (int ly = 0; ly < NY; ly++) for (int lx = 0; lx < 5; lx++) {
            float wx = (float)(bx + lx * 4), wy = (float)(ly * 4), wz = (float)(bz + lz * 4);
            float cheese = caveA.noise3(wx / 56.0f, wy / 34.0f, wz / 56.0f) + 0.25f * caveA.noise3(wx / 17.0f, wy / 13.0f, wz / 17.0f);
            float a = caveB.noise3(wx / 38.0f, wy / 26.0f, wz / 38.0f), b = caveC.noise3(wx / 38.0f, wy / 26.0f, wz / 38.0f);
            float sp = (a * a + b * b) - 0.0045f;
            float ch_ = 0.42f - cheese;
            lat[lz][ly][lx] = std::min(ch_ * 0.05f, sp);
        }
        for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            const ColumnInfo& ci = cols[x + z * 16];
            bool wet = ci.height < SEA_LEVEL || ci.biome == Biome::River;
            int top = std::min(127, ci.height - (wet ? 9 : 6));
            int gx = x >> 2, gz = z >> 2; float fx = (float)(x & 3) / 4.0f, fz = (float)(z & 3) / 4.0f;
            for (int y = 1; y <= top; y++) {
                int gy = y >> 2; float fy = (float)(y & 3) / 4.0f;
                auto L = [&](int dx, int dy, int dz) { return lat[gz + dz][gy + dy][gx + dx]; };
                float v00 = lerpf(L(0, 0, 0), L(1, 0, 0), fx), v10 = lerpf(L(0, 1, 0), L(1, 1, 0), fx);
                float v01 = lerpf(L(0, 0, 1), L(1, 0, 1), fx), v11 = lerpf(L(0, 1, 1), L(1, 1, 1), fx);
                float v = lerpf(lerpf(v00, v10, fy), lerpf(v01, v11, fy), fz);
                if (v < 0) {
                    BlockState cur = ch.get(x, y, z);
                    if (cur == cb.bedrock || cur == cb.water) continue;
                    ch.setGen(x, y, z, y <= 10 ? cb.lava : AIR);
                }
            }
        }
    }

    void placeOres(Chunk& ch) const {
        struct Ore { BlockState s; int veins, size, ymin, ymax; };
        const Ore ores[] = {
            {cb.coal_ore, 18, 12, 5, 128}, {cb.copper_ore, 10, 9, 24, 96}, {cb.iron_ore, 14, 8, 4, 72}, {cb.gold_ore, 3, 7, 4, 32},
            {cb.glimmer_ore, 6, 6, 4, 18}, {cb.azurite_ore, 1, 5, 4, 15}, {cb.gravel, 6, 22, 8, 100}, {cb.dirt, 6, 22, 30, 110},
        };
        Rng rng(hashCoords(st.seed ^ 0x0E5, ch.pos.x, ch.pos.z));
        for (const Ore& o : ores) for (int v = 0; v < o.veins; v++) {
            float x = (float)rng.range(16), z = (float)rng.range(16), y = (float)(o.ymin + rng.range(o.ymax - o.ymin));
            for (int i = 0; i < o.size; i++) {
                int ix = ifloor(x), iy = ifloor(y), iz = ifloor(z);
                if (ix >= 0 && ix < 16 && iz >= 0 && iz < 16 && iy > 0 && iy < WORLD_H) {
                    BlockState cur = ch.get(ix, iy, iz);
                    if (cur == cb.stone || cur == cb.slate) ch.setGen(ix, iy, iz, o.s);
                }
                x += rng.unit() * 2.0f - 1.0f; y += rng.unit() * 1.4f - 0.7f; z += rng.unit() * 2.0f - 1.0f;
            }
        }
    }

    // ---- cross-chunk features: evaluate candidates of the 3x3 neighbourhood, clip to this chunk
    struct Writer {
        Chunk& ch; int bx, bz; const CoreBlocks& cb; const BlockRegistry& reg;
        bool inside(int wx, int y, int wz) const { return wx >= bx && wx < bx + 16 && wz >= bz && wz < bz + 16 && y > 0 && y < WORLD_H; }
        void log(int wx, int y, int wz, BlockState s) {
            if (!inside(wx, y, wz)) return;
            BlockState cur = ch.get(wx - bx, y, wz - bz);
            if (cur == AIR || reg.def(cur).has(BF_LEAVES) || (reg.def(cur).has(BF_REPLACEABLE) && !reg.fluid(cur)) || cur == cb.snow)
                ch.setGen(wx - bx, y, wz - bz, s);
        }
        void leaf(int wx, int y, int wz, BlockState s) {
            if (!inside(wx, y, wz)) return;
            BlockState cur = ch.get(wx - bx, y, wz - bz);
            if (cur == AIR || (reg.def(cur).has(BF_REPLACEABLE) && !reg.fluid(cur))) ch.setGen(wx - bx, y, wz - bz, s);
        }
        void solid(int wx, int y, int wz, BlockState s) { if (inside(wx, y, wz)) ch.setGen(wx - bx, y, wz - bz, s); }
        void ground(int wx, int y, int wz, BlockState s) {   // only replaces surface soil
            if (!inside(wx, y, wz)) return;
            BlockState cur = ch.get(wx - bx, y, wz - bz);
            if (cur == cb.turf || cur == cb.snow || cur == cb.podzol) ch.setGen(wx - bx, y, wz - bz, s);
        }
    };

    static float treeDensity(Biome b) {
        switch (b) {
            case Biome::Forest: return 0.9f; case Biome::BirchForest: return 0.8f; case Biome::Taiga: return 0.75f;
            case Biome::Swamp: return 0.35f; case Biome::Plains: return 0.04f; case Biome::Savanna: return 0.08f;
            case Biome::Tundra: return 0.03f; case Biome::Mountains: return 0.1f; case Biome::Desert: return 0.12f; case Biome::Badlands: return 0.04f;
            default: return 0.0f;
        }
    }

    void placeFeatures(Chunk& ch) const {
        Writer w{ch, ch.pos.x * 16, ch.pos.z * 16, cb, reg};
        if (st.structures && st.version >= 3) placeVillage(w, ch.pos);   // v1/v2 output stays bit-identical
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            int cx = ch.pos.x + dx, cz = ch.pos.z + dz;
            if (st.structures) {
                Rng srng(hashCoords(st.seed ^ 0x5717C7, cx, cz));
                if (srng.range(120) == 0) {
                    int ox = cx * 16 + 4 + srng.range(8), oz = cz * 16 + 4 + srng.range(8);
                    ColumnInfo ci = column(ox, oz);
                    if (ci.height > SEA_LEVEL && (ci.biome == Biome::Plains || ci.biome == Biome::Forest || ci.biome == Biome::Desert || ci.biome == Biome::Savanna || ci.biome == Biome::Taiga))
                        placeRuin(w, ox, ci.height, oz, srng, ci.biome == Biome::Desert);
                }
                if (st.version >= 2) placeV2Structures(w, cx, cz);   // v1 output stays bit-identical
            }
            if (!st.trees) continue;
            for (int i = 0; i < 10; i++) {
                Rng rng(hashCoords(st.seed ^ 0x77EE, cx, cz, i));
                int wx = cx * 16 + rng.range(16), wz = cz * 16 + rng.range(16);
                ColumnInfo ci = column(wx, wz);
                float dens = treeDensity(ci.biome);
                if (rng.unit() * 1.0f >= dens * 0.6f) continue;
                if (ci.height <= SEA_LEVEL || ci.river) continue;
                TreeKind k = TreeKind::Oak;
                switch (ci.biome) {
                    case Biome::BirchForest: k = rng.chance(0.8f) ? TreeKind::Birch : TreeKind::Oak; break;
                    case Biome::Forest: k = rng.chance(0.25f) ? TreeKind::Birch : (rng.chance(0.12f) ? TreeKind::BigOak : TreeKind::Oak); break;
                    case Biome::Taiga: case Biome::Tundra: case Biome::Mountains: k = TreeKind::Pine; break;
                    case Biome::Desert: case Biome::Badlands: k = TreeKind::Cactus; break;
                    case Biome::Savanna: k = TreeKind::Acacia; break;
                    default: break;
                }
                placeTree(w, wx, ci.height + 1, wz, k, rng);
            }
        }
    }

    void placeTree(Writer& w, int x, int y, int z, TreeKind k, Rng& rng) const {
        if (k == TreeKind::Cactus) {
            int h = 1 + rng.range(3);
            for (int i = 0; i < h; i++) w.leaf(x, y + i, z, cb.cactus);
            return;
        }
        if (k == TreeKind::Pine) {
            int h = 7 + rng.range(5);
            w.ground(x, y - 1, z, cb.dirt);
            for (int i = 0; i < h; i++) w.log(x, y + i, z, cb.pine_log);
            int top = y + h;
            w.leaf(x, top, z, cb.pine_leaves); w.leaf(x, top + 1, z, cb.pine_leaves);
            int r = 1;
            for (int yy = top - 1; yy >= y + 2; yy--) {
                for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++)
                    if (dx * dx + dz * dz <= r * r + 1 && !(dx == 0 && dz == 0)) w.leaf(x + dx, yy, z + dz, cb.pine_leaves);
                r = ((top - yy) % 3 == 2) ? std::max(1, r - 1) : std::min(3, r + 1);
            }
            return;
        }
        if (k == TreeKind::Acacia) {
            int h = 4 + rng.range(2);
            w.ground(x, y - 1, z, cb.dirt);
            int dx = rng.range(3) - 1, dz = rng.range(3) - 1;
            int cx = x, cz = z;
            for (int i = 0; i < h; i++) { if (i >= h - 2) { cx += dx; cz += dz; } w.log(cx, y + i, cz, cb.oak_log); }
            int top = y + h;
            for (int ddz = -3; ddz <= 3; ddz++) for (int ddx = -3; ddx <= 3; ddx++)
                if (std::abs(ddx) + std::abs(ddz) <= 4) w.leaf(cx + ddx, top, cz + ddz, cb.oak_leaves);
            for (int ddz = -1; ddz <= 1; ddz++) for (int ddx = -1; ddx <= 1; ddx++) w.leaf(cx + ddx, top + 1, cz + ddz, cb.oak_leaves);
            return;
        }
        BlockState logS = k == TreeKind::Birch ? cb.birch_log : cb.oak_log;
        BlockState leafS = k == TreeKind::Birch ? cb.birch_leaves : cb.oak_leaves;
        int h = (k == TreeKind::Birch ? 5 : 4) + rng.range(3) + (k == TreeKind::BigOak ? 3 : 0);
        w.ground(x, y - 1, z, cb.dirt);
        for (int i = 0; i < h; i++) w.log(x, y + i, z, logS);
        int top = y + h - 1;
        int rad = k == TreeKind::BigOak ? 3 : 2;
        for (int yy = top - 2; yy <= top + 1; yy++) {
            int r = yy >= top ? rad - 1 : rad;
            for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
                bool corner = std::abs(dx) == r && std::abs(dz) == r;
                if (corner && (yy >= top || (hashCoords(st.seed, x + dx, yy, z + dz) & 1))) continue;
                w.leaf(x + dx, yy, z + dz, leafS);
            }
        }
    }

    // ---- generator v2 structures. Each has an extent <= 8 blocks from its origin, so evaluating
    // the 3x3 neighbourhood of candidate chunks covers it. Chests are generated EMPTY: their loot
    // is rolled deterministically from (seed, position) the first time they are opened, so a
    // structure never makes an untouched chunk need storage.
    void placeV2Structures(Writer& w, int cx, int cz) const {
        Rng r(hashCoords(st.seed ^ 0x57A2, cx, cz));
        int roll = r.range(1000);
        int ox = cx * 16 + 4 + r.range(8), oz = cz * 16 + 4 + r.range(8);
        ColumnInfo ci = column(ox, oz);
        bool land = ci.height > SEA_LEVEL && !ci.river;
        if (roll < 6 && land && (ci.biome == Biome::Plains || ci.biome == Biome::Forest || ci.biome == Biome::Taiga || ci.biome == Biome::Savanna))
            placeWatchtower(w, ox, ci.height, oz, r);
        else if (roll >= 6 && roll < 18 && land)
            placeCache(w, ox, 18 + r.range(20), oz, r);
        else if (roll >= 18 && roll < 34 && land && ci.biome == Biome::Desert)
            placeWell(w, ox, ci.height, oz);
    }

    // ---- generator v3: villages. A village belongs to an 8x8-chunk region and extends at most
    // ~26 blocks from its centre, which is kept >= 28 blocks inside the region, so only chunks of
    // that region can be touched. Every chunk recomputes the (cheap, deterministic) layout and
    // writes the parts that fall inside it.
    // Village centre of region (rx, rz), if the region has one. Shared by placement and /locate.
    bool villageInRegion(int rx, int rz, int& cx, int& cz, ColumnInfo& c0, Rng& r) const {
        r = Rng(hashCoords(st.seed ^ 0x1A6E, rx, rz));
        if (r.range(100) >= 30) return false;
        cx = rx * 128 + 28 + r.range(72); cz = rz * 128 + 28 + r.range(72);
        c0 = column(cx, cz);
        bool desert = c0.biome == Biome::Desert;
        return c0.height > SEA_LEVEL && !c0.river && (c0.biome == Biome::Plains || c0.biome == Biome::Savanna || desert || c0.biome == Biome::Taiga);
    }

    bool nearestVillage(int x, int z, int radius, int& vx, int& vy, int& vz) const override {
        if (st.version < 3 || !st.structures) return false;
        int rx0 = floorDiv(x, 128), rz0 = floorDiv(z, 128); long best = -1;
        for (int dz = -radius; dz <= radius; dz++) for (int dx = -radius; dx <= radius; dx++) {
            int cx, cz; ColumnInfo c0; Rng r(0);
            if (!villageInRegion(rx0 + dx, rz0 + dz, cx, cz, c0, r)) continue;
            long d = (long)(cx - x) * (cx - x) + (long)(cz - z) * (cz - z);
            if (best < 0 || d < best) { best = d; vx = cx; vy = c0.height + 1; vz = cz; }
        }
        return best >= 0;
    }

    void placeVillage(Writer& w, ChunkPos cp) const {
        int rx = floorDiv(cp.x, 8), rz = floorDiv(cp.z, 8);
        {   // cheap reject before evaluating terrain: is this chunk anywhere near the region's village?
            Rng q(hashCoords(st.seed ^ 0x1A6E, rx, rz));
            if (q.range(100) >= 30) return;
            int qx = rx * 128 + 28 + q.range(72), qz = rz * 128 + 28 + q.range(72);
            if (cp.x * 16 + 15 < qx - 28 || cp.x * 16 > qx + 28 || cp.z * 16 + 15 < qz - 28 || cp.z * 16 > qz + 28) return;
        }
        int cx, cz; ColumnInfo c0; Rng r(0);
        if (!villageInRegion(rx, rz, cx, cz, c0, r)) return;
        bool desert = c0.biome == Biome::Desert;
        int y0 = c0.height;
        BlockState path = desert ? cb.sandstone : cb.gravel, plaza = desert ? cb.sandstone : cb.cobblestone;
        // plaza with a well and lamp posts
        for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++) {
            for (int yy = y0 - 2; yy <= y0; yy++) w.solid(cx + dx, yy, cz + dz, yy == y0 ? plaza : cb.dirt);
            for (int yy = y0 + 1; yy <= y0 + 5; yy++) w.solid(cx + dx, yy, cz + dz, AIR);
        }
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            bool rim = dx != 0 || dz != 0;
            w.solid(cx + dx, y0 + 1, cz + dz, rim ? plaza : AIR);
            for (int yy = y0 - 3; yy <= y0; yy++) w.solid(cx + dx, yy, cz + dz, rim ? plaza : cb.water);
        }
        for (int s : {-4, 4}) for (int t : {-4, 4}) {
            for (int yy = 1; yy <= 3; yy++) w.solid(cx + s, y0 + yy, cz + t, cb.oak_log);
            w.solid(cx + s, y0 + 4, cz + t, reg.withProp(cb.torch, "facing", 4));
        }
        // cottages on a ring, doors facing the plaza
        int n = 4 + r.range(4);
        float a0 = r.unit() * 2 * PI;
        for (int i = 0; i < n; i++) {
            float a = a0 + i * 2 * PI / n + (r.unit() - 0.5f) * 0.3f;
            float dist = 13 + r.range(7);
            int hx = cx + (int)std::lround(std::cos(a) * dist), hz = cz + (int)std::lround(std::sin(a) * dist);
            ColumnInfo hc = column(hx, hz);
            if (hc.height <= SEA_LEVEL || hc.river || std::abs(hc.height - y0) > 6) continue;
            // door side: the axis-aligned side that faces the plaza
            int ddx = cx - hx, ddz = cz - hz;
            int face = std::abs(ddx) > std::abs(ddz) ? (ddx > 0 ? 3 : 2) : (ddz > 0 ? 1 : 0);   // 0 N 1 S 2 W 3 E
            Rng hr(hashCoords(st.seed ^ 0xC077, hx, hz));
            placeCottage(w, hx, hc.height, hz, face, hr, desert);
            // gravel path from the door to the plaza
            static const int FX[4] = {0, 0, -1, 1}, FZ[4] = {-1, 1, 0, 0};
            int px = hx + FX[face] * 3, pz = hz + FZ[face] * 3;
            for (int k = 0; k < 40; k++) {
                if (std::abs(px - cx) <= 4 && std::abs(pz - cz) <= 4) break;
                w.ground(px, column(px, pz).height, pz, path);
                if (std::abs(cx - px) > std::abs(cz - pz)) px += cx > px ? 1 : -1; else pz += cz > pz ? 1 : -1;
            }
        }
    }

    void placeCottage(Writer& w, int hx, int y, int hz, int face, Rng& rng, bool desert) const {
        BlockState wall = desert ? cb.sandstone : cb.oak_planks, corner = desert ? cb.sandstone : cb.oak_log, roof = desert ? cb.sandstone : reg.stateOf("pine_planks");
        int h = 4;
        for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
            for (int yy = y - 3; yy <= y; yy++) w.solid(hx + dx, yy, hz + dz, cb.cobblestone);   // foundation
            bool edgeX = std::abs(dx) == 2, edgeZ = std::abs(dz) == 2;
            for (int yy = 1; yy <= h; yy++) {
                BlockState b = AIR;
                if (edgeX && edgeZ) b = corner;
                else if (edgeX || edgeZ) b = (yy == 2 && (dx == 0 || dz == 0)) ? cb.glass : wall;
                w.solid(hx + dx, y + yy, hz + dz, b);
            }
            w.solid(hx + dx, y + h + 1, hz + dz, roof);
            if (std::abs(dx) <= 1 && std::abs(dz) <= 1) w.solid(hx + dx, y + h + 2, hz + dz, roof);
        }
        // door opening facing the plaza
        static const int FX[4] = {0, 0, -1, 1}, FZ[4] = {-1, 1, 0, 0};
        for (int yy = 1; yy <= 2; yy++) w.solid(hx + FX[face] * 2, y + yy, hz + FZ[face] * 2, AIR);
        // furnishings: torch, and one of workbench / chest / kiln in the back corner
        w.solid(hx - FX[face], y + 3, hz - FZ[face], reg.withProp(cb.torch, "facing", 4));
        w.solid(hx - FX[face], y + 2, hz - FZ[face], reg.stateOf("oak_planks"));
        int kind = rng.range(3);
        BlockState furn = kind == 0 ? cb.workbench : kind == 1 ? reg.withProp(cb.chest, "facing", face) : reg.withProp(cb.kiln, "facing", face);
        int sx = FZ[face] != 0 ? 1 : -FX[face], sz = FX[face] != 0 ? 1 : -FZ[face];
        w.solid(hx + sx, y + 1, hz + sz, furn);
    }

    void placeWatchtower(Writer& w, int ox, int y, int oz, Rng& rng) const {
        BlockState wall = reg.stateOf("stone_bricks"), wall2 = cb.cobblestone, plank = cb.oak_planks;
        int h = 10 + rng.range(5);
        for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
            for (int yy = y - 4; yy <= y; yy++) w.solid(ox + dx, yy, oz + dz, wall2);   // footing
            bool edge = std::abs(dx) == 2 || std::abs(dz) == 2;
            for (int yy = 1; yy <= h; yy++) {
                if (edge) {
                    bool door = dz == 2 && dx == 0 && yy <= 2;
                    bool window = (dx == 0 || dz == 0) && yy % 4 == 3;
                    w.solid(ox + dx, y + yy, oz + dz, door || window ? AIR : (rng.chance(0.25f) ? wall2 : wall));
                } else w.solid(ox + dx, y + yy, oz + dz, AIR);
            }
            w.solid(ox + dx, y + h + 1, oz + dz, plank);   // roof platform
            if (edge && ((dx + dz) & 1) == 0) w.solid(ox + dx, y + h + 2, oz + dz, wall);   // crenellations
        }
        // ladder on the north inner wall (facing south), hatch hole in the platform
        BlockState ladder = reg.withProp(reg.stateOf("ladder"), "facing", 1);
        for (int yy = 1; yy <= h + 1; yy++) w.solid(ox, y + yy, oz - 1, yy == h + 1 ? AIR : ladder);
        w.solid(ox, y + h + 1, oz - 1, AIR);
        w.solid(ox + 1, y + h + 2, oz + 1, reg.withProp(cb.chest, "facing", 1));
        w.solid(ox - 1, y + h + 2, oz + 1, reg.withProp(cb.torch, "facing", 4));
    }

    void placeCache(Writer& w, int ox, int y, int oz, Rng& rng) const {
        BlockState wall = reg.stateOf("stone_bricks");
        for (int dz = -3; dz <= 3; dz++) for (int dy = 0; dy <= 4; dy++) for (int dx = -3; dx <= 3; dx++) {
            bool shell = std::abs(dx) == 3 || std::abs(dz) == 3 || dy == 0 || dy == 4;
            w.solid(ox + dx, y + dy, oz + dz, shell ? (rng.chance(0.2f) ? cb.cobblestone : wall) : AIR);
        }
        w.solid(ox, y + 1, oz + 2, reg.withProp(cb.chest, "facing", 0));
        w.solid(ox - 2, y + 1, oz - 2, reg.withProp(cb.torch, "facing", 4));
        w.solid(ox + 2, y + 1, oz - 2, reg.stateOf("bookshelf"));
    }

    void placeWell(Writer& w, int ox, int y, int oz) const {
        for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
            bool rim = std::abs(dx) == 2 || std::abs(dz) == 2;
            for (int yy = y - 3; yy <= y; yy++) w.solid(ox + dx, yy, oz + dz, cb.sandstone);
            if (!rim && std::abs(dx) <= 1 && std::abs(dz) <= 1) for (int yy = y - 2; yy <= y; yy++) w.solid(ox + dx, yy, oz + dz, cb.water);
            w.solid(ox + dx, y + 1, oz + dz, rim ? cb.sandstone : AIR);
            if (std::abs(dx) == 2 && std::abs(dz) == 2) { w.solid(ox + dx, y + 2, oz + dz, cb.sandstone); w.solid(ox + dx, y + 3, oz + dz, cb.glowcrystal); }
        }
    }

    void placeRuin(Writer& w, int ox, int y, int oz, Rng& rng, bool desert) const {
        BlockState wall = desert ? cb.sandstone : cb.cobblestone;
        BlockState wall2 = desert ? cb.sandstone : reg.stateOf("stone_bricks");
        int r = 3;
        for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
            w.solid(ox + dx, y, oz + dz, wall2);                       // floor
            for (int yy = y - 3; yy < y; yy++) w.solid(ox + dx, yy, oz + dz, wall);   // foundation
            bool edge = std::abs(dx) == r || std::abs(dz) == r;
            int hgt = edge ? 1 + rng.range(4) : 0;
            for (int yy = 1; yy <= hgt; yy++) w.solid(ox + dx, y + yy, oz + dz, rng.chance(0.3f) ? wall2 : wall);
            if (!edge) for (int yy = 1; yy <= 5; yy++) w.solid(ox + dx, y + yy, oz + dz, AIR);
        }
        w.solid(ox, y + 1, oz, cb.glowcrystal);
    }

    void placeVegetation(Chunk& ch, const ColumnInfo* cols) const {
        const int bx = ch.pos.x * 16, bz = ch.pos.z * 16;
        for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            const ColumnInfo& ci = cols[x + z * 16];
            int y = ci.height;
            if (y + 1 >= WORLD_H || ch.get(x, y + 1, z) != AIR) continue;
            BlockState ground = ch.get(x, y, z);
            u64 h = hashCoords(st.seed ^ 0x6A55, bx + x, bz + z);
            int roll = (int)(h % 1000);
            if (ground == cb.turf) {
                int grass = ci.biome == Biome::Plains ? 220 : ci.biome == Biome::Savanna ? 260 : ci.biome == Biome::Swamp ? 120 : 90;
                if (roll < grass) ch.setGen(x, y + 1, z, ci.biome == Biome::Taiga || ((h >> 20) & 7) == 0 ? cb.fern : cb.tall_grass);
                else if (roll < grass + 12) {
                    int f = (int)((h >> 24) % 10);
                    ch.setGen(x, y + 1, z, f < 5 ? cb.sunpetal : (f < 9 ? cb.bluebell : cb.emberbloom));
                }
            } else if (ground == cb.podzol) {
                if (roll < 120) ch.setGen(x, y + 1, z, cb.fern);
            } else if ((ground == cb.sand || ground == cb.red_sand) && (ci.biome == Biome::Desert || ci.biome == Biome::Badlands)) {
                if (roll < 6) ch.setGen(x, y + 1, z, cb.dead_bush);
            }
        }
    }

    GenSettings st;
    const BlockRegistry& reg;
    CoreBlocks cb;
    Noise contN, eroN, ridN, hillN, tempN, humN, rivN, detN, caveA, caveB, caveC;
};

} // namespace

namespace {
// The Hollow: a sealed cavern dimension (added with generator version 2). Bedrock floor (y 0) and
// ceiling (y 127), a lava sea below y 32, hollowrock caverns from 3D noise. Density is sampled on a
// 4-block grid and trilinearly interpolated (5x33x5 samples per chunk instead of 32768).
class HollowGenerator final : public WorldGenerator {
public:
    HollowGenerator(const GenSettings& s, const BlockRegistry& r) : st(s), dens(s.seed ^ 0x4011A), patch(s.seed ^ 0x4011B) {
        cb.resolve(r);
        rock = r.stateOf("hollowrock"); ore = r.stateOf("ember_ore"); ash = r.stateOf("ashsoil"); cap = r.stateOf("glowcap");
    }
    static constexpr int CEIL = 127, LAVA = 31;
    void generate(Chunk& c) const override {
        VX_TIME_US(Stat::GenTimeUs);
        const int bx = c.pos.x * 16, bz = c.pos.z * 16;
        float grid[5][33][5];
        for (int gz = 0; gz < 5; gz++) for (int gy = 0; gy < 33; gy++) for (int gx = 0; gx < 5; gx++) {
            float x = (float)(bx + gx * 4), y = (float)(gy * 4), z = (float)(bz + gz * 4);
            float d = dens.fbm3(x / 40.0f, y / 28.0f, z / 40.0f, 3);
            float floorB = clamp((26.0f - y) / 14.0f, 0.0f, 1.0f) * 0.9f, ceilB = clamp((y - 100.0f) / 20.0f, 0.0f, 1.0f) * 1.0f;
            grid[gx][gy][gz] = d + floorB + ceilB - 0.12f;
        }
        for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            c.biome[x + z * 16] = (u8)Biome::Hollow;
            int gx = x >> 2, gz = z >> 2; float fx = (x & 3) / 4.0f, fz = (z & 3) / 4.0f;
            for (int y = 0; y <= CEIL; y++) {
                BlockState s;
                if (y == 0 || y == CEIL) s = cb.bedrock;
                else if ((y < 4 || y > CEIL - 4) && (hashCoords(st.seed ^ 0xBED, bx + x, y, bz + z) & 3) == 0) s = cb.bedrock;
                else {
                    int gy = y >> 2; float fy = (y & 3) / 4.0f;
                    auto L = [](float a, float b, float t) { return a + (b - a) * t; };
                    float c00 = L(grid[gx][gy][gz], grid[gx + 1][gy][gz], fx), c10 = L(grid[gx][gy + 1][gz], grid[gx + 1][gy + 1][gz], fx);
                    float c01 = L(grid[gx][gy][gz + 1], grid[gx + 1][gy][gz + 1], fx), c11 = L(grid[gx][gy + 1][gz + 1], grid[gx + 1][gy + 1][gz + 1], fx);
                    float v = L(L(c00, c10, fy), L(c01, c11, fy), fz);
                    s = v > 0 ? rock : (y <= LAVA ? cb.lava : AIR);
                }
                c.setGen(x, y, z, s);
            }
        }
        // ember ore veins
        Rng rng(hashCoords(st.seed ^ 0xE3BE, c.pos.x, c.pos.z));
        for (int v = 0; v < 10; v++) {
            int x = rng.range(16), y = 10 + rng.range(110), z = rng.range(16), n = 3 + rng.range(5);
            for (int k = 0; k < n; k++) {
                if (c.get(x, y, z) == rock) c.setGen(x, y, z, ore);
                x = clamp(x + rng.range(3) - 1, 0, 15); y = clamp(y + rng.range(3) - 1, 1, CEIL - 1); z = clamp(z + rng.range(3) - 1, 0, 15);
            }
        }
        // floors: ash patches and glowcaps; ceilings: hanging glowcrystal
        for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            float pn = patch.fbm2((bx + x) / 24.0f, (bz + z) / 24.0f, 2);
            for (int y = LAVA + 1; y < CEIL - 1; y++) {
                BlockState s = c.get(x, y, z), above = c.get(x, y + 1, z), below = c.get(x, y - 1, z);
                u64 h = hashCoords(st.seed ^ 0x6C4, bx + x, y, bz + z);
                if (s == rock && above == AIR) {
                    if (pn > 0.1f) c.setGen(x, y, z, ash);
                    if (pn > 0.1f && (h % 100) < 7) c.setGen(x, y + 1, z, cap);
                } else if (s == AIR && above == rock && (h % 1000) < 6) {
                    for (int k = 0; k < 1 + (int)(h >> 20) % 3 && y - k > LAVA; k++) if (c.get(x, y - k, z) == AIR) c.setGen(x, y - k, z, cb.glowcrystal);
                }
                (void)below;
            }
        }
        c.compact();
    }
    ColumnInfo column(int, int) const override { return {64, Biome::Hollow, false}; }
    u32 version() const override { return st.version; }
private:
    GenSettings st; CoreBlocks cb; Noise dens, patch;
    BlockState rock, ore, ash, cap;
};
} // namespace

std::unique_ptr<WorldGenerator> createGenerator(const GenSettings& s, const BlockRegistry& reg) {
    // Every released version stays available forever: worlds keep the terrain they were created with.
    if (s.version < 1 || s.version > LATEST_GENERATOR_VERSION) return nullptr;
    if (s.preset == "flat") return std::make_unique<FlatGenerator>(reg);
    if (s.dimension == "hollow") return s.version >= 2 ? std::make_unique<HollowGenerator>(s, reg) : nullptr;   // introduced in v2
    if (s.dimension != "overworld") return nullptr;
    return std::make_unique<GeneratorV1>(s, reg);   // v2 = v1 terrain + additional structures (see placeFeatures)
}

} // namespace vx
