#include "world/chunk.h"
#include "core/profiler.h"

namespace vx {

void Section::set(int i, BlockState s) {
    if (!data) {
        if (s == uniform) return;
        data = std::make_shared<SectionData>();
        Profiler::add(Stat::Allocations);
        std::fill(std::begin(data->b), std::end(data->b), uniform);
        data->nonAir = uniform == AIR ? 0 : 4096;
    } else if (data.use_count() > 1) {        // copy-on-write: a snapshot still references it
        data = std::make_shared<SectionData>(*data);
        Profiler::add(Stat::Allocations);
    }
    BlockState old = data->b[i];
    if (old == s) return;
    data->b[i] = s;
    if (old == AIR) data->nonAir++; else if (s == AIR) data->nonAir--;
}

void Section::compact() {
    if (!data) return;
    if (data->nonAir == 0) { data.reset(); uniform = AIR; return; }
    BlockState first = data->b[0];
    for (int i = 1; i < 4096; i++) if (data->b[i] != first) return;
    data.reset(); uniform = first;
}

bool Chunk::set(int lx, int y, int lz, BlockState s) {
    if ((unsigned)y >= (unsigned)WORLD_H) return false;
    BlockState cur = get(lx, y, lz);
    if (cur == s) return false;
    u16 idx = columnIndex(lx, y, lz);
    auto it = delta.find(idx);
    if (it == delta.end()) delta.emplace(idx, cur);           // first change: remember the baseline
    else if (it->second == s) delta.erase(it);                // reverted to baseline
    sec[y >> 4].set(Section::idx(lx, y & 15, lz), s);
    blockVersion++;
    unsaved = true;
    return true;
}

void Chunk::applyDelta(u16 index, BlockState s) {
    int lx, y, lz; columnIndexDecode(index, lx, y, lz);
    BlockState cur = get(lx, y, lz);
    if (cur == s) return;   // stored change equals baseline (e.g. generator fix) - nothing to record
    delta[index] = cur;
    sec[y >> 4].set(Section::idx(lx, y & 15, lz), s);
}

BlockEntity* Chunk::blockEntity(u16 index) {
    for (auto& be : blockEntities) if (be.index == index) return &be;
    return nullptr;
}
BlockEntity& Chunk::ensureBlockEntity(u16 index, u8 kind) {
    if (BlockEntity* be = blockEntity(index)) { if (be->kind != kind) { *be = BlockEntity(); be->index = index; be->kind = kind; } return *be; }
    BlockEntity be; be.index = index; be.kind = kind;
    be.items.resize(kind == BE_CHEST ? 27 : kind == BE_KILN ? 3 : 0);
    blockEntities.push_back(std::move(be));
    unsaved = true;
    return blockEntities.back();
}
void Chunk::removeBlockEntity(u16 index) {
    for (size_t i = 0; i < blockEntities.size(); i++)
        if (blockEntities[i].index == index) { blockEntities.erase(blockEntities.begin() + (long)i); unsaved = true; return; }
}

size_t Chunk::memoryBytes() const {
    size_t b = sizeof(Chunk);
    for (int i = 0; i < SECTIONS; i++) { if (sec[i].data) b += sizeof(SectionData); if (light[i].data) b += sizeof(LightData); }
    b += delta.size() * 16 + blockEntities.size() * sizeof(BlockEntity) + ticks.size() * sizeof(ScheduledTick);
    return b;
}

} // namespace vx
