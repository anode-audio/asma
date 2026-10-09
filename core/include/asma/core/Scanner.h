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

// The reasons a retry gives: the file is not where the library says, or
// reading it crashed the process doing the retry.
inline constexpr std::string_view kGoneReason = "The file is gone";
inline constexpr std::string_view kCrashedReason = "asma crashed reading it";

struct RetryStats {
    std::vector<std::int64_t> readable; // read again: queued for analysis
    std::size_t failed = 0;             // still cannot be read; the reason is the new one
    std::size_t gone = 0;               // no longer where the library says
    std::size_t skipped = 0;            // ok files that could not be reached: left as they are
};

// The Problems panel's retry: reads the files again whatever their size and
// mtime, as a scan would a new file. A file read again is ok, its analysis
// forgotten so analysePending (with fileIds) does it again; a failed file that
// still fails keeps the new reason, and one that is gone says kGoneReason (a
// scan reads it again once it is back). An ok file (its analysis failed) that
// cannot be reached is left as it is. Files are read outside any transaction
// and each outcome committed on its own, so writers never wait for the reads;
// each file is marked kCrashedReason while it is read, so a file that crashes
// the process costs only itself, and files so marked are read last.
// Throws std::invalid_argument, writing nothing, for an id the library does
// not know.
// `beforeRead`, for tests, is called before each file is read.
RetryStats retryFiles(Db& db, const std::vector<std::int64_t>& fileIds,
                      const std::function<void(std::int64_t fileId)>& beforeRead = {});

// Records that relPath crashed the scanner, so later scans skip it until its
// size or mtime changes. Creates the row if the file is not known yet.
void markFailedPath(Db& db, std::int64_t rootId, std::string_view relPath, std::string_view reason);

// Reads a renamed or moved file's header and name again, so what its name and
// folder say (BPM, key, loop, tags) follows it; analysis output stays. Does
// nothing when the file cannot be read.
void rederive(Db& db, std::int64_t fileId);

} // namespace asma
