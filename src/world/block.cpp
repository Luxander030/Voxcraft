#include "world/block.h"
#include "core/util.h"

namespace vx {

static BlockShape parseShape(const std::string& s) {
    if (s == "none") return BlockShape::None; if (s == "cross") return BlockShape::Cross; if (s == "fluid") return BlockShape::Fluid;
    if (s == "torch") return BlockShape::Torch; if (s == "flat_wall") return BlockShape::FlatWall; if (s == "wire") return BlockShape::Wire;
    if (s == "lever") return BlockShape::Lever; if (s == "button") return BlockShape::Button; if (s == "plate") return BlockShape::Plate;
    if (s == "slab_thin") return BlockShape::SlabThin; if (s == "piston_head") return BlockShape::PistonHead; if (s == "model") return BlockShape::Model;
    if (s == "slab") return BlockShape::Slab; if (s == "stairs") return BlockShape::Stairs; if (s == "fence") return BlockShape::Fence;
    if (s == "pane") return BlockShape::Pane; if (s == "door") return BlockShape::Door; if (s == "trapdoor") return BlockShape::Trapdoor;
    return BlockShape::Cube;
}
static ToolType parseTool(const std::string& s) {
    if (s == "pickaxe") return ToolType::Pickaxe; if (s == "axe") return ToolType::Axe; if (s == "shovel") return ToolType::Shovel;
    if (s == "hoe") return ToolType::Hoe; if (s == "sword") return ToolType::Sword; return ToolType::None;
}
static u32 parseFlag(const std::string& s) {
    static const std::pair<const char*, u32> t[] = {
        {"replaceable", BF_REPLACEABLE}, {"gravity", BF_GRAVITY}, {"fluid", BF_FLUID}, {"leaves", BF_LEAVES}, {"flammable", BF_FLAMMABLE},
        {"needs_soil", BF_NEEDS_SOIL}, {"block_entity", BF_BLOCK_ENTITY}, {"climbable", BF_CLIMBABLE}, {"attached", BF_ATTACHED},
        {"attached_floor", BF_ATTACHED_FLOOR}, {"circuit", BF_CIRCUIT}, {"spreads", BF_SPREADS}, {"slippery", BF_SLIPPERY}, {"hot", BF_HOT},
        {"prickly", BF_PRICKLY}, {"interact_craft", BF_INTERACT_CRAFT}, {"interact_chest", BF_INTERACT_CHEST}, {"interact_kiln", BF_INTERACT_KILN}, {"interact_container", BF_INTERACT_CONTAINER}};
    for (auto& p : t) if (s == p.first) return p.second;
    return 0;
}

bool BlockRegistry::loadFile(const std::string& path, std::string* err) {
    auto txt = readTextFile(path);
    if (!txt) { if (err) *err = "cannot read " + path; return false; }
    std::string perr; Json j = Json::parse(*txt, &perr);
    if (!perr.empty()) { if (err) *err = path + ": " + perr; return false; }
    return load(j, err);
}

bool BlockRegistry::load(const Json& root, std::string* err) {
    defs_.clear(); stateBlock_.clear(); byName_.clear();
    if (!root.isObject()) { if (err) *err = "blocks root must be object"; return false; }
    if (root.obj.empty() || root.obj[0].first != "air") { if (err) *err = "first block must be 'air'"; return false; }
    for (auto& [name, j] : root.obj) {
        BlockDef d; d.name = name; d.index = (u16)defs_.size(); d.base = (BlockState)stateBlock_.size();
        d.shape = parseShape(j["shape"].asString("cube"));
        std::string layer = j["layer"].asString("opaque");
        d.layer = layer == "cutout" ? RenderLayer::Cutout : layer == "translucent" ? RenderLayer::Translucent : RenderLayer::Opaque;
        std::string tint = j["tint"].asString("");
        d.tint = tint == "grass" ? Tint::Grass : tint == "foliage" ? Tint::Foliage : tint == "water" ? Tint::Water : Tint::None;
        d.solid = j["solid"].asBool(true);
        d.opaque = j["opaque"].asBool(d.shape == BlockShape::Cube && d.layer == RenderLayer::Opaque);
        d.light = (u8)j["light"].asInt(0);
        d.opacity = (u8)j["opacity"].asInt(d.opaque ? 15 : 0);
        d.hardness = (float)j["hardness"].asNumber(1.0);
        d.resistance = (float)j["resistance"].asNumber(d.hardness);
        d.tool = parseTool(j["tool"].asString("none"));
        d.toolLevel = (u8)j["level"].asInt(0);
        const Json* drop = j.find("drop");
        d.drop = drop ? drop->asString("") : name;
        if (d.drop == "self") d.drop = name;
        d.sound = j["sound"].asString("stone");
        if (const Json* fl = j.find("flags")) for (auto& f : fl->arr) {
            u32 bit = parseFlag(f.asString());
            if (!bit) { if (err) *err = name + ": unknown flag " + f.asString(); return false; }
            d.flags |= bit;
        }
        const Json& tex = j["tex"];
        if (tex.isString()) for (auto& t : d.tex) t = tex.str;
        else if (tex.isObject()) {
            std::string side = tex["side"].asString(tex["top"].asString(""));
            std::string top = tex["top"].asString(side), bottom = tex["bottom"].asString(top);
            d.tex[FACE_NX] = d.tex[FACE_PX] = d.tex[FACE_NZ] = d.tex[FACE_PZ] = side;
            d.tex[FACE_PY] = top; d.tex[FACE_NY] = bottom;
            d.texFront = tex["front"].asString("");
        }
        u16 stride = 1;
        if (const Json* props = j.find("props")) for (auto& [pn, pv] : props->obj) {
            int c = pv.asInt(1); if (c < 1 || c > 255) { if (err) *err = name + ": bad prop count"; return false; }
            d.props.push_back({pn, (u8)c, stride}); stride = (u16)(stride * c);
        }
        d.stateCount = stride;
        if (stateBlock_.size() + d.stateCount > 65535) { if (err) *err = "too many block states"; return false; }
        for (int i = 0; i < d.stateCount; i++) stateBlock_.push_back(d.index);
        byName_[name] = d.index;
        defs_.push_back(std::move(d));
    }
    size_t n = stateBlock_.size();
    opaque_.assign(n, 0); solid_.assign(n, 0); light_.assign(n, 0); opacity_.assign(n, 0); fluid_.assign(n, 0);
    for (size_t s = 0; s < n; s++) {
        const BlockDef& d = defs_[stateBlock_[s]];
        opaque_[s] = d.opaque; solid_[s] = d.solid; light_[s] = d.light; opacity_[s] = d.opacity; fluid_[s] = d.has(BF_FLUID);
    }
    // state-dependent overrides
    auto setLight = [&](const char* blk, const char* prop, int val, u8 light) {
        const BlockDef* d = byName(blk); if (!d) return; int pi = d->propIndex(prop);
        for (int i = 0; i < d->stateCount; i++) { BlockState s = (BlockState)(d->base + i); light_[s] = getProp(s, pi) == val ? light : 0; }
    };
    setLight("lamp", "lit", 1, 15);
    setLight("spark_torch", "lit", 1, 7);
    setLight("kiln", "lit", 1, 13);
    if (const BlockDef* ph = byName("piston"); ph) {   // extended piston no longer a full occluder
        int pi = ph->propIndex("extended");
        for (int i = 0; i < ph->stateCount; i++) { BlockState s = (BlockState)(ph->base + i); if (getProp(s, pi)) { opacity_[s] = 0; } }
    }
    buildShapes();
    return true;
}

namespace {
ShapeBox sb(int x0, int y0, int z0, int x1, int y1, int z1) { return ShapeBox{{(u8)x0, (u8)y0, (u8)z0}, {(u8)x1, (u8)y1, (u8)z1}}; }
// A full-height panel `t` pixels thick against side f.
ShapeBox panel(int f, int t, int y0 = 0, int y1 = 16) {
    switch (f & 3) {
    case 0: return sb(0, y0, 0, 16, y1, t);
    case 1: return sb(0, y0, 16 - t, 16, y1, 16);
    case 2: return sb(0, y0, 0, t, y1, 16);
    default: return sb(16 - t, y0, 0, 16, y1, 16);
    }
}
// An arm from the centre strip [lo,hi) toward side f (from the edge to `reach` pixels in).
ShapeBox arm(int f, int lo, int hi, int reach, int y0, int y1) {
    switch (f & 3) {
    case 0: return sb(lo, y0, 0, hi, y1, reach);
    case 1: return sb(lo, y0, 16 - reach, hi, y1, 16);
    case 2: return sb(0, y0, lo, reach, y1, hi);
    default: return sb(16 - reach, y0, lo, 16, y1, hi);
    }
}
} // namespace

void BlockRegistry::buildShapes() {
    size_t n = stateBlock_.size();
    boxed_.assign(n, 0); renderRange_.assign(n, 0); collRange_.assign(n, 0); boxes_.clear();
    std::vector<ShapeBox> r, c;
    for (const BlockDef& d : defs_) {
        if (!isBoxedShape(d.shape)) continue;
        static const char* SIDES[4] = {"north", "south", "west", "east"};
        int sideProp[4]; for (int k = 0; k < 4; k++) sideProp[k] = d.propIndex(SIDES[k]);
        for (int i = 0; i < d.stateCount; i++) {
            BlockState s = (BlockState)(d.base + i);
            auto prop = [&](const char* p) { return getProp(s, p); };
            r.clear(); c.clear();
            switch (d.shape) {
            case BlockShape::Slab: {
                int t = prop("type");   // 0 bottom, 1 top, 2 double
                r.push_back(t == 0 ? sb(0, 0, 0, 16, 8, 16) : t == 1 ? sb(0, 8, 0, 16, 16, 16) : sb(0, 0, 0, 16, 16, 16));
                c = r;
                if (t == 2) { opaque_[s] = d.layer == RenderLayer::Opaque; opacity_[s] = 15; }
                break;
            }
            case BlockShape::Stairs: {   // "facing" = side of the tall back; "half" 1 = upside down
                int f = prop("facing"), top = prop("half");
                r.push_back(top ? sb(0, 8, 0, 16, 16, 16) : sb(0, 0, 0, 16, 8, 16));
                ShapeBox b = panel(f, 8, top ? 0 : 8, top ? 8 : 16);
                r.push_back(b);
                c = r;
                break;
            }
            case BlockShape::Fence: {
                r.push_back(sb(6, 0, 6, 10, 16, 10)); c.push_back(sb(6, 0, 6, 10, 24, 10));
                for (int k = 0; k < 4; k++) if (sideProp[k] >= 0 && getProp(s, sideProp[k])) {
                    r.push_back(arm(k, 7, 9, 6, 12, 15)); r.push_back(arm(k, 7, 9, 6, 6, 9));
                    c.push_back(arm(k, 6, 10, 6, 0, 24));
                }
                break;
            }
            case BlockShape::Pane: {
                r.push_back(sb(7, 0, 7, 9, 16, 9));
                for (int k = 0; k < 4; k++) if (sideProp[k] >= 0 && getProp(s, sideProp[k])) r.push_back(arm(k, 7, 9, 7, 0, 16));
                c = r;
                break;
            }
            case BlockShape::Door: {   // panel on side "facing" when closed; opening swings it about the hinge
                int f = prop("facing"), open = prop("open"), hinge = prop("hinge");
                int side = !open ? f : hinge == 0 ? dirCW(f) : dirCCW(f);
                r.push_back(panel(side, 3)); c = r;
                break;
            }
            case BlockShape::Trapdoor: {
                int f = prop("facing"), open = prop("open"), top = prop("top");
                r.push_back(open ? panel(f, 3) : top ? sb(0, 13, 0, 16, 16, 16) : sb(0, 0, 0, 16, 3, 16)); c = r;
                break;
            }
            default: break;
            }
            boxed_[s] = 1;
            renderRange_[s] = (u32)(boxes_.size() << 4) | (u32)r.size(); boxes_.insert(boxes_.end(), r.begin(), r.end());
            collRange_[s] = (u32)(boxes_.size() << 4) | (u32)c.size(); boxes_.insert(boxes_.end(), c.begin(), c.end());
        }
    }
}

const BlockDef* BlockRegistry::byName(const std::string& name) const {
    auto it = byName_.find(name); return it == byName_.end() ? nullptr : &defs_[it->second];
}

std::string BlockRegistry::stateString(BlockState s) const {
    const BlockDef& d = def(s);
    if (d.props.empty()) return d.name;
    std::string r = d.name + "[";
    for (size_t i = 0; i < d.props.size(); i++) { if (i) r += ","; r += d.props[i].name + "=" + std::to_string(getProp(s, (int)i)); }
    return r + "]";
}

BlockState BlockRegistry::parseState(const std::string& str) const {
    size_t br = str.find('[');
    const BlockDef* d = byName(str.substr(0, br));
    if (!d) return AIR;
    BlockState s = d->base;
    if (br == std::string::npos) return s;
    std::string body = str.substr(br + 1, str.size() - br - 2);
    size_t pos = 0;
    while (pos < body.size()) {
        size_t comma = body.find(',', pos); if (comma == std::string::npos) comma = body.size();
        std::string kv = body.substr(pos, comma - pos); size_t eq = kv.find('=');
        if (eq != std::string::npos) {
            int pi = d->propIndex(kv.substr(0, eq).c_str());
            int v = atoi(kv.substr(eq + 1).c_str());
            if (pi >= 0 && v >= 0 && v < d->props[pi].count) s = withProp(s, pi, v);
        }
        pos = comma + 1;
    }
    return s;
}

void CoreBlocks::resolve(const BlockRegistry& r) {
    auto g = [&](const char* n) { BlockState s = r.stateOf(n); if (s == AIR && std::string(n) != "air") VX_WARN("core block missing: %s", n); return s; };
    air = AIR; stone = g("stone"); cobblestone = g("cobblestone"); slate = g("slate"); bedrock = g("bedrock"); dirt = g("dirt");
    turf = g("turf"); podzol = g("podzol"); sand = g("sand"); red_sand = g("red_sand"); gravel = g("gravel"); sandstone = g("sandstone");
    clay = g("clay"); snow = g("snow_block"); ice = g("ice"); water = g("water"); lava = g("lava");
    oak_log = g("oak_log"); birch_log = g("birch_log"); pine_log = g("pine_log");
    oak_leaves = g("oak_leaves"); birch_leaves = g("birch_leaves"); pine_leaves = g("pine_leaves"); cactus = g("cactus");
    tall_grass = g("tall_grass"); fern = g("fern"); dead_bush = g("dead_bush"); sunpetal = g("sunpetal"); bluebell = g("bluebell");
    emberbloom = g("emberbloom"); coal_ore = g("coal_ore"); copper_ore = g("copper_ore"); iron_ore = g("iron_ore");
    gold_ore = g("gold_ore"); glimmer_ore = g("glimmer_ore"); azurite_ore = g("azurite_ore"); glowcrystal = g("glowcrystal");
    chest = g("chest"); torch = g("torch"); wire = g("glimmer_wire"); spark_torch = g("spark_torch"); lever = g("lever");
    button = g("push_button"); plate = g("pressure_plate"); lamp = g("lamp"); relay = g("relay"); watcher = g("watcher");
    piston = g("piston"); piston_head = g("piston_head"); workbench = g("workbench"); kiln = g("kiln"); glass = g("glass");
    oak_planks = g("oak_planks");
}

} // namespace vx
