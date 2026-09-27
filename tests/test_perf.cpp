// SPDX-License-Identifier: GPL-3.0-only
// Hidden: timings reported by CI, never a pass/fail gate beyond sanity.
// Run with ./build/tests/asma_tests "[.perf]" on a Release build.
#include "Signals.h"
#include "TestUtil.h"
#include "asma/core/Analyser.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>

using namespace asma;

TEST_CASE("scan and analyse a generated library", "[.perf]")
{
    constexpr int kRate = 44100;
    constexpr int kHits = 1500;
    constexpr int kLoops = 200;
    test::TempDir dir;
    const auto kick = test::kickHit(kRate);
    const auto loop = test::drumLoop(120.0, 2, kRate);
    for (int i = 0; i < kHits; ++i) {
        auto v = kick;
        v[0] = static_cast<float>(i) / kHits; // distinct content, distinct hash
        test::writeWavSamples(dir.path() / "Hits" / ("Kick_" + std::to_string(i) + ".wav"), kRate, v);
    }
    for (int i = 0; i < kLoops; ++i) {
        auto v = loop;
        v[0] = static_cast<float>(i) / kLoops;
        test::writeWavSamples(dir.path() / "Loops" / ("Beat_" + std::to_string(i) + ".wav"), kRate, v);
    }

    Db db = Db::open(dir.path() / "data" / "library.db");
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    const ScanStats scan = scanRoot(db, root);
    const double scanSeconds = std::chrono::duration<double>(Clock::now() - start).count();
    start = Clock::now();
    const AnalyseStats analysis = analysePending(db);
    const double analyseSeconds = std::chrono::duration<double>(Clock::now() - start).count();

    const double files = kHits + kLoops;
    WARN("scan took " << scanSeconds << " s: " << files / scanSeconds << " files/s");
    WARN("analysis took " << analyseSeconds << " s: " << files / analyseSeconds << " files/s");
    CHECK(scan.added == files);
    CHECK(analysis.analysed == files);
}
