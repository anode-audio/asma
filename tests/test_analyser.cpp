// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "TestUtil.h"
#include "asma/core/Analyser.h"
#include "asma/core/Analysis.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

constexpr int kRate = 44100;

struct Fixture {
    TempDir dir;
    fs::path root = dir.path() / "lib";
    Db db = Db::openInMemory();
    Library lib{db};
    std::int64_t rootId = 0;

    Fixture()
    {
        fs::create_directories(root);
        rootId = lib.addRoot(root);
    }
    void write(const std::string& rel, const std::vector<float>& samples)
    {
        test::writeWavSamples(root / fromUtf8(rel), kRate, samples);
    }
    FileRecord file(const std::string& rel) { return lib.fileByPath(rootId, rel).value(); }
    int version(const std::string& rel)
    {
        auto q = db.prepare("SELECT analysis_version FROM files WHERE id = ?");
        q.bind(1, file(rel).id);
        q.step();
        return static_cast<int>(q.getInt(0));
    }
};

} // namespace

TEST_CASE("pending files are analysed once", "[analyser]")
{
    Fixture f;
    f.write("groove.wav", test::drumLoop(100.0, 2, kRate));
    f.write("kick.wav", test::kickHit(kRate));
    scanRoot(f.db, f.rootId);

    const AnalyseStats first = analysePending(f.db);
    CHECK(first.analysed == 2);
    CHECK(f.version("groove.wav") == kAnalysisVersion);
    const auto groove = f.lib.derived(f.file("groove.wav").id).value();
    REQUIRE(groove.bpm.has_value());
    CHECK(*groove.bpm == Catch::Approx(100.0).margin(0.01));
    CHECK(groove.bpmSource == FeatureSource::Analysis);
    CHECK(f.lib.derived(f.file("kick.wav").id)->isLoop == false);

    CHECK(analysePending(f.db).analysed == 0);
}

TEST_CASE("file-name values survive analysis", "[analyser]")
{
    Fixture f;
    f.write("Loops/Groove_Loop_90bpm.wav", test::drumLoop(120.0, 2, kRate)); // the name disagrees
    scanRoot(f.db, f.rootId);
    analysePending(f.db);
    const auto d = f.lib.derived(f.file("Loops/Groove_Loop_90bpm.wav").id).value();
    CHECK(d.bpm == 90.0);
    CHECK(d.bpmSource == FeatureSource::Filename);
}

TEST_CASE("undecodable files are recorded and not retried", "[analyser]")
{
    Fixture f;
    test::WavSpec spec;
    spec.formatTag = 0x55;
    test::writeWav(f.root / "odd.wav", spec);
    scanRoot(f.db, f.rootId);
    REQUIRE(f.file("odd.wav").status == FileStatus::Ok); // it probes fine

    const AnalyseStats s = analysePending(f.db);
    CHECK(s.failed == 1);
    CHECK(analysePending(f.db).failed == 0);
    auto q = f.db.prepare("SELECT analysis_error FROM files WHERE rel_path = 'odd.wav'");
    REQUIRE(q.step());
    CHECK_FALSE(q.getText(0).empty());
}

TEST_CASE("changed content is analysed again; moved files are not", "[analyser]")
{
    Fixture f;
    f.write("a.wav", test::drumLoop(100.0, 2, kRate));
    scanRoot(f.db, f.rootId);
    analysePending(f.db);

    fs::create_directories(f.root / "Moved");
    fs::rename(f.root / "a.wav", f.root / "Moved" / "a.wav");
    scanRoot(f.db, f.rootId);
    CHECK(analysePending(f.db).analysed == 0);
    CHECK(f.lib.derived(f.file("Moved/a.wav").id)->bpm.has_value());

    f.write("Moved/a.wav", test::drumLoop(140.0, 2, kRate));
    fs::last_write_time(f.root / "Moved" / "a.wav",
                        fs::last_write_time(f.root / "Moved" / "a.wav") + std::chrono::seconds(2));
    scanRoot(f.db, f.rootId);
    CHECK(analysePending(f.db).analysed == 1);
    CHECK(*f.lib.derived(f.file("Moved/a.wav").id)->bpm == Catch::Approx(140.0).margin(0.01));
}

