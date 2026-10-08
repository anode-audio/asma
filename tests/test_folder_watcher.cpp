// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/FolderWatcher.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>

using namespace asma;
using asma::test::TempDir;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

// Counts reports per folder. Change notices can lag on a busy machine, so
// waits are generous and end as soon as the report comes.
struct Reports {
    std::mutex mutex;
    std::map<std::int64_t, int> counts;
    void add(std::int64_t id)
    {
        const std::lock_guard lock(mutex);
        ++counts[id];
    }
    void clear()
    {
        const std::lock_guard lock(mutex);
        counts.clear();
    }
    int count(std::int64_t id)
    {
        const std::lock_guard lock(mutex);
        return counts[id];
    }
    // Waits until folder `id` has `n` reports; false after `limit`.
    bool waitFor(std::int64_t id, int n, std::chrono::milliseconds limit = 10s)
    {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            if (count(id) >= n) return true;
            std::this_thread::sleep_for(20ms);
        }
        return false;
    }
};

struct Rig {
    TempDir dir;
    fs::path a = dir.path() / "A";
    fs::path b = dir.path() / "B";
    Reports reports;
    FolderWatcher watcher{[this](std::int64_t id) { reports.add(id); }, 1s};
    Rig()
    {
        fs::create_directories(a / "Drums");
        fs::create_directories(b);
        CHECK(watcher.watch({{1, a}, {2, b}}).empty());
        // macOS can still deliver notices for the folders just made: let
        // them come and go before the test acts.
        std::this_thread::sleep_for(1500ms);
        reports.clear();
    }
};

} // namespace

TEST_CASE("a new, renamed or deleted file is reported once, for its folder", "[watcher]")
{
    Rig rig;
    test::writeBytes(rig.a / "Drums" / "kick.wav", "x");
    REQUIRE(rig.reports.waitFor(1, 1));
    std::this_thread::sleep_for(1500ms);
    CHECK(rig.reports.count(1) == 1);
    CHECK(rig.reports.count(2) == 0);

    fs::rename(rig.a / "Drums" / "kick.wav", rig.a / "Drums" / "kick_01.wav");
    CHECK(rig.reports.waitFor(1, 2));
    fs::remove(rig.a / "Drums" / "kick_01.wav");
    CHECK(rig.reports.waitFor(1, 3));
}

TEST_CASE("a burst of files is one report", "[watcher]")
{
    Rig rig;
    for (int i = 0; i < 200; ++i) test::writeBytes(rig.b / ("s" + std::to_string(i) + ".wav"), "x");
    REQUIRE(rig.reports.waitFor(2, 1));
    std::this_thread::sleep_for(1500ms);
    CHECK(rig.reports.count(2) == 1);
}

TEST_CASE("hidden files and ignored directories never count", "[watcher]")
{
    Rig rig;
    rig.watcher.ignore(rig.b / "data");
    fs::create_directories(rig.b / "data");
    test::writeBytes(rig.a / ".DS_Store", "x");
    test::writeBytes(rig.a / "._kick.wav", "x");
    fs::create_directories(rig.a / ".git");
    test::writeBytes(rig.a / ".git" / "index", "x");
    test::writeBytes(rig.b / "data" / "library.db", "x");
    std::this_thread::sleep_for(2500ms);
    CHECK(rig.reports.count(1) == 0);
    CHECK(rig.reports.count(2) == 0);
}

TEST_CASE("a folder no longer watched is not reported; a missing one is given back", "[watcher]")
{
    Rig rig;
    const auto failed = rig.watcher.watch({{1, rig.a}, {3, rig.dir.path() / "Unplugged"}});
    CHECK(failed == std::vector<std::int64_t>{3});
    test::writeBytes(rig.b / "new.wav", "x");
    test::writeBytes(rig.a / "new.wav", "x");
    REQUIRE(rig.reports.waitFor(1, 1));
    std::this_thread::sleep_for(1500ms);
    CHECK(rig.reports.count(2) == 0);
}

TEST_CASE("a folder of samples renamed inside a watched folder is reported", "[watcher]")
{
    Rig rig;
    fs::rename(rig.a / "Drums", rig.a / "Percussion");
    CHECK(rig.reports.waitFor(1, 1));
}
