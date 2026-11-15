#include "game/survival.h"

namespace vx {

bool canHarvest(const BlockDef& b, const ItemDef* held) {
    if (b.tool != ToolType::Pickaxe) return true;
    return held && held->tool == ToolType::Pickaxe && held->tier >= (int)b.toolLevel;
}

float breakSeconds(const BlockDef& b, const ItemDef* held, bool onGround, bool underwater) {
    if (b.hardness < 0) return -1;
    if (b.hardness == 0) return 0;
    float speed = 1.0f;
    if (held && held->tool != ToolType::None && held->tool == b.tool) speed = held->speed;
    if (held && held->tool == ToolType::Sword && b.has(BF_LEAVES)) speed = 1.5f;
    if (b.has(BF_LEAVES) && held && held->tool == ToolType::Axe) speed = held->speed;
    float t = b.hardness * (canHarvest(b, held) ? 1.5f : 5.0f) / speed;
    if (!onGround) t *= 5.0f;
    if (underwater) t *= 5.0f;
    return t;
}

std::vector<ItemStack> blockDrops(BlockState s, const BlockRegistry& blocks, const ItemRegistry& items, const RecipeBook& rb, const ItemDef* held, Rng& rng) {
    std::vector<ItemStack> out;
    const BlockDef& b = blocks.def(s);
    if (!canHarvest(b, held)) return out;
    if (b.name == "grain") {   // crops: ripe grain drops grain + seeds, unripe just a seed
        u16 seeds = items.id("grain_seeds"), grain = items.id("grain");
        if (blocks.getProp(s, "age") >= 7) { out.push_back({grain, 1, 0}); out.push_back({seeds, (u8)(1 + rng.range(3)), 0}); }
        else out.push_back({seeds, 1, 0});
        return out;
    }
    const auto* loot = rb.loot(b.name);
    if (loot) {
        for (auto& l : *loot) if (rng.chance(l.chance)) out.push_back({l.item, (u8)(1 + (l.extra ? rng.range(l.extra + 1) : 0)), 0});
        return out;
    }
    if (b.shape == BlockShape::Door && blocks.getProp(s, "upper")) return out;   // one door item, from the lower half
    u16 it = 0;
    if (b.drop == "self") it = items.forBlockType(b.index);
    else if (!b.drop.empty()) it = items.id(b.drop);
    if (it) out.push_back({it, (u8)(b.shape == BlockShape::Slab && blocks.getProp(s, "type") == 2 ? 2 : 1), 0});
    return out;
}

float fallDamage(float d) { return d > 3.0f ? std::ceil(d - 3.0f) : 0.0f; }

float PlayerStats::damage(float amount, DamageSource src) {
    if (dead || amount <= 0) return 0;
    if (hurtCooldown > 0 && src != DamageSource::Void) return 0;
    health = std::max(0.0f, health - amount);
    hurtCooldown = 10; lastDamage = src;
    exhaust(0.1f);
    if (health <= 0) dead = true;
    return amount;
}

void PlayerStats::eat(const ItemDef& f) {
    food = std::min(20.0f, food + (float)f.hunger);
    saturation = std::min(food, saturation + f.saturation);
}

void PlayerStats::tick(bool headInWater, bool inLava, bool creative) {
    if (dead) return;
    if (hurtCooldown > 0) hurtCooldown--;
    if (creative) { air = 300; return; }
    // hunger
    while (exhaustion >= 4.0f) { exhaustion -= 4.0f; if (saturation > 0) saturation = std::max(0.0f, saturation - 1); else food = std::max(0.0f, food - 1); }
    if (food >= 18 && health < 20) { if (++regenTimer >= 80) { regenTimer = 0; health = std::min(20.0f, health + 1); exhaust(6.0f); } }
    else regenTimer = 0;
    if (food <= 0) { if (++starveTimer >= 80) { starveTimer = 0; damage(1, DamageSource::Starve); } } else starveTimer = 0;
    // breath
    if (headInWater) { if (air > 0) air--; else if (++drownTimer >= 20) { drownTimer = 0; damage(2, DamageSource::Drown); } }
    else { air = std::min(300, air + 5); drownTimer = 0; }
    // lava
    if (inLava) { if (++lavaTimer >= 10) { lavaTimer = 0; hurtCooldown = 0; damage(4, DamageSource::Lava); } } else lavaTimer = 0;
}

std::vector<u8> PlayerStats::encode() const {
    ByteWriter w; w.f32_(health); w.f32_(food); w.f32_(saturation); w.f32_(exhaustion); w.u16_((u16)air); w.u8_(dead ? 1 : 0);
    return std::move(w.buf);
}
bool PlayerStats::decode(ByteReader& r) {
    health = r.f32_(); food = r.f32_(); saturation = r.f32_(); exhaustion = r.f32_(); air = r.u16_(); dead = r.u8_() != 0;
    return r.ok();
}

} // namespace vx
