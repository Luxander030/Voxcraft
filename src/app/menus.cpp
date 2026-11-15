// Menus: title, world selection/creation/deletion, settings, in-game pause menu, chat + commands.
// Immediate-mode widgets drawn with the UI batcher; all layout in UI units.
#include "app/game.h"
#include <ctime>

namespace vx {

namespace {
u32 C(u8 r, u8 g, u8 b, u8 a = 255) { return rgba(r, g, b, a); }
float MX() { return G->mouseX / G->ui.scale; }
float MY() { return G->mouseY / G->ui.scale; }
bool hover(float x, float y, float w, float h) { return MX() >= x && MX() < x + w && MY() >= y && MY() < y + h; }

bool button(float x, float y, float w, const std::string& label, bool enabled = true) {
    Ui& ui = G->ui; const float h = 20;
    bool hv = enabled && hover(x, y, w, h);
    ui.rect(x, y, w, h, enabled ? (hv ? C(110, 120, 150) : C(80, 82, 92)) : C(50, 50, 55));
    ui.frame(x, y, w, h, 1, hv ? C(240, 240, 255) : C(20, 20, 24));
    ui.rect(x + 1, y + 1, w - 2, 1, C(255, 255, 255, 50));
    ui.text(x + w / 2 - Ui::textWidth(label) / 2, y + 6, label, enabled ? C(240, 240, 240) : C(140, 140, 140));
    if (hv && G->clicked) { G->clicked = false; sfx("ui_click", 0.6f); return true; }
    return false;
}

// Horizontal slider; returns true while dragging. value in [lo, hi].
bool slider(float x, float y, float w, const std::string& label, float& v, float lo, float hi, float step = 0) {
    Ui& ui = G->ui; const float h = 20;
    bool hv = hover(x, y, w, h);
    ui.rect(x, y, w, h, C(40, 40, 46)); ui.frame(x, y, w, h, 1, C(20, 20, 24));
    float t = (v - lo) / (hi - lo);
    ui.rect(x + 2 + t * (w - 12), y + 1, 8, h - 2, hv ? C(170, 180, 210) : C(130, 132, 142));
    ui.text(x + w / 2 - Ui::textWidth(label) / 2, y + 6, label, C(240, 240, 240));
    if (hv && G->mouseHeld) {
        float nt = clamp((MX() - x - 6) / (w - 12), 0.0f, 1.0f);
        v = lo + nt * (hi - lo);
        if (step > 0) v = std::round(v / step) * step;
        return true;
    }
    return false;
}

void textField(float x, float y, float w, std::string& s, int id, const char* hint) {
    Ui& ui = G->ui; const float h = 20;
    if (hover(x, y, w, h) && G->clicked) { G->focusField = id; G->clicked = false; }
    bool f = G->focusField == id;
    ui.rect(x, y, w, h, C(0, 0, 0)); ui.frame(x, y, w, h, 1, f ? C(255, 255, 255) : C(120, 120, 120));
    if (s.empty() && !f) ui.text(x + 4, y + 6, hint, C(110, 110, 110));
    ui.text(x + 4, y + 6, s, C(230, 230, 230));
    if (f && ((int)(G->now * 2) & 1)) ui.rect(x + 4 + Ui::textWidth(s), y + 5, 1, 9, C(230, 230, 230));
}

void bigText(float cx, float y, const std::string& s, float k, u32 c) {   // scaled title text
    Ui& ui = G->ui; float old = ui.scale; ui.scale = old * k;
    ui.text(cx / k - Ui::textWidth(s) / 2, y / k, s, c);
    ui.scale = old;
}

void background(float W, float H) {
    // tiled darkened blocks drifting slowly: a cheap, original menu backdrop
    int layer = G->pack.layer("slate_cobble"), l2 = G->pack.layer("dirt");
    float off = std::fmod((float)G->now * 4.0f, 32.0f);
    for (float y = -32; y < H + 32; y += 32) for (float x = -32; x < W + 32; x += 32) {
        int k = ((int)((x + 64) / 32) * 7 + (int)((y + 64) / 32) * 13) % 5;
        G->ui.sprite(x + off, y, 32, 32, k == 0 ? l2 : layer, C(70, 70, 80));
    }
    G->ui.rect(0, 0, W, H, C(0, 0, 0, 90));
}

std::string folderFor(const std::string& name) {
    std::string f;
    for (char c : name) f += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    if (f.empty()) f = "World";
    fs::path base = userDataDir("Voxcraft") / "saves";
    std::string cand = f; int n = 2;
    while (fs::exists(base / cand)) cand = f + "-" + std::to_string(n++);
    return cand;
}

std::string keyName(int k) {
    if (k >= SAPP_KEYCODE_A && k <= SAPP_KEYCODE_Z) return std::string(1, (char)('A' + (k - SAPP_KEYCODE_A)));
    if (k >= SAPP_KEYCODE_0 && k <= SAPP_KEYCODE_9) return std::string(1, (char)('0' + (k - SAPP_KEYCODE_0)));
    switch (k) {
        case SAPP_KEYCODE_SPACE: return "Space"; case SAPP_KEYCODE_LEFT_SHIFT: return "L-Shift"; case SAPP_KEYCODE_RIGHT_SHIFT: return "R-Shift";
        case SAPP_KEYCODE_LEFT_CONTROL: return "L-Ctrl"; case SAPP_KEYCODE_RIGHT_CONTROL: return "R-Ctrl"; case SAPP_KEYCODE_LEFT_ALT: return "L-Alt";
        case SAPP_KEYCODE_TAB: return "Tab"; case SAPP_KEYCODE_UP: return "Up"; case SAPP_KEYCODE_DOWN: return "Down";
        case SAPP_KEYCODE_LEFT: return "Left"; case SAPP_KEYCODE_RIGHT: return "Right"; case SAPP_KEYCODE_ENTER: return "Enter";
        default: return "Key " + std::to_string(k);
    }
}

void controlsPanel(float W, float H, bool inGame) {
    UserOptions& o = G->options;
    float x = W / 2 - 155, y = std::max(24.0f, H / 2 - 120);
    G->ui.text(W / 2 - Ui::textWidth("Controls and accessibility") / 2, y - 14, "Controls and accessibility", C(255, 255, 255));
    for (int i = 0; i < UserOptions::ActionCount; i++) {
        float bx = x + (i % 2) * 160, by = y + (i / 2) * 22;
        std::string label = std::string(ACTION_NAMES[i]) + ": " + (G->rebinding == i ? "> press a key <" : keyName(o.keys[i]));
        if (button(bx, by, 150, label)) G->rebinding = i;
    }
    float ty = y + ((UserOptions::ActionCount + 1) / 2) * 22 + 6;
    if (button(x, ty, 150, o.invertMouse ? "Invert mouse: On" : "Invert mouse: Off")) o.invertMouse = !o.invertMouse;
    if (button(x + 160, ty, 150, o.subtitles ? "Subtitles: On" : "Subtitles: Off")) o.subtitles = !o.subtitles;
    if (button(x, ty + 22, 150, o.toggleSneak ? "Sneak: Toggle" : "Sneak: Hold")) o.toggleSneak = !o.toggleSneak;
    if (button(x + 160, ty + 22, 150, o.toggleSprint ? "Sprint: Toggle" : "Sprint: Hold")) o.toggleSprint = !o.toggleSprint;
    if (button(x + 80, ty + 50, 150, "Done")) {
        G->rebinding = -1; o.save();
        if (inGame) G->screen = Screen::Settings; else G->app = AppState::Settings;
    }
}

void settingsPanel(float W, float H, bool inGame) {
    UserOptions& o = G->options;
    float x = W / 2 - 155, y = std::max(30.0f, H / 2 - 110);
    float rd = (float)o.renderDistance;
    if (slider(x, y, 150, "Render distance: " + std::to_string(o.renderDistance), rd, 2, 24, 1)) o.renderDistance = (int)rd;
    slider(x + 160, y, 150, "FOV: " + std::to_string((int)o.fov), o.fov, 50, 110, 1);
    char b[64]; snprintf(b, sizeof b, "Mouse sensitivity: %d%%", (int)(o.sensitivity * 100));
    slider(x, y + 24, 150, b, o.sensitivity, 0.2f, 3.0f, 0.05f);
    float gs = (float)o.guiScale;
    if (slider(x + 160, y + 24, 150, o.guiScale ? "GUI scale: " + std::to_string(o.guiScale) : std::string("GUI scale: Auto"), gs, 0, 4, 1)) o.guiScale = (int)gs;
    for (int i = 0; i < 6; i++) {
        snprintf(b, sizeof b, "%s: %d%%", VOLUME_NAMES[i], (int)(o.volume[i] * 100));
        slider(x + (i % 2) * 160, y + 56 + (i / 2) * 24, 150, b, o.volume[i], 0, 1, 0.05f);
    }
    if (button(x, y + 128, 150, o.distantTerrain ? "Distant terrain: On" : "Distant terrain: Off")) o.distantTerrain = !o.distantTerrain;
    if (button(x + 160, y + 128, 150, "Controls...")) { if (inGame) G->screen = Screen::Controls; else G->app = AppState::Controls; }
    if (button(x + 80, y + 152, 150, "Done")) {
        o.save(); applyOptions();
        if (inGame) G->screen = Screen::Pause; else G->app = G->settingsReturn;
    }
    (void)H;
}
} // namespace

// ------------------------------------------------------------------ options
bool UserOptions::load() {
    auto t = readTextFile(userDataDir("Voxcraft") / "options.json");
    if (!t) return false;
    std::string e; Json j = Json::parse(*t, &e);
    if (!e.empty()) { VX_WARN("options.json: %s (using defaults)", e.c_str()); return false; }
    renderDistance = clamp(j["render_distance"].asInt(renderDistance), 2, 24);
    guiScale = clamp(j["gui_scale"].asInt(guiScale), 0, 4);
    fov = clamp((float)j["fov"].asNumber(fov), 50.0f, 110.0f);
    sensitivity = clamp((float)j["sensitivity"].asNumber(sensitivity), 0.2f, 3.0f);
    for (int i = 0; i < 6; i++) volume[i] = clamp((float)j["volume"][i].asNumber(volume[i]), 0.0f, 1.0f);
    distantTerrain = j["distant_terrain"].asBool(true);
    invertMouse = j["invert_mouse"].asBool(false); toggleSneak = j["toggle_sneak"].asBool(false);
    toggleSprint = j["toggle_sprint"].asBool(false); subtitles = j["subtitles"].asBool(false);
    if (const Json* k = j.find("keys")) for (int i = 0; i < ActionCount && i < (int)k->size(); i++) keys[i] = clamp((*k)[i].asInt(keys[i]), 1, 511);
    return true;
}
void UserOptions::save() const {
    Json j = Json::makeObject();
    j.set("render_distance", Json::of((double)renderDistance)); j.set("gui_scale", Json::of((double)guiScale));
    j.set("fov", Json::of((double)fov)); j.set("sensitivity", Json::of((double)sensitivity));
    Json v = Json::makeArray(); for (float f : volume) v.push(Json::of((double)f)); j.set("volume", v);
    j.set("distant_terrain", Json::of(distantTerrain));
    j.set("invert_mouse", Json::of(invertMouse)); j.set("toggle_sneak", Json::of(toggleSneak));
    j.set("toggle_sprint", Json::of(toggleSprint)); j.set("subtitles", Json::of(subtitles));
    Json k = Json::makeArray(); for (int key : keys) k.push(Json::of((double)key)); j.set("keys", k);
    std::string s = j.dump(2);
    if (!writeFileAtomic(userDataDir("Voxcraft") / "options.json", s.data(), s.size())) VX_WARN("could not save options");
}
void applyOptions() {
    G->opt.radius = G->options.renderDistance;
    G->budget.effectiveRadius = std::max(G->budget.effectiveRadius, G->opt.radius);   // a new setting is honoured until pressure says otherwise
    if (G->mixer) for (int i = 0; i < (int)SoundCat::Count; i++) G->mixer->setVolume((SoundCat)i, G->options.volume[i]);
}

// ------------------------------------------------------------------ worlds
void refreshWorldList() {
    G->worldList.clear();
    std::error_code ec;
    for (auto& e : fs::directory_iterator(userDataDir("Voxcraft") / "saves", ec)) {
        if (!e.is_directory(ec) || e.path().filename().string()[0] == '.') continue;
        WorldStorage st; WorldMeta m; std::string err;
        auto txt = readTextFile(e.path() / "world.json");
        if (!txt) continue;
        Json j = Json::parse(*txt, &err);
        if (!WorldMeta::fromJson(j, m, &err)) continue;
        G->worldList.push_back({e.path().filename().string(), m.name, m.gameMode, m.gen.seed, m.lastPlayedUnix});
    }
    std::sort(G->worldList.begin(), G->worldList.end(), [](const WorldEntry& a, const WorldEntry& b) { return a.lastPlayed > b.lastPlayed; });
    if (G->worldSel >= (int)G->worldList.size()) G->worldSel = -1;
}

static void enterWorld(const std::string& folder, const NewWorld* nw) {
    std::string err;
    if (!startSession(folder, nw, &err)) { chatPrint("Could not open world: " + err); VX_ERROR("%s", err.c_str()); G->app = AppState::Worlds; return; }
    G->app = AppState::Playing; G->screen = Screen::None;
    G->mouseLocked = true; sapp_lock_mouse(true);
}

void drawMenus(int w, int h) {
    float gs = G->options.guiScale ? (float)G->options.guiScale : std::max(1.0f, std::floor(std::min(w / 320.0f, h / 240.0f)));
    Ui& ui = G->ui; ui.begin((float)w, (float)h, gs);
    float W = w / gs, H = h / gs;
    background(W, H);
    switch (G->app) {
    case AppState::Title: {
        bigText(W / 2, H * 0.22f, "VOXCRAFT", 4, C(235, 230, 210));
        ui.text(W / 2 - Ui::textWidth("an original block sandbox") / 2, H * 0.22f + 34, "an original block sandbox", C(180, 200, 160));
        float bx = W / 2 - 100, by = H * 0.5f;
        if (button(bx, by, 200, "Singleplayer")) { refreshWorldList(); G->app = AppState::Worlds; }
        if (button(bx, by + 24, 200, "Settings")) { G->settingsReturn = AppState::Title; G->app = AppState::Settings; }
        if (button(bx, by + 48, 200, "Quit")) sapp_request_quit();
        ui.text(2, H - 10, "Voxcraft 0.1.0", C(160, 160, 160));
        break;
    }
    case AppState::Worlds: {
        ui.text(W / 2 - Ui::textWidth("Select world") / 2, 12, "Select world", C(255, 255, 255));
        float lx = W / 2 - 150, ly = 28, rowH = 26; int rows = std::max(1, (int)((H - 100) / rowH));
        G->worldScroll = clamp(G->worldScroll, 0, std::max(0, (int)G->worldList.size() - rows));
        for (int i = 0; i < rows && i + G->worldScroll < (int)G->worldList.size(); i++) {
            int k = i + G->worldScroll; const WorldEntry& e = G->worldList[k];
            float y = ly + i * rowH;
            bool sel = k == G->worldSel;
            if (hover(lx, y, 300, rowH - 2) && G->clicked) {
                G->clicked = false;
                if (sel) { enterWorld(e.folder, nullptr); return; }   // double-click style: second click plays
                G->worldSel = k;
            }
            ui.rect(lx, y, 300, rowH - 2, sel ? C(60, 60, 80, 220) : C(0, 0, 0, 120));
            if (sel) ui.frame(lx, y, 300, rowH - 2, 1, C(220, 220, 220));
            char when[32] = "never";
            if (e.lastPlayed) { time_t t = (time_t)e.lastPlayed; struct tm tmv;
#if defined(_WIN32)
                localtime_s(&tmv, &t);
#else
                localtime_r(&t, &tmv);
#endif
                strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tmv); }
            ui.text(lx + 4, y + 3, e.name, C(255, 255, 255));
            ui.text(lx + 4, y + 13, e.folder + "  " + e.mode + "  " + when, C(150, 150, 150));
        }
        if (G->worldList.empty()) ui.text(W / 2 - Ui::textWidth("No worlds yet") / 2, ly + 20, "No worlds yet", C(180, 180, 180));
        float by = H - 52;
        bool has = G->worldSel >= 0;
        if (button(W / 2 - 154, by, 150, "Play selected", has)) { enterWorld(G->worldList[G->worldSel].folder, nullptr); return; }
        if (button(W / 2 + 4, by, 150, "Create new world")) { G->fieldName = "New World"; G->fieldSeed.clear(); G->focusField = 1; G->app = AppState::CreateWorld; }
        if (button(W / 2 - 154, by + 24, 150, "Delete", has)) G->app = AppState::ConfirmDelete;
        if (button(W / 2 + 4, by + 24, 150, "Back")) G->app = AppState::Title;
        break;
    }
    case AppState::ConfirmDelete: {
        const WorldEntry& e = G->worldList[G->worldSel];
        std::string q = "Delete '" + e.name + "'?";
        ui.text(W / 2 - Ui::textWidth(q) / 2, H / 2 - 40, q, C(255, 255, 255));
        const char* n = "It is moved to saves/.trash and can be restored from there.";
        ui.text(W / 2 - Ui::textWidth(n) / 2, H / 2 - 28, n, C(200, 200, 200));
        if (button(W / 2 - 154, H / 2, 150, "Delete")) {
            fs::path base = userDataDir("Voxcraft") / "saves";
            std::error_code ec; fs::create_directories(base / ".trash", ec);
            std::string dst = e.folder + "-" + std::to_string((long long)time(nullptr));
            fs::rename(base / e.folder, base / ".trash" / dst, ec);
            if (ec) chatPrint("Could not move world to trash: " + ec.message());
            G->worldSel = -1; refreshWorldList(); G->app = AppState::Worlds;
        }
        if (button(W / 2 + 4, H / 2, 150, "Cancel")) G->app = AppState::Worlds;
        break;
    }
    case AppState::CreateWorld: {
        float x = W / 2 - 100, y = H / 2 - 70;
        ui.text(W / 2 - Ui::textWidth("Create new world") / 2, y - 24, "Create new world", C(255, 255, 255));
        ui.text(x, y - 10, "World name", C(200, 200, 200)); textField(x, y, 200, G->fieldName, 1, "");
        ui.text(x, y + 26, "Seed (blank = random)", C(200, 200, 200)); textField(x, y + 36, 200, G->fieldSeed, 2, "random");
        if (button(x, y + 64, 98, G->newCreative ? "Mode: Creative" : "Mode: Survival")) G->newCreative = !G->newCreative;
        if (button(x + 102, y + 64, 98, G->newFlat ? "Type: Flat" : "Type: Default")) G->newFlat = !G->newFlat;
        if (button(x, y + 96, 98, "Create")) {
            NewWorld nw; nw.displayName = G->fieldName.empty() ? "New World" : G->fieldName;
            nw.creative = G->newCreative; nw.flat = G->newFlat; nw.mustBeNew = true;
            if (!G->fieldSeed.empty()) {
                nw.seedSet = true;
                char* end = nullptr; unsigned long long v = std::strtoull(G->fieldSeed.c_str(), &end, 10);
                nw.seed = (end && *end == 0) ? (u64)v : fnv1a(G->fieldSeed);   // text seeds hash to a number
            }
            enterWorld(folderFor(nw.displayName), &nw); return;
        }
        if (button(x + 102, y + 96, 98, "Cancel")) G->app = AppState::Worlds;
        break;
    }
    case AppState::Settings:
        ui.text(W / 2 - Ui::textWidth("Settings") / 2, 12, "Settings", C(255, 255, 255));
        settingsPanel(W, H, false);
        break;
    case AppState::Controls:
        controlsPanel(W, H, false);
        break;
    default: break;
    }
    G->clicked = false;
}

