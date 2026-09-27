// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace asma {

struct AnalyseStats {
    std::size_t analysed = 0;
    std::size_t failed = 0;  // could not be decoded; not retried until the file changes
    std::size_t skipped = 0; // could not be read this time; retried next run
};

struct AnalyseOptions {
    unsigned threads = 0;              // 0 = std::thread::hardware_concurrency()
    std::size_t batchSize = 50;        // files per write transaction
    std::optional<std::int64_t> rootId; // only this root; all enabled roots otherwise
    double maxSeconds = 30.0;          // analyse at most this much of each file
    // Both callbacks may be called from worker threads; calls are serialised.
    std::function<void(std::string_view relPath)> onFileStart;
    std::function<void(std::size_t done, std::size_t total, std::string_view relPath)> onProgress;
};

// Analyses every ok file whose analysis_version is below kAnalysisVersion.
AnalyseStats analysePending(Db& db, const AnalyseOptions& options = {});

// Records that analysing relPath crashed the process, so it is not analysed
// again until its content changes. No-op for an unknown path.
void markAnalysisFailed(Db& db, std::int64_t rootId, std::string_view relPath, std::string_view reason);

} // namespace asma
