// Chunk lighting from a 3x3 neighbourhood snapshot.
//
// Light travels at most 15 blocks, so the light of the centre chunk is fully determined by blocks
// within 15 blocks of it - all contained in the 3x3 snapshot. The computation is a pure function
// of the snapshot: no cross-chunk writes, no locking, deterministic, and parallel across chunks.
// Light is never persisted; it is recomputed from blocks after loading.
#include "world/world.h"
#include "core/profiler.h"

namespace vx {

namespace {
constexpr int M = 15;              // margin
constexpr int RW = 16 + 2 * M;     // 46: region width

struct Scratch {
    std::vector<u8> op, emit, sky, blk;
    std::vector<u32> queue;
};
} // namespace

void computeChunkLight(const LightSnapshot& snap, const BlockRegistry& reg, LightSection out[SECTIONS], u8 heightmap[256]) {
    VX_TIME_US(Stat::LightTimeUs);
    static thread_local Scratch S;
    // vertical extent: everything above the highest non-empty section of the 9 chunks is open sky
    int topSec = -1;
    for (int c = 0; c < 9; c++) for (int s = SECTIONS - 1; s > topSec; s--) if (!snap.sec[c][s].isEmpty()) { topSec = s; break; }
    const int H = std::min(WORLD_H, (topSec + 2) * 16);   // one extra empty section so light spills upward correctly
    const size_t N = (size_t)RW * RW * H;
    S.op.assign(N, 0); S.emit.assign(N, 0); S.sky.assign(N, 0); S.blk.assign(N, 0);
    auto I = [&](int x, int y, int z) -> size_t { return ((size_t)y * RW + (size_t)z) * RW + (size_t)x; };

    // 1. opacity/emission for the region
    for (int z = 0; z < RW; z++) for (int x = 0; x < RW; x++) {
        int wx = x - M, wz = z - M;                     // relative to centre chunk origin, [-15, 31)
        int ci = (floorDiv(wx, 16) + 1) + (floorDiv(wz, 16) + 1) * 3;
        int lx = floorMod(wx, 16), lz = floorMod(wz, 16);
        const Section* secs = snap.sec[ci];
        for (int y = 0; y < H; y++) {
            BlockState s = secs[y >> 4].get(lx, y & 15, lz);
            if (s == AIR) continue;
            size_t i = I(x, y, z);
            S.op[i] = reg.opacity(s); S.emit[i] = reg.light(s);
        }
    }
    // 2. sky light straight down
    for (int z = 0; z < RW; z++) for (int x = 0; x < RW; x++) {
        int l = 15;
        int hm = 0;
        for (int y = H - 1; y >= 0; y--) {
            size_t i = I(x, y, z);
            u8 o = S.op[i];
            if (o) { if (!hm) hm = y + 1; l = std::max(0, l - (int)o); }
            S.sky[i] = (u8)l;
            if (l == 0) break;   // rest of the column stays 0
        }
        if (x >= M && x < M + 16 && z >= M && z < M + 16) heightmap[(x - M) + (z - M) * 16] = (u8)std::min(255, hm);
    }
    // 3. flood fill (BFS) for sky and block light
    auto flood = [&](std::vector<u8>& L) {
        size_t head = 0;
        while (head < S.queue.size()) {
            u32 i = S.queue[head++];
            int x = (int)(i % RW), z = (int)((i / RW) % RW), y = (int)(i / (RW * RW));
            int l = L[i];
            if (l <= 1) continue;
            auto go = [&](int nx, int ny, int nz) {
                if (nx < 0 || nx >= RW || nz < 0 || nz >= RW || ny < 0 || ny >= H) return;
                size_t n = I(nx, ny, nz);
                int o = S.op[n]; if (o >= 15) return;
                int nl = l - std::max(1, o);
                if (nl > L[n]) { L[n] = (u8)nl; S.queue.push_back((u32)n); }
            };
            go(x - 1, y, z); go(x + 1, y, z); go(x, y - 1, z); go(x, y + 1, z); go(x, y, z - 1); go(x, y, z + 1);
        }
    };
    S.queue.clear();
    for (int y = 0; y < H; y++) for (int z = 0; z < RW; z++) for (int x = 0; x < RW; x++) {
        size_t i = I(x, y, z);
        u8 l = S.sky[i]; if (l <= 1) continue;
        // seed only cells that can brighten a horizontal or lower neighbour
        bool seed = false;
        if (x > 0 && S.sky[i - 1] + 1 < l && S.op[i - 1] < 15) seed = true;
        else if (x + 1 < RW && S.sky[i + 1] + 1 < l && S.op[i + 1] < 15) seed = true;
        else if (z > 0 && S.sky[i - RW] + 1 < l && S.op[i - RW] < 15) seed = true;
        else if (z + 1 < RW && S.sky[i + RW] + 1 < l && S.op[i + RW] < 15) seed = true;
        else if (y > 0 && S.sky[i - RW * RW] + 1 < l && S.op[i - RW * RW] < 15) seed = true;
        if (seed) S.queue.push_back((u32)i);
    }
    flood(S.sky);
    S.queue.clear();
    for (size_t i = 0; i < N; i++) if (S.emit[i]) { S.blk[i] = S.emit[i]; S.queue.push_back((u32)i); }
    flood(S.blk);

    // 4. write centre chunk sections
    for (int s = 0; s < SECTIONS; s++) {
        int y0 = s * 16;
        if (y0 >= H) { out[s].data.reset(); out[s].uniform = 0xF0; continue; }
        auto ld = std::make_shared<LightData>();
        u8 first = 0; bool uniform = true;
        for (int y = 0; y < 16; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            size_t i = I(x + M, y0 + y, z + M);
            u8 v = (u8)((S.sky[i] << 4) | S.blk[i]);
            int k = Section::idx(x, y, z);
            ld->v[k] = v;
            if (k == 0) first = v; else if (v != first) uniform = false;
        }
        if (uniform) { out[s].data.reset(); out[s].uniform = first; }
        else out[s].data = std::move(ld);
    }
    Profiler::add(Stat::ChunksLit);
}

} // namespace vx
