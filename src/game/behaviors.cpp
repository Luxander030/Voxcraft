#include "game/behaviors.h"
#include "game/building.h"
#include "game/rails.h"

namespace vx {

namespace {
const BlockPos HORIZ[4] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}};
const BlockPos ALL6[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
}

BlockBehaviors::BlockBehaviors(World& w) : w_(w) {
    cb.resolve(w.reg);
    waterIdx_ = w.reg.def(cb.water).index; lavaIdx_ = w.reg.def(cb.lava).index;
    lvlProp_ = w.reg.def(cb.water).propIndex("level");
    auto idx = [&](const char* n) { const BlockDef* d = w.reg.byName(n); return d ? d->index : (u16)0xFFFF; };
    wireIdx_ = idx("glimmer_wire"); torchIdx_ = idx("spark_torch"); leverIdx_ = idx("lever"); buttonIdx_ = idx("push_button");
    plateIdx_ = idx("pressure_plate"); lampIdx_ = idx("lamp"); relayIdx_ = idx("relay"); watcherIdx_ = idx("watcher");
    pistonIdx_ = idx("piston"); headIdx_ = idx("piston_head");
    riftIdx_ = idx("rift"); slateIdx_ = idx("slate");
    stickyIdx_ = idx("sticky_piston"); comparatorIdx_ = idx("comparator"); dropperIdx_ = idx("dropper"); dispenserIdx_ = idx("dispenser"); hopperIdx_ = idx("hopper");
    w.onNeighborChanged = [this](const BlockPos& p, const BlockPos& from) { neighborChanged(p, from); };
    w.onScheduledTick = [this](const BlockPos& p, BlockState s) { scheduled(p, s); };
    w.onRandomTick = [this](const BlockPos& p, BlockState s) { randomTick(p, s); };
}

BlockBehaviors::~BlockBehaviors() { w_.onNeighborChanged = nullptr; w_.onScheduledTick = nullptr; w_.onRandomTick = nullptr; }

bool BlockBehaviors::canFlowInto(BlockState s, u16 fluidIdx) const {
    if (s == AIR) return true;
    const BlockDef& d = w_.reg.def(s);
    if (d.index == fluidIdx) return true;
    return d.has(BF_REPLACEABLE) && !d.has(BF_FLUID);
}

void BlockBehaviors::placeFluidSource(const BlockPos& p, bool lava) {
    BlockState s = lava ? cb.lava : cb.water;
    if (w_.setBlock(p, s)) w_.scheduleTick(p, s, lava ? LAVA_DELAY : WATER_DELAY);
}

void BlockBehaviors::neighborChanged(const BlockPos& p, const BlockPos& from) {
    // Iterative FIFO instead of recursion: long circuits cannot overflow the stack, and the
    // processing order is deterministic.
    queue_.push_back({p, from});
    if (inUpdate_) return;
    inUpdate_ = true;
    for (size_t i = 0; i < queue_.size(); i++) {
        if (i > 200000) { VX_WARN("block update storm truncated"); break; }
        auto [q, f] = queue_[i];
        processUpdate(q, f);
    }
    queue_.clear();
    inUpdate_ = false;
}

void BlockBehaviors::processUpdate(const BlockPos& p, const BlockPos& from) {
    BlockState s = w_.getBlock(p);
    if (s == AIR) return;
    const BlockDef& d = w_.reg.def(s);
    if (d.has(BF_CIRCUIT) && circuitUpdate(p, s, from)) return;
    if (isBoxedShape(d.shape)) { buildingNeighborUpdate(w_, p, [this](const BlockPos& q) { return inputPower(q); }); return; }
    if (d.shape == BlockShape::Rail) { railNeighborUpdate(w_, p, [this](const BlockPos& q) { return inputPower(q); }); return; }
    if (d.index == riftIdx_) { if (!riftSupported(p)) w_.setBlock(p, AIR); return; }   // frame broken: collapse (cascades)
    if (d.name == "lilypad") { if (w_.reg.def(w_.getBlock({p.x, p.y - 1, p.z})).name != "water") w_.setBlock(p, AIR); return; }
    if (d.index == waterIdx_) w_.scheduleTick(p, s, WATER_DELAY);
    else if (d.index == lavaIdx_) w_.scheduleTick(p, s, LAVA_DELAY);
    else if (d.has(BF_GRAVITY)) w_.scheduleTick(p, s, FALL_DELAY);
    else if (d.has(BF_NEEDS_SOIL) || d.has(BF_ATTACHED_FLOOR)) {
        BlockState below = w_.getBlock({p.x, p.y - 1, p.z});
        if (!w_.reg.solid(below)) w_.setBlock(p, AIR);   // lost its support
    }
}

