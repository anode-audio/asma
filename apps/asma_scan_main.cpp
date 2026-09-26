// SPDX-License-Identifier: GPL-3.0-only
// asma-scan: out-of-process scanner. Protocol: docs/scan-protocol.md.
#include "Args.h"
#include "CliCommon.h"

#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Scanner.h"
#include "asma/core/WriterLock.h"

#include <iostream>
#include <string>

using namespace asma;
using namespace asma::cli;

namespace {

void emit(const JsonLine& line) { std::cout << line.build() << '\n' << std::flush; }

} // namespace

int main(int argc, char** argv)
{
    setupConsole();
    try {
        Args args = Args::fromMain(argc, argv);
        const auto db = args.option("db");
        const auto root = args.option("root");
        const auto threads = args.option("threads");
        const auto failed = args.options("fail");
        if (!db || !root || !args.rest().empty())
            throw UsageError("usage: asma-scan --db PATH --root ID [--threads N] [--fail RELPATH]...");

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
        return kOk;
    } catch (const UsageError& e) {
        std::cerr << e.what() << "\n";
        return kUsage;
    } catch (const std::exception& e) {
        emit(JsonLine().str("event", "error").str("code", "failed").str("message", e.what()));
        return kError;
    }
}
