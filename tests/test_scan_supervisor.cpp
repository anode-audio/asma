// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/ScanSupervisor.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <thread>

using namespace asma;
using asma::test::ScopedEnv;
using asma::test::TempDir;
using Result = ScanReport::Result;
using Strings = std::vector<std::string>;
namespace fs = std::filesystem;

namespace {

// Runs the fake worker in the test child with a script; the log records the
// arguments of every run.
struct FakeScan {
    TempDir dir;
    fs::path log = dir.path() / "runs.txt";
    ScopedEnv logEnv{"ASMA_FAKE_SCAN_LOG", toUtf8(log).c_str()};
    ScopedEnv script;
    ScanSupervisor supervisor;

    explicit FakeScan(const char* text) : script("ASMA_FAKE_SCAN", text) {}

    ScanReport run(unsigned threads = 4, bool analyse = true, ScanSupervisor::Listener listener = {})
    {
        ScanRequest request;
        request.worker = fromUtf8(ASMA_TEST_CHILD_PATH);
        request.db = dir.path() / "library.db";
        request.rootId = 1;
        request.threads = threads;
        request.analyse = analyse;
        return supervisor.run(request, listener);
    }

    Strings runs() const
    {
        Strings lines;
        std::ifstream in(log);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line.substr(line.find("--root 1")));
        return lines;
    }
};

} // namespace

TEST_CASE("scanArguments lays out one attempt", "[supervisor]")
{
    ScanRequest request;
    request.db = fromUtf8("/data/library.db");
    request.rootId = 3;
    request.analyse = false;
    ScanAttempt attempt;
    attempt.threads = 1;
    attempt.fail = {"a b.wav"};
    attempt.failAnalysis = {"c.wav"};
    CHECK(scanArguments(request, attempt) == Strings{"--db", toUtf8(request.db), "--root", "3", "--threads", "1",
                                                     "--no-analysis", "--fail", "a b.wav", "--fail-analysis",
                                                     "c.wav"});
    CHECK(scanArguments(request, {}) == Strings{"--db", toUtf8(request.db), "--root", "3", "--no-analysis"});
}

TEST_CASE("a clean run finishes in one go and passes every event on", "[supervisor]")
{
    FakeScan fake("files=a,b,c");
    int events = 0;
    const ScanReport report = fake.run(4, true, [&](const ScanEvent&) { ++events; });
    CHECK(report.result == Result::Finished);
    CHECK(report.runs == 1);
    CHECK(report.index.added == 3);
    CHECK(report.analysis.analysed == 3);
    CHECK(report.culprits.empty());
    CHECK(events == 3 * 2 * 2 + 2); // start and progress per file per phase, plus two summaries
}

TEST_CASE("an indexing crash is pinned on its file and the scan completes", "[supervisor]")
{
    FakeScan fake("files=a,b,c,d,e;crash_index=c");
    const ScanReport report = fake.run();
    CHECK(report.result == Result::Finished);
    REQUIRE(report.culprits.size() == 1);
    CHECK(report.culprits[0] == std::make_pair(ScanPhase::Index, std::string("c")));
    CHECK(report.index.added == 4);
    CHECK(report.index.failed == 1);
    CHECK(fake.runs() == Strings{"--root 1 --threads 4", "--root 1 --threads 1", "--root 1 --threads 4 --fail c"});
}

TEST_CASE("an analysis crash is pinned with --fail-analysis", "[supervisor]")
{
    FakeScan fake("files=a,b;crash_analyse=b");
    const ScanReport report = fake.run(2);
    CHECK(report.result == Result::Finished);
    CHECK(report.culprits == std::vector<std::pair<ScanPhase, std::string>>{{ScanPhase::Analyse, "b"}});
    CHECK(report.analysis.analysed == 1);
    CHECK(fake.runs().back() == "--root 1 --threads 2 --fail-analysis b");
}

TEST_CASE("two bad files in one root are both found", "[supervisor]")
{
    FakeScan fake("files=a,b,c;crash_index=a;crash_analyse=c");
    const ScanReport report = fake.run(1);
    CHECK(report.result == Result::Finished);
    CHECK(report.culprits.size() == 2);
    CHECK(fake.runs().back() == "--root 1 --threads 1 --fail a --fail-analysis c");
}

TEST_CASE("a worker that crashes before any file gives up", "[supervisor]")
{
    FakeScan fake("crash_start=1");
    const ScanReport report = fake.run();
    CHECK(report.result == Result::Crashed);
    CHECK(report.runs == 3); // four threads, then one thread twice
}

TEST_CASE("locked and failed are reported, not retried", "[supervisor]")
{
    FakeScan locked("error=locked");
    const ScanReport a = locked.run();
    CHECK(a.result == Result::Locked);
    CHECK(a.lockHolder == 4242);
    CHECK(a.runs == 1);

    FakeScan failed("error=failed");
    const ScanReport b = failed.run();
    CHECK(b.result == Result::Failed);
    CHECK(b.message == "disk full");
}

TEST_CASE("a missing worker is a failure, not a crash loop", "[supervisor]")
{
    ScanSupervisor supervisor;
    ScanRequest request;
    request.worker = fromUtf8(ASMA_TEST_CHILD_PATH).parent_path() / "no-such-worker";
    const ScanReport report = supervisor.run(request);
    CHECK(report.result == Result::Failed);
    CHECK(report.runs == 0);
    CHECK_FALSE(report.message.empty());
}

TEST_CASE("cancel stops a hanging worker promptly", "[supervisor]")
{
    FakeScan fake("files=a;hang=1");
    const auto started = std::chrono::steady_clock::now();
    std::thread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        fake.supervisor.cancel();
    });
    const ScanReport report = fake.run();
    canceller.join();
    CHECK(report.result == Result::Cancelled);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));

    // The supervisor can run again afterwards.
    FakeScan again("files=a");
    CHECK(again.run().result == Result::Finished);
}

TEST_CASE("a cancel while no scan runs does not cancel the next one", "[supervisor]")
{
    // A Cancel pressed just as a scan ends must not kill the user's next scan.
    FakeScan fake("files=a");
    CHECK(fake.run().result == Result::Finished);
    fake.supervisor.cancel();
    CHECK(fake.run().result == Result::Finished);
}

TEST_CASE("supervising the real asma-scan", "[supervisor][e2e]")
{
    TempDir dir;
    const fs::path lib = dir.path() / fromUtf8("Café");
    test::WavSpec spec;
    test::writeWav(lib / "Kick_01.wav", spec);
    spec.seed = 2;
    test::writeWav(lib / "Loops" / "Bass_Loop_Am_128.wav", spec);
    const fs::path dbPath = dir.path() / "data" / "library.db";
    std::int64_t rootId = 0;
    {
        Db db = Db::open(dbPath);
        rootId = Library(db).addRoot(lib);
    }

    ScanSupervisor supervisor;
    ScanRequest request;
    request.worker = fromUtf8(ASMA_SCAN_PATH);
    request.db = dbPath;
    request.rootId = rootId;
    const ScanReport report = supervisor.run(request);
    CHECK(report.result == Result::Finished);
    CHECK(report.runs == 1);
    CHECK(report.index.added == 2);
    CHECK(report.analysis.analysed == 2);
}
