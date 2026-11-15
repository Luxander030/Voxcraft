// Kiln (smelting) block entity logic. Slots: 0 input, 1 fuel, 2 output.
// data[0] = fuel ticks left, data[1] = fuel ticks of the current item, data[2] = cook progress.
#pragma once
#include "game/items.h"

namespace vx {
constexpr int KILN_COOK_TICKS = 200;
constexpr int KILN_SLOTS = 3, CHEST_SLOTS = 27;
// One 20 Hz tick. Returns true if anything persistent changed. `lit` reports whether it is burning.
bool tickKiln(BlockEntity& be, const ItemRegistry& items, const RecipeBook& rb, bool& lit);
}
