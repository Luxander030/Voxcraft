#include "game/behaviors.h"

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
