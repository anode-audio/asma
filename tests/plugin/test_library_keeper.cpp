// SPDX-License-Identifier: GPL-3.0-only
#include "EditorRig.h"
#include "LibraryKeeper.h"
#include "asma/core/Library.h"

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
        keeper = std::make_unique<LibraryKeeper>(f.dbPath, std::move(fake));
    }
};

} // namespace

TEST_CASE("the keeper scans every folder once at startup, one at a time", "[keeper]")
{
    KeeperRig rig;
    rig.keeper->tick(rig.t0);
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
    rig.keeper->tick(rig.t0);
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
    rig.keeper->tick(rig.t0);
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
    rig.keeper->tick(rig.t0);
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
    auto a = LibraryKeeper::shared(f.dbPath, "asma-scan");
    auto b = LibraryKeeper::shared(f.dbPath, "asma-scan");
    CHECK(a == b);
    a.reset();
    b.reset();
    CHECK(LibraryKeeper::shared(f.dbPath, "asma-scan") != nullptr); // made again once none held it
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
