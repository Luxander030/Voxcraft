// Immediate-mode UI batcher: rectangles, bitmap text, isometric block icons. Built on the CPU each
// frame into one vertex array and drawn with a single draw call (src/app/renderer.cpp).
#pragma once
#include "core/common.h"
#include "render/atlas.h"
#include <string_view>

namespace vx {

struct UiVertex { float x, y; float u, v, layer, mode; u32 color; };   // mode 0 solid, 1 font, 2 block texture

// Original 5x7 font, ASCII 32..126, rows top to bottom, bit 4 = leftmost column.
extern const u8 FONT_5X7[95][7];
constexpr int FONT_CELL_W = 6, FONT_CELL_H = 8, FONT_GLYPHS = 95;
// RGBA8 atlas of all glyphs in one row: (95*6) x 8.
std::vector<u8> buildFontAtlas(int& w, int& h);

inline u32 rgba(u8 r, u8 g, u8 b, u8 a = 255) { return (u32)r | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24); }

class Ui {
public:
    std::vector<UiVertex> verts;
    float width = 0, height = 0, scale = 2;   // GUI scale: pixels per UI unit

    void begin(float w, float h, float guiScale) { verts.clear(); width = w; height = h; scale = guiScale; }
    void rect(float x, float y, float w, float h, u32 color);
    void frame(float x, float y, float w, float h, float t, u32 color);
    // Text in UI units (one glyph cell = 6x8 units * scale pixels). Returns advance width.
    float text(float x, float y, std::string_view s, u32 color, bool shadow = true);
    static float textWidth(std::string_view s) { return (float)s.size() * FONT_CELL_W; }
    // Small isometric cube using the block's top and side textures.
    void blockIcon(float cx, float cy, float size, BlockState s, const BlockRegistry& reg, const BlockTextures& tex);
private:
    void quad(const float p[4][2], const float uv[4][2], float layer, float mode, u32 color);
};

} // namespace vx
