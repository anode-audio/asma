// SPDX-License-Identifier: GPL-3.0-only
#include "Args.h"
#include "CliCommon.h"

#include "asma/core/Analyser.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Library.h"
#include "asma/core/NameParse.h"
#include "asma/core/Query.h"
#include "asma/core/Scanner.h"
#include "asma/core/Similar.h"
#include "asma/core/WriterLock.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

using namespace asma;
using namespace asma::cli;

namespace {

constexpr const char* kUsageText =
    "usage: asma [--db PATH] <command>\n"
    "  root add <dir>          add a sample folder\n"
    "  root list               list sample folders\n"
    "  scan [--root ID] [--threads N] [--no-analysis]\n"
    "  query [words...] [--type loop|oneshot] [--bpm N|MIN-MAX] [--key K]...\n"
    "        [--tag T]... [--format F]... [--min-duration S] [--max-duration S]\n"
    "        [--sort name|bpm|duration|key] [--desc] [--limit N] [--json]\n"
    "  similar <file> [--limit N] [--json]   files that sound like <file>\n"
    "  --version\n";

void rejectLeftovers(const Args& args)
{
    if (!args.rest().empty()) throw UsageError("unexpected argument: " + args.rest().front());
}

double toDouble(const std::string& text, const char* what)
{
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used == text.size()) return value;
    } catch (const std::exception&) {
    }
    throw UsageError(std::string("not a number for ") + what + ": " + text);
}

// One line per row: TSV (path, bpm, key, type, duration[, similarity]) or JSON.
void printRows(const std::vector<SearchRow>& rows, bool json, const std::vector<double>& similarity)
{
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const SearchRow& row = rows[i];
        const std::string path = row.rootPath + "/" + row.relPath;
        if (json) {
            JsonLine line;
            line.num("id", row.id).str("path", path).str("format", row.format).real("duration", row.duration);
            if (row.bpm) line.real("bpm", *row.bpm);
            else line.null("bpm");
            if (row.key) line.str("key", *row.key);
            else line.null("key");
            if (row.isLoop) line.boolean("is_loop", *row.isLoop);
            else line.null("is_loop");
            if (i < similarity.size()) line.real("similarity", similarity[i]);
            std::cout << line.build() << "\n";
        } else {
            char duration[32];
            std::snprintf(duration, sizeof duration, "%.2f", row.duration);
            char bpm[32] = "-";
            if (row.bpm) std::snprintf(bpm, sizeof bpm, "%g", std::round(*row.bpm * 100.0) / 100.0);
            const char* type = !row.isLoop ? "-" : (*row.isLoop ? "loop" : "oneshot");
            std::cout << path << "\t" << bpm << "\t" << row.key.value_or("-") << "\t" << type << "\t" << duration;
            if (i < similarity.size()) {
                char score[32];
                std::snprintf(score, sizeof score, "%.3f", similarity[i]);
                std::cout << "\t" << score;
            }
            std::cout << "\n";
        }
    }
}

int cmdRoot(Args& args, Db& db)
{
    Library lib(db);
    const auto sub = args.positional();
    if (sub == "add") {
        const auto dir = args.positional();
        if (!dir) throw UsageError("root add needs a directory");
        rejectLeftovers(args);
        const auto id = lib.addRoot(fromUtf8(*dir));
        std::cout << "root " << id << " " << lib.root(id)->path << "\n";
        return kOk;
    }
    if (sub == "list") {
        rejectLeftovers(args);
        for (const auto& r : lib.roots()) std::cout << r.id << "\t" << r.path << "\t" << (r.enabled ? "on" : "off") << "\n";
        return kOk;
    }
    throw UsageError("root needs 'add' or 'list'");
}

int cmdScan(Args& args, Db& db, const std::filesystem::path& dbPath)
{
    const auto root = args.option("root");
    const auto threads = args.option("threads");
    const bool analyseFiles = !args.flag("no-analysis");
    rejectLeftovers(args);

    auto lock = WriterLock::tryAcquire(dbPath.parent_path());
    if (!lock) {
        std::cerr << "another asma process is writing to this library";
        if (const auto pid = WriterLock::holder(dbPath.parent_path())) std::cerr << " (pid " << *pid << ")";
        std::cerr << "\n";
        return kLocked;
    }

    Library lib(db);
    ScanOptions options;
    if (threads) options.threads = static_cast<unsigned>(toDouble(*threads, "--threads"));
    options.onProgress = [](std::size_t done, std::size_t total, std::string_view) {
        if (done % 500 == 0 || done == total) std::cerr << "\r" << done << "/" << total << std::flush;
    };
    for (const auto& r : lib.roots()) {
        if (root && std::to_string(r.id) != *root) continue;
        if (!r.enabled) continue;
        const ScanStats s = scanRoot(db, r.id, options);
        std::cerr << "\r";
        std::cout << "root " << r.id << ": added " << s.added << ", updated " << s.updated << ", unchanged "
                  << s.unchanged << ", relinked " << s.relinked << ", missing " << s.missing << ", failed "
                  << s.failed << ", skipped " << s.skipped << "\n";
    }
    if (analyseFiles) {
        AnalyseOptions analysis;
        analysis.threads = options.threads;
        if (root) analysis.rootId = static_cast<std::int64_t>(toDouble(*root, "--root"));
        analysis.onProgress = options.onProgress;
        const AnalyseStats a = analysePending(db, analysis);
        std::cerr << "\r";
        std::cout << "analysis: analysed " << a.analysed << ", failed " << a.failed << ", skipped " << a.skipped
                  << "\n";
    }
    return kOk;
}

