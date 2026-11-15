// Glimmer circuits: signal sources, wire, inverting spark torches, lamps, relays, watchers, pistons.
//
// Model (Voxcraft's own rules, following the genre's conventions):
//  * Floor sources (lever, button, plate when on) emit 15 into all six neighbours and strongly
//    power the block they stand on. A lit spark torch emits 15 into every neighbour except the
//    block it stands on, and strongly powers the block above it. It turns off (2 ticks later)
//    when the block it stands on is powered: an inverter.
//  * Glimmer wire carries strength 0..15, losing 1 per block. It connects to horizontal wires and
//    to wires one block up/down diagonally. A wire weakly powers the block below it.
//  * A strongly powered solid block feeds wire and components around it. A weakly powered block
//    feeds components but not wire, which prevents feedback through the block under a wire.
//  * A relay outputs 15 forward (strongly powering that block) after (delay+1)*2 ticks.
//  * A watcher emits a 2-tick pulse from its back when the block in front of it changes.
//  * A piston pushes up to 12 blocks when powered and retracts when the power is gone.
// Wire updates are immediate (bounded: strength strictly decreases along a wire). Everything with
// a delay uses scheduled ticks, which are persistent and processed in a deterministic order.
#include "game/behaviors.h"

namespace vx {

namespace {
const BlockPos DIRS[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
const BlockPos FOUR[4] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};   // 4-way facing N S W E
const BlockPos SIX[6] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};   // 6-way: down up N S W E
BlockPos neg(const BlockPos& d) { return {-d.x, -d.y, -d.z}; }
bool eq(const BlockPos& a, const BlockPos& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
}

int BlockBehaviors::strongPower(const BlockPos& b) const {
    if (!w_.reg.solid(w_.getBlock(b))) return 0;
    int best = 0;
    // a source standing on it
    BlockState up = w_.getBlock({b.x, b.y + 1, b.z});
    u16 ui = w_.reg.def(up).index;
    if ((ui == leverIdx_ || ui == buttonIdx_ || ui == plateIdx_) && w_.reg.getProp(up, "on")) best = 15;
    // a lit torch below it
    BlockState dn = w_.getBlock({b.x, b.y - 1, b.z});
    if (w_.reg.def(dn).index == torchIdx_ && w_.reg.getProp(dn, "lit")) best = 15;
    // a relay or watcher outputting into it
    for (auto& d : DIRS) {
        BlockPos n = b + d; BlockState ns = w_.getBlock(n); u16 ni = w_.reg.def(ns).index;
        if (ni == relayIdx_ && w_.reg.getProp(ns, "powered") && eq(n + FOUR[w_.reg.getProp(ns, "facing")], b)) best = 15;
        if (ni == watcherIdx_ && w_.reg.getProp(ns, "powered") && eq(n + neg(SIX[w_.reg.getProp(ns, "facing")]), b)) best = 15;
    }
    return best;
}

bool BlockBehaviors::weaklyPowered(const BlockPos& b) const {
    BlockState up = w_.getBlock({b.x, b.y + 1, b.z});
    return w_.reg.solid(w_.getBlock(b)) && w_.reg.def(up).index == wireIdx_ && w_.reg.getProp(up, "power") > 0;
}

// Power that the block at src delivers into dst (a neighbour). forWire: the receiver is a wire.
int BlockBehaviors::emitInto(const BlockPos& src, const BlockPos& dst, bool forWire) const {
    BlockState s = w_.getBlock(src);
    if (s == AIR) return 0;
    const BlockDef& d = w_.reg.def(s);
    u16 i = d.index;
    if (i == leverIdx_ || i == buttonIdx_ || i == plateIdx_) return w_.reg.getProp(s, "on") ? 15 : 0;
    if (i == torchIdx_) return (w_.reg.getProp(s, "lit") && !eq(dst, {src.x, src.y - 1, src.z})) ? 15 : 0;
    if (i == wireIdx_) {
        if (forWire) return 0;   // wire-to-wire handled by the wire rule
        if (dst.y > src.y) return 0;   // wire does not power upwards
        return w_.reg.getProp(s, "power");
    }
    if (i == relayIdx_) return (w_.reg.getProp(s, "powered") && eq(src + FOUR[w_.reg.getProp(s, "facing")], dst)) ? 15 : 0;
    if (i == watcherIdx_) return (w_.reg.getProp(s, "powered") && eq(src + neg(SIX[w_.reg.getProp(s, "facing")]), dst)) ? 15 : 0;
    if (d.solid && d.has(BF_CIRCUIT) == false) {
        int sp = strongPower(src);
        if (sp) return sp;
        if (!forWire && weaklyPowered(src)) return 15;
    }
    return 0;
}

int BlockBehaviors::inputPower(const BlockPos& p) const {
    int best = 0;
    for (auto& d : DIRS) best = std::max(best, emitInto(p + d, p, false));
    return best;
}

void BlockBehaviors::notifyAround(const BlockPos& p) {
    // Two-block radius: a source change can power the block below/above it, which in turn feeds
    // components next to that block.
    for (auto& d : DIRS) {
        BlockPos n = p + d;
        neighborChanged(n, p);
        for (auto& e : DIRS) { BlockPos m = n + e; if (!eq(m, p)) neighborChanged(m, n); }
    }
}

bool BlockBehaviors::circuitUpdate(const BlockPos& p, BlockState s, const BlockPos& from) {
    u16 i = w_.reg.def(s).index;
    if (i == wireIdx_) {
        // wire strength: strongest neighbour source, or neighbour wire - 1
        int best = 0;
        for (auto& d : DIRS) best = std::max(best, emitInto(p + d, p, true));
        for (auto& d : FOUR) {
            for (int dy = -1; dy <= 1; dy++) {
                BlockPos n{p.x + d.x, p.y + dy, p.z + d.z};
                BlockState ns = w_.getBlock(n);
                if (w_.reg.def(ns).index != wireIdx_) continue;
                if (dy == 1 && w_.reg.opaque(w_.getBlock({p.x, p.y + 1, p.z}))) continue;   // cut by a block above us
                if (dy == -1 && w_.reg.opaque(w_.getBlock({p.x + d.x, p.y, p.z + d.z}))) continue;
                best = std::max(best, w_.reg.getProp(ns, "power") - 1);
            }
        }
        if (best != w_.reg.getProp(s, "power")) {
            w_.setBlock(p, w_.reg.withProp(s, "power", best), SET_NOTIFY);
            notifyAround(p);
        }
        return true;
    }
    if (eq(p, from) && (i == leverIdx_ || i == buttonIdx_ || i == plateIdx_)) { notifyAround(p); return true; }
    if (i == torchIdx_) {
        BlockPos below{p.x, p.y - 1, p.z};
        bool powered = strongPower(below) > 0 || weaklyPowered(below);
        if (powered == (w_.reg.getProp(s, "lit") != 0)) w_.scheduleTick(p, s, 2);
        if (!w_.reg.solid(w_.getBlock(below))) w_.setBlock(p, AIR);
        return true;
    }
    if (i == lampIdx_) {
        bool on = inputPower(p) > 0, lit = w_.reg.getProp(s, "lit") != 0;
        if (on && !lit) w_.setBlock(p, w_.reg.withProp(s, "lit", 1));
        else if (!on && lit) w_.scheduleTick(p, s, 2);
        return true;
    }
    if (i == relayIdx_) {
        BlockPos back = p + neg(FOUR[w_.reg.getProp(s, "facing")]);
        bool in = emitInto(back, p, false) > 0;
        if (in != (w_.reg.getProp(s, "powered") != 0)) w_.scheduleTick(p, s, (w_.reg.getProp(s, "delay") + 1) * 2);
        if (eq(p, from)) notifyAround(p);
        return true;
    }
    if (i == watcherIdx_) {
        BlockPos front = p + SIX[w_.reg.getProp(s, "facing")];
        if (eq(from, front) && !w_.reg.getProp(s, "powered")) w_.scheduleTick(p, s, 2);
        if (eq(p, from)) notifyAround(p);
        return true;
    }
    if (i == pistonIdx_) {
        bool on = false;
        BlockPos front = p + SIX[w_.reg.getProp(s, "facing")];
        for (auto& d : DIRS) { BlockPos n = p + d; if (!eq(n, front)) on = on || emitInto(n, p, false) > 0; }
        if (on != (w_.reg.getProp(s, "extended") != 0)) w_.scheduleTick(p, s, 2);
        // head removed by someone else: retract state
        if (w_.reg.getProp(s, "extended") && w_.reg.def(w_.getBlock(front)).index != headIdx_) w_.setBlock(p, w_.reg.withProp(s, "extended", 0));
        return true;
    }
    if (i == headIdx_) {
        BlockPos back = p + neg(SIX[w_.reg.getProp(s, "facing")]);
        BlockState b = w_.getBlock(back);
        if (w_.reg.def(b).index != pistonIdx_ || !w_.reg.getProp(b, "extended")) w_.setBlock(p, AIR);   // orphaned head
        return true;
    }
    return false;
}

bool BlockBehaviors::circuitTick(const BlockPos& p, BlockState s) {
    u16 i = w_.reg.def(s).index;
    if (i == torchIdx_) {
        BlockPos below{p.x, p.y - 1, p.z};
        bool powered = strongPower(below) > 0 || weaklyPowered(below);
        int lit = powered ? 0 : 1;
        if (lit != w_.reg.getProp(s, "lit")) { w_.setBlock(p, w_.reg.withProp(s, "lit", lit)); notifyAround(p); }
    } else if (i == lampIdx_) {
        if (inputPower(p) == 0) w_.setBlock(p, w_.reg.withProp(s, "lit", 0));
    } else if (i == relayIdx_) {
        BlockPos back = p + neg(FOUR[w_.reg.getProp(s, "facing")]);
        int in = emitInto(back, p, false) > 0 ? 1 : 0;
        if (in != w_.reg.getProp(s, "powered")) { w_.setBlock(p, w_.reg.withProp(s, "powered", in)); notifyAround(p); }
    } else if (i == watcherIdx_) {
        if (!w_.reg.getProp(s, "powered")) { BlockState on = w_.reg.withProp(s, "powered", 1); w_.setBlock(p, on); notifyAround(p); w_.scheduleTick(p, on, 2); }
        else { w_.setBlock(p, w_.reg.withProp(s, "powered", 0)); notifyAround(p); }
    } else if (i == buttonIdx_) {
        if (w_.reg.getProp(s, "on")) w_.setBlock(p, w_.reg.withProp(s, "on", 0));
    } else if (i == plateIdx_) {
        if (w_.reg.getProp(s, "on")) {
            if (entityOnPlate && entityOnPlate(p)) w_.scheduleTick(p, s, 10);
            else w_.setBlock(p, w_.reg.withProp(s, "on", 0));
        }
    } else if (i == pistonIdx_) {
        bool on = false;
        BlockPos front = p + SIX[w_.reg.getProp(s, "facing")];
        for (auto& d : DIRS) { BlockPos n = p + d; if (!eq(n, front)) on = on || emitInto(n, p, false) > 0; }
        if (on != (w_.reg.getProp(s, "extended") != 0)) pistonSet(p, s, on);
    }
    return true;
}

void BlockBehaviors::pistonSet(const BlockPos& p, BlockState s, bool extend) {
    int f = w_.reg.getProp(s, "facing");
    BlockPos dir = SIX[f], front = p + dir;
    if (!extend) {
        if (w_.reg.def(w_.getBlock(front)).index == headIdx_) w_.setBlock(front, AIR, SET_NOTIFY);
        w_.setBlock(p, w_.reg.withProp(s, "extended", 0));
        return;
    }
    // collect the pushed line
    std::vector<BlockPos> line;
    BlockPos q = front;
    for (int n = 0; n <= 12; n++, q = q + dir) {
        BlockState b = w_.getBlock(q);
        const BlockDef& d = w_.reg.def(b);
        if (b == AIR || d.has(BF_REPLACEABLE) || d.has(BF_FLUID)) break;
        if (n == 12 || d.hardness < 0 || d.has(BF_BLOCK_ENTITY) || d.index == pistonIdx_ || d.index == headIdx_ || q.y + dir.y < 0 || q.y + dir.y >= WORLD_H) return;   // blocked
        line.push_back(q);
    }
    for (auto it = line.rbegin(); it != line.rend(); ++it) {
        BlockState b = w_.getBlock(*it);
        w_.setBlock(*it + dir, b, SET_NOTIFY);
        w_.setBlock(*it, AIR, SET_NOTIFY);
    }
    w_.setBlock(p, w_.reg.withProp(s, "extended", 1), SET_NOTIFY);
    BlockState head = w_.reg.withProp(w_.reg.stateOf("piston_head"), "facing", f);
    w_.setBlock(front, head, SET_NOTIFY);
    // let everything around the moved line react once the move is complete
    for (auto& b : line) notifyAround(b + dir);
    notifyAround(p);
}

void BlockBehaviors::toggleLever(const BlockPos& p) {
    BlockState s = w_.getBlock(p);
    if (w_.reg.def(s).index != leverIdx_) return;
    w_.setBlock(p, w_.reg.withProp(s, "on", w_.reg.getProp(s, "on") ? 0 : 1));
}
void BlockBehaviors::pressButton(const BlockPos& p) {
    BlockState s = w_.getBlock(p);
    if (w_.reg.def(s).index != buttonIdx_ || w_.reg.getProp(s, "on")) return;
    BlockState on = w_.reg.withProp(s, "on", 1);
    w_.setBlock(p, on); w_.scheduleTick(p, on, 20);
}
void BlockBehaviors::pressPlate(const BlockPos& p) {
    BlockState s = w_.getBlock(p);
    if (w_.reg.def(s).index != plateIdx_ || w_.reg.getProp(s, "on")) return;
    BlockState on = w_.reg.withProp(s, "on", 1);
    w_.setBlock(p, on); w_.scheduleTick(p, on, 10);
}

} // namespace vx
