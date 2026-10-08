// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/audio/SampleSource.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/WriterLock.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#endif

using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

struct RunResult {
    int exitCode = -1;
    std::string out;
};

// UTF-8 text of a path with native separators, in double quotes.
std::string quote(const fs::path& p)
{
    const std::u8string u8 = fs::path(p).make_preferred().u8string();
    return "\"" + std::string(reinterpret_cast<const char*>(u8.data()), u8.size()) + "\"";
}

int systemUtf8(const std::string& command)
{
#ifdef _WIN32
    // cmd.exe strips one pair of outer quotes, and the narrow system() would
    // mangle non-ASCII text, so wrap the command and go through UTF-16.
    const std::string wrapped = "\"" + command + "\"";
    const int size = MultiByteToWideChar(CP_UTF8, 0, wrapped.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, wrapped.c_str(), -1, wide.data(), size);
    return _wsystem(wide.c_str());
#else
    const int status = std::system(command.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

// Runs an executable with arguments, capturing stdout.
RunResult run(const char* exe, const std::string& arguments, const fs::path& scratch)
{
    const fs::path outFile = scratch / "stdout.txt";
    RunResult r;
    r.exitCode = systemUtf8(quote(asma::fromUtf8(exe)) + " " + arguments + " > " + quote(outFile));
    std::ifstream in(outFile); // text mode: the CLI writes \r\n on Windows, tests expect \n
    std::stringstream ss;
    ss << in.rdbuf();
    r.out = ss.str();
    return r;
}

struct Cli {
    TempDir dir;
    fs::path lib = dir.path() / asma::fromUtf8("Café Samples");
    fs::path db = dir.path() / "data" / "library.db";

    Cli()
    {
        asma::test::WavSpec loop;
        loop.seed = 1;
        asma::test::writeWav(lib / "Loops" / "Bass_Loop_Am_128.wav", loop);
        asma::test::WavSpec kick;
        kick.seed = 2;
        asma::test::writeWav(lib / "Drums" / asma::fromUtf8("Kick Ü_01.wav"), kick);
    }

    RunResult runAsma(const std::string& args) { return run(ASMA_CLI_PATH, "--db " + quote(db) + " " + args, dir.path()); }
    RunResult runScan(const std::string& args)
    {
        return run(ASMA_SCAN_PATH, "--db " + quote(db) + " " + args, dir.path());
    }
};

} // namespace

TEST_CASE("asma --version", "[e2e]")
{
    TempDir dir;
    const RunResult r = run(ASMA_CLI_PATH, "--version", dir.path());
    CHECK(r.exitCode == 0);
    CHECK(r.out.find(std::string("asma ") + ASMA_VERSION) != std::string::npos);
}

TEST_CASE("root add, scan and query from the CLI, with non-ASCII paths", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    const RunResult list = cli.runAsma("root list");
    CHECK(list.out.find("Café Samples") != std::string::npos);

    REQUIRE(cli.runAsma("scan").exitCode == 0);

    const RunResult kick = cli.runAsma("query kick");
    CHECK(kick.exitCode == 0);
    CHECK(kick.out.find("Kick Ü_01.wav") != std::string::npos);
    CHECK(kick.out.find("Bass_Loop") == std::string::npos);

    const RunResult loops = cli.runAsma("query --type loop --key Am --json");
    CHECK(loops.out.find("\"bpm\":128") != std::string::npos);
    CHECK(loops.out.find("\"key\":\"Am\"") != std::string::npos);
}

TEST_CASE("asma-scan speaks JSON lines and honours --fail", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    const RunResult r = cli.runScan("--root 1 --fail " + quote(asma::fromUtf8("Drums/Kick Ü_01.wav")));
    CHECK(r.exitCode == 0);
    CHECK(r.out.find("{\"event\":\"marked_failed\"") != std::string::npos);
    CHECK(r.out.find("{\"event\":\"start\",\"path\":\"Loops/Bass_Loop_Am_128.wav\"}") != std::string::npos);
    CHECK(r.out.find("{\"event\":\"progress\",\"done\":1,\"total\":1") != std::string::npos);
    CHECK(r.out.find("{\"event\":\"done\",\"added\":1") != std::string::npos);

    CHECK(cli.runAsma("query kick").out.find("Kick") == std::string::npos);
}

TEST_CASE("a second writer is refused with exit code 3", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    auto lock = asma::WriterLock::tryAcquire(cli.db.parent_path());
    REQUIRE(lock.has_value());
    CHECK(cli.runAsma("scan").exitCode == 3);
    const RunResult worker = cli.runScan("--root 1");
    CHECK(worker.exitCode == 3);
    CHECK(worker.out.find("\"code\":\"locked\"") != std::string::npos);
    lock.reset();
    CHECK(cli.runAsma("query").out.empty()); // nothing was scanned while locked
}

TEST_CASE("bad usage exits with 2", "[e2e]")
{
    Cli cli;
    CHECK(cli.runAsma("frobnicate").exitCode == 2);
    CHECK(cli.runAsma("query --bogus").exitCode == 2);
    CHECK(cli.runAsma("query --key H").exitCode == 2);
    CHECK(cli.runAsma("query --key C").exitCode == 0); // a plain major key, as asma prints it
    CHECK(cli.runScan("").exitCode == 2);
}

TEST_CASE("scan analyses by default and --no-analysis skips it", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    const RunResult quick = cli.runAsma("scan --no-analysis");
    CHECK(quick.out.find("analysis:") == std::string::npos);
    const RunResult full = cli.runAsma("scan");
    CHECK(full.out.find("analysis: analysed 2, failed 0, skipped 0") != std::string::npos);
}

TEST_CASE("similar lists other files with a similarity score", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan").exitCode == 0);
    const RunResult r = cli.runAsma("similar " + quote(cli.lib / "Loops" / "Bass_Loop_Am_128.wav") + " --json");
    CHECK(r.exitCode == 0);
    CHECK(r.out.find("Kick") != std::string::npos);
    CHECK(r.out.find("\"similarity\":") != std::string::npos);
    CHECK(r.out.find("Bass_Loop") == std::string::npos);

    CHECK(cli.runAsma("similar " + quote(cli.lib / "nope.wav")).exitCode == 1);
    CHECK(cli.runAsma("similar").exitCode == 2);
}

TEST_CASE("asma-scan reports analysis and honours --fail-analysis", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runScan("--root 1 --no-analysis").exitCode == 0);
    const RunResult r =
        cli.runScan("--root 1 --fail-analysis " + quote(asma::fromUtf8("Loops/Bass_Loop_Am_128.wav")));
    CHECK(r.exitCode == 0);
    CHECK(r.out.find("{\"event\":\"marked_analysis_failed\",\"path\":\"Loops/Bass_Loop_Am_128.wav\"}") !=
          std::string::npos);
    CHECK(r.out.find("{\"event\":\"analyse_start\",\"path\":\"Drums/Kick") != std::string::npos);
    CHECK(r.out.find("{\"event\":\"analyse_done\",\"analysed\":1,\"failed\":0,\"skipped\":0}") != std::string::npos);
}