#ifndef _WIN32
TEST_CASE("unreadable files are skipped and retried", "[analyser]")
{
    Fixture f;
    f.write("a.wav", test::kickHit(kRate));
    scanRoot(f.db, f.rootId);
    fs::permissions(f.root / "a.wav", fs::perms::none);
    const AnalyseStats s = analysePending(f.db);
    fs::permissions(f.root / "a.wav", fs::perms::owner_read | fs::perms::owner_write);
    CHECK(s.skipped == 1);
    CHECK(f.version("a.wav") == 0);
    CHECK(analysePending(f.db).analysed == 1);
}
#endif

TEST_CASE("markAnalysisFailed stops a file being analysed", "[analyser]")
{
    Fixture f;
    f.write("crashy.wav", test::kickHit(kRate));
    scanRoot(f.db, f.rootId);
    markAnalysisFailed(f.db, f.rootId, "crashy.wav", "crashed the analyser");
    markAnalysisFailed(f.db, f.rootId, "unknown.wav", "ignored");
    CHECK(analysePending(f.db).analysed == 0);
    CHECK(f.version("crashy.wav") == kAnalysisVersion);
}

TEST_CASE("rootId limits the run and callbacks cover every file", "[analyser]")
{
    Fixture f;
    const fs::path other = f.dir.path() / "other";
    fs::create_directories(other);
    const auto otherId = f.lib.addRoot(other);
    f.write("a.wav", test::kickHit(kRate));
    test::writeWavSamples(other / "b.wav", kRate, test::kickHit(kRate));
    scanRoot(f.db, f.rootId);
    scanRoot(f.db, otherId);

    std::size_t starts = 0;
    std::size_t lastTotal = 0;
    AnalyseOptions options;
    options.rootId = otherId;
    options.batchSize = 1;
    options.onFileStart = [&](std::string_view) { ++starts; };
    options.onProgress = [&](std::size_t, std::size_t total, std::string_view) { lastTotal = total; };
    CHECK(analysePending(f.db, options).analysed == 1);
    CHECK(starts == 1);
    CHECK(lastTotal == 1);
    CHECK(f.version("a.wav") == 0);
}

TEST_CASE("renaming away a file-name BPM hands the file back to analysis", "[analyser]")
{
    Fixture f;
    f.write("Groove_90bpm.wav", test::drumLoop(128.0, 2, kRate));
    scanRoot(f.db, f.rootId);
    analysePending(f.db);
    REQUIRE(f.lib.derived(f.file("Groove_90bpm.wav").id)->bpm == 90.0);

    fs::rename(f.root / "Groove_90bpm.wav", f.root / "Groove.wav");
    scanRoot(f.db, f.rootId);
    CHECK(analysePending(f.db).analysed == 1);
    const auto d = f.lib.derived(f.file("Groove.wav").id).value();
    REQUIRE(d.bpm.has_value());
    CHECK(*d.bpm == Catch::Approx(128.0).margin(0.01));
    CHECK(d.bpmSource == FeatureSource::Analysis);
    CHECK(d.isLoop == true);
}

TEST_CASE("fileIds limits the run to those files, and none to nothing", "[analyser][retry]")
{
    Fixture f;
    f.write("a.wav", test::kickHit(kRate));
    f.write("b.wav", test::kickHit(kRate));
    scanRoot(f.db, f.rootId);

    AnalyseOptions none;
    none.fileIds = std::vector<std::int64_t>{};
    CHECK(analysePending(f.db, none).analysed == 0);

    AnalyseOptions one;
    one.fileIds = std::vector<std::int64_t>{f.file("b.wav").id};
    CHECK(analysePending(f.db, one).analysed == 1);
    CHECK(f.version("a.wav") == 0);
    CHECK(f.version("b.wav") == kAnalysisVersion);
}

TEST_CASE("a retried file whose analysis failed is analysed again", "[analyser][retry]")
{
    Fixture f;
    f.write("a.wav", test::kickHit(kRate));
    scanRoot(f.db, f.rootId);
    markAnalysisFailed(f.db, f.rootId, "a.wav", "crashed the analyser");

    const RetryStats r = retryFiles(f.db, {f.file("a.wav").id});
    REQUIRE(r.readable.size() == 1);
    AnalyseOptions options;
    options.fileIds = r.readable;
    CHECK(analysePending(f.db, options).analysed == 1);
    CHECK(f.lib.problems().empty());
}
