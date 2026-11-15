// Fishing: a cast bobber flies under gravity, floats on water, and after a random wait a fish
// bites. Reeling in while the bobber is dipped (the bite window) lands a catch from a weighted
// table: mostly fish, some junk, rarely treasure. Water under open sky bites twice as often as
// covered water. The bobber is transient (never saved): quitting or switching items reels it in.
#pragma once
#include "game/player.h"
#include "game/items.h"

namespace vx {

struct Bobber {
    Vec3 pos, prevPos, vel;
    bool inWater = false, removed = false;
    int wait = 0;        // ticks until the next bite (counts down while floating)
    int bite = 0;        // > 0 while a fish is on: reel now
    int age = 0;
};

struct CatchEntry { const char* item; int weight; int count; };

class Fishing {
public:
    static constexpr float BOX = 0.125f;   // half-size of the bobber box
    static constexpr int BITE_WINDOW = 30, MIN_WAIT = 100, MAX_WAIT = 500, MAX_AGE = 20 * 60 * 3;
    static constexpr float MAX_LINE = 32.0f;
    // Starts a cast from `eye` along `look`.
    static Bobber cast(Vec3 eye, Vec3 look, Rng& rng);
    // One tick. Returns true on the tick a fish bites (for a splash).
    static bool tick(const World& w, Bobber& b, Vec3 rodHolder, Rng& rng);
    // Reel in: the catch (empty if nothing was biting). Removes the bobber either way.
    static ItemStack reel(Bobber& b, const ItemRegistry& items, Rng& rng);
    static const std::vector<CatchEntry>& table();
};

} // namespace vx