TEST_CASE("rate, fav and tag files by path or id, then filter on them", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
    const auto loop = quote(cli.lib / "Loops" / "Bass_Loop_Am_128.wav");
    const auto kick = quote(cli.lib / "Drums" / asma::fromUtf8("Kick Ü_01.wav"));

    const RunResult rate = cli.runAsma("rate 4 " + loop + " " + kick);
    CHECK(rate.exitCode == 0);
    CHECK(rate.out.empty());
    const std::string kickRow = cli.runAsma("query kick --json").out; // {"id":N,...
    const std::string kickId = kickRow.substr(6, kickRow.find(',') - 6);
    CHECK(cli.runAsma("rate --id " + kickId + " 5").exitCode == 0); // --id before the value still works
    CHECK(cli.runAsma("fav on " + loop).exitCode == 0);
    CHECK(cli.runAsma("tag add Dusty " + kick).exitCode == 0);

    const RunResult top = cli.runAsma("query --min-rating 5 --json");
    CHECK(top.out.find("\"rating\":5,\"favourite\":false") != std::string::npos);
    CHECK(top.out.find("Bass_Loop") == std::string::npos);
    CHECK(cli.runAsma("query --favourites").out.find("Bass_Loop") != std::string::npos);
    CHECK(cli.runAsma("query dusty").out.find("Kick") != std::string::npos);
    const RunResult byRating = cli.runAsma("query --sort rating --desc");
    CHECK(byRating.out.find("Kick") < byRating.out.find("Bass_Loop"));

    CHECK(cli.runAsma("tag remove dusty " + kick).exitCode == 0);
    CHECK(cli.runAsma("query dusty").out.empty());
    CHECK(cli.runAsma("rate 0 --id 1 --id 2").exitCode == 0);
    CHECK(cli.runAsma("query --min-rating 1").out.empty());
}

