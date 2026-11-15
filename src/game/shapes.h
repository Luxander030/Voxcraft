// Block shape bounds in block-local units (0..1), matching the mesher's geometry. Used for the
// selection outline (and later for precise raycasts / collision of non-cube blocks).
#pragma once
#include "world/block.h"

namespace vx {
inline AABB blockShapeBounds(const BlockRegistry& reg, BlockState s) {
    const BlockDef& d = reg.def(s);
    if (reg.boxed(s)) {   // building shapes: union of their boxes
        AABB u{{1, 1, 1}, {0, 0, 0}};
        for (const ShapeBox& b : reg.renderBoxes(s))
            for (int a = 0; a < 3; a++) { u.min[a] = std::min(u.min[a], b.mn[a] / 16.0f); u.max[a] = std::max(u.max[a], b.mx[a] / 16.0f); }
        return u;
    }
    auto box = [](float x0, float y0, float z0, float x1, float y1, float z1) { return AABB{{x0 / 16, y0 / 16, z0 / 16}, {x1 / 16, y1 / 16, z1 / 16}}; };
    int facing = d.propIndex("facing") >= 0 ? reg.getProp(s, "facing") : 0;
    switch (d.shape) {
    case BlockShape::Cross: return box(2, 0, 2, 14, 13, 14);
    case BlockShape::Torch:
        if (facing >= 4 || d.propIndex("facing") < 0) return box(6, 0, 6, 10, 12, 10);
        { int ox = facing == 2 ? 6 : facing == 3 ? -6 : 0, oz = facing == 0 ? 6 : facing == 1 ? -6 : 0; return box(6.0f + ox, 3, 6.0f + oz, 10.0f + ox, 15, 10.0f + oz); }
    case BlockShape::Wire: return box(0, 0, 0, 16, 1, 16);
    case BlockShape::Plate: return box(1, 0, 1, 15, 1, 15);
    case BlockShape::Button: return box(5, 0, 6, 11, 2, 10);
    case BlockShape::Lever: return box(5, 0, 5, 11, 10, 11);
    case BlockShape::SlabThin: return box(0, 0, 0, 16, 2, 16);
    case BlockShape::Rail: { int sh = d.propIndex("shape") >= 0 ? reg.getProp(s, "shape") : 0; return sh >= 2 && sh <= 5 ? box(0, 0, 0, 16, 8, 16) : box(0, 0, 0, 16, 2, 16); }
    case BlockShape::FlatWall:
        return facing == 0 ? box(0, 0, 14, 16, 16, 16) : facing == 1 ? box(0, 0, 0, 16, 16, 2) : facing == 2 ? box(14, 0, 0, 16, 16, 16) : box(0, 0, 0, 2, 16, 16);
    default: return box(0, 0, 0, 16, 16, 16);
    }
}
}
