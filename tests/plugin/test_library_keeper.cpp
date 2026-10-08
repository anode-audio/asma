// SPDX-License-Identifier: GPL-3.0-only
#include "EditorRig.h"
#include "LibraryKeeper.h"
#include "asma/core/Backup.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/UserData.h"

#include <fstream>
#include <sqlite3.h>
#include <sstream>

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using namespace std::chrono_literals;
using app::LibraryKeeper;
namespace fs = std::filesystem;

namespace {

// Records what it is asked to scan and finishes when told.
struct FakeRunner final : app::ScanRunner {
    std::vector<std::int64_t> started;
    bool running = false;
    std::optional<ScanReport> report;
    bool start(std::int64_t rootId, const std::string&) override
    {
        if (running) return false;
        started.push_back(rootId);
        running = true;
        return true;
    }
    bool busy() const override { return running; }
    std::string progress() const override { return running ? "Scanning" : ""; }
    std::optional<ScanReport> takeReport() override
    {
        auto r = report;
        report.reset();
        return r;
    }
    bool ready() const override { return true; }
    void finish(ScanReport::Result result = ScanReport::Result::Finished, std::size_t added = 0)
    {
        ScanReport r;
        r.result = result;
        r.index.added = added;
        report = r;
        running = false;
    }
};

struct KeeperRig {
    test::LibraryFixture f;
    const juce::ScopedJuceInitialiser_GUI gui;
    FakeRunner* runner = nullptr;
    std::unique_ptr<LibraryKeeper> keeper;
    std::int64_t samples = 0, other = 0;
    const LibraryKeeper::Clock::time_point t0 = LibraryKeeper::Clock::now();
    KeeperRig()
    {
        f.scan();
        fs::create_directories(f.dir.path() / "Other");
        {
            Db db = Db::open(f.dbPath);
            Library lib(db);
            samples = lib.roots().front().id;
            other = lib.addRoot(f.dir.path() / "Other");
        }
        auto fake = std::make_unique<FakeRunner>();
        runner = fake.get();
        keeper = std::make_unique<LibraryKeeper>(f.dbPath, std::move(fake), ASMA_CLI_PATH);
    }
    // Ticks at t0 until the startup check (through the helper) is done; the
    // tick after it starts the first scan.
    void start()
    {
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (std::chrono::steady_clock::now() < deadline) {
            keeper->tick(t0);
            if (!runner->started.empty()) return;
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        }
    }
};

} // namespace

TEST_CASE("the keeper scans every folder once at startup, one at a time", "[keeper]")
{
    KeeperRig rig;
    rig.start();
    CHECK(rig.runner->started == std::vector<std::int64_t>{rig.samples});
    rig.keeper->tick(rig.t0 + 1s); // still running: nothing more
    CHECK(rig.runner->started.size() == 1);
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 2s);
    CHECK(rig.runner->started == std::vector<std::int64_t>{rig.samples, rig.other});
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 3s);
    CHECK(rig.runner->started.size() == 2);
    rig.keeper->tick(rig.t0 + 16min); // the poll
    CHECK(rig.runner->started.size() == 3);
}

TEST_CASE("the keeper scans a folder the watcher reports", "[keeper]")
{
    KeeperRig rig;
    rig.start();
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 1s);
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 2s);
    rig.keeper->folderChanged(rig.other);
    rig.keeper->tick(rig.t0 + 3s);
    CHECK(rig.runner->started.back() == rig.other);
    CHECK(rig.runner->started.size() == 3);
}

TEST_CASE("the keeper reports news, not quiet scans or a lock refused", "[keeper]")
{
    KeeperRig rig;
    rig.start();
    rig.runner->finish(ScanReport::Result::Finished, 0);
    rig.keeper->tick(rig.t0 + 1s);
    CHECK(rig.keeper->messageCount() == 0);
    rig.runner->finish(ScanReport::Result::Locked);
    rig.keeper->tick(rig.t0 + 2s);
    CHECK(rig.keeper->messageCount() == 0);
    rig.keeper->folderChanged(rig.samples);
    rig.keeper->tick(rig.t0 + 3s);
    rig.runner->finish(ScanReport::Result::Finished, 3);
    rig.keeper->tick(rig.t0 + 4s);
    CHECK(rig.keeper->messageCount() == 1);
    CHECK(rig.keeper->message() == "Scan finished: 3 added");
}

TEST_CASE("a folder added elsewhere is scanned at the next tick", "[keeper]")
{
    KeeperRig rig;
    rig.start();
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 1s);
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 2s);
    fs::create_directories(rig.f.dir.path() / "Third");
    std::int64_t third = 0;
    {
        Db db = Db::open(rig.f.dbPath); // the app, or the CLI
        third = Library(db).addRoot(rig.f.dir.path() / "Third");
    }
    rig.keeper->tick(rig.t0 + 3s);
    CHECK(rig.runner->started.back() == third);
}

TEST_CASE("every window in a process shares one keeper", "[keeper]")
{
    test::LibraryFixture f;
    const juce::ScopedJuceInitialiser_GUI gui;
    auto a = LibraryKeeper::shared(f.dbPath, "asma-scan", "asma-cli");
    auto b = LibraryKeeper::shared(f.dbPath, "asma-scan", "asma-cli");
    CHECK(a == b);
    a.reset();
    b.reset();
    CHECK(LibraryKeeper::shared(f.dbPath, "asma-scan", "asma-cli") != nullptr); // made again once none held it
}