TEST_CASE("a rating waits for a scan batch that is committing", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);

    // Stands in for asma-scan holding the write lock while it commits a batch.
    asma::Db scanner = asma::Db::open(cli.db);
    auto batch = std::make_unique<asma::Transaction>(scanner);
    std::thread commitLater([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        batch->commit();
    });
    const auto started = std::chrono::steady_clock::now();
    const RunResult rate = cli.runAsma("rate 3 --id 1");
    const auto waited = std::chrono::steady_clock::now() - started;
    commitLater.join();
    CHECK(rate.exitCode == 0);
    CHECK(waited >= std::chrono::milliseconds(400)); // it waited instead of failing
    CHECK_FALSE(cli.runAsma("query --min-rating 3").out.empty());
}

TEST_CASE("organise commands refuse bad input without writing anything", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
    const auto loop = quote(cli.lib / "Loops" / "Bass_Loop_Am_128.wav");

    CHECK(cli.runAsma("rate 6 " + loop).exitCode == 1);
    CHECK(cli.runAsma("rate 2.5 " + loop).exitCode == 2);
    CHECK(cli.runAsma("rate 3").exitCode == 2);
    CHECK(cli.runAsma("fav maybe " + loop).exitCode == 2);
    // One unknown file fails the whole batch; the known one is not rated.
    CHECK(cli.runAsma("rate 3 " + loop + " " + quote(cli.lib / "nope.wav")).exitCode == 1);
    CHECK(cli.runAsma("rate 3 --id 1 --id 999").exitCode == 1);
    CHECK(cli.runAsma("query --min-rating 1").out.empty());
    CHECK(cli.runAsma("query --min-rating 7").exitCode == 2);
    CHECK(cli.runAsma("query --collection Nope").exitCode == 2);
    CHECK(cli.runAsma("query --saved Nope").exitCode == 2);
}

TEST_CASE("collections and saved searches from the CLI", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
    const auto loop = quote(cli.lib / "Loops" / "Bass_Loop_Am_128.wav");

    const RunResult created = cli.runAsma("collection create " + quote(asma::fromUtf8("Set Ü")));
    CHECK(created.exitCode == 0);
    CHECK(created.out == "1\n");
    CHECK(cli.runAsma("collection create " + quote(asma::fromUtf8("SET Ü"))).exitCode == 1);
    CHECK(cli.runAsma("collection add " + quote(asma::fromUtf8("Set Ü")) + " " + loop).exitCode == 0);
    CHECK(cli.runAsma("collection list").out.find("Set Ü\t1") != std::string::npos);
    CHECK(cli.runAsma("query --collection " + quote(asma::fromUtf8("set Ü"))).out.find("Bass_Loop") !=
          std::string::npos);
    CHECK(cli.runAsma("collection list --id 1").exitCode == 2);

    CHECK(cli.runAsma("search save Loops --type loop --key Am").exitCode == 0);
    CHECK(cli.runAsma("search save --type loop Loops").exitCode == 2);
    const RunResult list = cli.runAsma("search list");
    CHECK(list.out.find("Loops\t{\"v\":1,\"type\":\"loop\",\"keys\":[\"Am\"]}") != std::string::npos);
    const RunResult saved = cli.runAsma("query --saved loops");
    CHECK(saved.out.find("Bass_Loop") != std::string::npos);
    CHECK(saved.out.find("Kick") == std::string::npos);
    // Options after --saved change the saved model for this query only.
    CHECK(cli.runAsma("query --saved Loops --type oneshot").out.empty());

    CHECK(cli.runAsma("search delete Loops").exitCode == 0);
    CHECK(cli.runAsma("search delete Loops").exitCode == 1);
    CHECK(cli.runAsma("collection rename " + quote(asma::fromUtf8("Set Ü")) + " Keepers").exitCode == 0);
    CHECK(cli.runAsma("collection delete Keepers").exitCode == 0);
    CHECK(cli.runAsma("query").out.find("Bass_Loop") != std::string::npos); // files stay
}

TEST_CASE("search save takes the app's JSON, and saved searches rename", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);

#ifdef _WIN32
    // CommandLineToArgvW reads \" inside quotes as a quote; cmd.exe leaves
    // it alone, since JSON holds none of its special characters.
    const std::string json = "\"{\\\"v\\\":1,\\\"sort\\\":\\\"bpm\\\",\\\"desc\\\":true}\"";
#else
    const std::string json = "'{\"v\":1,\"sort\":\"bpm\",\"desc\":true}'";
