// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Analyser.h"

#include "Parallel.h"
#include "asma/core/Analysis.h"
#include "asma/core/AudioProbe.h"
#include "asma/core/Decode.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace asma {

namespace {

struct Pending {
    std::int64_t id = 0;
    fs::path path;
    std::string relPath;
};

struct Outcome {
    std::optional<AnalysisResult> result;
    std::string error;
    bool unreadable = false;
};

std::vector<Pending> pendingFiles(Db& db, const std::optional<std::int64_t>& rootId)
{
    std::string sql = "SELECT f.id, r.path, f.rel_path FROM files f JOIN roots r ON r.id = f.root_id "
                      "WHERE f.status = 'ok' AND r.enabled = 1 AND f.analysis_version < ?";
    if (rootId) sql += " AND f.root_id = ?";
    sql += " ORDER BY f.id";
    auto q = db.prepare(sql);
    q.bind(1, kAnalysisVersion);
    if (rootId) q.bind(2, *rootId);
    std::vector<Pending> out;
    while (q.step()) {
        Pending p;
        p.id = q.getInt(0);
        p.relPath = q.getText(2);
        p.path = fromUtf8(q.getText(1)) / fromUtf8(p.relPath);
        out.push_back(std::move(p));
    }
    return out;
}

Outcome analyseOne(const Pending& file, double maxSeconds)
{
    Outcome o;
    try {
        o.result = analyse(decodeFile(file.path, maxSeconds));
    } catch (const FileAccessError& e) {
        o.error = e.what();
        o.unreadable = true;
    } catch (const std::exception& e) {
        o.error = e.what();
    }
    return o;
}

} // namespace

AnalyseStats analysePending(Db& db, const AnalyseOptions& options)
{
    const std::vector<Pending> files = pendingFiles(db, options.rootId);
    const unsigned threads = detail::threadCount(options.threads);
    const std::size_t batchSize = std::max<std::size_t>(1, options.batchSize);
    Library lib(db);
    AnalyseStats stats;
    std::mutex callbackMutex;
    std::size_t done = 0;

    for (std::size_t start = 0; start < files.size(); start += batchSize) {
        const std::size_t end = std::min(files.size(), start + batchSize);
        std::vector<Outcome> outcomes(end - start);
        detail::parallelFor(start, end, threads, [&](std::size_t i) {
            if (options.onFileStart) {
                std::lock_guard lock(callbackMutex);
                options.onFileStart(files[i].relPath);
            }
            outcomes[i - start] = analyseOne(files[i], options.maxSeconds);
            std::lock_guard lock(callbackMutex);
            ++done;
            if (options.onProgress) options.onProgress(done, files.size(), files[i].relPath);
        });

        Transaction tx(db);
        for (std::size_t i = start; i < end; ++i) {
            const Outcome& o = outcomes[i - start];
            if (o.result) {
                lib.setAnalysis(files[i].id, *o.result);
                ++stats.analysed;
            } else if (o.unreadable) {
                ++stats.skipped; // leave it pending
            } else {
                lib.setAnalysisError(files[i].id, o.error);
                ++stats.failed;
            }
        }
        tx.commit();
    }
    return stats;
}

void markAnalysisFailed(Db& db, std::int64_t rootId, std::string_view relPath, std::string_view reason)
{
    Library lib(db);
    if (const auto file = lib.fileByPath(rootId, relPath)) lib.setAnalysisError(file->id, reason);
}

} // namespace asma
