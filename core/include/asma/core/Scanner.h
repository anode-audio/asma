// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace asma {

struct ScanStats {
    std::size_t added = 0;
    std::size_t updated = 0;
    std::size_t unchanged = 0;
    std::size_t relinked = 0;
    std::size_t missing = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0; // could not be read this time; retried next scan
};

struct ScanOptions {
    unsigned threads = 0;         // 0 = std::thread::hardware_concurrency()
    std::size_t batchSize = 200;  // files per write transaction
    // Both callbacks may be called from worker threads; calls are serialised.
    std::function<void(std::string_view relPath)> onFileStart;
    std::function<void(std::size_t done, std::size_t total, std::string_view relPath)> onProgress;
};

// Brings the database in line with the files under one root. Read-only on
// disk. Throws std::invalid_argument for an unknown root, DbError on database
// failure.
ScanStats scanRoot(Db& db, std::int64_t rootId, const ScanOptions& options = {});

// Records that relPath crashed the scanner, so later scans skip it until its
// size or mtime changes. Creates the row if the file is not known yet.
void markFailedPath(Db& db, std::int64_t rootId, std::string_view relPath, std::string_view reason);

} // namespace asma
