// vxworld: world maintenance CLI.
//
//   vxworld info     <world-dir>              storage usage report (read-only)
//   vxworld verify   <world-dir>              decode every record, check region checksums (read-only)
//   vxworld optimize <world-dir> [--apply]    migrate full records to sparse, drop records equal to
//                                             the generated baseline. Dry run unless --apply; --apply
//                                             always writes a timestamped backup first.
#include "storage/maintenance.h"
#include <cstdio>
#include <cstring>

using namespace vx;

static int usage() {
    fprintf(stderr, "usage: vxworld info|verify|optimize <world-dir> [--apply] [--blocks <blocks.json>]\n");
    return 2;
}

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    std::string cmd = argv[1];
    fs::path dir = argv[2];
    bool apply = false;
    std::string blocksPath = std::string(VX_SOURCE_DIR) + "/assets/data/blocks.json";
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--apply")) apply = true;
        else if (!strcmp(argv[i], "--blocks") && i + 1 < argc) blocksPath = argv[++i];
        else return usage();
    }
    BlockRegistry reg; std::string err;
    if (!reg.loadFile(blocksPath, &err)) { fprintf(stderr, "blocks: %s\n", err.c_str()); return 1; }
    // Item names are stored as strings in records; the CLI never interprets them, so an identity
    // mapping through a private table round-trips them unchanged.
    auto table = std::make_shared<std::vector<std::string>>(1, "");
    ItemNames items{
        [table](u16 id) { return id < table->size() ? (*table)[id] : std::string(); },
        [table](const std::string& n) {
            for (size_t i = 1; i < table->size(); i++) if ((*table)[i] == n) return (u16)i;
            table->push_back(n); return (u16)(table->size() - 1);
        }};

    if (cmd == "info") {
        WorldStorage st;
        if (!st.open(dir, false, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        auto u = st.usage();
        printf("region files: %llu\nrecords:      %llu\nregion bytes: %llu\ntotal bytes:  %llu\n",
               (unsigned long long)u.regionFiles, (unsigned long long)u.records, (unsigned long long)u.regionBytes, (unsigned long long)u.totalBytes);
        if (!st.lastRecovery.empty()) printf("recovery: %s\n", st.lastRecovery.c_str());
        return 0;
    }
    MaintenanceReport rep;
    bool ok;
    if (cmd == "verify") ok = verifyWorld(dir, reg, items, rep, &err);
    else if (cmd == "optimize") ok = optimizeWorld(dir, reg, items, !apply, rep, &err);
    else return usage();
    for (auto& l : rep.log) printf("  %s\n", l.c_str());
    if (!ok) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    printf("regions %d, records %d: removed %d, migrated %d, kept %d, failed %d\n",
           rep.regions, rep.records, rep.recordsRemoved, rep.recordsMigrated, rep.recordsKept, rep.recordsFailed);
    if (cmd == "optimize") {
        printf("bytes %llu -> %llu%s\n", (unsigned long long)rep.bytesBefore, (unsigned long long)rep.bytesAfter, apply ? "" : " (dry run, nothing changed)");
        if (!rep.backupDir.empty()) printf("backup: %s\n", rep.backupDir.string().c_str());
    }
    return rep.recordsFailed ? 1 : 0;
}