TEST_CASE("a sample dropped into a watched folder appears in the table", "[keeper][watch]")
{
    test::EditorRig rig; // a plugin: it scans too, through asma-scan
    REQUIRE(rig.editor->table().getNumRows() == 3);
    dynamic_cast<app::ScanJob&>(rig.editor->keeper().runner()).setWorker(ASMA_SCAN_PATH);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(2000); // watching, and the startup scan under way
    test::writeWavFloat(rig.f.lib / "Drums" / "Hat_03.wav", 48000, {test::hatHit(48000, 5)});
    const auto deadline = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < deadline && rig.editor->table().getNumRows() < 4) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // the keeper's timer
        rig.editor->poll();
    }
    CHECK(rig.editor->table().getNumRows() == 4);
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

// Ticks until the keeper has nothing in hand with the helper.
void settle(LibraryKeeper& keeper, LibraryKeeper::Clock::time_point now)
{
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    do {
        keeper.tick(now);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    } while (!keeper.settled() && std::chrono::steady_clock::now() < deadline);
}

// A backup of the library as it is, said to be written on 7 October.
void backupOn7October(const test::LibraryFixture& f)
{
    {
        Db db = Db::open(f.dbPath);
        writeBackup(db, f.dbPath.parent_path() / "backup.json", f.dbPath.parent_path() / "backup-previous.json");
    }
    std::ifstream in(f.dbPath.parent_path() / "backup.json");
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    const auto at = text.find("\"written\":\"") + 11;
    text.replace(at, 20, "2026-10-07T09:00:00Z");
    std::ofstream(f.dbPath.parent_path() / "backup.json", std::ios::trunc) << text;
}

} // namespace

TEST_CASE("a damaged library at startup is rebuilt, and the footer says from when", "[keeper][safety]")
{
    KeeperRig rig;
    {
        Db db = Db::open(rig.f.dbPath);
        UserData(db).setRating(Library(db).fileByAbsolutePath(rig.f.kick)->id, 5);
    }
    backupOn7October(rig.f);
    damage(rig.f.dbPath);
    settle(*rig.keeper, rig.t0);
    CHECK(rig.keeper->messageCount() == 1);
    CHECK(rig.keeper->message() ==
          "The library was damaged and has been rebuilt; your ratings and collections were restored from 7 October.");
    CHECK(fs::exists(rig.f.dbPath.parent_path() / "library.db.corrupt"));
    Db db = Db::open(rig.f.dbPath);
    CHECK(UserData(db).rating(Library(db).fileByAbsolutePath(rig.f.kick)->id) == 5);
}

TEST_CASE("a sound library is checked and left alone", "[keeper][safety]")
{
    KeeperRig rig;
    settle(*rig.keeper, rig.t0);
    CHECK(rig.keeper->messageCount() == 0);
    CHECK_FALSE(fs::exists(rig.f.dbPath.parent_path() / "library.db.corrupt"));
}

TEST_CASE("damage met while reading is rebuilt too", "[keeper][safety]")
{
    KeeperRig rig;
    settle(*rig.keeper, rig.t0); // checked: sound
    {
        // Garbage in the schema, through another connection (Windows-safe).
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(toUtf8(rig.f.dbPath).c_str(), &db) == SQLITE_OK);
        REQUIRE(sqlite3_exec(db, "PRAGMA writable_schema = ON; UPDATE sqlite_master SET sql = 'garbage' "
                                 "WHERE name = 'files'; PRAGMA schema_version = 999;",
                             nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);
    }
    settle(*rig.keeper, rig.t0 + 1s);
    settle(*rig.keeper, rig.t0 + 2s);
    CHECK(rig.keeper->message().rfind("The library was damaged and has been rebuilt", 0) == 0);
}

TEST_CASE("the user's data is backed up once a day", "[keeper][safety]")
{
    KeeperRig rig;
    const auto backup = rig.f.dbPath.parent_path() / "backup.json";
    settle(*rig.keeper, rig.t0);
    settle(*rig.keeper, rig.t0 + 1s);
    REQUIRE(fs::exists(backup)); // none yet: made now
    const auto written = fs::last_write_time(backup);
    settle(*rig.keeper, rig.t0 + 2min);
    const bool untouched = fs::last_write_time(backup) == written;
    CHECK(untouched); // a day has not passed
    fs::last_write_time(backup, written - std::chrono::hours(25));
    settle(*rig.keeper, rig.t0 + 4min);
    settle(*rig.keeper, rig.t0 + 4min + 1s);
    const bool rewritten = fs::last_write_time(backup) > written - std::chrono::hours(1);
    CHECK(rewritten);
    CHECK(fs::exists(rig.f.dbPath.parent_path() / "backup-previous.json"));
}

TEST_CASE("the backup's day reads as a person would say it", "[keeper]")
{
    CHECK(app::backupDay("2026-10-07T09:00:00Z") == "7 October");
    CHECK(app::backupDay("2027-01-31T23:59:59Z") == "31 January");
    CHECK(app::backupDay("").empty());
    CHECK(app::backupDay("yesterday").empty());
}
