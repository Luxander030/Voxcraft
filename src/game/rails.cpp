#include "game/rails.h"

namespace vx {

bool isRail(const BlockRegistry& reg, BlockState s) { return s != AIR && reg.def(s).shape == BlockShape::Rail; }
bool isPoweredRail(const BlockRegistry& reg, BlockState s) { return isRail(reg, s) && reg.def(s).propIndex("powered") >= 0; }
bool isDetectorRail(const BlockRegistry& reg, BlockState s) { return isRail(reg, s) && reg.def(s).propIndex("on") >= 0; }
bool isTripRail(const BlockRegistry& reg, BlockState s) { return isRail(reg, s) && reg.def(s).propIndex("active") >= 0; }

void railShapeExits(int shape, int& d0, int& d1) {
    static const int E[10][2] = {{0, 1}, {2, 3}, {1, 0}, {0, 1}, {3, 2}, {2, 3}, {1, 3}, {1, 2}, {0, 2}, {0, 3}};
    // ascending shapes list the low end first, so a/b run uphill
    d0 = E[shape][0]; d1 = E[shape][1];
}

RailGeom railGeometry(int shape) {
    RailGeom g; railShapeExits(shape, g.dirA, g.dirB);
    auto edge = [](int d) { int dx, dz; dirOffset(d, dx, dz); return Vec3(0.5f + dx * 0.5f, 0, 0.5f + dz * 0.5f); };
    g.a = edge(g.dirA); g.b = edge(g.dirB);
    if (shape >= 2 && shape <= 5) g.b.y = 1;   // the uphill end
    return g;
}

namespace {
// A rail at (p + d) on the same level, one level up (we ascend toward it) or one level down
// (it ascends toward us).
bool railToward(const World& w, const BlockPos& p, int d, bool& up) {
    int dx, dz; dirOffset(d, dx, dz);
    up = false;
    if (isRail(w.reg, w.getBlock({p.x + dx, p.y, p.z + dz}))) return true;
    if (isRail(w.reg, w.getBlock({p.x + dx, p.y + 1, p.z + dz}))) { up = true; return true; }
    BlockState below = w.getBlock({p.x + dx, p.y - 1, p.z + dz});
    if (isRail(w.reg, below)) {
        int sh = w.reg.getProp(below, "shape");
        if (sh >= 2 && sh <= 5) { int lo, hi; railShapeExits(sh, lo, hi); return hi == dirOpposite(d); }   // it climbs toward us
    }
    return false;
}
}

BlockState withRailShape(const World& w, const BlockPos& p, BlockState s) {
    if (!isRail(w.reg, s)) return s;
    bool straightOnly = isPoweredRail(w.reg, s) || isDetectorRail(w.reg, s) || isTripRail(w.reg, s);
    bool c[4], up[4];
    for (int d = 0; d < 4; d++) c[d] = railToward(w, p, d, up[d]);
    int shape = w.reg.getProp(s, "shape");
    if (c[0] && c[1]) shape = up[0] ? 2 : up[1] ? 3 : 0;
    else if (c[2] && c[3]) shape = up[2] ? 4 : up[3] ? 5 : 1;
    else if (!straightOnly && (c[0] || c[1]) && (c[2] || c[3])) {
        bool n = c[0], e = c[3];
        shape = !n && e ? 6 : !n && !e ? 7 : n && !e ? 8 : 9;
    }
    else if (c[0] || c[1]) { int d = c[0] ? 0 : 1; shape = up[d] ? (d == 0 ? 2 : 3) : 0; }
    else if (c[2] || c[3]) { int d = c[2] ? 2 : 3; shape = up[d] ? (d == 2 ? 4 : 5) : 1; }
    else if (shape >= 2) shape = (shape == 4 || shape == 5) ? 1 : 0;   // alone: lie flat
    return w.reg.withProp(s, "shape", shape);
}

bool railNeighborUpdate(World& w, const BlockPos& p, const std::function<int(const BlockPos&)>& power) {
    BlockState s = w.getBlock(p);
    if (!isRail(w.reg, s)) return false;
    if (!w.reg.solid(w.getBlock({p.x, p.y - 1, p.z}))) { w.setBlock(p, AIR); return true; }
    BlockState n = withRailShape(w, p, s);
    if (isPoweredRail(w.reg, n) && power) n = w.reg.withProp(n, "powered", power(p) > 0 ? 1 : 0);
    if (isTripRail(w.reg, n) && power) n = w.reg.withProp(n, "active", power(p) > 0 ? 1 : 0);
    if (n != s) w.setBlock(p, n);
    // Rails one level up or down next to this one connect diagonally, but block updates only reach
    // face neighbours: re-evaluate those too (each change re-runs this, so slopes settle).
    for (int d = 0; d < 4; d++) for (int dy : {-1, 1}) {
        int dx, dz; dirOffset(d, dx, dz);
        BlockPos q{p.x + dx, p.y + dy, p.z + dz};
        BlockState qs = w.getBlock(q);
        if (!isRail(w.reg, qs)) continue;
        BlockState qn = withRailShape(w, q, qs);
        if (qn != qs) w.setBlock(q, qn);
    }
    return true;
}

} // namespace vx
