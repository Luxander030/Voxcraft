#include "game/kiln.h"

namespace vx {
bool tickKiln(BlockEntity& be, const ItemRegistry& items, const RecipeBook& rb, bool& lit) {
    if (be.items.size() < KILN_SLOTS) be.items.resize(KILN_SLOTS);
    ItemStack &in = be.items[0], &fuel = be.items[1], &out = be.items[2];
    bool changed = false;
    u16 result = in.empty() ? 0 : rb.smelt(in.item);
    bool canCook = result && (out.empty() || (out.item == result && out.count < items.maxStack(result)));
    if (be.data[0] <= 0 && canCook && !fuel.empty() && items.def(fuel.item).fuel > 0) {
        be.data[0] = be.data[1] = items.def(fuel.item).fuel;
        fuel.count--; if (!fuel.count) fuel = {};
        changed = true;
    }
    lit = be.data[0] > 0;
    if (be.data[0] > 0) {
        be.data[0]--; changed = true;
        if (canCook) {
            if (++be.data[2] >= KILN_COOK_TICKS) {
                be.data[2] = 0;
                if (out.empty()) out = {result, 1, 0}; else out.count++;
                in.count--; if (!in.count) in = {};
            }
        } else if (be.data[2]) be.data[2] = 0;
    } else if (be.data[2] > 0) { be.data[2] = std::max(0, be.data[2] - 2); changed = true; }   // cools down
    return changed;
}
}
