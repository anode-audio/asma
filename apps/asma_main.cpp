// SPDX-License-Identifier: GPL-3.0-only
#include "Args.h"
#include "CliCommon.h"
#include "OrganiseCommands.h"
#include "SearchArgs.h"

#include "asma/audio/Render.h"
#include "asma/audio/Sync.h"
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
    "  query [words...] [--saved NAME] [--type loop|oneshot|any] [--bpm N|MIN-MAX]\n"
    "        [--key K]... [--tag T]... [--format F]... [--min-duration S]\n"
    "        [--max-duration S] [--min-rating N] [--favourites] [--collection NAME]\n"
    "        [--sort name|bpm|duration|key|rating] [--desc] [--limit N] [--json]\n"
    "  similar <file> [--limit N] [--json]   files that sound like <file>\n"
    "  rate <0-5> <file>... | --id N...      0 clears the rating\n"
    "  fav on|off <file>... | --id N...\n"
    "  tag add|remove <tag> <file>... | --id N...\n"
    "  collection list | create <name> | rename <name> <new> | delete <name>\n"
    "  collection add|remove <name> <file>... | --id N...\n"
    "  search list | save <name> [query options] [words...] | delete <name>\n"
    "  render <file> [--trim-start S] [--trim-end S] [--reverse | --ping-pong]\n"
    "         [--tempo BPM] [--key K] [--transpose N] [--rate HZ] [--renders DIR]\n"
    "                          print the file to drag, rendering edits if any\n"
    "  renders [clear] [--renders DIR]   size of the kept renders, or delete them\n"
    "  --version\n";

// One line per row: TSV (path, bpm, key, type, duration[, similarity]) or JSON,
// which also carries the rating and favourite flag.
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
            if (row.rating) line.num("rating", *row.rating);
            else line.null("rating");
            line.boolean("favourite", row.favourite);
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

int cmdRender(Args& args, Db& db)
{
    audio::RenderSettings settings;
    if (const auto v = args.option("trim-start")) settings.edits.trimStart = toDouble(*v, "--trim-start");
    if (const auto v = args.option("trim-end")) settings.edits.trimEnd = toDouble(*v, "--trim-end");
    const bool reverse = args.flag("reverse");
    const bool pingPong = args.flag("ping-pong");
    if (reverse && pingPong) throw UsageError("--reverse and --ping-pong do not go together");
    if (reverse) settings.edits.direction = audio::Direction::Reverse;
    if (pingPong) settings.edits.direction = audio::Direction::PingPong;
    audio::SyncSettings sync;
    sync.tempo = false;
    if (const auto v = args.option("tempo")) {
        sync.tempo = true;
        sync.hostBpm = toDouble(*v, "--tempo");
    }
    if (const auto v = args.option("key")) {
        auto key = canonicalKey(*v);
        if (!key) key = parseKeyToken(*v);
        if (!key) throw UsageError("not a key: " + *v);
        sync.key = true;
        sync.projectKey = audio::KeyName(*key);
    }
    double transpose = 0.0;
    if (const auto v = args.option("transpose")) transpose = toDouble(*v, "--transpose");
    if (const auto v = args.option("rate")) {
        const double rate = toDouble(*v, "--rate");
        if (rate != std::floor(rate) || rate < 1000 || rate > 768000)
            throw UsageError("--rate wants a whole number of Hz from 1000 to 768000");
        settings.sampleRate = static_cast<int>(rate);
    }
    const auto rendersOption = args.option("renders");
    const std::filesystem::path rendersDir = rendersOption ? fromUtf8(*rendersOption) : audio::RenderStore::defaultDir();
    const auto target = args.positional();
    rejectLeftovers(args);
    if (!target) throw UsageError("render needs a file");

    const std::filesystem::path source = std::filesystem::absolute(fromUtf8(*target));
    Library lib(db);
    audio::SampleInfo info;
    std::string hash;
    if (const auto file = lib.fileByAbsolutePath(source)) {
        info = audio::sampleInfo(lib, file->id);
        hash = file->contentHash;
    }
    const audio::SyncPlan plan = audio::planSync(info, sync);
    if (plan.tempoUnsure) std::cerr << "asma: the tempo is unknown or a guess; left as it is\n";
    else if (sync.tempo && !plan.tempoSynced) std::cerr << "asma: not a loop; tempo left as it is\n";
    if (plan.keyUnsure) std::cerr << "asma: the key is a guess; not transposed\n";
    else if (sync.key && !plan.keySynced) std::cerr << "asma: no key known; not transposed\n";
    settings.ratio = plan.ratio;
    settings.semitones = plan.semitones + transpose;

    audio::RenderStore store(rendersDir);
    std::cout << toUtf8(store.fileFor(source, settings, hash)) << "\n";
    return kOk;
}

int cmdRenders(Args& args, Db&)
{
    const auto rendersOption = args.option("renders");
    audio::RenderStore store(rendersOption ? fromUtf8(*rendersOption) : audio::RenderStore::defaultDir());
    const auto action = args.positional();
    rejectLeftovers(args);
    if (action && *action == "clear") {
        std::cout << store.clear() << " renders removed\n";
        return kOk;
    }
    if (action) throw UsageError("renders takes nothing or clear");
    std::size_t count = 0;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(store.dir(), ec))
        if (e.path().extension() == ".wav") ++count;
    std::cout << count << " renders, " << (store.bytes() + (1u << 19)) / (1u << 20) << " MB\n";
    return kOk;
}

int cmdQuery(Args& args, Db& db)
{
    const bool json = args.flag("json");
    printRows(search(db, modelFromArgs(args, db)), json, {});
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
        using Command = int (*)(Args&, Db&);
        const std::pair<const char*, Command> commands[] = {
            {"root", cmdRoot},
            {"query", cmdQuery},
            {"similar", cmdSimilar},
            {"rate", cmdRate},
            {"fav", cmdFav},
            {"tag", cmdTag},
            {"collection", cmdCollection},
            {"search", cmdSearch},
            {"render", cmdRender},
            {"renders", cmdRenders},
        };
        if (*command == "scan") {
            Db db = Db::open(dbPath);
            return cmdScan(args, db, dbPath);
        }
        for (const auto& [name, run] : commands) {
            if (*command != name) continue;
            Db db = Db::open(dbPath);
            return run(args, db);
        }
        throw UsageError("unknown command: " + *command);
    } catch (const UsageError& e) {
        std::cerr << "asma: " << e.what() << "\n" << kUsageText;
        return kUsage;
    } catch (const std::exception& e) {
        std::cerr << "asma: " << e.what() << "\n";
        return kError;
    }
}
