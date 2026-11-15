#include "render/mesher.h"
#include "core/profiler.h"
#include "world/worldgen.h"
#include "render/visibility.h"

namespace vx {

namespace {
constexpr int P = 18;   // padded edge: -1..16
inline int pidx(int x, int y, int z) { return ((y + 1) * P + (z + 1)) * P + (x + 1); }

const int DIR[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
// u/v tangent axes per face axis: x faces (u=z, v=y), y faces (u=x, v=z), z faces (u=x, v=y)
const int UAX[3] = {2, 0, 0}, VAX[3] = {1, 2, 1};
constexpr int FACE_FLAT = 6;   // shading id for non-directional geometry (plants)

struct Padded {
    BlockState b[P * P * P];
    u8 sky[P * P * P], blk[P * P * P];
    u8 opq[P * P * P];
    u32 grass[P * P], foliage[P * P], water[P * P];   // per padded column (z,x)
};

u32 swapRB(u32 rgb) { return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF); }   // 0xRRGGBB -> R in low byte

void fill(const MeshSnapshot& s, int sy, const BlockRegistry& reg, Padded& p) {
    for (int z = -1; z <= 16; z++) for (int x = -1; x <= 16; x++) {
        int dx = x < 0 ? -1 : (x > 15 ? 1 : 0), dz = z < 0 ? -1 : (z > 15 ? 1 : 0);
        int ci = MeshSnapshot::idx(dx, dz), lx = x & 15, lz = z & 15;
        BiomeColors bc = biomeColors((Biome)s.biome[ci][lx + lz * 16]);
        int col = (z + 1) * P + (x + 1);
        p.grass[col] = swapRB(bc.grass); p.foliage[col] = swapRB(bc.foliage); p.water[col] = swapRB(bc.water);
        for (int y = -1; y <= 16; y++) {
            int wy = sy * 16 + y, i = pidx(x, y, z);
            if (wy < 0) { p.b[i] = AIR; p.opq[i] = 1; p.sky[i] = 0; p.blk[i] = 0; continue; }
            if (wy >= WORLD_H) { p.b[i] = AIR; p.opq[i] = 0; p.sky[i] = 15; p.blk[i] = 0; continue; }
            int si = Section::idx(lx, wy & 15, lz);
            BlockState b = s.sec[ci][wy >> 4].get(si);
            u8 l = s.light[ci][wy >> 4].get(si);
            p.b[i] = b; p.opq[i] = reg.opaque(b) ? 1 : 0; p.sky[i] = l >> 4; p.blk[i] = l & 15;
        }
    }
}

struct Emitter {
    SectionMesh& out;
    // corners: 4 x (x,y,z) in 1/16 units, uv in 1/16 tile units
    void quad(int ml, const int pos[4][3], const int uv[4][2], int face, int layer, const u8 ao[4], const u8 sky[4], const u8 blk[4], u32 tint, bool flipDiag = false) {
        static const int order[2][4] = {{0, 1, 2, 3}, {1, 2, 3, 0}};
        auto& v = out.data[ml];
        for (int k = 0; k < 4; k++) {
            int c = order[flipDiag ? 1 : 0][k];
            u32 w0 = (u32)pos[c][0] | ((u32)pos[c][1] << 9) | ((u32)pos[c][2] << 18) | ((u32)face << 27) | ((u32)ao[c] << 30);
            u32 w1 = (u32)(uv[c][0] & 511) | ((u32)(uv[c][1] & 511) << 9) | ((u32)layer << 18);
            u32 w2 = (u32)sky[c] | ((u32)blk[c] << 4) | ((tint & 0xFFFFFF) << 8);
            v.push_back(w0); v.push_back(w1); v.push_back(w2);
        }
    }
    void flatQuad(int ml, const int pos[4][3], const int uv[4][2], int face, int layer, u8 sky, u8 blk, u32 tint, bool twoSided) {
        u8 ao[4] = {3, 3, 3, 3}, s[4] = {sky, sky, sky, sky}, b[4] = {blk, blk, blk, blk};
        quad(ml, pos, uv, face, layer, ao, s, b, tint);
        if (twoSided) {
            int p2[4][3], uv2[4][2];
            for (int k = 0; k < 4; k++) { std::memcpy(p2[k], pos[3 - k], sizeof p2[k]); std::memcpy(uv2[k], uv[3 - k], sizeof uv2[k]); }
            quad(ml, p2, uv2, face, layer, ao, s, b, tint);
        }
    }
    // Axis-aligned box in 1/16 units; uv taken from the face's projection onto the tile.
    void box(int ml, const int mn[3], const int mx[3], const u16 tex[6], u8 sky, u8 blk, u32 tint, const bool faces[6] = nullptr, const int* topUV = nullptr) {
        for (int f = 0; f < 6; f++) {
            if (faces && !faces[f]) continue;
            int a = f >> 1, ua = UAX[a], va = VAX[a]; bool pos = f & 1;
            int pts[4][3], uv[4][2];
            const int cu[4] = {0, 1, 1, 0}, cv[4] = {0, 0, 1, 1};
            for (int k = 0; k < 4; k++) {
                int p3[3]; p3[a] = pos ? mx[a] : mn[a];
                p3[ua] = cu[k] ? mx[ua] : mn[ua]; p3[va] = cv[k] ? mx[va] : mn[va];
                std::memcpy(pts[k], p3, sizeof p3);
                int tu = p3[ua], tv = (a == 1) ? p3[va] : 16 - p3[va];
                if (f == FACE_PX || f == FACE_NZ) tu = 16 - tu;
                uv[k][0] = tu; uv[k][1] = tv;
                if (a == 1 && topUV) { uv[k][0] = cu[k] ? topUV[2] : topUV[0]; uv[k][1] = cv[k] ? topUV[3] : topUV[1]; }
            }
            bool reverse = (f == FACE_PX || f == FACE_PY || f == FACE_NZ);
            if (reverse) { std::swap(pts[1], pts[3]); std::swap(uv[1], uv[3]); }
            flatQuad(ml, pts, uv, f, tex[f], sky, blk, tint, false);
        }
    }
};

struct FaceInfo {
    u16 layer = 0; u8 ml = 0; bool valid = false; bool uniform = false;
    u32 tint = 0; u8 ao[4], sky[4], blk[4];
    bool same(const FaceInfo& o) const {
        return valid && o.valid && uniform && o.uniform && layer == o.layer && ml == o.ml && tint == o.tint &&
               ao[0] == o.ao[0] && sky[0] == o.sky[0] && blk[0] == o.blk[0];
    }
};

int mlOf(const BlockDef& d) { return d.layer == RenderLayer::Opaque ? ML_OPAQUE : d.layer == RenderLayer::Cutout ? ML_CUTOUT : ML_TRANSLUCENT; }

u32 tintFor(const BlockDef& d, const Padded& p, int x, int z, int face) {
    int col = (z + 1) * P + (x + 1);
    switch (d.tint) {
        case Tint::Grass: return (face == FACE_PY || face == FACE_FLAT) ? p.grass[col] : 0xFFFFFF;
        case Tint::Foliage: return p.foliage[col];
        case Tint::Water: return p.water[col];
        default: return 0xFFFFFF;
    }
}

int fluidHeight(const BlockRegistry& reg, const Padded& p, int x, int y, int z) {
    BlockState s = p.b[pidx(x, y, z)];
    const BlockDef& d = reg.def(s);
    if (reg.def(p.b[pidx(x, y + 1, z)]).index == d.index) return 16;
    int lvl = reg.getProp(s, "level");
    if (lvl == 0 || lvl >= 8) return 14;
    return std::max(2, 14 - 2 * lvl);
}

void cubeFaces(const MeshContext& ctx, const Padded& p, Emitter& em) {
    const BlockRegistry& reg = *ctx.reg;
    FaceInfo mask[16 * 16];
    for (int f = 0; f < 6; f++) {
        const int a = f >> 1, ua = UAX[a], va = VAX[a], sgn = (f & 1) ? 1 : -1;
        for (int d = 0; d < 16; d++) {
            // build mask
            for (int j = 0; j < 16; j++) for (int i = 0; i < 16; i++) {
                FaceInfo& fi = mask[j * 16 + i]; fi.valid = false;
                int c[3]; c[a] = d; c[ua] = i; c[va] = j;
                BlockState b = p.b[pidx(c[0], c[1], c[2])];
                if (b == AIR) continue;
                const BlockDef& def = reg.def(b);
                if (def.shape != BlockShape::Cube) continue;
                int n[3] = {c[0] + DIR[f][0], c[1] + DIR[f][1], c[2] + DIR[f][2]};
                int ni = pidx(n[0], n[1], n[2]);
                if (p.opq[ni]) continue;
                BlockState nb = p.b[ni];
                if (nb != AIR && reg.def(nb).index == def.index && !def.opaque) continue;   // glass-glass, leaves-leaves
                fi.valid = true; fi.layer = ctx.tex->face(b, f); fi.ml = (u8)mlOf(def);
                fi.tint = tintFor(def, p, c[0], c[2], f);
                // per-corner smooth light + AO: corners (0,0) (1,0) (1,1) (0,1) in (u,v)
                const int cu[4] = {-1, 1, 1, -1}, cv[4] = {-1, -1, 1, 1};
                for (int k = 0; k < 4; k++) {
                    if (!ctx.smoothLight) { fi.ao[k] = 3; fi.sky[k] = p.sky[ni]; fi.blk[k] = p.blk[ni]; continue; }
                    int o1[3] = {n[0], n[1], n[2]}, o2[3] = {n[0], n[1], n[2]};
                    o1[ua] += cu[k]; o2[va] += cv[k];
                    int o3[3] = {o1[0], o1[1], o1[2]}; o3[va] += cv[k];
                    int i1 = pidx(o1[0], o1[1], o1[2]), i2 = pidx(o2[0], o2[1], o2[2]), i3 = pidx(o3[0], o3[1], o3[2]);
                    int s1 = p.opq[i1], s2 = p.opq[i2], s3 = p.opq[i3];
                    fi.ao[k] = (u8)((s1 && s2) ? 0 : 3 - (s1 + s2 + s3));
                    int ss = p.sky[ni], bs = p.blk[ni], cnt = 1;
                    if (!s1) { ss += p.sky[i1]; bs += p.blk[i1]; cnt++; }
                    if (!s2) { ss += p.sky[i2]; bs += p.blk[i2]; cnt++; }
                    if (!s3 && !(s1 && s2)) { ss += p.sky[i3]; bs += p.blk[i3]; cnt++; }
                    fi.sky[k] = (u8)((ss + cnt / 2) / cnt); fi.blk[k] = (u8)((bs + cnt / 2) / cnt);
                }
                fi.uniform = ctx.greedy && fi.ao[0] == fi.ao[1] && fi.ao[0] == fi.ao[2] && fi.ao[0] == fi.ao[3] &&
                             fi.sky[0] == fi.sky[1] && fi.sky[0] == fi.sky[2] && fi.sky[0] == fi.sky[3] &&
                             fi.blk[0] == fi.blk[1] && fi.blk[0] == fi.blk[2] && fi.blk[0] == fi.blk[3];
            }
            // greedy merge
            for (int j = 0; j < 16; j++) for (int i = 0; i < 16;) {
                FaceInfo fi = mask[j * 16 + i];
                if (!fi.valid) { i++; continue; }
                int w = 1, h = 1;
                if (fi.uniform) {
                    while (i + w < 16 && fi.same(mask[j * 16 + i + w])) w++;
                    for (bool ok = true; j + h < 16 && ok; ) {
                        for (int k = 0; k < w; k++) if (!fi.same(mask[(j + h) * 16 + i + k])) { ok = false; break; }
                        if (ok) h++;
                    }
                }
                for (int jj = 0; jj < h; jj++) for (int ii = 0; ii < w; ii++) mask[(j + jj) * 16 + i + ii].valid = false;
                const int cu[4] = {i, i + w, i + w, i}, cv[4] = {j, j, j + h, j + h};
                int pos[4][3], uv[4][2];
                for (int k = 0; k < 4; k++) {
                    pos[k][a] = (d + (sgn > 0 ? 1 : 0)) * 16; pos[k][ua] = cu[k] * 16; pos[k][va] = cv[k] * 16;
                    int tu = (cu[k] - i) * 16, tv = (a == 1) ? (cv[k] - j) * 16 : (j + h - cv[k]) * 16;
                    if (f == FACE_PX || f == FACE_NZ) tu = (i + w - cu[k]) * 16;
                    uv[k][0] = tu; uv[k][1] = tv;
                }
                u8 ao[4], sk[4], bl[4];
                std::memcpy(ao, fi.ao, 4); std::memcpy(sk, fi.sky, 4); std::memcpy(bl, fi.blk, 4);
                bool reverse = (f == FACE_PX || f == FACE_PY || f == FACE_NZ);
                if (reverse) {   // keep counter-clockwise winding seen from outside
                    std::swap(pos[1], pos[3]); std::swap(uv[1], uv[3]);
                    std::swap(ao[1], ao[3]); std::swap(sk[1], sk[3]); std::swap(bl[1], bl[3]);
                }
                bool flip = ao[0] + ao[2] < ao[1] + ao[3];
                em.quad(fi.ml, pos, uv, f, fi.layer, ao, sk, bl, fi.tint, flip);
                i += w;
            }
        }
    }
}

void specialShapes(const MeshContext& ctx, const Padded& p, Emitter& em) {
    const BlockRegistry& reg = *ctx.reg;
    for (int y = 0; y < 16; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
        int i = pidx(x, y, z);
        BlockState b = p.b[i];
        if (b == AIR) continue;
        const BlockDef& d = reg.def(b);
        if (d.shape == BlockShape::Cube || d.shape == BlockShape::None) continue;
        int ml = mlOf(d);
        u8 sky = p.sky[i], blk = p.blk[i];
        u16 tex[6]; for (int f = 0; f < 6; f++) tex[f] = ctx.tex->face(b, f);
        const int X = x * 16, Y = y * 16, Z = z * 16;
        auto off = [&](int (&v)[3]) { v[0] += X; v[1] += Y; v[2] += Z; };
        switch (d.shape) {
        case BlockShape::Cross: {
            u32 tint = tintFor(d, p, x, z, FACE_FLAT);
            const int diag[2][4][3] = {{{1, 0, 1}, {15, 0, 15}, {15, 16, 15}, {1, 16, 1}}, {{1, 0, 15}, {15, 0, 1}, {15, 16, 1}, {1, 16, 15}}};
            const int uv[4][2] = {{0, 16}, {16, 16}, {16, 0}, {0, 0}};
            for (auto& q : diag) {
                int pos[4][3]; for (int k = 0; k < 4; k++) { std::memcpy(pos[k], q[k], sizeof pos[k]); off(pos[k]); }
                em.flatQuad(ml, pos, uv, FACE_FLAT, tex[0], sky, blk, tint, true);
            }
            break;
        }
        case BlockShape::Rail: {
            // A flat quad 1 px above the floor (sloped for ascending rails). The texture draws a
            // north-south track (curves: a south-east bend); other shapes rotate/mirror its UVs.
            int sh = d.propIndex("shape") >= 0 ? reg.getProp(b, "shape") : 0;
            int hN = 1, hS = 1, hW = 1, hE = 1;   // heights of the north/south/west/east edges
            if (sh == 2) { hN = 17; } else if (sh == 3) { hS = 17; } else if (sh == 4) { hW = 17; } else if (sh == 5) { hE = 17; }
            // corners in order (x0,z0) (x0,z1) (x1,z1) (x1,z0)
            const int cx[4] = {0, 0, 16, 16}, cz[4] = {0, 16, 16, 0};
            int pos[4][3], uv[4][2];
            for (int k = 0; k < 4; k++) {
                int x = cx[k], z = cz[k];
                int h = std::max(z == 0 ? hN : hS, x == 0 ? hW : hE);
                if (sh == 0 || sh == 2 || sh == 3) h = z == 0 ? hN : hS;
                if (sh == 1 || sh == 4 || sh == 5) h = x == 0 ? hW : hE;
                pos[k][0] = x + X; pos[k][1] = h + Y; pos[k][2] = z + Z;
                int u = x, v = z;
                switch (sh) {
                case 1: case 4: case 5: u = z; v = 16 - x; break;   // east-west: rotate 90
                case 7: u = 16 - x; break;                           // S+W: mirror
                case 8: u = 16 - x; v = 16 - z; break;               // N+W: rotate 180
                case 9: v = 16 - z; break;                           // N+E: flip
                default: break;
                }
                uv[k][0] = u; uv[k][1] = v;
            }
            em.flatQuad(ml, pos, uv, FACE_PY, tex[FACE_PY], sky, blk, 0xFFFFFF, sh >= 2 && sh <= 5);
            break;
        }
        case BlockShape::Fluid: {
            // Smooth surface: each top corner takes the mean height of the fluid cells sharing it
            // (full height if any of them has the same fluid above), so flows slope continuously.
            u32 tint = tintFor(d, p, x, z, FACE_PY);
            auto same = [&](int xx, int yy, int zz) { BlockState q = p.b[pidx(xx, yy, zz)]; return q != AIR && reg.def(q).index == d.index; };
            int hc[4];   // corners: (x0,z0) (x1,z0) (x1,z1) (x0,z1)
            const int ccx[4] = {0, 1, 1, 0}, ccz[4] = {0, 0, 1, 1};
            for (int k = 0; k < 4; k++) {
                int sum = 0, n = 0; bool full = false;
                for (int dz = ccz[k] - 1; dz <= ccz[k]; dz++) for (int dx = ccx[k] - 1; dx <= ccx[k]; dx++) {
                    int xx = x + dx, zz = z + dz;
                    if (!same(xx, y, zz)) continue;
                    if (same(xx, y + 1, zz)) full = true;
                    sum += fluidHeight(reg, p, xx, y, zz); n++;
                }
                hc[k] = full ? 16 : (n ? (sum + n / 2) / n : 14);
            }
            int layer = tex[0];
            auto lightOf = [&](int f, u8& s, u8& bl) {
                int ni = pidx(x + DIR[f][0], y + DIR[f][1], z + DIR[f][2]);
                s = std::max(sky, p.opq[ni] ? sky : p.sky[ni]); bl = std::max(blk, p.opq[ni] ? blk : p.blk[ni]);
            };
            auto emit = [&](int f, int (&q)[4][3], const int (&uv)[4][2]) {
                for (auto& v : q) { v[0] += X; v[1] += Y; v[2] += Z; }
                u8 fs, fb; lightOf(f, fs, fb);
                em.flatQuad(ml, q, uv, f, layer, fs, fb, tint, false);
            };
            auto visible = [&](int f) {
                int ni = pidx(x + DIR[f][0], y + DIR[f][1], z + DIR[f][2]);
                return !same(x + DIR[f][0], y + DIR[f][1], z + DIR[f][2]) && (!p.opq[ni] || f == FACE_PY);
            };
            if (visible(FACE_PY) && !(hc[0] == 16 && hc[1] == 16 && hc[2] == 16 && hc[3] == 16 && p.opq[pidx(x, y + 1, z)])) {
                int q[4][3] = {{0, hc[0], 0}, {0, hc[3], 16}, {16, hc[2], 16}, {16, hc[1], 0}};
                const int uv[4][2] = {{0, 0}, {0, 16}, {16, 16}, {16, 0}};
                emit(FACE_PY, q, uv);
            }
            if (visible(FACE_NY)) { int q[4][3] = {{0, 0, 0}, {16, 0, 0}, {16, 0, 16}, {0, 0, 16}}; const int uv[4][2] = {{0, 0}, {16, 0}, {16, 16}, {0, 16}}; emit(FACE_NY, q, uv); }
            if (visible(FACE_NX)) { int q[4][3] = {{0, 0, 0}, {0, 0, 16}, {0, hc[3], 16}, {0, hc[0], 0}}; const int uv[4][2] = {{0, 16}, {16, 16}, {16, 16 - hc[3]}, {0, 16 - hc[0]}}; emit(FACE_NX, q, uv); }
            if (visible(FACE_PX)) { int q[4][3] = {{16, 0, 0}, {16, hc[1], 0}, {16, hc[2], 16}, {16, 0, 16}}; const int uv[4][2] = {{16, 16}, {16, 16 - hc[1]}, {0, 16 - hc[2]}, {0, 16}}; emit(FACE_PX, q, uv); }
            if (visible(FACE_NZ)) { int q[4][3] = {{0, 0, 0}, {0, hc[0], 0}, {16, hc[1], 0}, {16, 0, 0}}; const int uv[4][2] = {{16, 16}, {16, 16 - hc[0]}, {0, 16 - hc[1]}, {0, 16}}; emit(FACE_NZ, q, uv); }
            if (visible(FACE_PZ)) { int q[4][3] = {{0, 0, 16}, {16, 0, 16}, {16, hc[2], 16}, {0, hc[3], 16}}; const int uv[4][2] = {{0, 16}, {16, 16}, {16, 16 - hc[2]}, {0, 16 - hc[3]}}; emit(FACE_PZ, q, uv); }
            break;
        }
        case BlockShape::Slab: case BlockShape::Stairs: case BlockShape::Fence: case BlockShape::Pane:
        case BlockShape::Door: case BlockShape::Trapdoor: case BlockShape::FenceGate: case BlockShape::Wall: {
            // Building shapes: their precomputed boxes. A face lying on the block boundary is
            // skipped when the neighbour there is opaque (a double slab inside a wall costs nothing).
            SectionMesh tmp; Emitter te{tmp};
            for (const ShapeBox& sbx : reg.renderBoxes(b)) {
                int mn[3] = {sbx.mn[0], sbx.mn[1], sbx.mn[2]}, mx[3] = {sbx.mx[0], sbx.mx[1], sbx.mx[2]};
                for (int f = 0; f < 6; f++) {
                    int a = f >> 1; bool pos = f & 1;
                    bool onEdge = pos ? mx[a] == 16 : mn[a] == 0;
                    int ni = pidx(x + DIR[f][0], y + DIR[f][1], z + DIR[f][2]);
                    if (onEdge && p.opq[ni]) continue;   // hidden against an opaque neighbour
                    // boundary faces are lit from the cell they face (an opaque double slab has no
                    // light of its own); inner faces from the block's own cell
                    u8 fs = onEdge ? std::max(p.sky[ni], p.opq[i] ? (u8)0 : sky) : sky;
                    u8 fb = onEdge ? std::max(p.blk[ni], p.opq[i] ? (u8)0 : blk) : blk;
                    bool one[6] = {}; one[f] = true;
                    te.box(ml, mn, mx, tex, fs, fb, 0xFFFFFF, one, nullptr);
                }
            }
            for (size_t k = 0; k < tmp.data[ml].size(); k += 3) {
                u32 w0 = tmp.data[ml][k];
                u32 px = (w0 & 511) + X, py = ((w0 >> 9) & 511) + Y, pz = ((w0 >> 18) & 511) + Z;
                em.out.data[ml].push_back((w0 & ~0x7FFFFFFu) | px | (py << 9) | (pz << 18));
                em.out.data[ml].push_back(tmp.data[ml][k + 1]); em.out.data[ml].push_back(tmp.data[ml][k + 2]);
            }
            break;
        }
        default: {
            int mn[3], mx[3]; int topUV[4] = {7, 4, 9, 6}; const int* tuv = nullptr;
            int facing = d.propIndex("facing") >= 0 ? reg.getProp(b, "facing") : 0;
            u32 tint = 0xFFFFFF;
            switch (d.shape) {
            case BlockShape::Torch: {
                tuv = topUV;
                if (facing >= 4) { int a[3] = {7, 0, 7}, c[3] = {9, 12, 9}; std::memcpy(mn, a, 12); std::memcpy(mx, c, 12); }
                else {   // on the wall opposite to where it points
                    int ox = facing == 2 ? 6 : facing == 3 ? -6 : 0, oz = facing == 0 ? 6 : facing == 1 ? -6 : 0;
                    int a[3] = {7 + ox, 3, 7 + oz}, c[3] = {9 + ox, 15, 9 + oz}; std::memcpy(mn, a, 12); std::memcpy(mx, c, 12);
                }
                break;
            }
            case BlockShape::Wire: {
                int power = d.propIndex("power") >= 0 ? reg.getProp(b, "power") : 0;
                u32 r = (u32)(90 + power * 11);
                tint = r | (u32)(power ? 10 : 0) << 8 | (u32)(power ? 20 : 0) << 16;
                int pos[4][3] = {{0, 1, 0}, {0, 1, 16}, {16, 1, 16}, {16, 1, 0}}; const int uv[4][2] = {{0, 0}, {0, 16}, {16, 16}, {16, 0}};
                for (auto& q : pos) off(q);
                em.flatQuad(ml, pos, uv, FACE_PY, tex[FACE_PY], sky, blk, tint, false);
                continue;
            }
            case BlockShape::FlatWall: {
                // ladder etc: thin plane 1px in front of the wall it hangs on (wall is opposite to facing)
                int pos[4][3]; const int uv[4][2] = {{0, 16}, {16, 16}, {16, 0}, {0, 0}};
                int zf = 15, xf = 15;
                if (facing == 0) { int q[4][3] = {{0, 0, zf}, {16, 0, zf}, {16, 16, zf}, {0, 16, zf}}; std::memcpy(pos, q, sizeof q); }
                else if (facing == 1) { int q[4][3] = {{16, 0, 1}, {0, 0, 1}, {0, 16, 1}, {16, 16, 1}}; std::memcpy(pos, q, sizeof q); }
                else if (facing == 2) { int q[4][3] = {{xf, 0, 16}, {xf, 0, 0}, {xf, 16, 0}, {xf, 16, 16}}; std::memcpy(pos, q, sizeof q); }
                else { int q[4][3] = {{1, 0, 0}, {1, 0, 16}, {1, 16, 16}, {1, 16, 0}}; std::memcpy(pos, q, sizeof q); }
                for (auto& q : pos) off(q);
                em.flatQuad(ml, pos, uv, FACE_FLAT, tex[0], sky, blk, tint, true);
                continue;
            }
            case BlockShape::Lever: { int a[3] = {7, 0, 7}, c[3] = {9, 10, 9}; std::memcpy(mn, a, 12); std::memcpy(mx, c, 12); break; }
            case BlockShape::Button: { int a[3] = {5, 0, 6}, c[3] = {11, 2, 10}; std::memcpy(mn, a, 12); std::memcpy(mx, c, 12); break; }
            case BlockShape::Plate: { int on = d.propIndex("on") >= 0 ? reg.getProp(b, "on") : 0; int a[3] = {1, 0, 1}, c[3] = {15, on ? 1 : 2, 15}; std::memcpy(mn, a, 12); std::memcpy(mx, c, 12); break; }
            case BlockShape::SlabThin: { int a[3] = {0, 0, 0}, c[3] = {16, 2, 16}; std::memcpy(mn, a, 12); std::memcpy(mx, c, 12); break; }
            case BlockShape::PistonHead: {
                static const int fmn[6][3] = {{0, 0, 0}, {0, 12, 0}, {0, 0, 0}, {0, 0, 12}, {0, 0, 0}, {12, 0, 0}};
                static const int fmx[6][3] = {{16, 4, 16}, {16, 16, 16}, {16, 16, 4}, {16, 16, 16}, {4, 16, 16}, {16, 16, 16}};
                int f6 = std::min(facing, 5); std::memcpy(mn, fmn[f6], 12); std::memcpy(mx, fmx[f6], 12); break;
            }
            default: { int a[3] = {2, 0, 2}, c[3] = {14, 14, 14}; std::memcpy(mn, a, 12); std::memcpy(mx, c, 12); break; }   // Model placeholder
            }
            SectionMesh tmp; Emitter te{tmp};
            te.box(ml, mn, mx, tex, sky, blk, tint, nullptr, tuv);
            for (size_t k = 0; k < tmp.data[ml].size(); k += 3) {
                u32 w0 = tmp.data[ml][k];
                u32 px = (w0 & 511) + X, py = ((w0 >> 9) & 511) + Y, pz = ((w0 >> 18) & 511) + Z;
                em.out.data[ml].push_back((w0 & ~0x7FFFFFFu) | px | (py << 9) | (pz << 18));
                em.out.data[ml].push_back(tmp.data[ml][k + 1]); em.out.data[ml].push_back(tmp.data[ml][k + 2]);
            }
        }
        }
    }
}
} // namespace

bool sectionTriviallyEmpty(const MeshSnapshot& s, int sy, const BlockRegistry& reg) {
    const Section& c = s.sec[MeshSnapshot::idx(0, 0)][sy];
    if (c.isEmpty()) return true;
    if (!c.isUniform() || !reg.opaque(c.uniform)) return false;
    auto buried = [&](const Section& n) { return n.isUniform() && reg.opaque(n.uniform); };
    if (sy > 0 && !buried(s.sec[MeshSnapshot::idx(0, 0)][sy - 1])) return false;
    if (sy + 1 < SECTIONS && !buried(s.sec[MeshSnapshot::idx(0, 0)][sy + 1])) return false;
    if (sy + 1 >= SECTIONS) return false;
    return buried(s.sec[MeshSnapshot::idx(-1, 0)][sy]) && buried(s.sec[MeshSnapshot::idx(1, 0)][sy]) &&
           buried(s.sec[MeshSnapshot::idx(0, -1)][sy]) && buried(s.sec[MeshSnapshot::idx(0, 1)][sy]);
}

void meshSection(const MeshSnapshot& s, int sy, const MeshContext& ctx, SectionMesh& out) {
    VX_TIME_US(Stat::MeshTimeUs);
    for (auto& d : out.data) d.clear();
    const Section& self = s.sec[MeshSnapshot::idx(0, 0)][sy];
    if (sectionTriviallyEmpty(s, sy, *ctx.reg)) { out.connectivity = self.isEmpty() ? ALL_CONNECTED : NONE_CONNECTED; return; }
    {
        thread_local bool opq[4096];
        for (int i = 0; i < 4096; i++) opq[i] = ctx.reg->opaque(self.get(i));
        out.connectivity = computeConnectivity(opq);
    }
    thread_local std::unique_ptr<Padded> tp;   // 40 KiB scratch per worker, reused
    if (!tp) tp = std::make_unique<Padded>();
    Padded& p = *tp;
    fill(s, sy, *ctx.reg, p);
    Emitter em{out};
    cubeFaces(ctx, p, em);
    specialShapes(ctx, p, em);
    Profiler::add(Stat::ChunksMeshed);
}

} // namespace vx
