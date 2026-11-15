// Game session state shared by the app modules (gameplay.cpp, screens.cpp, main.cpp).
#pragma once
#include "app/renderer.h"
#include "app/ui.h"
#include "game/behaviors.h"
#include "game/entities.h"
#include "game/items.h"
#include "game/kiln.h"
#include "game/player.h"
#include "game/survival.h"
#include "world/world.h"

namespace vx {

enum class Screen { None, Pause, Inventory, Workbench, Chest, Kiln, Dead };

struct Options {
    std::string world = "World1";
    u64 seed = 0; bool seedSet = false, flat = false, creative = false;
    int radius = 10;
    int smokeFrames = 0;
    std::string screenshot;
};

// Slot shown on a container screen. `ptr` points into the inventory, a grid or a block entity.
struct UiSlot { float x, y; ItemStack* ptr; enum Kind : u8 { Normal, CraftResult, TakeOnly } kind = Normal; int group = 0; };

struct Game {
    Options opt;
    fs::path assetDir, saveDir;
    BlockRegistry reg;
    TexturePack pack;
    ItemRegistry items;
    RecipeBook recipes;
    std::unique_ptr<JobSystem> jobs;
    std::unique_ptr<EntityManager> entities;   // declared before world: must outlive it
    std::unique_ptr<World> world;
    std::unique_ptr<BlockBehaviors> behaviors;   // fluids, falling blocks, growth (after world)
    Renderer renderer;
    Ui ui;
    Rng rng{0xC0FFEE};

    // player
    Player player;
    PlayerStats stats;
    Inventory inv{36};               // 0..8 hotbar, 9..35 main
    int hotbarSel = 0;
    bool creative() const { return world && world->meta.gameMode == "creative"; }
    const ItemDef* held() const { const ItemStack& s = inv.slots[hotbarSel]; return s.empty() ? nullptr : &items.def(s.item); }

    // input
    PlayerInput input;
    bool keys[512] = {};
    bool mouseDown[3] = {};
    float mouseX = 0, mouseY = 0;
    bool mouseLocked = false, debug = false, quitting = false;

    // interaction state
    RayHit target;
    BlockPos breaking{0, -1, 0};
    float breakProgress = 0;     // 0..1
    int useCooldown = 0, eatTicks = 0;

    // screens
    Screen screen = Screen::Pause;
    ItemStack cursor;                 // stack held by the mouse on container screens
    ItemStack craft[9];               // 2x2 (inventory) or 3x3 (workbench) crafting grid
    ItemStack craftResult;
    BlockPos openBlock{0, -1, 0};     // chest/kiln being viewed
    std::vector<UiSlot> slots;        // rebuilt each frame by the active screen
    int creativeScroll = 0;

    // timing / stats
    double accumulator = 0, lastSave = 0, now = 0;
    u64 frame = 0;
    float frameMsAvg = 16, frameMsMax = 0, fps = 60;
    bool playerReady = false;
    std::string status; double statusUntil = 0;
};

extern Game* G;

// gameplay.cpp
bool startSession(std::string* err);
void endSession();
bool saveWorld(bool sync);
void simulateTick();             // one 20 Hz step: player, survival, entities, kilns, world
void updateInteraction(float dt); // continuous block breaking / item use (per frame)
void clickUse();                 // right click: interact with block, place, or start eating
void pickBlock();
void dropSelected(bool wholeStack);
void showStatus(const std::string& s);
void giveOrDropInv(ItemStack s);
void respawn();

// screens.cpp
void drawFrameUi(int w, int h);
void openScreen(Screen s);
void closeScreen();
void screenMouseDown(bool right, bool shift);
void screenScroll(float dy);
bool screenCapturesMouse();
void drawItemIcon(Ui& ui, float x, float y, const ItemStack& s, float size = 16);

} // namespace vx
