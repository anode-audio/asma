// SPDX-License-Identifier: GPL-3.0-only
// asma-scan: out-of-process scanner. Protocol: docs/scan-protocol.md.
#include "Args.h"
#include "CliCommon.h"

#include "asma/core/Analyser.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Scanner.h"
#include "asma/core/WriterLock.h"

#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace asma;
using namespace asma::cli;

namespace {

void emit(const JsonLine& line) { std::cout << line.build() << '\n' << std::flush; }

} // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    // A crash must end the process at once so the supervisor can restart it,
    // not wait behind a Windows Error Reporting dialog.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    setupConsole();
    try {
        Args args = Args::fromMain(argc, argv);
        const auto db = args.option("db");
        const auto root = args.option("root");
        const auto threads = args.option("threads");
        const auto failed = args.options("fail");
        const auto analysisFailed = args.options("fail-analysis");
        const bool analyseFiles = !args.flag("no-analysis");
        if (!db || !root || !args.rest().empty())
            throw UsageError("usage: asma-scan --db PATH --root ID [--threads N] [--no-analysis] "
                             "[--fail RELPATH]... [--fail-analysis RELPATH]...");

        const std::filesystem::path dbPath = fromUtf8(*db);
        auto lock = WriterLock::tryAcquire(dbPath.parent_path());
        if (!lock) {
            JsonLine line;
            line.str("event", "error").str("code", "locked");
            if (const auto pid = WriterLock::holder(dbPath.parent_path())) line.num("pid", *pid);
            emit(line);
            return kLocked;
        }

        Db database = Db::open(dbPath);
        const std::int64_t rootId = std::stoll(*root);
        for (const auto& path : failed) {
            const std::string rel = toUtf8(fromUtf8(path));
            markFailedPath(database, rootId, rel, "crashed the scanner");
            emit(JsonLine().str("event", "marked_failed").str("path", rel));
        }
        for (const auto& path : analysisFailed) {
            const std::string rel = toUtf8(fromUtf8(path));
            markAnalysisFailed(database, rootId, rel, "crashed the analyser");
            emit(JsonLine().str("event", "marked_analysis_failed").str("path", rel));
        }

        ScanOptions options;
        if (threads) options.threads = static_cast<unsigned>(std::stoul(*threads));
        options.onFileStart = [](std::string_view rel) { emit(JsonLine().str("event", "start").str("path", rel)); };
        options.onProgress = [](std::size_t done, std::size_t total, std::string_view rel) {
            emit(JsonLine()
                     .str("event", "progress")
                     .num("done", static_cast<std::int64_t>(done))
                     .num("total", static_cast<std::int64_t>(total))
                     .str("path", rel));
        };
        const ScanStats s = scanRoot(database, rootId, options);
        emit(JsonLine()
                 .str("event", "done")
                 .num("added", static_cast<std::int64_t>(s.added))
                 .num("updated", static_cast<std::int64_t>(s.updated))
                 .num("unchanged", static_cast<std::int64_t>(s.unchanged))
                 .num("relinked", static_cast<std::int64_t>(s.relinked))
                 .num("missing", static_cast<std::int64_t>(s.missing))
                 .num("failed", static_cast<std::int64_t>(s.failed))
                 .num("skipped", static_cast<std::int64_t>(s.skipped)));

        if (analyseFiles) {
            AnalyseOptions analysis;
            analysis.threads = options.threads;
            analysis.rootId = rootId;
            analysis.onFileStart = [](std::string_view rel) {
                emit(JsonLine().str("event", "analyse_start").str("path", rel));
            };
            analysis.onProgress = [](std::size_t done, std::size_t total, std::string_view rel) {
                emit(JsonLine()
                         .str("event", "analyse_progress")
                         .num("done", static_cast<std::int64_t>(done))
                         .num("total", static_cast<std::int64_t>(total))
                         .str("path", rel));
            };
            const AnalyseStats a = analysePending(database, analysis);
            emit(JsonLine()
                     .str("event", "analyse_done")
                     .num("analysed", static_cast<std::int64_t>(a.analysed))
                     .num("failed", static_cast<std::int64_t>(a.failed))
                     .num("skipped", static_cast<std::int64_t>(a.skipped)));
        }
        return kOk;
    } catch (const UsageError& e) {
        std::cerr << e.what() << "\n";
        return kUsage;
    } catch (const std::exception& e) {
        emit(JsonLine().str("event", "error").str("code", "failed").str("message", e.what()));
        return kError;
    }
}
