#include "render/visibility.h"
#include <unordered_set>
#include <vector>

namespace vx {

FaceMask computeConnectivity(const bool* opaque) {
    static thread_local std::vector<u16> stack;
    static thread_local std::vector<u8> seen;
    seen.assign(4096, 0);
    FaceMask result = 0;
    for (int start = 0; start < 4096; start++) {
        if (opaque[start] || seen[start]) continue;
        u8 faces = 0;
        stack.clear(); stack.push_back((u16)start); seen[start] = 1;
        while (!stack.empty()) {
            int i = stack.back(); stack.pop_back();
            int x = i & 15, z = (i >> 4) & 15, y = i >> 8;
            if (x == 0) faces |= 1; if (x == 15) faces |= 2; if (y == 0) faces |= 4; if (y == 15) faces |= 8; if (z == 0) faces |= 16; if (z == 15) faces |= 32;
            const int nb[6] = {x > 0 ? i - 1 : -1, x < 15 ? i + 1 : -1, y > 0 ? i - 256 : -1, y < 15 ? i + 256 : -1, z > 0 ? i - 16 : -1, z < 15 ? i + 16 : -1};
            for (int n : nb) if (n >= 0 && !opaque[n] && !seen[n]) { seen[n] = 1; stack.push_back((u16)n); }
        }
        for (int a = 0; a < 6; a++) for (int b = a + 1; b < 6; b++) if ((faces >> a & 1) && (faces >> b & 1)) result |= (FaceMask)(1u << pairBit(a, b));
        if (result == ALL_CONNECTED) break;
    }
    return result;
}

void traverseVisible(SectionKey cam, int radius, const std::function<FaceMask(const SectionKey&)>& mask,
                     const std::function<bool(const SectionKey&)>& inFrustum, const std::function<void(const SectionKey&)>& visit) {
    static const int D[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    struct Node { SectionKey k; int entry; };   // entry face (-1 = camera section)
    auto key = [](const SectionKey& k) { return ((u64)(u32)(k.x + 100000) << 32) ^ ((u64)(u32)k.y << 24) ^ (u64)(u32)(k.z + 100000); };
    std::unordered_set<u64> seen;
    std::vector<Node> queue{{cam, -1}};
    seen.insert(key(cam));
    for (size_t qi = 0; qi < queue.size(); qi++) {
        Node n = queue[qi];
        visit(n.k);
        FaceMask m = mask(n.k);
        for (int f = 0; f < 6; f++) {
            if (n.entry >= 0 && !connected(m, n.entry, f)) continue;
            SectionKey nk{n.k.x + D[f][0], n.k.y + D[f][1], n.k.z + D[f][2]};
            if (nk.y < 0 || nk.y >= 16) continue;
            if (std::abs(nk.x - cam.x) > radius || std::abs(nk.z - cam.z) > radius) continue;
            // only move away from the camera along this axis (prevents re-entering through the back)
            int a = f >> 1, d = D[f][a], delta = (a == 0 ? nk.x - cam.x : a == 1 ? nk.y - cam.y : nk.z - cam.z);
            if (delta * d < 0) continue;
            if (seen.count(key(nk)) || !inFrustum(nk)) continue;
            seen.insert(key(nk));
            queue.push_back({nk, f ^ 1});   // entered through the opposite face
        }
    }
}

} // namespace vx
