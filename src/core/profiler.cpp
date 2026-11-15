#include "core/profiler.h"

namespace vx {
std::atomic<u64> Profiler::counters[(int)Stat::Count] = {};
const char* statName(Stat s) {
    static const char* n[] = {"chunks_generated", "chunks_meshed", "chunks_lit", "chunks_saved", "chunks_loaded_disk", "chunks_unloaded",
                              "gen_us", "mesh_us", "light_us", "save_us", "load_us",
                              "draw_calls", "triangles", "allocations", "mesh_bytes_uploaded"};
    static_assert(sizeof(n) / sizeof(n[0]) == (size_t)Stat::Count, "stat names");
    return n[(int)s];
}
} // namespace vx