#endif
    CHECK(cli.runAsma("search save Fast --json " + json).exitCode == 0);
    CHECK(cli.runAsma("search list").out.find("Fast\t{\"v\":1,\"sort\":\"bpm\",\"desc\":true}") != std::string::npos);
    CHECK(cli.runAsma("search save Bad --json nope").exitCode == 2);
    CHECK(cli.runAsma("search save Kicks kick").exitCode == 0);
    CHECK(cli.runAsma("search rename Fast Faster").exitCode == 0);
    CHECK(cli.runAsma("search list").out.find("Faster\t") != std::string::npos);
    CHECK(cli.runAsma("search rename Faster kicks").exitCode == 1);
    CHECK(cli.runAsma("search rename Nope Other").exitCode == 1);
}

TEST_CASE("asked to, asma says what went wrong on stdout", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
    const RunResult r = cli.runAsma("--errors-to-stdout collection rename Nope Other");
    CHECK(r.exitCode == 1);
    CHECK(r.out == "error: no collection called 'Nope'\n");
    const RunResult usage = cli.runAsma("--errors-to-stdout rate");
    CHECK(usage.exitCode == 2);
    CHECK(usage.out.rfind("error: missing rating", 0) == 0);
}

TEST_CASE("retry reads a fixed file again and analyses it", "[e2e][retry]")
{
    Cli cli;
    asma::test::writeBytes(cli.lib / "Drums" / "broken.wav", "not audio");
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan").exitCode == 0);
    const auto broken = quote(cli.lib / "Drums" / "broken.wav");
    const RunResult still = cli.runAsma("retry " + broken);
    CHECK(still.exitCode == 0);
    CHECK(still.out == "retried 1: readable 0, failed 1, gone 0; analysed 0, failed 0\n");

    asma::test::WavSpec spec;
    spec.seed = 9;
    asma::test::writeWav(cli.lib / "Drums" / "broken.wav", spec);
    const RunResult fixed = cli.runAsma("retry " + broken);
    CHECK(fixed.exitCode == 0);
    CHECK(fixed.out == "retried 1: readable 1, failed 0, gone 0; analysed 1, failed 0\n");
    CHECK(cli.runAsma("query broken").out.find("broken.wav") != std::string::npos);
}

TEST_CASE("retry waits while a scan holds the library", "[e2e][retry]")
{
    Cli cli;
    asma::test::writeBytes(cli.lib / "Drums" / "broken.wav", "not audio");
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);

    auto lock = std::make_unique<std::optional<asma::WriterLock>>(asma::WriterLock::tryAcquire(cli.db.parent_path()));
    REQUIRE(lock->has_value());
    std::thread release([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        lock.reset();
    });
    const auto started = std::chrono::steady_clock::now();
    const RunResult r = cli.runAsma("retry " + quote(cli.lib / "Drums" / "broken.wav"));
    const auto waited = std::chrono::steady_clock::now() - started;
    release.join();
    CHECK(r.exitCode == 0);
    CHECK(r.out.rfind("waiting for the scan\n", 0) == 0);
    CHECK(waited >= std::chrono::milliseconds(500));
}

TEST_CASE("asma render prints the file to drag", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
    const auto loop = cli.lib / "Loops" / "Bass_Loop_Am_128.wav";
    const auto kick = cli.lib / "Drums" / asma::fromUtf8("Kick Ü_01.wav");
    const std::string cache = " --renders " + quote(cli.dir.path() / "renders");
    const auto printed = [](const RunResult& r) { return asma::fromUtf8(r.out.substr(0, r.out.find('\n'))); };

    // The name says 128 and loop: at 100 it stretches by 100/128.
    const RunResult synced = cli.runAsma("render " + quote(loop) + " --tempo 100" + cache);
    REQUIRE(synced.exitCode == 0);
    const auto rendered = printed(synced);
    CHECK(rendered.parent_path() == cli.dir.path() / "renders");
    CHECK(asma::audio::loadAudio(rendered).frames() == 5645); // 4410 * 128 / 100

    // A one-shot keeps its tempo: nothing to render, the original is dragged.
    const RunResult oneShot = cli.runAsma("render " + quote(kick) + " --tempo 100" + cache);
    CHECK(oneShot.exitCode == 0);
    CHECK(printed(oneShot) == kick);

    const RunResult reversed = cli.runAsma("render " + quote(kick) + " --reverse" + cache);
    CHECK(reversed.exitCode == 0);
    CHECK(printed(reversed) != kick);

    CHECK(cli.runAsma("render " + quote(kick) + " --reverse --ping-pong").exitCode == 2);
    CHECK(cli.runAsma("render " + quote(kick) + " --key Q").exitCode == 2);
    CHECK(cli.runAsma("render " + quote(kick) + " --key C" + cache).exitCode == 0); // a plain major key
    CHECK(cli.runAsma("render " + quote(cli.lib / "gone.wav") + " --reverse" + cache).exitCode == 1);
    CHECK(cli.runAsma("render " + quote(kick) + " --rate 10" + cache).exitCode == 2);
    CHECK(cli.runAsma("render " + quote(kick) + " --rate 44100.5" + cache).exitCode == 2);
    CHECK(cli.runAsma("render " + quote(kick) + " --trim-start 1 --reverse" + cache).exitCode == 1); // 0.1 s file

    // Two renders so far: the stretched loop and the reversed kick.
    const RunResult size = cli.runAsma("renders" + cache);
    CHECK(size.exitCode == 0);
    CHECK(size.out.rfind("2 renders, ", 0) == 0);
    CHECK(cli.runAsma("renders clear" + cache).out == "2 renders removed\n");
    CHECK(cli.runAsma("renders" + cache).out == "0 renders, 0 MB\n");
    CHECK(cli.runAsma("renders shrink" + cache).exitCode == 2);
}