void BlockBehaviors::scheduled(const BlockPos& p, BlockState s) {
    const BlockDef& d = w_.reg.def(s);
    if (d.has(BF_CIRCUIT)) { circuitTick(p, s); return; }
    if (d.index == waterIdx_ || d.index == lavaIdx_) fluidTick(p, s);
    else if (d.has(BF_GRAVITY)) fallTick(p, s);
}

void BlockBehaviors::fluidTick(const BlockPos& p, BlockState s) {
    const u16 F = w_.reg.def(s).index;
    const bool lava = F == lavaIdx_;
    const int drop = lava ? 2 : 1, delay = lava ? LAVA_DELAY : WATER_DELAY;
    const BlockState base = lava ? cb.lava : cb.water;
    int lvl = level(s);

    // 1. Re-evaluate a non-source cell from its feeders.
    if (lvl != 0) {
        int want = 99;
        BlockState above = w_.getBlock({p.x, p.y + 1, p.z});
        if (isFluid(above, F)) want = 8;
        else {
            int sources = 0;
            for (auto& d : HORIZ) {
                BlockState n = w_.getBlock(p + d);
                if (!isFluid(n, F)) continue;
                int nl = level(n);
                if (nl == 0) sources++;
                int eff = (nl == 0 || nl >= 8) ? 0 : nl;
                want = std::min(want, eff + drop);
            }
            BlockState below = w_.getBlock({p.x, p.y - 1, p.z});
            if (!lava && sources >= 2 && (w_.reg.solid(below) || (isFluid(below, F) && level(below) == 0))) want = 0;   // renewable water
        }
        // Support got weaker (or vanished): drain immediately instead of creeping up one level per
        // tick. Mutually-feeding flowing cells would otherwise count slowly to infinity; cells that
        // still have a real path to a source are refilled by it on the next tick.
        if ((want > 7 && want != 8) || (lvl < 8 && want != 8 && want > lvl)) { w_.setBlock(p, AIR); return; }
        if (want != lvl) {
            BlockState ns = w_.reg.withProp(base, lvlProp_, want);
            w_.setBlock(p, ns);
            w_.scheduleTick(p, ns, delay);
            return;
        }
    }

    // 2. Interact with the other fluid: water touching lava hardens it.
    for (auto& d : ALL6) {
        BlockPos q = p + d; BlockState n = w_.getBlock(q);
        u16 other = lava ? waterIdx_ : lavaIdx_;
        if (!isFluid(n, other)) continue;
        BlockPos lavaPos = lava ? p : q; BlockState lavaState = lava ? s : n;
        if (d.y == -1 && !lava) continue;   // water above lava: lava hardens via the lava's own tick
        w_.setBlock(lavaPos, level(lavaState) == 0 ? cb.slate : cb.cobblestone);
        if (lava) return;
    }

    // 3. Spread: down first; sideways only when resting on something solid.
    BlockPos down{p.x, p.y - 1, p.z};
    BlockState below = w_.getBlock(down);
    if (p.y > 0 && canFlowInto(below, F)) {
        if (!(isFluid(below, F) && (level(below) == 0 || level(below) == 8))) {
            BlockState fall = w_.reg.withProp(base, lvlProp_, 8);
            if (w_.setBlock(down, fall)) w_.scheduleTick(down, fall, delay);
        }
        if (!isFluid(below, F) || level(below) != 0) return;
    }
    int eff = (lvl == 0 || lvl >= 8) ? 0 : lvl;
    int next = eff + drop;
    if (next > 7) return;
    for (auto& d : HORIZ) {
        BlockPos q = p + d; BlockState n = w_.getBlock(q);
        if (!canFlowInto(n, F)) continue;
        if (isFluid(n, F)) { int nl = level(n); if (nl == 0 || nl >= 8 || nl <= next) continue; }
        BlockState ns = w_.reg.withProp(base, lvlProp_, next);
        if (w_.setBlock(q, ns)) w_.scheduleTick(q, ns, delay);
    }
}

