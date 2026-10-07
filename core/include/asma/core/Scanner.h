// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

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

struct RetryStats {
    std::vector<std::int64_t> readable; // read again: queued for analysis
    std::size_t failed = 0;             // still cannot be read; the reason is the new one
    std::size_t gone = 0;               // no longer where the library says
};

// The Problems panel's retry: reads the files again whatever their size and
// mtime, as a scan would a new file. A file read again is ok, its analysis
// forgotten so analysePending (with fileIds) does it again; one that still
// fails keeps the new reason; one that is gone is failed with "The file is
// gone" (the next scan of its folder marks it missing). One transaction.
// Throws std::invalid_argument, writing nothing, for an id the library does
// not know.
RetryStats retryFiles(Db& db, const std::vector<std::int64_t>& fileIds);

// Records that relPath crashed the scanner, so later scans skip it until its
// size or mtime changes. Creates the row if the file is not known yet.
void markFailedPath(Db& db, std::int64_t rootId, std::string_view relPath, std::string_view reason);

} // namespace asma
