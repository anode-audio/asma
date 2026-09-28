// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/ScanEvents.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using Kind = ScanEvent::Kind;
using Next = ScanRecovery::Next;
using Strings = std::vector<std::string>;

namespace {

void feed(ScanRecovery& r, std::initializer_list<const char*> lines)
{
    for (const char* line : lines) r.onEvent(parseScanEvent(line).value());
}

} // namespace

TEST_CASE("parseScanEvent reads every event of the protocol", "[scanevents]")
{
    const auto start = parseScanEvent(R"({"event":"start","path":"Drums/Kick Ü.wav"})").value();
    CHECK(start.kind == Kind::Start);
    CHECK(start.path == "Drums/Kick Ü.wav");

    const auto progress = parseScanEvent(R"({"event":"analyse_progress","done":3,"total":8,"path":"a.wav"})");
    CHECK(progress->kind == Kind::AnalyseProgress);
    CHECK(progress->done == 3);
    CHECK(progress->total == 8);

    const auto done = parseScanEvent(
        R"({"event":"done","added":1,"updated":2,"unchanged":3,"relinked":4,"missing":5,"failed":6,"skipped":7})");
    CHECK(done->index.added == 1);
    CHECK(done->index.relinked == 4);
    CHECK(done->index.skipped == 7);

    const auto analysed = parseScanEvent(R"({"event":"analyse_done","analysed":9,"failed":1,"skipped":2})");
    CHECK(analysed->analysis.analysed == 9);
    CHECK(analysed->analysis.skipped == 2);

    const auto locked = parseScanEvent(R"({"event":"error","code":"locked","pid":4242})");
    CHECK(locked->kind == Kind::Error);
    CHECK(locked->code == "locked");
    CHECK(locked->pid == 4242);

    CHECK(parseScanEvent(R"({"event":"marked_failed","path":"x"})")->kind == Kind::MarkedFailed);
    CHECK(parseScanEvent(R"({"event":"marked_analysis_failed","path":"x"})")->kind == Kind::MarkedAnalysisFailed);
    CHECK(parseScanEvent(R"({"event":"analyse_start","path":"x"})")->kind == Kind::AnalyseStart);
    CHECK(parseScanEvent(R"({"event":"progress","done":1,"total":1,"path":"x"})")->kind == Kind::Progress);
}

TEST_CASE("parseScanEvent ignores what is not an event", "[scanevents]")
{
    CHECK_FALSE(parseScanEvent(""));
    CHECK_FALSE(parseScanEvent("warning: something"));
    CHECK_FALSE(parseScanEvent(R"({"path":"x"})"));
    CHECK_FALSE(parseScanEvent(R"({"event":5})"));
    CHECK_FALSE(parseScanEvent(R"({"event":"start","path":"cut off)"));
    CHECK(parseScanEvent(R"({"event":"from_the_future","x":1})")->kind == Kind::Unknown);
    CHECK(parseScanEvent(R"({"event":"done","added":-5})")->index.added == 0);
}

TEST_CASE("a run that reports done and analyse_done is finished", "[scanevents]")
{
    ScanRecovery r(4, true);
    feed(r, {R"({"event":"start","path":"a"})", R"({"event":"progress","done":1,"total":1,"path":"a"})",
             R"({"event":"done"})", R"({"event":"analyse_done"})"});
    CHECK(r.onExit() == Next::Finished);

    ScanRecovery indexOnly(4, false);
    feed(indexOnly, {R"({"event":"done"})"});
    CHECK(indexOnly.onExit() == Next::Finished);
}

TEST_CASE("errors end the scan without a retry", "[scanevents]")
{
    ScanRecovery locked(0, true);
    feed(locked, {R"({"event":"error","code":"locked","pid":7})"});
    CHECK(locked.onExit() == Next::Locked);
    CHECK(locked.error()->pid == 7);

    ScanRecovery failed(0, true);
    feed(failed, {R"({"event":"error","code":"failed","message":"disk I/O error"})"});
    CHECK(failed.onExit() == Next::Failed);
    CHECK(failed.error()->message == "disk I/O error");
}

TEST_CASE("a parallel crash retries on one thread, then marks the file", "[scanevents]")
{
    ScanRecovery r(4, true);
    CHECK(r.attempt().threads == 4);
    feed(r, {R"({"event":"start","path":"a"})", R"({"event":"start","path":"b"})"});
    REQUIRE(r.onExit() == Next::Retry);
    CHECK(r.attempt().threads == 1);
    CHECK(r.attempt().fail.empty());

    feed(r, {R"({"event":"start","path":"a"})", R"({"event":"progress","done":1,"total":2,"path":"a"})",
             R"({"event":"start","path":"b"})"});
    REQUIRE(r.onExit() == Next::Retry);
    CHECK(r.attempt().threads == 4); // back to full speed once the culprit is known
    CHECK(r.attempt().fail == Strings{"b"});
    REQUIRE(r.culprits().size() == 1);
    CHECK(r.culprits()[0] == std::make_pair(ScanPhase::Index, std::string("b")));

    feed(r, {R"({"event":"marked_failed","path":"b"})", R"({"event":"done"})", R"({"event":"analyse_done"})"});
    CHECK(r.onExit() == Next::Finished);
}

TEST_CASE("an analysis crash marks the file with --fail-analysis", "[scanevents]")
{
    ScanRecovery r(1, true);
    feed(r, {R"({"event":"done"})", R"({"event":"analyse_start","path":"x.wav"})"});
    REQUIRE(r.onExit() == Next::Retry);
    CHECK(r.attempt().fail.empty());
    CHECK(r.attempt().failAnalysis == Strings{"x.wav"});
    CHECK(r.culprits()[0].first == ScanPhase::Analyse);
}

TEST_CASE("the same path in both phases is tracked separately", "[scanevents]")
{
    ScanRecovery r(1, true);
    // Indexed fine, then crashed while analysing the same file.
    feed(r, {R"({"event":"start","path":"x"})", R"({"event":"progress","done":1,"total":1,"path":"x"})",
             R"({"event":"done"})", R"({"event":"analyse_start","path":"x"})"});
    REQUIRE(r.onExit() == Next::Retry);
    CHECK(r.attempt().failAnalysis == Strings{"x"});
    CHECK(r.attempt().fail.empty());
}

TEST_CASE("crashes no file explains give up after two tries on one thread", "[scanevents]")
{
    ScanRecovery r(4, true);
    CHECK(r.onExit() == Next::Retry); // 4 threads, nothing in flight: try one thread
    CHECK(r.attempt().threads == 1);
    CHECK(r.onExit() == Next::Retry);
    CHECK(r.onExit() == Next::Crashed);
}

TEST_CASE("a file that crashes the worker again after being marked gives up", "[scanevents]")
{
    ScanRecovery r(1, true);
    feed(r, {R"({"event":"start","path":"evil"})"});
    REQUIRE(r.onExit() == Next::Retry);
    feed(r, {R"({"event":"start","path":"evil"})"});
    CHECK(r.onExit() == Next::Crashed);
}

TEST_CASE("unknown events and progress for unstarted files are harmless", "[scanevents]")
{
    ScanRecovery r(1, false);
    feed(r, {R"({"event":"from_the_future"})", R"({"event":"progress","done":1,"total":1,"path":"never-started"})",
             R"({"event":"done"})"});
    CHECK(r.onExit() == Next::Finished);
}
