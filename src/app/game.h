// Game session state shared by the app modules (gameplay.cpp, screens.cpp, main.cpp).
#pragma once
#include "app/renderer.h"
#include "app/ui.h"
#include "audio/audio.h"
#include "core/budget.h"
#include "game/behaviors.h"
#include "game/entities.h"
#include "game/items.h"
#include "game/containers.h"
#include "game/kiln.h"
#include "game/mobs.h"
#include "game/particles.h"
#include "game/projectiles.h"
#include "game/shapes.h"
#include "game/player.h"
#include "game/survival.h"
#include "world/world.h"

namespace vx {

enum class Screen { None, Pause, Inventory, Workbench, Chest, Kiln, Dead, Chat, Settings, Trade, Controls };
enum class AppState { Title, Worlds, CreateWorld, ConfirmDelete, Settings, Controls, Playing };

// Persistent user options (<user data>/Voxcraft/options.json).
struct UserOptions {
    int renderDistance = 10, guiScale = 0;           // 0 = automatic
    float fov = 70, sensitivity = 1.0f;
    float volume[6] = {0.8f, 0.5f, 1.0f, 1.0f, 0.7f, 0.8f};   // master, music, blocks, creatures, ambient, ui
    bool distantTerrain = true;
    // controls & accessibility
    enum Action { Forward, Back, Left, Right, Jump, Sneak, Sprint, InventoryKey, Drop, Chat, View, ActionCount };
    int keys[ActionCount] = {SAPP_KEYCODE_W, SAPP_KEYCODE_S, SAPP_KEYCODE_A, SAPP_KEYCODE_D, SAPP_KEYCODE_SPACE, SAPP_KEYCODE_LEFT_SHIFT,
                             SAPP_KEYCODE_LEFT_CONTROL, SAPP_KEYCODE_E, SAPP_KEYCODE_Q, SAPP_KEYCODE_T, SAPP_KEYCODE_V};
    bool invertMouse = false, toggleSneak = false, toggleSprint = false, subtitles = false;
    bool load();
    void save() const;
};
static const char* const VOLUME_NAMES[6] = {"Master", "Music", "Blocks", "Creatures", "Ambient", "Interface"};
static const char* const ACTION_NAMES[UserOptions::ActionCount] = {"Forward", "Back", "Left", "Right", "Jump", "Sneak", "Sprint", "Inventory", "Drop", "Chat", "Camera view"};

struct WorldEntry { std::string folder, name, mode; u64 seed = 0, lastPlayed = 0; };

struct Options {
    std::string world = "World1";
    u64 seed = 0; bool seedSet = false, flat = false, creative = false;
    int radius = 10;
    int smokeFrames = 0;
    std::string screenshot;
    bool directWorld = false;
    int view = 0;               // --view: start in this camera view
    int menuState = 0;          // --menu: which menu --title-smoke shows   // --world/--smoke: start in a world instead of the title screen
};

// Slot shown on a container screen. `ptr` points into the inventory, a grid or a block entity.
struct UiSlot { float x, y; ItemStack* ptr; enum Kind : u8 { Normal, CraftResult, TakeOnly } kind = Normal; int group = 0; };

struct Game {
    Options opt;
    UserOptions options;
    BudgetPolicy budget;
    bool farInFlight = false; int farCX = 1 << 30, farCZ = 0, farRadiusBuilt = 0; u32 farEpoch = 0;
    double nextBudgetCheck = 0;
    AppState app = AppState::Title;
    std::vector<WorldEntry> worldList; int worldSel = -1, worldScroll = 0;
    std::string fieldName = "New World", fieldSeed; int focusField = 0; bool newCreative = false, newFlat = false;
    AppState settingsReturn = AppState::Title;
    bool clicked = false, mouseHeld = false;      // menu input for this frame
    int rebinding = -1;                           // action waiting for a key press
    bool sneakLatched = false, sprintLatched = false;
    struct Caption { std::string text; double until; int side; };   // side: -1 left, 0 centre, 1 right
    std::vector<Caption> captions;
    std::string chatLine; std::vector<std::pair<std::string, double>> chatLog;
    fs::path assetDir, saveDir;                 // saveDir = main save folder (overworld)
    std::string dimension = "overworld", pendingDimension = "overworld";
    int riftTicks = 0;
    BlockRegistry reg;
    TexturePack pack;
    ItemRegistry items;
    RecipeBook recipes;
    MobRegistry mobReg;
    // third-person view: 0 first person, 1 behind the player, 2 facing the player
    int view = 0;
    int playerModel = -1;
    std::unordered_map<std::string, Clip> playerClips;
    Animator playerAnim;
    bool swingRequested = false;
    float camYaw = 0, camPitch = 0;   // the rendering camera (differs from the player's view in third person)
    std::vector<Mat4> scratchBones;
    SoundBank sounds;
    ParticleSystem particles;
    ProjectileSystem projectiles;
    int bowCharge = 0; bool prevRight = false;
    u32 tradeSeed = 0; Vec3 tradePos; bool tradeClick = false;   // the settler being traded with
    std::unique_ptr<Mixer> mixer;
    float stepDistance = 0, lastHealth = 20; double nextMusic = 45, nextAmbient = 20; bool wasInFluid = false; float digTimer = 0;
    std::unique_ptr<MobSystem> mobs;   // owned before world (persisted through entities)
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
    ItemStack armor[4];              // head, chest, legs, feet
    int hotbarSel = 0;
    bool creative() const { return world && world->meta.gameMode == "creative"; }
    const ItemDef* held() const { const ItemStack& s = inv.slots[hotbarSel]; return s.empty() ? nullptr : &items.def(s.item); }