namespace {

// Overwrites a closed library's pages after the first, as a disk fault would.
void damage(const fs::path& db)
{
    std::fstream f(db, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(4096);
    const std::string junk(4096 * 3, '\xA5');
    f.write(junk.data(), static_cast<std::streamsize>(junk.size()));
}

} // namespace

TEST_CASE("check, backup, restore and repair from the CLI", "[e2e][safety]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
    const auto loop = quote(cli.lib / "Loops" / "Bass_Loop_Am_128.wav");
    REQUIRE(cli.runAsma("rate 4 " + loop).exitCode == 0);

    CHECK(cli.runAsma("check").out == "{\"health\":\"ok\"}\n");
    const RunResult backup = cli.runAsma("backup");
    CHECK(backup.exitCode == 0);
    const fs::path data = cli.db.parent_path();
    CHECK(fs::exists(data / "backup.json"));
    CHECK(cli.runAsma("backup").exitCode == 0);
    CHECK(fs::exists(data / "backup-previous.json"));
    CHECK(cli.runAsma("backup --out " + quote(cli.dir.path() / "mine.json")).exitCode == 0);
    CHECK(fs::exists(cli.dir.path() / "mine.json"));

    CHECK(cli.runAsma("repair").out == "{\"result\":\"healthy\"}\n"); // a sound library stays

    damage(cli.db);
    CHECK(cli.runAsma("check").out.rfind("{\"health\":\"damaged\"", 0) == 0);
    const RunResult repaired = cli.runAsma("repair");
    CHECK(repaired.exitCode == 0);
    CHECK(repaired.out.rfind("{\"result\":\"repaired\"", 0) == 0);
    CHECK(repaired.out.find("\"files\":1,\"unmatched\":0") != std::string::npos);
    CHECK(fs::exists(data / "library.db.corrupt"));
    CHECK(cli.runAsma("query --min-rating 4").out.find("Bass_Loop") != std::string::npos);

    REQUIRE(cli.runAsma("rate 0 " + loop).exitCode == 0);
    const RunResult restored = cli.runAsma("restore " + quote(cli.dir.path() / "mine.json"));
    CHECK(restored.exitCode == 0);
    CHECK(restored.out == "restored 1 organised samples, 0 not found, 0 collections, 0 saved searches\n");
    CHECK(cli.runAsma("query --min-rating 4").out.find("Bass_Loop") != std::string::npos);
    CHECK(cli.runAsma("restore " + quote(cli.dir.path() / "nothing.json")).exitCode == 1);
}

TEST_CASE("two repairs at once on a damaged library: one rebuilds, the other waits its turn", "[e2e][safety]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
    REQUIRE(cli.runAsma("backup").exitCode == 0);
    damage(cli.db);
    // The second finds the lock taken, or, coming after, a sound library.
    auto lock = std::make_unique<std::optional<asma::WriterLock>>(asma::WriterLock::tryAcquire(cli.db.parent_path()));
    REQUIRE(lock->has_value());
    const RunResult refused = cli.runAsma("repair");
    CHECK(refused.exitCode == 3);
    CHECK(refused.out == "{\"result\":\"locked\"}\n");
    CHECK_FALSE(fs::exists(cli.db.parent_path() / "library.db.corrupt"));
    lock.reset();
    CHECK(cli.runAsma("repair").out.rfind("{\"result\":\"repaired\"", 0) == 0);
    CHECK(cli.runAsma("repair").out == "{\"result\":\"healthy\"}\n");
}
