#include "game/building.h"

namespace vx {

namespace {
int facingToward(Vec3 look) {   // 4-way facing pointing back at a viewer looking along `look`
    if (std::fabs(look.x) > std::fabs(look.z)) return look.x > 0 ? 2 : 3;
    return look.z > 0 ? 0 : 1;
}
bool replaceable(const World& w, const BlockPos& p) { return w.reg.def(w.getBlock(p)).has(BF_REPLACEABLE); }
}

bool isDoor(const BlockRegistry& reg, BlockState s) { return s != AIR && reg.def(s).shape == BlockShape::Door; }
bool isOpenable(const BlockRegistry& reg, BlockState s) {
    if (s == AIR) return false;
    BlockShape sh = reg.def(s).shape;
    return sh == BlockShape::Door || sh == BlockShape::Trapdoor || sh == BlockShape::FenceGate;
}

BlockState withStairShape(const World& w, const BlockPos& p, BlockState s) {
    const BlockRegistry& reg = w.reg;
    if (reg.def(s).shape != BlockShape::Stairs) return s;
    int f = reg.getProp(s, "facing"), half = reg.getProp(s, "half");
    auto stairAt = [&](int dir, int& facing) {
        int dx, dz; dirOffset(dir, dx, dz);
        BlockState n = w.getBlock({p.x + dx, p.y, p.z + dz});
        if (n == AIR || reg.def(n).shape != BlockShape::Stairs || reg.getProp(n, "half") != half) return false;
        facing = reg.getProp(n, "facing"); return true;
    };
    auto sameRun = [&](int dir) { int g; return stairAt(dir, g) && g == f; };   // a straight neighbour continuing this run
    int shape = 0, g;
    // a perpendicular stair behind: outer corner, tall quarter toward its back
    if (stairAt(f, g) && (g == dirCW(f) || g == dirCCW(f)) && !sameRun(dirOpposite(g)))
        shape = g == dirCW(f) ? 3 : 4;
    // a perpendicular stair in front: inner corner, extra quarter toward its back
    else if (stairAt(dirOpposite(f), g) && (g == dirCW(f) || g == dirCCW(f)) && !sameRun(g))
        shape = g == dirCW(f) ? 1 : 2;
    return reg.withProp(s, "shape", shape);
}

BlockState withConnections(const World& w, const BlockPos& p, BlockState s) {
    const BlockDef& d = w.reg.def(s);
    if (d.shape == BlockShape::Stairs) return withStairShape(w, p, s);
    if (d.shape != BlockShape::Fence && d.shape != BlockShape::Pane && d.shape != BlockShape::Wall) return s;
    static const char* SIDES[4] = {"north", "south", "west", "east"};
    for (int k = 0; k < 4; k++) {
        int dx, dz; dirOffset(k, dx, dz);
        BlockState n = w.getBlock({p.x + dx, p.y, p.z + dz});
        const BlockDef& nd = w.reg.def(n);
        // a gate joins fences/walls that run along its axis (gate facing N/S runs east-west)
        bool gate = n != AIR && nd.shape == BlockShape::FenceGate && (w.reg.getProp(n, "facing") <= 1) == (k >= 2);
        bool c = n != AIR && (nd.shape == d.shape                                   // same family (any wood fence / any pane / any wall)
                              || (nd.shape == BlockShape::Cube && w.reg.opaque(n))   // solid block
                              || (d.shape != BlockShape::Pane && gate)
                              || (d.shape == BlockShape::Pane && nd.name == "glass"));
        s = w.reg.withProp(s, SIDES[k], c ? 1 : 0);
    }
    return s;
}

PlacePlan planBuildingPlacement(const World& w, BlockState item, const BlockPos& clicked, const BlockPos& normal, Vec3 hit, Vec3 look) {
    PlacePlan plan;
    const BlockRegistry& reg = w.reg;
    const BlockDef& d = reg.def(item);
    BlockState clickedS = w.getBlock(clicked);
    float fy = hit.y - std::floor(hit.y);   // height of the hit point within its block
    // slab onto the matching half of the same slab -> double slab, in place
    if (d.shape == BlockShape::Slab) {
        if (reg.def(clickedS).index == d.index) {
            int t = reg.getProp(clickedS, "type");
            if ((t == 0 && normal.y > 0) || (t == 1 && normal.y < 0)) { plan.ok = true; plan.at = clicked; plan.state = reg.withProp(clickedS, "type", 2); return plan; }
        }
    }
    BlockPos at = clicked;
    if (!replaceable(w, at)) at = at + normal;
    BlockState there = w.getBlock(at);
    if (d.shape == BlockShape::Slab && reg.def(there).index == d.index && reg.getProp(there, "type") != 2) {   // fill the other half
        plan.ok = true; plan.at = at; plan.state = reg.withProp(there, "type", 2); return plan;
    }
    if (!replaceable(w, at)) return plan;
    // upper half when clicking a ceiling, or the upper part of a side face
    bool upperHalf = normal.y < 0 || (normal.y == 0 && fy > 0.5f);
    int toward = facingToward(look);
    BlockState s = item;
    switch (d.shape) {
    case BlockShape::Slab: s = reg.withProp(s, "type", upperHalf ? 1 : 0); break;
    case BlockShape::Stairs: s = withStairShape(w, at, reg.withProp(reg.withProp(s, "facing", dirOpposite(toward)), "half", upperHalf ? 1 : 0)); break;
    case BlockShape::FenceGate: s = reg.withProp(s, "facing", toward); break;
    case BlockShape::Wall: s = withConnections(w, at, s); break;
    case BlockShape::Trapdoor: {
        // hinged on the block it was placed against when clicked on a side; otherwise on the far edge
        int f = normal.y == 0 ? (normal.z > 0 ? 0 : normal.z < 0 ? 1 : normal.x > 0 ? 2 : 3) : dirOpposite(toward);
        s = reg.withProp(reg.withProp(s, "facing", f), "top", upperHalf ? 1 : 0);
        break;
    }
    case BlockShape::Fence: case BlockShape::Pane: s = withConnections(w, at, s); break;
    case BlockShape::Door: {
        BlockPos up{at.x, at.y + 1, at.z};
        if (!replaceable(w, up) || !reg.solid(w.getBlock({at.x, at.y - 1, at.z}))) return plan;
        int f = toward;
        int hinge = 0;
        // a door to the clockwise side with the same facing and a clockwise hinge: mirror it (double door)
        int dx, dz; dirOffset(dirCW(f), dx, dz);
        BlockState nb = w.getBlock({at.x + dx, at.y, at.z + dz});
        if (isDoor(reg, nb) && reg.getProp(nb, "facing") == f && reg.getProp(nb, "hinge") == 0) hinge = 1;
        s = reg.withProp(reg.withProp(reg.withProp(s, "facing", f), "hinge", hinge), "upper", 0);
        plan.upper = reg.withProp(s, "upper", 1);
        break;
    }
    default: break;
    }
    plan.ok = true; plan.at = at; plan.state = s;
    return plan;
}

bool applyPlacement(World& w, const PlacePlan& plan) {
    if (!plan.ok) return false;
    BlockPos up{plan.at.x, plan.at.y + 1, plan.at.z};
    if (plan.upper != AIR && !w.setBlock(up, plan.upper, SET_NOTIFY)) return false;
    if (!w.setBlock(plan.at, plan.state)) { if (plan.upper != AIR) w.setBlock(up, AIR); return false; }
    return true;
}

bool toggleOpenable(World& w, const BlockPos& p) {
    BlockState s = w.getBlock(p);
    if (!isOpenable(w.reg, s)) return false;
    if (w.reg.def(s).name.rfind("iron_", 0) == 0) return false;   // iron doors/trapdoors: circuits only
    int open = !w.reg.getProp(s, "open");
    w.setBlock(p, w.reg.withProp(s, "open", open));
    if (isDoor(w.reg, s)) {   // keep both halves in step
        BlockPos o{p.x, p.y + (w.reg.getProp(s, "upper") ? -1 : 1), p.z};
        BlockState os = w.getBlock(o);
        if (isDoor(w.reg, os) && w.reg.def(os).index == w.reg.def(s).index) w.setBlock(o, w.reg.withProp(os, "open", open));
    }
    return true;
}

bool buildingNeighborUpdate(World& w, const BlockPos& p, const std::function<int(const BlockPos&)>& power) {
    BlockState s = w.getBlock(p);
    if (s == AIR || !isBoxedShape(w.reg.def(s).shape)) return false;
    const BlockDef& d = w.reg.def(s);
    if (d.shape == BlockShape::Fence || d.shape == BlockShape::Pane || d.shape == BlockShape::Wall || d.shape == BlockShape::Stairs) {
        BlockState c = withConnections(w, p, s);
        if (c != s) w.setBlock(p, c);
        return true;
    }
    if (d.shape == BlockShape::Door) {
        bool upper = w.reg.getProp(s, "upper");
        BlockPos o{p.x, p.y + (upper ? -1 : 1), p.z};
        BlockState os = w.getBlock(o);
        if (!(isDoor(w.reg, os) && w.reg.def(os).index == d.index && w.reg.getProp(os, "upper") != (int)upper)) { w.setBlock(p, AIR); return true; }
        if (!upper && !w.reg.solid(w.getBlock({p.x, p.y - 1, p.z}))) { w.setBlock(p, AIR); return true; }   // lost its floor (upper follows)
        if (power) {
            BlockPos lower = upper ? o : p, top = upper ? p : o;
            bool powered = power(lower) > 0 || power(top) > 0;
            if (powered != (bool)w.reg.getProp(s, "powered")) {
                for (const BlockPos& q : {lower, top}) {
                    BlockState qs = w.getBlock(q);
                    w.setBlock(q, w.reg.withProp(w.reg.withProp(qs, "powered", powered), "open", powered));
                }
            }
        }
        return true;
    }
    if ((d.shape == BlockShape::Trapdoor || d.shape == BlockShape::FenceGate) && power) {
        bool powered = power(p) > 0;
        if (powered != (bool)w.reg.getProp(s, "powered")) w.setBlock(p, w.reg.withProp(w.reg.withProp(s, "powered", powered), "open", powered));
        return true;
    }
    return true;
}

} // namespace vx