void drawPauseOrSettingsOverlay() {
    Ui& ui = G->ui; float W = ui.width / ui.scale, H = ui.height / ui.scale;
    ui.rect(0, 0, W, H, C(0, 0, 0, 120));
    if (G->screen == Screen::Settings) { settingsPanel(W, H, true); G->clicked = false; return; }
    if (G->screen == Screen::Controls) { controlsPanel(W, H, true); G->clicked = false; return; }
    ui.text(W / 2 - Ui::textWidth("Game paused") / 2, H / 2 - 60, "Game paused", C(255, 255, 255));
    float x = W / 2 - 100;
    if (button(x, H / 2 - 40, 200, "Back to game")) closeScreen();
    if (button(x, H / 2 - 16, 200, "Settings")) G->screen = Screen::Settings;
    if (button(x, H / 2 + 8, 200, "Save and quit to title")) {
        G->quitToTitle = true;
    }
    G->clicked = false;
}

// ------------------------------------------------------------------ text input
void menuChar(u32 c) {
    if (c < 32 || c > 126) return;
    std::string* s = nullptr;
    if (G->app == AppState::Playing && G->screen == Screen::Chat) s = &G->chatLine;
    else if (G->app == AppState::CreateWorld) s = G->focusField == 1 ? &G->fieldName : G->focusField == 2 ? &G->fieldSeed : nullptr;
    if (s && s->size() < 64) *s += (char)c;
}

