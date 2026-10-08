// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Backup.h"
#include "asma/core/Scanner.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace asma {

enum class LibraryHealth {
    Ok,
    Missing,    // no library yet
    Damaged,    // SQLite finds the file malformed
    Unreadable, // cannot be checked now (locked past the busy timeout, no permission): not damaged
};

struct HealthReport {
    LibraryHealth health = LibraryHealth::Ok;
    std::string detail; // what SQLite said, when not Ok
};

// Reads the library with PRAGMA quick_check: the page and record damage
// integrity_check finds, without verifying index contents, so seconds on a
// large library. Read-only; safe inside a host. Never throws.
HealthReport checkLibrary(const std::filesystem::path& dbPath);

struct RepairReport {
    enum class Result {
        Healthy,  // nothing to repair: the library was left as it is
        Repaired,
        Locked,   // another asma holds the writer lock; nothing was touched
        InUse,    // the damaged file could not be moved (Windows: another asma has it open)
    };
    Result result = Result::Healthy;
    std::filesystem::path movedTo; // where the damaged library went
    std::size_t folders = 0;       // folders the new library has
    std::string backupWrittenAt;   // the backup restored, empty when none was
    RestoreStats restored;
    std::string detail; // what the check found
};

// Rebuilds a damaged library. Takes the writer lock and checks again, so a
// sound library is never moved. Builds a new library as library.db.rebuild
// with the backup's folders (or those the damaged file still yields), scans
// them without analysis (the next scan does that) and restores the backup at
// `backup`, when there is one; only then moves library.db and its -wal and
// -shm to library.db.corrupt (with the date and time when that exists;
// nothing is deleted) and the new library into its place. A rebuild cut short
// leaves the damaged library where it was, to be rebuilt again.
RepairReport repairLibrary(const std::filesystem::path& dbPath, const std::filesystem::path& backup,
                           const ScanOptions& scan = {});

} // namespace asma