    // input
    PlayerInput input;
    bool keys[512] = {};
    bool mouseDown[3] = {};
    float mouseX = 0, mouseY = 0;
    bool noOcclusion = false, smokeUnderground = false;
    bool mouseLocked = false, debug = false, quitting = false, initFailed = false, ignoreChar = false, quitToTitle = false;

    // interaction state
    RayHit target;
    BlockPos breaking{0, -1, 0};
    float breakProgress = 0;     // 0..1
    int useCooldown = 0, eatTicks = 0, attackCooldown = 0;
    std::vector<EntityVertex> entityVerts;

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
    std::string lastSaveError;   // empty after a successful save
};

extern Game* G;

struct NewWorld {
    std::string displayName; u64 seed = 0; bool seedSet = false, flat = false, creative = false, mustBeNew = false;
};

// gameplay.cpp
bool appInit(std::string* err);                                                         // once: registries, GPU, jobs
bool startSession(const std::string& folder, const NewWorld* create, std::string* err); // open or create a world
void resetPlayerState();
bool endSession(bool force = false);   // false: the save failed and the session stays open
void showSaveFailure();
bool saveWorld(bool sync);
void simulateTick();             // one 20 Hz step: player, survival, entities, kilns, world
void updateInteraction(float dt); // continuous block breaking / item use (per frame)
void clickUse();                 // right click: interact with block, place, or start eating
void pickBlock();
void dropSelected(bool wholeStack);
void showStatus(const std::string& s);
void giveOrDropInv(ItemStack s);
float applyArmor(float dmg);   // reduces incoming attack damage by worn armor, wears it down
int armorPoints();
void respawn();
bool enterDimension(const std::string& dim, Vec3 pos, bool arriveThroughRift, std::string* err);

bool attackMob();               // left click: melee the creature under the crosshair
MobContext mobContext();

// entity_render.cpp
void buildEntityMesh(std::vector<EntityVertex>& out, Vec3 eye, float alpha, float dt);
void buildPlayerMesh(std::vector<EntityVertex>& out, Vec3 eye, Vec3 playerPos, float alpha, float dt);   // third-person body + held item

// menus.cpp
void drawMenus(int w, int h);                     // title / worlds / create / settings (not in game)
void drawPauseOrSettingsOverlay();                // in-game pause menu and settings
void menuChar(u32 c);
void menuKey(int key);
void refreshWorldList();
void chatSubmit(const std::string& line);
void chatPrint(const std::string& line);
void applyOptions();
void sfx(const std::string& name, float gain = 1.0f);                            // UI / non-positional
void sfx3d(const std::string& name, Vec3 at, SoundCat cat, float gain = 1.0f);    // positional, random pitch

// screens.cpp
void drawFrameUi(int w, int h);
void openScreen(Screen s);
void closeScreen();
void screenMouseDown(bool right, bool shift);
void screenScroll(float dy);
bool screenCapturesMouse();
void drawItemIcon(Ui& ui, float x, float y, const ItemStack& s, float size = 16);

} // namespace vx
