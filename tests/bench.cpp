// voxbench: repeatable micro-benchmarks for the engine core (PERFORMANCE.md "Benchmark methodology").
// Run a Release build: ./voxbench [chunks]. Reports median-of-N timings so results are comparable
// across runs and machines.
#include "world/world.h"
#include <cstdio>
#include <cstdlib>

using namespace vx;

static double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0 : v[v.size() / 2]; }

int main(int argc, char** argv) {
    logSetLevel(LogLevel::Warn);
    int n = argc > 1 ? atoi(argv[1]) : 256;
    BlockRegistry reg; std::string err;
    if (!reg.loadFile(std::string(VX_SOURCE_DIR) + "/assets/data/blocks.json", &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    GenSettings gs; gs.seed = 12345;
    auto gen = createGenerator(gs, reg);
    ItemNames items{[](u16) { return std::string(); }, [](const std::string&) { return (u16)0; }};

    // 1. generation (single thread)
    std::vector<double> genMs; size_t mem = 0; int side = 1; while (side * side < n) side++;
    std::vector<std::unique_ptr<Chunk>> chunks;
    for (int i = 0; i < n; i++) {
        auto c = std::make_unique<Chunk>(ChunkPos{i % side - side / 2, i / side - side / 2});
        Stopwatch sw; gen->generate(*c); c->compact(); genMs.push_back(sw.ms());
        mem += c->memoryBytes(); chunks.push_back(std::move(c));
    }
    printf("generate      median %7.3f ms/chunk   (%d chunks, %.1f KiB/chunk resident)\n", median(genMs), n, mem / 1024.0 / n);

    // 2. lighting (3x3 snapshot around interior chunks)
    std::vector<double> lightMs;
    auto at = [&](int x, int z) -> Chunk* { int ix = x + side / 2, iz = z + side / 2; if (ix < 0 || iz < 0 || ix >= side || iz >= side || iz * side + ix >= n) return nullptr; return chunks[iz * side + ix].get(); };
    for (auto& c : chunks) {
        LightSnapshot snap; snap.center = c->pos; bool ok = true;
        for (int dz = -1; dz <= 1 && ok; dz++) for (int dx = -1; dx <= 1; dx++) {
            Chunk* nb = at(c->pos.x + dx, c->pos.z + dz); if (!nb) { ok = false; break; }
            for (int s = 0; s < SECTIONS; s++) snap.sec[(dz + 1) * 3 + dx + 1][s] = nb->sec[s];
        }
        if (!ok) continue;
        Stopwatch sw; computeChunkLight(snap, reg, c->light, c->heightmap); lightMs.push_back(sw.ms());
    }
    printf("light         median %7.3f ms/chunk   (%zu interior chunks)\n", median(lightMs), lightMs.size());

    // 3. sparse record encode/decode for a lightly edited chunk
    Chunk& c = *chunks[0];
    BlockState glass = reg.stateOf("glass");
    for (int i = 0; i < 64; i++) c.set(i & 15, 120 + (i >> 4), 0, glass);
    std::vector<double> encMs, decMs; std::vector<u8> rec;
    for (int i = 0; i < 50; i++) { Stopwatch sw; rec = encodeChunkRecord(c, reg, items, 0); encMs.push_back(sw.ms()); }
    for (int i = 0; i < 50; i++) { Chunk d(c.pos); gen->generate(d); Stopwatch sw; decodeChunkRecord(rec.data(), rec.size(), d, reg, items, 0); decMs.push_back(sw.ms()); }
    auto full = encodeChunkRecordFull(c, reg, items, 0);
    printf("record encode median %7.3f ms   decode %7.3f ms   sparse %zu B vs full %zu B (64 edits)\n", median(encMs), median(decMs), rec.size(), full.size());
    return 0;
}
