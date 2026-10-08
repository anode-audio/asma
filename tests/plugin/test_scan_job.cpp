// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryFixture.h"
#include "LibraryView.h"
#include "PluginTestUtil.h"
#include "ScanJob.h"
#include "asma/core/Library.h"

#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace asma;
using app::ScanJob;
namespace fs = std::filesystem;

namespace {

// Waits for the job to finish and returns its report.
std::optional<ScanReport> finish(ScanJob& job)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (job.busy() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return job.takeReport();
}

void writeSamples(const fs::path& folder)
{
    test::writeWavFloat(folder / "Loops" / "Bass_Loop_Am_120.wav", 48000, {test::sine(110.0, 1.0, 0.4, 48000)});
    test::writeWavFloat(folder / "Kick_01.wav", 48000, {test::kickHit(48000)});
}

} // namespace

TEST_CASE("ScanJob adds a folder and scans it into a new library", "[scanjob]")
{
    test::LibraryFixture f; // nothing scanned: no library file yet
    writeSamples(f.lib);
    ScanJob job(f.dbPath, ASMA_SCAN_PATH);
    CHECK_FALSE(job.busy());
    REQUIRE(job.addAndScan(f.lib));
    CHECK_FALSE(job.addAndScan(f.lib)); // one scan at a time
    const auto report = finish(job);
    REQUIRE(report);
    CHECK(report->result == ScanReport::Result::Finished);
    CHECK(report->index.added == 2);
    CHECK_FALSE(job.takeReport()); // reported once
    CHECK(job.progress().empty()); // nothing running

    app::LibraryView view(f.dbPath);
    REQUIRE(view.refresh() == app::LibraryState::Open);
    CHECK(view.search({}).size() == 2);
    CHECK(view.info(view.search({})[0].id).lufs); // analysed too
}

TEST_CASE("ScanJob reports a missing worker instead of hanging", "[scanjob]")
{
    test::LibraryFixture f;
    writeSamples(f.lib);
    ScanJob job(f.dbPath, f.dir.path() / "no-such-asma-scan");
    REQUIRE(job.addAndScan(f.lib));
    const auto report = finish(job);
    REQUIRE(report);
    CHECK(report->result == ScanReport::Result::Failed);
    CHECK_FALSE(report->message.empty());
}

TEST_CASE("ScanJob refuses a folder that is not there", "[scanjob]")
{
    test::LibraryFixture f;
    ScanJob job(f.dbPath, ASMA_SCAN_PATH);
    std::string why;
    CHECK_FALSE(job.addAndScan(f.dir.path() / "gone", &why));
    CHECK_FALSE(why.empty());
    CHECK_FALSE(job.busy());
}

TEST_CASE("the scanner is looked for next to the app", "[scanjob]")
{
    const fs::path app = fs::path("apps") / "asma";
#ifdef _WIN32
    CHECK(ScanJob::workerNextTo(app) == fs::path("apps") / "asma-scan.exe");
#else
    CHECK(ScanJob::workerNextTo(app) == fs::path("apps") / "asma-scan");
#endif
}

TEST_CASE("ScanJob scans a folder already in the library, saying which", "[scanjob]")
{
    test::LibraryFixture f;
    f.scan();
    test::writeWavFloat(f.lib / "Drums" / "Hat_03.wav", 48000, {test::hatHit(48000, 5)});
    std::int64_t root = 0;
    {
        Db db = Db::open(f.dbPath);
        root = Library(db).roots().front().id;
    }
    ScanJob job(f.dbPath, ASMA_SCAN_PATH);
    CHECK(job.ready());
    REQUIRE(job.start(root, "Samples"));
    CHECK(job.progress().rfind("Scanning Samples", 0) == 0);
    const auto report = finish(job);
    REQUIRE(report);
    CHECK(report->index.added == 1);
    CHECK_FALSE(ScanJob(f.dbPath, f.dir.path() / "no-such-asma-scan").ready());
}

TEST_CASE("progress counts read with thousands grouped", "[scanjob]")
{
    CHECK(app::groupDigits(7) == "7");
    CHECK(app::groupDigits(1200) == "1,200");
    CHECK(app::groupDigits(1234567) == "1,234,567");
}
