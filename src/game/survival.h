// Survival rules: block breaking time and harvesting, drops, health / hunger / air.
// All numbers are Voxcraft's own tuning; see REFERENCE_NOTES.md for the conventions followed.
#pragma once
#include "game/items.h"
#include "core/common.h"

namespace vx {

// A block "needs" its tool when it is a pickaxe block: stone-like blocks drop nothing by hand.
bool canHarvest(const BlockDef& b, const ItemDef* held);
// Seconds to break with the held item (0 = instant, <0 = unbreakable).
float breakSeconds(const BlockDef& b, const ItemDef* held, bool onGround, bool underwater);
// Items dropped when a block is broken (empty if not harvestable).
std::vector<ItemStack> blockDrops(BlockState s, const BlockRegistry& blocks, const ItemRegistry& items, const RecipeBook& rb, const ItemDef* held, Rng& rng);

enum class DamageSource : u8 { Fall, Drown, Lava, Starve, Void, Attack, Generic };

struct PlayerStats {
    float health = 20, food = 20, saturation = 5, exhaustion = 0;
    int air = 300;                 // ticks of breath
    int hurtCooldown = 0, regenTimer = 0, starveTimer = 0, drownTimer = 0, lavaTimer = 0;
    bool dead = false;
    DamageSource lastDamage = DamageSource::Generic;

    // Returns damage actually applied (respects the post-hit invulnerability window).
    float damage(float amount, DamageSource src);
    void exhaust(float a) { exhaustion += a; }
    void eat(const ItemDef& food);
    // Per 20 Hz tick. `headInWater`, `inLava` from the player; `creative` disables everything.
    void tick(bool headInWater, bool inLava, bool creative);
    void respawn() { *this = PlayerStats{}; }
    std::vector<u8> encode() const;
    bool decode(ByteReader& r);
};

float fallDamage(float distance);   // 0 for falls up to 3 blocks

} // namespace vx