int cmdSimilar(Args& args, Db& db)
{
    const auto id = args.option("id");
    int limit = 10;
    if (const auto v = args.option("limit")) limit = static_cast<int>(toDouble(*v, "--limit"));
    const bool json = args.flag("json");
    const auto target = args.positional();
    rejectLeftovers(args);

    std::int64_t fileId = 0;
    if (id) {
        fileId = static_cast<std::int64_t>(toDouble(*id, "--id"));
    } else {
        if (!target) throw UsageError("similar needs a file path or --id N");
        Library lib(db);
        const auto file = lib.fileByAbsolutePath(fromUtf8(*target));
        if (!file) {
            std::cerr << "asma: not in the library: " << *target << "\n";
            return kError;
        }
        fileId = file->id;
    }
    const auto matches = findSimilar(db, fileId, limit);
    std::vector<std::int64_t> ids;
    std::vector<double> scores;
    for (const auto& m : matches) ids.push_back(m.id);
    const auto rows = rowsForIds(db, ids);
    for (const auto& row : rows)
        for (const auto& m : matches)
            if (m.id == row.id) scores.push_back(m.similarity);
    printRows(rows, json, scores);
    return kOk;
}

int cmdQuery(Args& args, Db& db)
{
    SearchModel m;
    if (const auto type = args.option("type")) {
        if (*type == "loop") m.type = SampleType::Loop;
        else if (*type == "oneshot") m.type = SampleType::OneShot;
        else throw UsageError("--type must be loop or oneshot");
    }
    if (const auto bpm = args.option("bpm")) {
        const auto dash = bpm->find('-');
        if (dash == std::string::npos) {
            const double v = toDouble(*bpm, "--bpm");
            m.bpmMin = v - 0.5;
            m.bpmMax = v + 0.5;
        } else {
            m.bpmMin = toDouble(bpm->substr(0, dash), "--bpm");
            m.bpmMax = toDouble(bpm->substr(dash + 1), "--bpm");
        }
    }
    for (const auto& key : args.options("key")) {
        const auto canonical = parseKeyToken(key);
        if (!canonical) throw UsageError("not a key: " + key + " (try Am, C#m, Eb, F#maj)");
        m.keys.push_back(*canonical);
    }
    m.tags = args.options("tag");
    m.formats = args.options("format");
    if (const auto v = args.option("min-duration")) m.durationMin = toDouble(*v, "--min-duration");
    if (const auto v = args.option("max-duration")) m.durationMax = toDouble(*v, "--max-duration");
    if (const auto sort = args.option("sort")) {
        if (*sort == "name") m.sort = SortField::Name;
        else if (*sort == "bpm") m.sort = SortField::Bpm;
        else if (*sort == "duration") m.sort = SortField::Duration;
        else if (*sort == "key") m.sort = SortField::Key;
        else throw UsageError("--sort must be name, bpm, duration or key");
    }
    m.descending = args.flag("desc");
    if (const auto limit = args.option("limit")) m.limit = static_cast<int>(toDouble(*limit, "--limit"));
    const bool json = args.flag("json");

    for (const auto& word : args.rest()) {
        if (word.rfind("--", 0) == 0) throw UsageError("unknown option: " + word);
        if (!m.text.empty()) m.text += ' ';
        m.text += word;
    }

    printRows(search(db, m), json, {});
    return kOk;
}

} // namespace

int main(int argc, char** argv)
{
    setupConsole();
    try {
        Args args = Args::fromMain(argc, argv);
        if (args.flag("version")) {
            std::cout << "asma " << ASMA_VERSION << "\n";
            return kOk;
        }
        const std::filesystem::path dbPath = resolveDbPath(args);
        const auto command = args.positional();
        if (!command) throw UsageError("missing command");
        if (*command != "root" && *command != "scan" && *command != "query" && *command != "similar")
            throw UsageError("unknown command: " + *command);

        Db db = Db::open(dbPath);
        if (*command == "root") return cmdRoot(args, db);
        if (*command == "scan") return cmdScan(args, db, dbPath);
        if (*command == "similar") return cmdSimilar(args, db);
        return cmdQuery(args, db);
    } catch (const UsageError& e) {
        std::cerr << "asma: " << e.what() << "\n" << kUsageText;
        return kUsage;
    } catch (const std::exception& e) {
        std::cerr << "asma: " << e.what() << "\n";
        return kError;
    }
}
