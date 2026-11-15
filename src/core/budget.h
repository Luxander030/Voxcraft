// Adaptive memory budget policy (CLAUDE.md 5.3). Pure logic, evaluated about once per second.
//
// When mesh VRAM or process memory exceed their budgets, the effective render distance is reduced
// one step at a time (min 4 chunks): the World then unloads distant chunks (modified ones are
// written to the pending set first, so nothing is ever lost) and the renderer frees their meshes;
// the clean-chunk cache budget shrinks too. When usage stays below 70% of the budget for a while,
// the distance steps back up toward the user's setting. Hysteresis avoids oscillation.
#pragma once
#include "core/common.h"

namespace vx {

struct BudgetInputs {
    size_t meshBytes = 0, processBytes = 0;
    size_t meshBudget = 192u << 20, processBudget = 2048ull << 20;
    int desiredRadius = 10;
};

class BudgetPolicy {
public:
    int effectiveRadius = 10;
    size_t chunkCacheBytes = 96u << 20;
    int calmSeconds = 0;
    // Returns true if the effective radius changed.
    bool update(const BudgetInputs& in) {
        int before = effectiveRadius;
        if (effectiveRadius > in.desiredRadius) effectiveRadius = in.desiredRadius;
        double meshUse = in.meshBudget ? (double)in.meshBytes / (double)in.meshBudget : 0;
        double procUse = in.processBudget ? (double)in.processBytes / (double)in.processBudget : 0;
        double use = std::max(meshUse, procUse);
        if (use > 1.0) {
            calmSeconds = 0;
            if (effectiveRadius > 4) effectiveRadius--;
            chunkCacheBytes = std::max<size_t>(8u << 20, chunkCacheBytes / 2);
        } else if (use < 0.7) {
            if (++calmSeconds >= 10 && effectiveRadius < in.desiredRadius) { effectiveRadius++; calmSeconds = 0; }
            chunkCacheBytes = std::min<size_t>(96u << 20, chunkCacheBytes + (8u << 20));
        } else calmSeconds = 0;
        return effectiveRadius != before;
    }
};

} // namespace vx
