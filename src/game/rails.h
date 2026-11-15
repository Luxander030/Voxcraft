// Rails: shapes, connection rules and the geometry carts follow.
//
// A rail's "shape" property is derived from neighbouring rails (refreshed by neighbour updates,
// like fence connections), so meshing and cart physics read only the block state:
//   0 north-south, 1 east-west, 2..5 ascending toward N/S/W/E (that end is one block higher),
//   6..9 curves joining S+E, S+W, N+W, N+E. Powered rails use only 0..5.
#pragma once
#include "world/world.h"

namespace vx {

bool isRail(const BlockRegistry& reg, BlockState s);
bool isPoweredRail(const BlockRegistry& reg, BlockState s);

// The rail's two ends in block-local coordinates (x/z in 0..1 on the cell edges, y 0 or 1) and the
// outward horizontal direction (4-way index) of each end.
struct RailGeom { Vec3 a, b; int dirA = 0, dirB = 1; };
RailGeom railGeometry(int shape);
void railShapeExits(int shape, int& d0, int& d1);   // the two directions a shape connects

// Shape recomputed from neighbouring rails; `fallback` is used when nothing connects.
BlockState withRailShape(const World& w, const BlockPos& p, BlockState s);

// Neighbour update for rails: drop when unsupported, refresh shape and powered state.
// Returns true if p holds a rail.
bool railNeighborUpdate(World& w, const BlockPos& p, const std::function<int(const BlockPos&)>& power);

} // namespace vx
