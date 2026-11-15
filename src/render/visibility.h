// Occlusion culling for caves and enclosed spaces ("section connectivity graph").
//
// For every 16^3 section the mesher records which pairs of its 6 faces are connected through
// non-opaque blocks (15 bits). Each frame a breadth-first walk starts at the camera's section and
// only steps into a neighbour through a face that is reachable from the face it entered by, only
// moves away from the camera (no back-tracking), and only into sections inside the frustum.
// Underground, this skips the whole surface and every sealed cave; above ground it costs a
// BFS over the visible sections (tested, measured in PERFORMANCE.md).
#pragma once
#include "core/common.h"
#include <functional>

namespace vx {

using FaceMask = u16;   // bit for each unordered face pair (a<b): see pairBit()
constexpr FaceMask ALL_CONNECTED = 0x7FFF, NONE_CONNECTED = 0;
inline int pairBit(int a, int b) {   // faces 0..5 (NX PX NY PY NZ PZ)
    if (a > b) std::swap(a, b);
    static const int base[6] = {0, 5, 9, 12, 14, 15};
    return base[a] + (b - a - 1);
}
inline bool connected(FaceMask m, int a, int b) { return a == b || ((m >> pairBit(a, b)) & 1); }

// Connectivity of a 16^3 block of cells (opaque[i], i = (y<<8)|(z<<4)|x).
FaceMask computeConnectivity(const bool* opaque);

struct SectionKey { i32 x, y, z; bool operator==(const SectionKey& o) const { return x == o.x && y == o.y && z == o.z; } };

// BFS from the camera section; `mask` returns connectivity (ALL_CONNECTED for unknown/empty);
// `inFrustum` tests a section; `visit` is called once for every reachable section.
void traverseVisible(SectionKey camera, int radiusChunks, const std::function<FaceMask(const SectionKey&)>& mask,
                     const std::function<bool(const SectionKey&)>& inFrustum, const std::function<void(const SectionKey&)>& visit);

} // namespace vx
