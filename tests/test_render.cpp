// Mesher and texture pack tests (CPU side of the renderer).
#include "testing.h"
#include "render/mesher.h"
#include "world/world.h"

using namespace vx;
using vxtest::blocks;

namespace {
const TexturePack& pack() {
    static TexturePack p; static bool ok = [] { std::string e; bool r = p.load(std::string(VX_SOURCE_DIR) + "/assets/generated/voxcraft.pak", &e); if (!r) fprintf(stderr, "%s\n", e.c_str()); return r; }();
    (void)ok; return p;
}
const BlockTextures& btex() { static BlockTextures t; static bool ok = [] { std::string e; bool r = t.build(blocks(), pack(), &e); if (!r) fprintf(stderr, "%s\n", e.c_str()); return r; }(); (void)ok; return t; }
MeshContext ctx() { MeshContext c; c.reg = &blocks(); c.tex = &btex(); return c; }

// empty air world around one section; light full sky
std::unique_ptr<MeshSnapshot> emptySnap() {
    auto s = std::make_unique<MeshSnapshot>();
    std::memset(s->biome, (int)Biome::Plains, sizeof s->biome);
    return s;
}
// signed area normal of a quad, compared against the expected face direction
bool windingOk(const u32* q) {
    UnpackedVertex v[3] = {unpackVertex(q), unpackVertex(q + 3), unpackVertex(q + 6)};
    int e1[3] = {v[1].x - v[0].x, v[1].y - v[0].y, v[1].z - v[0].z}, e2[3] = {v[2].x - v[0].x, v[2].y - v[0].y, v[2].z - v[0].z};
    int n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
    static const int dir[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    int f = v[0].face; if (f > 5) return true;
    return n[0] * dir[f][0] + n[1] * dir[f][1] + n[2] * dir[f][2] > 0;
}
} // namespace

TEST(render_pack_loads_and_resolves_all_block_textures) {
    REQUIRE(pack().tileCount > 0);
    CHECK_EQ(pack().tileSize, 16);
    std::string err; BlockTextures t; CHECK(t.build(blocks(), pack(), &err));
    int levels = 0; auto mips = pack().buildMips(levels);
    CHECK_EQ(levels, 5);
    size_t expect = 0; for (int s = 16; s >= 1; s >>= 1) expect += (size_t)s * s * 4 * pack().tileCount;
    CHECK_EQ(mips.size(), expect);
}

TEST(render_single_block_has_six_faces_ccw) {
    auto s = emptySnap();
    s->sec[MeshSnapshot::idx(0, 0)][4].set(Section::idx(5, 5, 5), blocks().stateOf("stone"));
    SectionMesh m; meshSection(*s, 4, ctx(), m);
    CHECK_EQ(m.quads(ML_OPAQUE), 6);
    CHECK_EQ(m.quads(ML_CUTOUT) + m.quads(ML_TRANSLUCENT), 0);
    for (int q = 0; q < m.quads(ML_OPAQUE); q++) CHECK(windingOk(&m.data[ML_OPAQUE][q * 12]));
    // all vertices within the block's box
    for (size_t i = 0; i < m.data[0].size(); i += 3) { auto v = unpackVertex(&m.data[0][i]); CHECK(v.x >= 80 && v.x <= 96 && v.y >= 80 && v.y <= 96); }
}

TEST(render_greedy_merges_flat_floor) {
    auto s = emptySnap();
    Section& sec = s->sec[MeshSnapshot::idx(0, 0)][4];
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) sec.set(Section::idx(x, 0, z), blocks().stateOf("stone"));
    // neighbours continue the floor so side faces are hidden
    for (int n = 0; n < 9; n++) if (n != 4) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) s->sec[n][4].set(Section::idx(x, 0, z), blocks().stateOf("stone"));
    SectionMesh m; meshSection(*s, 4, ctx(), m);
    CHECK_EQ(m.quads(ML_OPAQUE), 2);   // one top + one bottom quad for 256 blocks
    MeshContext c = ctx(); c.greedy = false;
    SectionMesh m2; meshSection(*s, 4, c, m2);
    CHECK_EQ(m2.quads(ML_OPAQUE), 512);
}

TEST(render_buried_section_is_skipped) {
    auto s = emptySnap();
    for (int n = 0; n < 9; n++) for (int y = 3; y <= 5; y++) s->sec[n][y].fill(blocks().stateOf("stone"));
    CHECK(sectionTriviallyEmpty(*s, 4, blocks()));
    CHECK(!sectionTriviallyEmpty(*s, 5, blocks()));
    SectionMesh m; meshSection(*s, 4, ctx(), m);
    CHECK(m.empty());
}

TEST(render_transparent_and_plants) {
    auto s = emptySnap();
    Section& sec = s->sec[MeshSnapshot::idx(0, 0)][4];
    sec.set(Section::idx(1, 1, 1), blocks().stateOf("glass"));
    sec.set(Section::idx(2, 1, 1), blocks().stateOf("glass"));      // glass-glass face culled
    sec.set(Section::idx(8, 1, 8), blocks().stateOf("tall_grass"));
    sec.set(Section::idx(10, 1, 10), blocks().stateOf("water"));
    SectionMesh m; meshSection(*s, 4, ctx(), m);
    CHECK_EQ(m.quads(ML_CUTOUT), 6 + 4);     // glass pair: shared face culled, rest greedy-merged to 6; plant: 2 planes x 2 sides
    CHECK_EQ(m.quads(ML_TRANSLUCENT), 6);    // lone water block
}

TEST(render_meshes_generated_terrain) {
    World w(blocks(), *new JobSystem(2), vxtest::testItems());   // leaked on purpose: outlives World
    auto dir = vxtest::tempDir("render");
    WorldMeta m; m.gen.seed = 99; std::string err; REQUIRE(w.create(dir, m, &err));
    w.setFocus({8, 80, 8}, 2); w.waitForJobs();
    MeshSnapshot snap; REQUIRE(buildMeshSnapshot(w, {0, 0}, snap));
    int quads = 0, secs = 0; double ms = 0; size_t bytes = 0;
    for (int sy = 0; sy < SECTIONS; sy++) {
        SectionMesh sm; Stopwatch sw; meshSection(snap, sy, ctx(), sm); ms += sw.ms();
        quads += sm.totalQuads(); bytes += sm.bytes(); if (!sm.empty()) secs++;
        for (int l = 0; l < ML_COUNT; l++) for (int q = 0; q < sm.quads(l); q++) CHECK(windingOk(&sm.data[l][q * 12]));
    }
    printf("    chunk 0,0: %d sections, %d quads, %.1f KiB, %.2f ms\n", secs, quads, bytes / 1024.0, ms);
    CHECK(quads > 50);
    CHECK(secs > 0);
}