void BlockBehaviors::fallTick(const BlockPos& p, BlockState s) {
    BlockPos down{p.x, p.y - 1, p.z};
    if (p.y <= 0) return;
    BlockState below = w_.getBlock(down);
    const BlockDef& bd = w_.reg.def(below);
    if (below != AIR && !bd.has(BF_FLUID) && !bd.has(BF_REPLACEABLE)) return;
    w_.setBlock(p, AIR);
    w_.setBlock(down, s);
    w_.scheduleTick(down, s, 1);   // keep falling one block per tick
}

bool BlockBehaviors::riftSupported(const BlockPos& p) const {
    // valid if, in one vertical plane, every in-plane neighbour is rift or slate
    for (int axis = 0; axis < 2; axis++) {
        BlockPos h = axis == 0 ? BlockPos{1, 0, 0} : BlockPos{0, 0, 1};
        bool ok = true;
        for (BlockPos d : {h, BlockPos{-h.x, 0, -h.z}, BlockPos{0, 1, 0}, BlockPos{0, -1, 0}}) {
            u16 i = w_.reg.def(w_.getBlock(p + d)).index;
            if (i != riftIdx_ && i != slateIdx_) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

int BlockBehaviors::lightRift(const BlockPos& inside) {
    if (w_.getBlock(inside) != AIR) return 0;
    for (int axis = 0; axis < 2; axis++) {
        BlockPos h = axis == 0 ? BlockPos{1, 0, 0} : BlockPos{0, 0, 1};
        std::vector<BlockPos> cells{inside}, stack{inside};
        bool closed = true;
        while (!stack.empty() && closed) {
            BlockPos c = stack.back(); stack.pop_back();
            for (BlockPos d : {h, BlockPos{-h.x, 0, -h.z}, BlockPos{0, 1, 0}, BlockPos{0, -1, 0}}) {
                BlockPos n = c + d;
                BlockState s = w_.getBlock(n);
                if (w_.reg.def(s).index == slateIdx_) continue;
                if (s != AIR) { closed = false; break; }
                if (std::find(cells.begin(), cells.end(), n) != cells.end()) continue;
                if (cells.size() >= 21) { closed = false; break; }   // too large: not a gate
                cells.push_back(n); stack.push_back(n);
            }
        }
        if (!closed) continue;
        BlockState rift = w_.reg.stateOf("rift");
        for (auto& c : cells) w_.setBlock(c, rift, SET_NOTIFY);   // no neighbour updates until complete
        return (int)cells.size();
    }
    return 0;
}

void BlockBehaviors::buildRiftGate(const BlockPos& base, bool alongX) {
    BlockPos h = alongX ? BlockPos{1, 0, 0} : BlockPos{0, 0, 1}, o = alongX ? BlockPos{0, 0, 1} : BlockPos{1, 0, 0};
    BlockState slate = w_.reg.stateOf("slate");
    for (int i = -1; i <= 2; i++) for (int j = -1; j <= 3; j++) {
        BlockPos p{base.x + h.x * i, base.y + j, base.z + h.z * i};
        bool frame = i == -1 || i == 2 || j == -1 || j == 3;
        w_.setBlock(p, frame ? slate : AIR, SET_NOTIFY);
        for (int k : {-1, 1}) {   // clear standing room on both sides, with a floor
            BlockPos q{p.x + o.x * k, p.y, p.z + o.z * k};
            if (j >= 0 && j <= 2) w_.setBlock(q, AIR, SET_NOTIFY);
            if (j == -1 && !w_.reg.solid(w_.getBlock(q))) w_.setBlock(q, slate, SET_NOTIFY);
        }
    }
    lightRift(base);
}

void BlockBehaviors::randomTick(const BlockPos& p, BlockState s) {
    if (s == cb.turf) {
        BlockState above = w_.getBlock({p.x, p.y + 1, p.z});
        if (w_.reg.opaque(above) || w_.reg.fluid(above)) { w_.setBlock(p, cb.dirt); return; }   // smothered
        if (w_.skyLight({p.x, p.y + 1, p.z}) < 9 && w_.blockLight({p.x, p.y + 1, p.z}) < 9) return;
        // spread to one nearby dirt block with light and air above
        static u32 r = 12345; r = r * 1103515245u + 12345u;
        BlockPos q{p.x + (int)(r >> 8) % 3 - 1, p.y + (int)(r >> 12) % 3 - 1, p.z + (int)(r >> 16) % 3 - 1};
        if (w_.getBlock(q) == cb.dirt && !w_.reg.opaque(w_.getBlock({q.x, q.y + 1, q.z})) && w_.skyLight({q.x, q.y + 1, q.z}) >= 4)
            w_.setBlock(q, cb.turf);
    } else if (w_.reg.def(s).name == "farmland") {
        bool wet = false;   // moist if water within 4 blocks on the same level or one below
        for (int dz = -4; dz <= 4 && !wet; dz++) for (int dx = -4; dx <= 4 && !wet; dx++) for (int dy = -1; dy <= 0 && !wet; dy++)
            wet = w_.reg.def(w_.getBlock({p.x + dx, p.y + dy, p.z + dz})).name == "water";
        if (wet != (w_.reg.getProp(s, "moist") != 0)) w_.setBlock(p, w_.reg.withProp(s, "moist", wet ? 1 : 0), SET_NOTIFY);
        if (w_.reg.opaque(w_.getBlock({p.x, p.y + 1, p.z}))) w_.setBlock(p, cb.dirt);   // covered: back to dirt
    } else if (w_.reg.def(s).name == "grain") {
        int age = w_.reg.getProp(s, "age");
        BlockState soil = w_.getBlock({p.x, p.y - 1, p.z});
        bool moist = w_.reg.def(soil).name == "farmland" && w_.reg.getProp(soil, "moist");
        static u32 r = 99991; r = r * 1103515245u + 12345u;
        if (age < 7 && (r >> 16) % (moist ? 3u : 7u) == 0 && std::max(w_.skyLight(p), w_.blockLight(p)) >= 9)
            w_.setBlock(p, w_.reg.withProp(s, "age", age + 1), SET_NOTIFY);
    } else if (w_.reg.def(s).name == "sapling") {
        if (w_.skyLight(p) >= 9) { static u32 r = 777; r = r * 1103515245u + 12345u; if ((r >> 16) % 7 == 0) growTree(p); }
    }
}

void BlockBehaviors::growTree(const BlockPos& p) {
    int h = 4 + (int)((u32)(p.x * 73856093 ^ p.z * 19349663) % 3);
    for (int y = 1; y <= h + 1; y++) if (w_.getBlock({p.x, p.y + y, p.z}) != AIR) return;   // no room
    for (int y = 0; y < h; y++) w_.setBlock({p.x, p.y + y, p.z}, cb.oak_log);
    for (int dy = h - 2; dy <= h + 1; dy++) {
        int r = dy >= h ? 1 : 2;
        for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
            if (std::abs(dx) == r && std::abs(dz) == r && (dy >= h || ((p.x + dx * 7 + dz * 13 + dy) & 1))) continue;   // ragged corners
            BlockPos q{p.x + dx, p.y + dy, p.z + dz};
            if (w_.getBlock(q) == AIR) w_.setBlock(q, cb.oak_leaves);
        }
    }
}

} // namespace vx
