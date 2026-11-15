// HUD and container screens (inventory, workbench, chest, kiln, creative palette, pause, death).
// All layout is in UI units (scaled by the integer GUI scale); slot pitch is 18 units.
#include "app/game.h"
#include "core/profiler.h"

namespace vx {

namespace {
constexpr float PW = 176, PH = 166;   // container panel size

// 7x7 HUD icons (original), MSB = left pixel; drawn with a 1px dark outline at 8-unit pitch
const u16 HEART[7]  = {0x6C00, 0xFE00, 0xFE00, 0x7C00, 0x3800, 0x1000, 0x0000};
const u16 FOOD[7]   = {0x1000, 0x6C00, 0xFE00, 0xFE00, 0xFE00, 0x7C00, 0x2800};
const u16 SHIELD[7] = {0xFE00, 0xFE00, 0xFE00, 0x7C00, 0x7C00, 0x3800, 0x1000};
const u16 BUBBLE[7] = {0x3800, 0x4400, 0x9200, 0xA200, 0x8200, 0x4400, 0x3800};
const u16 HALF_MASK = 0xF000;   // left 4 columns

u32 C(u8 r, u8 g, u8 b, u8 a = 255) { return rgba(r, g, b, a); }

float panelX() { return std::floor(G->ui.width / G->ui.scale / 2 - PW / 2); }
float panelY() { return std::floor(G->ui.height / G->ui.scale / 2 - PH / 2); }

void iconRow(float x, float y, const u16* bm, float value, u32 full, u32 empty, bool rightToLeft) {
    for (int i = 0; i < 10; i++) {
        float ix = rightToLeft ? x + (9 - i) * 8 : x + i * 8;
        float v = value - i * 2;
        if ((empty >> 24) != 0) {   // outline + empty fill (skipped for "hide when empty" rows like air)
            for (int d = 0; d < 4; d++) G->ui.bitmap(ix + (d == 0) - (d == 1), y + (d == 2) - (d == 3), bm, 7, 7, rgba(16, 12, 12, 230));
            G->ui.bitmap(ix, y, bm, 7, 7, empty);
        } else if (v < 1) continue;
        if (v >= 2) G->ui.bitmap(ix, y, bm, 7, 7, full);
        else if (v >= 1) {
            u16 half[7]; for (int r = 0; r < 7; r++) half[r] = (u16)(bm[r] & (rightToLeft ? (u16)(~HALF_MASK) : HALF_MASK));
            G->ui.bitmap(ix, y, half, 7, 7, full);
        }
    }
}

std::vector<u16>& creativeList() {
    static std::vector<u16> list;
    if (list.empty()) for (u16 i = 1; i < G->items.size(); i++) list.push_back(i);
    return list;
}

void slotBox(float x, float y) {
    G->ui.rect(x - 1, y - 1, 18, 18, C(55, 55, 62));
    G->ui.rect(x, y, 16, 16, C(30, 30, 36));
}

void addSlot(float x, float y, ItemStack* p, UiSlot::Kind k = UiSlot::Normal, int group = 0) { G->slots.push_back({x, y, p, k, group}); }

void addPlayerInventory(float px, float py) {
    for (int r = 0; r < 3; r++) for (int c = 0; c < 9; c++) addSlot(px + 8 + c * 18, py + 84 + r * 18, &G->inv.slots[9 + r * 9 + c], UiSlot::Normal, 0);
    for (int c = 0; c < 9; c++) addSlot(px + 8 + c * 18, py + 142, &G->inv.slots[c], UiSlot::Normal, 0);
}

void updateCraft() {
    int n = G->screen == Screen::Workbench ? 3 : 2;
    ItemStack grid[9];
    for (int i = 0; i < n * n; i++) grid[i] = G->craft[i];
    auto r = G->recipes.match(grid, n, n);
    G->craftResult = r ? *r : ItemStack{};
}

void giveOrDrop(ItemStack s) {
    if (s.empty()) return;
    int left = G->inv.add(s, G->items);
    if (left) { s.count = (u8)left; G->entities->spawnItem(G->player.eye(), s, G->rng, G->player.look() * 0.3f); }
}

void buildSlots() {
    G->slots.clear();
    float px = panelX(), py = panelY();
    switch (G->screen) {
    case Screen::Inventory:
        if (G->creative()) {
            auto& list = creativeList();
            int rows = 3, start = G->creativeScroll * 9;
            static ItemStack palette[27];
            for (int i = 0; i < rows * 9; i++) {
                int k = start + i;
                palette[i] = k < (int)list.size() ? ItemStack{list[k], (u8)G->items.maxStack(list[k]), 0} : ItemStack{};
                addSlot(px + 8 + (i % 9) * 18, py + 18 + (i / 9) * 18, &palette[i], UiSlot::TakeOnly, 9);
            }
        } else {
            for (int i = 0; i < 4; i++) addSlot(px + 8, py + 8 + i * 18, &G->armor[i], UiSlot::Normal, 4 + i);   // armor: groups 4..7
            for (int i = 0; i < 4; i++) addSlot(px + 98 + (i % 2) * 18, py + 18 + (i / 2) * 18, &G->craft[i], UiSlot::Normal, 1);
            addSlot(px + 154, py + 28, &G->craftResult, UiSlot::CraftResult, 2);
        }
        addPlayerInventory(px, py);
        break;
    case Screen::Workbench:
        for (int i = 0; i < 9; i++) addSlot(px + 30 + (i % 3) * 18, py + 17 + (i / 3) * 18, &G->craft[i], UiSlot::Normal, 1);
        addSlot(px + 124, py + 35, &G->craftResult, UiSlot::CraftResult, 2);
        addPlayerInventory(px, py);
        break;
    case Screen::Chest:   // chests, hoppers, droppers, dispensers, storage carts
        if (G->openCart) {
            if (Cart* c = G->carts->byId(G->openCart))
                for (int i = 0; i < (int)c->items.size(); i++) addSlot(px + 8 + (i % 9) * 18, py + 18 + (i / 9) * 18, &c->items[i], UiSlot::Normal, 3);
        } else if (BlockEntity* be = G->world->blockEntity(G->openBlock)) {
            int n = std::max(1, containerSlots(G->reg, G->world->getBlock(G->openBlock)));
            be->items.resize(n);
            int cols = n == 9 ? 3 : 9;
            float x0 = n == 9 ? px + 62 : n == HOPPER_SLOTS ? px + 44 : px + 8;
            for (int i = 0; i < n; i++) addSlot(x0 + (i % cols) * 18, py + 18 + (i / cols) * 18, &be->items[i], UiSlot::Normal, 3);
        }
        addPlayerInventory(px, py);
        break;
    case Screen::Kiln:
        if (BlockEntity* be = G->world->blockEntity(G->openBlock)) {
            be->items.resize(KILN_SLOTS);
            addSlot(px + 56, py + 17, &be->items[0], UiSlot::Normal, 3);
            addSlot(px + 56, py + 53, &be->items[1], UiSlot::Normal, 3);
            addSlot(px + 116, py + 35, &be->items[2], UiSlot::TakeOnly, 3);
        }
        addPlayerInventory(px, py);
        break;
    default: break;
    }
}

void markContainerDirty() {
    if ((G->screen == Screen::Chest || G->screen == Screen::Kiln) && !G->openCart) {   // (carts save with their chunk's entities)
        if (ChunkPtr c = G->world->chunk(G->openBlock.chunk())) c->unsaved = true;
        if (G->behaviors) G->behaviors->containerChanged(G->openBlock);   // comparators reading this container
    }
}

const UiSlot* slotAt(float mx, float my) {
    for (auto& s : G->slots) if (mx >= s.x - 1 && mx < s.x + 17 && my >= s.y - 1 && my < s.y + 17) return &s;
    return nullptr;
}

void quickMove(const UiSlot& s) {
    ItemStack& st = *s.ptr;
    if (st.empty()) return;
    bool fromInv = s.group == 0;
    bool container = G->screen == Screen::Chest || G->screen == Screen::Kiln;
    if (fromInv && container) {
        for (auto& t : G->slots) {   // into container slots (merge, then empty); kiln: input slot unless it's fuel
            if (t.group != 3 || t.kind == UiSlot::TakeOnly || st.empty()) continue;
            if (G->screen == Screen::Kiln) {
                bool isFuel = G->items.def(st.item).fuel > 0 && !G->recipes.smelt(st.item);
                if ((t.ptr == &G->world->blockEntity(G->openBlock)->items[1]) != isFuel) continue;
            }
            if (t.ptr->empty()) { *t.ptr = st; st = {}; }
            else if (canMerge(*t.ptr, st, G->items)) {
                int n = std::min<int>(st.count, G->items.maxStack(st.item) - t.ptr->count);
                t.ptr->count = (u8)(t.ptr->count + n); st.count = (u8)(st.count - n); if (!st.count) st = {};
            }
        }
    } else if (fromInv) {
        int idx = (int)(s.ptr - G->inv.slots.data());
        int left = idx < 9 ? G->inv.add(st, G->items, 9, 35) : G->inv.add(st, G->items, 0, 8);
        if (left == st.count) return;
        st.count = (u8)left; if (!left) st = {};
    } else {
        int left = G->inv.add(st, G->items);
        st.count = (u8)left; if (!left) st = {};
    }
    markContainerDirty();
}

void drawPanel(const char* title) {
    float px = panelX(), py = panelY();
    Ui& ui = G->ui;
    ui.rect(0, 0, ui.width / ui.scale, ui.height / ui.scale, C(0, 0, 0, 120));
    ui.rect(px, py, PW, PH, C(150, 146, 140));
    ui.frame(px, py, PW, PH, 1, C(60, 58, 56));
    ui.rect(px + 1, py + 1, PW - 2, 1, C(225, 222, 216));
    ui.text(px + 8, py + 6, title, C(50, 50, 55), false);
    ui.text(px + 8, py + 73, "Inventory", C(50, 50, 55), false);
}

void drawSlotsAndCursor() {
    Ui& ui = G->ui;
    for (auto& s : G->slots) {
        slotBox(s.x, s.y);
        if (!s.ptr->empty()) drawItemIcon(ui, s.x, s.y, *s.ptr);
    }
    float mx = G->mouseX / ui.scale, my = G->mouseY / ui.scale;
    if (const UiSlot* h = slotAt(mx, my)) {
        ui.rect(h->x, h->y, 16, 16, C(255, 255, 255, 60));
        if (!h->ptr->empty() && G->cursor.empty()) {
            std::string n = G->items.def(h->ptr->item).name;
            ui.rect(mx + 8, my - 12, Ui::textWidth(n) + 4, 11, C(20, 10, 30, 230));
            ui.text(mx + 10, my - 10, n, C(255, 255, 255));
        }
    }
    if (!G->cursor.empty()) drawItemIcon(ui, mx - 8, my - 8, G->cursor);
}

void drawDebug() {
    Ui& ui = G->ui;
    char buf[256]; float y = 2;
    auto line = [&](const char* fmt, auto... args) { snprintf(buf, sizeof buf, fmt, args...); ui.rect(1, y - 1, Ui::textWidth(buf) + 2, 9, C(0, 0, 0, 120)); ui.text(2, y, buf, C(230, 230, 230)); y += 9; };
    const Player& p = G->player;
    auto ws = G->world->stats(); auto js = G->jobs->stats(); auto rs = G->renderer.stats();
#if defined(SOKOL_METAL)
    const char* backend = "Metal";
#else
    const char* backend = "D3D11";
#endif
    line("Voxcraft 0.1.0 (%s)  %.0f fps  %.2f ms avg  %.1f ms max", backend, G->fps, G->frameMsAvg, G->frameMsMax);
    const char* facing; { float d = std::fmod(p.yaw * 180.0f / PI + 360.0f * 4, 360.0f); facing = d < 45 || d >= 315 ? "north (-Z)" : d < 135 ? "east (+X)" : d < 225 ? "south (+Z)" : "west (-X)"; }
    line("XYZ %.2f / %.2f / %.2f   chunk %d %d   facing %s", p.pos.x, p.pos.y, p.pos.z, (int)std::floor(p.pos.x) >> 4, (int)std::floor(p.pos.z) >> 4, facing);
    BlockPos bp{(int)std::floor(p.pos.x), (int)std::floor(p.pos.y), (int)std::floor(p.pos.z)};
    ColumnInfo col = G->world->generator().column(bp.x, bp.z);
    line("biome %s   light sky %d block %d   %s %s%s", biomeName(col.biome), G->world->skyLight(bp), G->world->blockLight(bp), G->world->meta.gameMode.c_str(), p.flying ? "flying " : "", p.onGround ? "ground" : "");
    i64 dt = G->world->meta.dayTime; int hh = (int)((dt / 1000 + 6) % 24), mm = (int)(dt % 1000 * 60 / 1000);
    line("dimension %s   day %lld  %02d:%02d   seed %llu", G->dimension.c_str(), (long long)(G->world->meta.tick / 24000), hh, mm, (unsigned long long)G->world->meta.gen.seed);
    line("chunks ready %d  gen %d  cached %d  modified %d  pending writes %d", ws.ready, ws.generating + ws.requested, ws.cached, ws.withData, ws.pendingWrites);
    line("jobs queued %d  running %d/%d  mesh in flight %d  dirty %d  items %zu  mobs %zu", js.queued, js.running, js.workers, rs.meshJobsInFlight, rs.dirtyChunks, G->entities->count(), G->mobs->mobs.size());
    line("render %d/%d sections  %d draws  %llu tris  mesh VRAM %.1f MiB", rs.visibleSections, rs.sections, rs.drawCalls, (unsigned long long)(rs.quads * 2), rs.gpuBytes / 1048576.0);
    auto avg = [](Stat t, Stat n) { u64 c = Profiler::get(n); return c ? Profiler::get(t) / 1000.0 / (double)c : 0.0; };
    line("avg ms: gen %.2f  light %.2f  mesh/section %.3f   save %.1f total", avg(Stat::GenTimeUs, Stat::ChunksGenerated), avg(Stat::LightTimeUs, Stat::ChunksLit),
         avg(Stat::MeshTimeUs, Stat::ChunksMeshed), Profiler::get(Stat::SaveTimeUs) / 1000.0);
    line("memory rss %.0f MiB  chunks %.1f MiB  cache %.1f/%.0f MiB  radius %d/%d", processMemoryBytes() / 1048576.0, ws.chunkBytes / 1048576.0, ws.cacheBytes / 1048576.0,
         G->budget.chunkCacheBytes / 1048576.0, std::min(G->opt.radius, G->budget.effectiveRadius), G->opt.radius);
    if (G->target.hit) {
        BlockState s = G->world->getBlock(G->target.block);
        line("target %s at %d %d %d", G->reg.stateString(s).c_str(), G->target.block.x, G->target.block.y, G->target.block.z);
    }
}
} // namespace

void drawItemIcon(Ui& ui, float x, float y, const ItemStack& s, float size) {
    if (s.empty()) return;
    const ItemDef& d = G->items.def(s.item);
    if (d.texture >= 0) ui.sprite(x + (16 - size) / 2, y + (16 - size) / 2, size, size, d.texture);
    else ui.blockIcon(x + 8, y + 8, size * 0.85f, d.places, G->reg, G->renderer.textures());
    if (d.durability > 0 && s.damage > 0) {
        float f = 1.0f - (float)s.damage / d.durability;
        ui.rect(x + 2, y + 13, 13, 2, C(0, 0, 0));
        ui.rect(x + 2, y + 13, 13 * f, 1, C((u8)(255 * (1 - f)), (u8)(255 * f), 0));
    }
    if (s.count > 1) { std::string n = std::to_string(s.count); ui.text(x + 17 - Ui::textWidth(n), y + 9, n, C(255, 255, 255)); }
}

bool screenCapturesMouse() { return G->screen != Screen::None; }

void openScreen(Screen s) {
    G->screen = s;
    G->mouseLocked = false; sapp_lock_mouse(false);
    for (bool& k : G->keys) k = false;
    G->mouseDown[0] = G->mouseDown[1] = G->mouseDown[2] = false;
    if (s == Screen::Inventory || s == Screen::Workbench) updateCraft();
}

void closeScreen() {
    if (G->screen == Screen::None) return;
    for (auto& c : G->craft) { giveOrDrop(c); c = {}; }
    G->craftResult = {};
    giveOrDrop(G->cursor); G->cursor = {};
    markContainerDirty();
    G->screen = Screen::None;
    G->openCart = 0;
    G->slots.clear();
    G->mouseLocked = true; sapp_lock_mouse(true);
}

void screenScroll(float dy) {
    if (G->screen == Screen::Inventory && G->creative()) {
        int maxRow = std::max(0, ((int)creativeList().size() + 8) / 9 - 3);
        G->creativeScroll = clamp(G->creativeScroll + (dy < 0 ? 1 : -1), 0, maxRow);
    }
}

void screenMouseDown(bool right, bool shift) {
    if (G->screen == Screen::Pause || G->screen == Screen::Settings || G->screen == Screen::Chat) return;   // handled by menus
    if (G->screen == Screen::Trade) { G->tradeClick = !right; return; }   // offers are resolved while drawing
    if (G->screen == Screen::Dead) { respawn(); closeScreen(); G->screen = Screen::None; return; }
    buildSlots();
    float mx = G->mouseX / G->ui.scale, my = G->mouseY / G->ui.scale;
    const UiSlot* s = slotAt(mx, my);
    if (!s) {
        bool outside = mx < panelX() || my < panelY() || mx > panelX() + PW || my > panelY() + PH;
        if (outside && !G->cursor.empty()) {   // throw the held stack
            ItemStack d = G->cursor; if (right) { d.count = 1; G->cursor.count--; if (!G->cursor.count) G->cursor = {}; } else G->cursor = {};
            G->entities->spawnItem(G->player.eye(), d, G->rng, G->player.look() * 0.3f);
            G->entities->items.back().pickupDelay = 40;
        }
        return;
    }
    ItemStack& slot = *s->ptr;
    if (s->group == 9) {   // creative palette
        if (!G->cursor.empty()) G->cursor = {};
        else if (!slot.empty()) { G->cursor = slot; if (right) G->cursor.count = 1; if (shift) G->inv.add(slot, G->items); if (shift) G->cursor = {}; }
        return;
    }
    if (s->kind == UiSlot::CraftResult) {
        if (slot.empty()) return;
        int n = G->screen == Screen::Workbench ? 9 : 4;
        if (shift) {
            for (int guard = 0; guard < 64 && !G->craftResult.empty(); guard++) {
                ItemStack r = G->craftResult;
                if (G->inv.add(r, G->items) > 0) break;
                RecipeBook::consume(G->craft, n); updateCraft();
            }
        } else if (G->cursor.empty() || (canMerge(G->cursor, slot, G->items) && G->cursor.count + slot.count <= G->items.maxStack(slot.item))) {
            if (G->cursor.empty()) G->cursor = slot; else G->cursor.count = (u8)(G->cursor.count + slot.count);
            RecipeBook::consume(G->craft, n);
        }
        updateCraft();
        return;
    }
    if (s->group >= 4 && s->group <= 7) {   // armor slots only accept matching armor
        if (!G->cursor.empty() && G->items.def(G->cursor.item).armorSlot != s->group - 4) return;
        if (shift && !slot.empty()) { giveOrDropInv(slot); slot = {}; return; }
        clickSlot(slot, G->cursor, false, G->items);
        return;
    }
    if (shift && !slot.empty() && s->group == 0) {   // shift-click armor from the inventory: equip
        int as = G->items.def(slot.item).armorSlot;
        if (as >= 0 && G->armor[as].empty() && G->screen == Screen::Inventory && !G->creative()) { G->armor[as] = slot; slot = {}; return; }
    }
    if (shift) { quickMove(*s); }
    else if (s->kind == UiSlot::TakeOnly) {
        if (slot.empty()) return;
        if (G->cursor.empty()) { G->cursor = slot; slot = {}; }
        else if (canMerge(G->cursor, slot, G->items)) {
            int n = std::min<int>(slot.count, G->items.maxStack(slot.item) - G->cursor.count);
            G->cursor.count = (u8)(G->cursor.count + n); slot.count = (u8)(slot.count - n); if (!slot.count) slot = {};
        }
    } else clickSlot(slot, G->cursor, right, G->items);
    if (s->group == 1) updateCraft();
    markContainerDirty();
}

void drawFrameUi(int w, int h) {
    float gs = G->options.guiScale ? (float)G->options.guiScale : std::max(1.0f, std::floor(std::min(w / 320.0f, h / 240.0f)));
    Ui& ui = G->ui; ui.begin((float)w, (float)h, gs);
    float W = w / gs, H = h / gs;
    bool creative = G->creative();
    // chat log (fades after 10 s unless chat is open) and input line
    {
        bool open = G->screen == Screen::Chat;
        float y = H - 40;
        int shown = 0;
        for (auto it = G->chatLog.rbegin(); it != G->chatLog.rend() && shown < 10; ++it) {
            if (!open && G->now - it->second > 10) break;
            y -= 9; shown++;
            ui.rect(2, y - 1, Ui::textWidth(it->first) + 4, 9, C(0, 0, 0, 110));
            ui.text(4, y, it->first, C(240, 240, 240));
        }
        if (open) {
            ui.rect(2, H - 14, W - 4, 11, C(0, 0, 0, 160));
            ui.text(4, H - 12, G->chatLine + (((int)(G->now * 2) & 1) ? "_" : ""), C(255, 255, 255));
        }
    }

    if (G->screen == Screen::None || G->screen == Screen::Pause) {
        ui.rect(W / 2 - 4.5f, H / 2 - 0.5f, 9, 1, C(240, 240, 240, 200));
        ui.rect(W / 2 - 0.5f, H / 2 - 4.5f, 1, 9, C(240, 240, 240, 200));
    }
    // hotbar
    float bw = 18 * 9 + 2, bx = std::floor(W / 2 - bw / 2), by = H - 22;
    ui.rect(bx, by, bw, 20, C(20, 20, 24, 170));
    for (int i = 0; i < 9; i++) {
        float x = bx + 1 + i * 18;
        ui.frame(x, by + 1, 18, 18, 1, C(80, 80, 90, 220));
        drawItemIcon(ui, x + 1, by + 2, G->inv.slots[i]);
    }
    ui.frame(bx + G->hotbarSel * 18, by, 20, 20, 1.5f, C(250, 250, 250));
    if (!creative && armorPoints() > 0) iconRow(bx + 1, by - 19, SHIELD, (float)armorPoints(), C(200, 205, 215), C(60, 60, 70, 255), false);
    if (!creative) {
        iconRow(bx + 1, by - 10, HEART, G->stats.health, C(220, 30, 40), C(70, 30, 30, 255), false);
        iconRow(bx + bw - 80, by - 10, FOOD, G->stats.food, C(205, 135, 60), C(70, 50, 30, 255), true);
        if (G->stats.air < 300) iconRow(bx + bw - 80, by - 19, BUBBLE, std::ceil(G->stats.air / 15.0f), C(140, 200, 255), C(0, 0, 0, 0), true);
    }
    if (G->bowCharge > 0) {
        ui.rect(W / 2 - 10, H / 2 + 8, 20, 3, C(0, 0, 0, 160));
        ui.rect(W / 2 - 10, H / 2 + 8, 20 * std::min(1.0f, G->bowCharge / 20.0f), 3, C(255, 220, 120, 230));
    }
    if (G->breakProgress > 0 && !creative) {
        ui.rect(W / 2 - 10, H / 2 + 8, 20, 3, C(0, 0, 0, 160));
        ui.rect(W / 2 - 10, H / 2 + 8, 20 * std::min(1.0f, G->breakProgress), 3, C(240, 240, 240, 220));
    }
    if (const ItemDef* hd = G->held()) ui.text(W / 2 - Ui::textWidth(hd->name) / 2, by - (creative ? 11 : 32), hd->name, C(255, 255, 255, 230));
    if (G->now < G->statusUntil) ui.text(W / 2 - Ui::textWidth(G->status) / 2, H / 2 + 20, G->status, C(255, 240, 160));
    if (!G->playerReady) { const char* t = "Generating terrain..."; ui.text(W / 2 - Ui::textWidth(t) / 2, H / 2 - 30, t, C(255, 255, 255)); }
    if (G->debug) drawDebug();
    if (G->options.subtitles) {   // sound captions, bottom right, with direction markers
        float y = H - 48;
        for (auto it = G->captions.rbegin(); it != G->captions.rend(); ++it) {
            if (it->until < G->now) continue;
            std::string t = (it->side < 0 ? "< " : "  ") + it->text + (it->side > 0 ? " >" : "  ");
            float w = Ui::textWidth(t) + 6;
            ui.rect(W - w - 4, y - 1, w, 10, C(0, 0, 0, 170));
            ui.text(W - w - 1, y, t, C(255, 255, 255), false);
            y -= 11;
        }
    }

    switch (G->screen) {
    case Screen::Pause: case Screen::Settings: case Screen::Controls:
        drawPauseOrSettingsOverlay();
        break;
    case Screen::Dead: {
        ui.rect(0, 0, W, H, C(120, 0, 0, 110));
        const char* t1 = "You died"; const char* t2 = "Click to respawn";
        ui.text(W / 2 - Ui::textWidth(t1) / 2, H / 2 - 20, t1, C(255, 255, 255));
        ui.text(W / 2 - Ui::textWidth(t2) / 2, H / 2 - 6, t2, C(220, 200, 200));
        break;
    }
    case Screen::Trade: {
        drawPanel("Settler");
        float px = panelX(), py = panelY();
        auto offers = G->recipes.offersFor(G->tradeSeed);
        float mx = G->mouseX / ui.scale, my = G->mouseY / ui.scale;
        for (size_t i = 0; i < offers.size(); i++) {
            const auto& t = G->recipes.trades[offers[i]];
            float y = py + 18 + i * 20;
            bool can = G->creative() || G->inv.count(t.give) >= t.giveCount;
            bool hv = mx >= px + 8 && mx < px + PW - 8 && my >= y && my < y + 18;
            ui.rect(px + 8, y, PW - 16, 18, hv && can ? C(110, 120, 150) : C(120, 116, 110));
            drawItemIcon(ui, px + 10, y + 1, {t.give, (u8)t.giveCount, 0});
            ui.text(px + 34, y + 6, "->", C(40, 40, 40), false);
            drawItemIcon(ui, px + 50, y + 1, {t.get, (u8)t.getCount, 0});
            std::string label = G->items.def(t.get).name;
            ui.text(px + 72, y + 6, label, can ? C(30, 30, 30) : C(90, 80, 80), false);
            if (hv && can && G->tradeClick) {
                G->tradeClick = false;
                if (!G->creative()) G->inv.remove(t.give, t.giveCount);
                giveOrDropInv({t.get, (u8)t.getCount, 0});
                sfx("pickup", 0.7f);
            }
        }
        G->tradeClick = false;
        G->slots.clear();
        addPlayerInventory(px, py);
        for (auto& s : G->slots) { slotBox(s.x, s.y); if (!s.ptr->empty()) drawItemIcon(ui, s.x, s.y, *s.ptr); }
        break;
    }
    case Screen::Inventory: case Screen::Workbench: case Screen::Chest: case Screen::Kiln: {
        buildSlots();
        std::string cname = G->screen != Screen::Chest ? "" : G->openCart ? "Storage cart" : G->reg.def(G->world->getBlock(G->openBlock)).name;
        if (!cname.empty()) cname[0] = (char)std::toupper((unsigned char)cname[0]);
        std::string titleS = G->screen == Screen::Workbench ? "Workbench" : G->screen == Screen::Chest ? cname : G->screen == Screen::Kiln ? "Kiln" : creative ? "Creative" : "Crafting";
        const char* title = titleS.c_str();
        drawPanel(title);
        float px = panelX(), py = panelY();
        if (G->screen == Screen::Kiln) {
            if (BlockEntity* be = G->world->blockEntity(G->openBlock)) {
                float cook = (float)be->data[2] / KILN_COOK_TICKS, burn = be->data[1] ? (float)be->data[0] / be->data[1] : 0;
                ui.rect(px + 80, py + 40, 24, 6, C(90, 88, 84)); ui.rect(px + 80, py + 40, 24 * cook, 6, C(240, 240, 240));
                ui.rect(px + 57, py + 37, 14, 12, C(90, 88, 84)); ui.rect(px + 57, py + 37 + 12 * (1 - burn), 14, 12 * burn, C(240, 140, 40));
            }
        }
        if (G->screen == Screen::Inventory || G->screen == Screen::Workbench) {
            float ax = G->screen == Screen::Workbench ? px + 90 : px + 136, ay = G->screen == Screen::Workbench ? py + 40 : py + 32;
            ui.rect(ax, ay, 22, 4, C(90, 88, 84));
        }
        drawSlotsAndCursor();
        break;
    }
    default: break;
    }
}

} // namespace vx