void menuKey(int key) {
    if (G->rebinding >= 0) {   // assign the pressed key (Esc cancels)
        if (key != SAPP_KEYCODE_ESCAPE) G->options.keys[G->rebinding] = key;
        G->rebinding = -1;
        return;
    }
    std::string* s = nullptr;
    if (G->app == AppState::Playing && G->screen == Screen::Chat) s = &G->chatLine;
    else if (G->app == AppState::CreateWorld) s = G->focusField == 1 ? &G->fieldName : G->focusField == 2 ? &G->fieldSeed : nullptr;
    if (key == SAPP_KEYCODE_BACKSPACE && s && !s->empty()) s->pop_back();
    if (key == SAPP_KEYCODE_TAB && G->app == AppState::CreateWorld) G->focusField = G->focusField == 1 ? 2 : 1;
    if (key == SAPP_KEYCODE_ENTER && G->app == AppState::Playing && G->screen == Screen::Chat) {
        std::string line = G->chatLine; G->chatLine.clear(); closeScreen();
        if (!line.empty()) chatSubmit(line);
    }
}

// ------------------------------------------------------------------ chat & commands
void chatPrint(const std::string& line) {
    G->chatLog.push_back({line, G->now});
    if (G->chatLog.size() > 50) G->chatLog.erase(G->chatLog.begin());
    VX_INFO("[chat] %s", line.c_str());
}

