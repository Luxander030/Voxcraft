// World maintenance: migration of full chunk records to sparse deltas, removal of records that
// match the generated baseline, storage reports. Always validates before replacing data, makes a
// backup before any destructive change, and supports dry runs.
#pragma once
#include "storage/storage.h"

namespace vx {

struct MaintenanceReport {
    int regions = 0, records = 0;
    int recordsRemoved = 0;        // identical to generated baseline -> regenerable
    int recordsMigrated = 0;       // full -> sparse (or re-encoded smaller)
    int recordsKept = 0;
    int recordsFailed = 0;         // could not be decoded/validated -> left untouched
    u64 bytesBefore = 0, bytesAfter = 0;
    fs::path backupDir;
    std::vector<std::string> log;
};

// dryRun: analyse and report only. Otherwise backs up the world directory first.
bool optimizeWorld(const fs::path& dir, const BlockRegistry& reg, const ItemNames& items, bool dryRun, MaintenanceReport& rep, std::string* err);

// Verify every record decodes and region checksums match. Read-only.
bool verifyWorld(const fs::path& dir, const BlockRegistry& reg, const ItemNames& items, MaintenanceReport& rep, std::string* err);

// Copy a world directory to a sibling "<name>-backup-YYYYMMDD-HHMMSS". Returns the backup path.
fs::path backupWorld(const fs::path& dir, std::string* err);

} // namespace vx
