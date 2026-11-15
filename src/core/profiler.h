// Lightweight profiling hooks. Counters/timers are always cheap (a relaxed atomic add);
// scoped timers compile to nothing unless VX_PROFILE is defined (on by default in non-release
// builds, toggleable with -DVX_PROFILE=ON for release profiling).
#pragma once
#include "core/common.h"
#include "core/util.h"
#include <atomic>

namespace vx {

enum class Stat : int {
    ChunksGenerated, ChunksMeshed, ChunksLit, ChunksSaved, ChunksLoadedFromDisk, ChunksUnloaded,
    GenTimeUs, MeshTimeUs, LightTimeUs, SaveTimeUs, LoadTimeUs,
    DrawCalls, Triangles, Allocations, MeshBytesUploaded,
    Count
};
const char* statName(Stat s);

struct Profiler {
    static std::atomic<u64> counters[(int)Stat::Count];
    static void add(Stat s, u64 v = 1) { counters[(int)s].fetch_add(v, std::memory_order_relaxed); }
    static u64 get(Stat s) { return counters[(int)s].load(std::memory_order_relaxed); }
    static void set(Stat s, u64 v) { counters[(int)s].store(v, std::memory_order_relaxed); }
};

struct ScopedTimerUs {
    Stat stat; double t0;
    explicit ScopedTimerUs(Stat s) : stat(s), t0(nowSeconds()) {}
    ~ScopedTimerUs() { Profiler::add(stat, (u64)((nowSeconds() - t0) * 1e6)); }
};

#define VX_CONCAT2(a, b) a##b
#define VX_CONCAT(a, b) VX_CONCAT2(a, b)
#define VX_TIME_US(stat) ::vx::ScopedTimerUs VX_CONCAT(_vxtimer_, __LINE__)(stat)

} // namespace vx