void chatSubmit(const std::string& line) {
    if (line[0] != '/') { chatPrint("<you> " + line); return; }
    std::vector<std::string> a; { std::string cur; for (char c : line.substr(1)) { if (c == ' ') { if (!cur.empty()) a.push_back(cur); cur.clear(); } else cur += c; } if (!cur.empty()) a.push_back(cur); }
    if (a.empty()) return;
    const std::string& c = a[0];
    World& w = *G->world;
    if (c == "help") chatPrint("commands: /gamemode survival|creative, /time set day|night|<ticks>, /tp x y z, /give item [n], /seed, /spawnpoint, /kill, /summon mob, /weather clear|rain, /locate village");
    else if (c == "gamemode" && a.size() > 1 && (a[1] == "survival" || a[1] == "creative")) {
        w.meta.gameMode = a[1]; G->player.canFly = G->creative(); if (!G->creative()) G->player.flying = false; chatPrint("Game mode set to " + a[1]);
    } else if (c == "time" && a.size() > 2 && a[1] == "set") {
        i64 t = a[2] == "day" ? 1000 : a[2] == "noon" ? 6000 : a[2] == "night" ? 13000 : a[2] == "midnight" ? 18000 : atoll(a[2].c_str());
        w.meta.dayTime = ((t % 24000) + 24000) % 24000; chatPrint("Time set to " + std::to_string(w.meta.dayTime));
    } else if (c == "tp" && a.size() > 3) {
        G->player.pos = {(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str())}; G->player.prevPos = G->player.pos; G->player.vel = {};
        G->playerReady = false; chatPrint("Teleported");
    } else if (c == "give" && a.size() > 1) {
        u16 it = G->items.id(a[1]);
        if (!it) { chatPrint("Unknown item " + a[1]); return; }
        int n = a.size() > 2 ? clamp(atoi(a[2].c_str()), 1, 64 * 36) : 1;
        while (n > 0) { int k = std::min(n, G->items.maxStack(it)); giveOrDropInv({it, (u8)k, 0}); n -= k; }
        chatPrint("Gave " + a[1]);
    } else if (c == "seed") chatPrint("Seed: " + std::to_string(w.meta.gen.seed));
    else if (c == "locate" && a.size() > 1 && a[1] == "village") {
        int vx, vy, vz;
        if (G->dimension == "overworld" && w.generator().nearestVillage((int)G->player.pos.x, (int)G->player.pos.z, 8, vx, vy, vz))
            chatPrint("Nearest village: " + std::to_string(vx) + " " + std::to_string(vy) + " " + std::to_string(vz));
        else chatPrint("No village nearby (villages exist in worlds created with generator v3)");
    }
    else if (c == "weather" && a.size() > 1 && (a[1] == "clear" || a[1] == "rain")) {
        w.meta.raining = a[1] == "rain"; w.meta.weatherTicks = 12000; chatPrint("Weather: " + a[1]);
    }
    else if (c == "spawnpoint") { w.meta.spawn = {(int)std::floor(G->player.pos.x), (int)std::floor(G->player.pos.y), (int)std::floor(G->player.pos.z)}; chatPrint("Spawn point set"); }
    else if (c == "kill") { G->stats.damage(1000, DamageSource::Void); }
    else if (c == "summon" && a.size() > 1) {
        u16 t = G->mobReg.id(a[1]);
        if (t == 0xFFFF) { chatPrint("Unknown creature " + a[1]); return; }
        G->mobs->spawn(t, G->player.pos + G->player.look() * 3.0f + Vec3(0, 0.5f, 0), G->rng); chatPrint("Summoned " + a[1]);
    } else chatPrint("Unknown command. Try /help");
}

} // namespace vx
