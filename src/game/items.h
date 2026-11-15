// Items, inventories, crafting and kiln recipes. Data-driven (assets/data/items.json, recipes.json).
//
// Item ids are assigned at load time (block items first, in blocks.json order, then items.json)
// and are never persisted: saves store item names (ItemNames), so adding or reordering items
// never corrupts worlds.
#pragma once
#include "core/common.h"
#include "core/json.h"
#include "storage/storage.h"
#include "world/block.h"
#include "world/chunk.h"
#include <optional>
#include <unordered_map>

namespace vx {

// Name lookup for texture layers, so the item registry can resolve icons without the GPU layer.
class TexturePackNames {
public:
    virtual ~TexturePackNames() = default;
    virtual int layerOf(const std::string& name) const = 0;
};

struct ItemDef {
    std::string name;
    u16 id = 0;
    int maxStack = 64;
    int durability = 0;            // 0 = not damageable
    ToolType tool = ToolType::None;
    int tier = -1;
    float speed = 1.0f;            // mining speed multiplier on matching blocks
    float damage = 1.0f;           // attack damage
    int hunger = 0; float saturation = 0;
    BlockState places = AIR;       // block placed on use
    int fuel = 0;                  // kiln burn ticks
    int texture = -1;              // texture layer for flat icons (-1: draw `places` as a block icon)
    bool isFood() const { return hunger > 0; }
};

class ItemRegistry {
public:
    bool load(const Json& items, const BlockRegistry& blocks, const TexturePackNames* tex, std::string* err);
    bool loadFile(const std::string& path, const BlockRegistry& blocks, const TexturePackNames* tex, std::string* err);
    const ItemDef& def(u16 id) const { return id < defs_.size() ? defs_[id] : defs_[0]; }
    u16 id(const std::string& name) const { auto it = byName_.find(name); return it == byName_.end() ? 0 : it->second; }
    // item that places this block's type (0 if none)
    u16 forBlockType(u16 blockIndex) const { auto it = blockItem_.find(blockIndex); return it == blockItem_.end() ? 0 : it->second; }
    size_t size() const { return defs_.size(); }
    ItemNames names() const;
    int maxStack(u16 id) const { return def(id).maxStack; }
private:
    std::vector<ItemDef> defs_{ItemDef{"empty", 0, 0}};
    std::unordered_map<std::string, u16> byName_;
    std::unordered_map<u16, u16> blockItem_;   // block type index -> item
};

// ------------------------------------------------------------------ stacks and inventories
bool canMerge(const ItemStack& a, const ItemStack& b, const ItemRegistry& reg);

class Inventory {
public:
    explicit Inventory(int size = 36) : slots(size) {}
    std::vector<ItemStack> slots;
    // Adds as much as fits (existing stacks first, then empty slots in order). Returns the leftover count.
    int add(ItemStack s, const ItemRegistry& reg, int firstSlot = 0, int lastSlot = -1);
    int count(u16 item) const;
    bool remove(u16 item, int n);
    void clear() { for (auto& s : slots) s = {}; }
};

// Click semantics shared by every container screen. `held` is the stack on the cursor.
void clickSlot(ItemStack& slot, ItemStack& held, bool rightButton, const ItemRegistry& reg);

// ------------------------------------------------------------------ recipes
struct CraftResult { ItemStack result; };

class RecipeBook {
public:
    bool load(const Json& root, const ItemRegistry& items, std::string* err);
    bool loadFile(const std::string& path, const ItemRegistry& items, std::string* err);
    // grid is row-major gw x gh. Returns the result for the arrangement, if any.
    std::optional<ItemStack> match(const ItemStack* grid, int gw, int gh) const;
    // Consumes one item from every non-empty grid cell (call after taking the result).
    static void consume(ItemStack* grid, int n);
    u16 smelt(u16 input) const { auto it = kiln_.find(input); return it == kiln_.end() ? 0 : it->second; }
    struct Loot { u16 item; float chance; int extra; };
    const std::vector<Loot>* loot(const std::string& block) const { auto it = loot_.find(block); return it == loot_.end() ? nullptr : &it->second; }
    size_t shapedCount() const { return shaped_.size(); }
private:
    struct Shaped { int w, h; std::vector<std::vector<u16>> cells; ItemStack result; };   // empty cell = empty vector
    std::vector<Shaped> shaped_;
    std::unordered_map<u16, u16> kiln_;
    std::unordered_map<std::string, std::vector<Loot>> loot_;
};

} // namespace vx
