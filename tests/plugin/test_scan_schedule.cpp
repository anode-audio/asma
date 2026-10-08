// SPDX-License-Identifier: GPL-3.0-only
#include "ScanSchedule.h"

#include <catch2/catch_test_macros.hpp>

using asma::app::ScanSchedule;
using namespace std::chrono_literals;

namespace {

const ScanSchedule::Clock::time_point t0{};

// Every folder next() gives at `now`, as a scan would take them in turn.
std::vector<std::int64_t> drain(ScanSchedule& s, ScanSchedule::Clock::time_point now)
{
    std::vector<std::int64_t> out;
    while (const auto id = s.next(now)) {
        out.push_back(*id);
        s.scanned(*id, now);
    }
    return out;
}

} // namespace

TEST_CASE("every folder is scanned once at startup, then every 15 minutes", "[schedule]")
{
    ScanSchedule s;
    s.setFolders({1, 2}, t0);
    CHECK(drain(s, t0) == std::vector<std::int64_t>{1, 2});
    CHECK(drain(s, t0 + 14min) .empty());
    CHECK(drain(s, t0 + 15min) == std::vector<std::int64_t>{1, 2});
}

TEST_CASE("a changed folder is scanned next, and only once however often it changes", "[schedule]")
{
    ScanSchedule s;
    s.setFolders({1, 2}, t0);
    drain(s, t0);
    s.changed(2);
    s.changed(2);
    CHECK(drain(s, t0 + 1min) == std::vector<std::int64_t>{2});
    // Its poll counts from that scan.
    CHECK(drain(s, t0 + 15min) == std::vector<std::int64_t>{1});
    CHECK(drain(s, t0 + 16min) == std::vector<std::int64_t>{2});
}

TEST_CASE("a change during a folder's scan scans it again after", "[schedule]")
{
    ScanSchedule s;
    s.setFolders({1}, t0);
    REQUIRE(s.next(t0) == 1); // scanning
    s.changed(1);             // the scan may already have passed the change
    s.scanned(1, t0 + 10s);
    CHECK(s.next(t0 + 10s) == 1);
}

TEST_CASE("a folder added elsewhere is scanned at once; one removed is not", "[schedule]")
{
    ScanSchedule s;
    s.setFolders({1}, t0);
    drain(s, t0);
    s.changed(1);
    s.setFolders({3}, t0 + 1min); // 1 removed, 3 added
    CHECK(drain(s, t0 + 1min) == std::vector<std::int64_t>{3});
    s.changed(1); // a stray report for a folder no longer there
    CHECK(drain(s, t0 + 2min).empty());
}

TEST_CASE("a refused scan just waits for the next change or poll", "[schedule]")
{
    ScanSchedule s;
    s.setFolders({1}, t0);
    REQUIRE(s.next(t0) == 1);
    s.scanned(1, t0); // refused by the writer lock: counts as done
    CHECK_FALSE(s.next(t0 + 1s));
    CHECK(s.next(t0 + 15min) == 1);
}
