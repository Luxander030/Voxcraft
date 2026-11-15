#include "game/items.h"
#include "core/util.h"

namespace vx {

static ToolType parseTool(const std::string& s) {
    if (s == "pickaxe") return ToolType::Pickaxe; if (s == "axe") return ToolType::Axe; if (s == "shovel") return ToolType::Shovel;
    if (s == "hoe") return ToolType::Hoe; if (s == "sword") return ToolType::Sword; return ToolType::None;
}

bool ItemRegistry::load(const Json& root, const BlockRegistry& blocks, const TexturePackNames* tex, std::string* err) {
    if (!root.isObject()) { if (err) *err = "items.json: not an object"; return false; }
    defs_.resize(1); byName_.clear(); blockItem_.clear();
    auto addDef = [&](ItemDef d) -> ItemDef& {
        d.id = (u16)defs_.size(); byName_[d.name] = d.id; defs_.push_back(std::move(d)); return defs_.back();
    };
    // block items
    for (const BlockDef& b : blocks.defs()) {
        if (b.index == 0 || b.has(BF_FLUID) || b.name == "piston_head" || b.name == "glimmer_wire") continue;
        ItemDef d; d.name = b.name; d.places = b.base;
        ItemDef& added = addDef(std::move(d));
        blockItem_[b.index] = added.id;
    }
    // explicit items (may extend block items)
    for (auto& [name, j] : root.obj) {
        u16 existing = id(name);
        ItemDef* d;
        if (existing) d = &defs_[existing];
        else { ItemDef nd; nd.name = name; d = &addDef(std::move(nd)); }
        d->maxStack = j["stack"].asInt(d->maxStack);
        d->durability = j["durability"].asInt(d->durability);
        d->damage = (float)j["damage"].asNumber(d->damage);
        d->fuel = j["fuel"].asInt(d->fuel);
        if (const Json* t = j.find("tool")) { d->tool = parseTool((*t)["type"].asString()); d->tier = (*t)["tier"].asInt(0); d->speed = (float)(*t)["speed"].asNumber(1); }
        if (const Json* a = j.find("armor")) {
            std::string sl = (*a)["slot"].asString();
            d->armorSlot = sl == "head" ? 0 : sl == "chest" ? 1 : sl == "legs" ? 2 : sl == "feet" ? 3 : -1;
            d->armorPoints = (*a)["points"].asInt(0);
        }
        if (const Json* f = j.find("food")) { d->hunger = (*f)["hunger"].asInt(0); d->saturation = (float)(*f)["saturation"].asNumber(0); }
        if (const Json* p = j.find("places")) {
            const BlockDef* b = blocks.byName(p->asString());
            if (!b) { if (err) *err = "item '" + name + "' places unknown block '" + p->asString() + "'"; return false; }
            d->places = b->base;
        }
        if (const Json* t = j.find("tex")) {
            if (tex) { d->texture = tex->layerOf(t->asString()); if (d->texture < 0) { if (err) *err = "item '" + name + "': missing texture " + t->asString(); return false; } }
        }
        if (d->durability > 0) d->maxStack = 1;
    }
    return true;
}

bool ItemRegistry::loadFile(const std::string& path, const BlockRegistry& blocks, const TexturePackNames* tex, std::string* err) {
    auto txt = readTextFile(pathFromUtf8(path));
    if (!txt) { if (err) *err = "cannot read " + path; return false; }
    std::string perr; Json j = Json::parse(*txt, &perr);
    if (!perr.empty()) { if (err) *err = path + ": " + perr; return false; }
    return load(j, blocks, tex, err);
}



ItemNames ItemRegistry::names() const {
    return {[this](u16 i) { return i && i < defs_.size() ? defs_[i].name : std::string(); },
            [this](const std::string& n) { return id(n); }};
}

// ------------------------------------------------------------------ stacks
bool canMerge(const ItemStack& a, const ItemStack& b, const ItemRegistry& reg) {
    return a.item == b.item && a.damage == 0 && b.damage == 0 && reg.maxStack(a.item) > 1;
}

int Inventory::add(ItemStack s, const ItemRegistry& reg, int first, int last) {
    if (s.empty()) return 0;
    if (last < 0) last = (int)slots.size() - 1;
    int left = s.count, max = reg.maxStack(s.item);
    for (int i = first; i <= last && left > 0; i++) {
        ItemStack& t = slots[i];
        if (!t.empty() && canMerge(t, s, reg) && t.count < max) { int n = std::min(left, max - t.count); t.count = (u8)(t.count + n); left -= n; }
    }
    for (int i = first; i <= last && left > 0; i++) {
        ItemStack& t = slots[i];
        if (t.empty()) { int n = std::min(left, max); t = s; t.count = (u8)n; left -= n; }
    }
    return left;
}

int Inventory::count(u16 item) const { int n = 0; for (auto& s : slots) if (s.item == item) n += s.count; return n; }

bool Inventory::remove(u16 item, int n) {
    if (count(item) < n) return false;
    for (auto& s : slots) {
        if (s.item != item || n == 0) continue;
        int k = std::min<int>(n, s.count); s.count = (u8)(s.count - k); n -= k;
        if (s.count == 0) s = {};
    }
    return true;
}

void clickSlot(ItemStack& slot, ItemStack& held, bool right, const ItemRegistry& reg) {
    if (held.empty()) {
        if (slot.empty()) return;
        if (right) { int take = (slot.count + 1) / 2; held = slot; held.count = (u8)take; slot.count = (u8)(slot.count - take); if (!slot.count) slot = {}; }
        else { held = slot; slot = {}; }
        return;
    }
    if (slot.empty()) {
        if (right) { slot = held; slot.count = 1; held.count--; if (!held.count) held = {}; }
        else { slot = held; held = {}; }
        return;
    }
    if (canMerge(slot, held, reg)) {
        int max = reg.maxStack(slot.item), room = max - slot.count;
        int n = right ? std::min(1, room) : std::min<int>(room, held.count);
        slot.count = (u8)(slot.count + n); held.count = (u8)(held.count - n);
        if (!held.count) held = {};
        return;
    }
    std::swap(slot, held);
}

// ------------------------------------------------------------------ recipes
bool RecipeBook::load(const Json& root, const ItemRegistry& items, std::string* err) {
    shaped_.clear(); kiln_.clear(); loot_.clear();
    std::unordered_map<std::string, std::vector<u16>> tags;
    for (auto& [t, arr] : root["tags"].obj) for (auto& n : arr.arr) {
        u16 i = items.id(n.asString()); if (!i) { if (err) *err = "tag " + t + ": unknown item " + n.asString(); return false; }
        tags[t].push_back(i);
    }
    auto resolve = [&](const std::string& key, std::vector<u16>& out) -> bool {
        if (!key.empty() && key[0] == '#') { auto it = tags.find(key.substr(1)); if (it == tags.end()) { if (err) *err = "unknown tag " + key; return false; } out = it->second; return true; }
        u16 i = items.id(key); if (!i) { if (err) *err = "unknown item " + key; return false; } out = {i}; return true;
    };
    auto addShaped = [&](const Json& pattern, const std::function<bool(char, std::vector<u16>&)>& keyOf, const std::string& result, int count) -> bool {
        Shaped s; s.h = (int)pattern.size(); s.w = 0;
        for (auto& row : pattern.arr) s.w = std::max(s.w, (int)row.str.size());
        s.cells.resize((size_t)s.w * s.h);
        for (int y = 0; y < s.h; y++) for (int x = 0; x < s.w; x++) {
            char c = x < (int)pattern[y].str.size() ? pattern[y].str[x] : ' ';
            if (c != ' ' && !keyOf(c, s.cells[y * s.w + x])) return false;
        }
        u16 r = items.id(result); if (!r) { if (err) *err = "unknown result " + result; return false; }
        s.result = {r, (u8)count, 0};
        shaped_.push_back(std::move(s));
        return true;
    };
    for (auto& r : root["shaped"].arr) {
        const Json& key = r["key"];
        if (!addShaped(r["pattern"], [&](char c, std::vector<u16>& out) { const Json* k = key.find(std::string(1, c)); if (!k) { if (err) *err = std::string("missing key ") + c; return false; } return resolve(k->asString(), out); },
                       r["result"].asString(), r["count"].asInt(1))) return false;
    }
    const Json& tools = root["tools"];
    for (auto& [mat, src] : tools["materials"].obj) for (auto& [kind, pat] : tools["patterns"].obj) {
        std::string m = src.asString();
        if (!addShaped(pat, [&](char c, std::vector<u16>& out) { return c == 'M' ? resolve(m, out) : resolve("stick", out); }, mat + "_" + kind, 1)) return false;
    }
    for (auto& r : root["kiln"].arr) {
        std::vector<u16> in, out;
        if (!resolve(r["input"].asString(), in) || !resolve(r["result"].asString(), out)) return false;
        for (u16 i : in) kiln_[i] = out[0];
    }
    structureLoot.clear();
    for (auto& e : root["structure_loot"].arr) {
        u16 i = items.id(e["item"].asString()); if (!i) { if (err) *err = "structure_loot: unknown item " + e["item"].asString(); return false; }
        structureLoot.push_back({i, e["min"].asInt(1), e["max"].asInt(1), (float)e["chance"].asNumber(1)});
    }
    for (auto& [block, arr] : root["loot"].obj) for (auto& e : arr.arr) {
        u16 i = items.id(e["item"].asString()); if (!i) { if (err) *err = "loot: unknown item " + e["item"].asString(); return false; }
        loot_[block].push_back({i, (float)e["chance"].asNumber(1), e["extra"].asInt(0)});
    }
    return true;
}

bool RecipeBook::loadFile(const std::string& path, const ItemRegistry& items, std::string* err) {
    auto txt = readTextFile(pathFromUtf8(path));
    if (!txt) { if (err) *err = "cannot read " + path; return false; }
    std::string perr; Json j = Json::parse(*txt, &perr);
    if (!perr.empty()) { if (err) *err = path + ": " + perr; return false; }
    return load(j, items, err);
}

std::optional<ItemStack> RecipeBook::match(const ItemStack* grid, int gw, int gh) const {
    int x0 = gw, y0 = gh, x1 = -1, y1 = -1;
    for (int y = 0; y < gh; y++) for (int x = 0; x < gw; x++) if (!grid[y * gw + x].empty()) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
    if (x1 < 0) return std::nullopt;
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    for (const Shaped& r : shaped_) {
        if (r.w != w || r.h != h) continue;
        for (int mirror = 0; mirror < 2; mirror++) {
            bool ok = true;
            for (int y = 0; y < h && ok; y++) for (int x = 0; x < w && ok; x++) {
                const ItemStack& g = grid[(y0 + y) * gw + x0 + x];
                const auto& cell = r.cells[y * w + (mirror ? w - 1 - x : x)];
                if (cell.empty()) ok = g.empty();
                else ok = !g.empty() && std::find(cell.begin(), cell.end(), g.item) != cell.end();
            }
            if (ok) return r.result;
        }
    }
    return std::nullopt;
}

void RecipeBook::rollStructureLoot(u64 seed, const BlockPos& p, std::vector<ItemStack>& slots) const {
    Rng rng(hashCoords(seed ^ 0x100750, p.x, p.y, p.z));
    for (auto& l : structureLoot) {
        if (!rng.chance(l.chance)) continue;
        int n = l.min + rng.range(l.max - l.min + 1);
        if (n <= 0) continue;
        for (int tries = 0; tries < 8; tries++) {   // random free slot
            size_t k = (size_t)rng.range((int)slots.size());
            if (slots[k].empty()) { slots[k] = {l.item, (u8)n, 0}; break; }
        }
    }
}

void RecipeBook::consume(ItemStack* grid, int n) {
    for (int i = 0; i < n; i++) if (!grid[i].empty()) { grid[i].count--; if (!grid[i].count) grid[i] = {}; }
}

} // namespace vx
