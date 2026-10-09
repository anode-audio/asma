// SPDX-License-Identifier: GPL-3.0-only
#include "FileOpsJob.h"
#include "LibraryFixture.h"
#include "asma/core/Fs.h"
#include "asma/core/WriterLock.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_events/juce_events.h>

#include <chrono>

using namespace asma;
namespace fs = std::filesystem;
using app::FileOpsJob;
using app::FileOutcome;
using app::FileRequest;

namespace {

void settle(FileOpsJob& job)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!job.idle() && std::chrono::steady_clock::now() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}

// A trash in a folder of its own.
TrashBackend folderTrash(const fs::path& dir)
{
    fs::create_directories(dir);
    TrashBackend t;
    t.available = [](const fs::path&) { return true; };
    t.move = [dir](const fs::path& file) {
        TrashResult r;
        r.where = dir / file.filename();
        r.ok = !renameNoReplace(file, r.where);
        return r;
    };
    t.restore = [](const fs::path& where, const fs::path& to) {
        return renameNoReplace(where, to) ? std::string("its old place is taken") : std::string();
    };
    return t;
}

std::int64_t idOf(const fs::path& db, const fs::path& file)
{
    Db d = Db::open(db);
    return Library(d).fileByAbsolutePath(file)->id;
}

} // namespace

TEST_CASE("The file operations job trashes and undoes off the message thread", "[fileopsjob]")
{
    test::LibraryFixture f;
    f.scan();
    FileOpsJob job(f.dbPath, folderTrash(f.dir.path() / "Trash"));
    settle(job);
    CHECK(job.messageCount() == 0); // a clean start says nothing
    CHECK(job.undoLabel().empty());

    const auto kick = idOf(f.dbPath, f.kick);
    FileOutcome got;
    job.run(FileRequest::trash({kick}), [&](const FileOutcome& o) { got = o; });
    settle(job);
    CHECK(got.done);
    CHECK(got.text == "Moved Kick_01.wav to the Trash. " + FileOpsJob::undoKey() + " to undo.");
    CHECK(got.files == std::vector<std::int64_t>{kick});
    CHECK(job.message() == got.text);
    CHECK(job.messageCount() == 1);
    CHECK_FALSE(fs::exists(f.kick));
    CHECK(job.undoLabel() == "Move Kick_01.wav to the Trash");

    job.run(FileRequest::undo(), [&](const FileOutcome& o) { got = o; });
    settle(job);
    CHECK(got.text == "Undid Move Kick_01.wav to the Trash.");
    CHECK(got.files == std::vector<std::int64_t>{kick});
    CHECK(fs::exists(f.kick));
    CHECK(job.undoLabel().empty());

    job.run(FileRequest::move({kick}, f.lib / "Drums"), [&](const FileOutcome& o) { got = o; });
    settle(job);
    CHECK_FALSE(got.done);
    CHECK(got.text == "Kick_01.wav is already in Drums.");
}

TEST_CASE("The file operations job waits for the writer lock, then gives up", "[fileopsjob]")
{
    test::LibraryFixture f;
    f.scan();
    auto lock = WriterLock::tryAcquire(f.dbPath.parent_path());
    REQUIRE(lock);
    FileOpsJob job(f.dbPath, folderTrash(f.dir.path() / "Trash"), std::chrono::milliseconds(300));
    FileOutcome got;
    job.run(FileRequest::trash({idOf(f.dbPath, f.kick)}), [&](const FileOutcome& o) { got = o; });
    settle(job);
    CHECK_FALSE(got.done);
    CHECK(got.text == "The library is busy scanning; try again in a moment.");
    CHECK(fs::exists(f.kick));
}

TEST_CASE("The file operations job starts by rolling back and merging, and says so", "[fileopsjob]")
{
    test::LibraryFixture f;
    f.scan();
    {
        Db db = Db::open(f.dbPath);
        Library lib(db);
        lib.addRoot(f.lib / "Drums"); // nested, as older versions allowed
        FileOps ops(db, folderTrash(f.dir.path() / "Trash"));
        ops.crashAfter(1);
        CHECK_THROWS_AS(ops.trash({lib.fileByAbsolutePath(f.loop)->id, lib.fileByAbsolutePath(f.kick)->id}),
                        SimulatedCrash);
    }
    FileOpsJob job(f.dbPath, folderTrash(f.dir.path() / "Trash"));
    settle(job);
    CHECK(job.messageCount() == 1);
    CHECK(job.message()
          == "asma was interrupted while moving 2 samples to the Trash; they are back where they were. "
             "Merged Drums into Samples, which contains it.");
    CHECK(fs::exists(f.loop));
}

TEST_CASE("The file operations job adds a folder in place of those inside it", "[fileopsjob]")
{
    test::LibraryFixture f;
    f.scan();
    {
        Db db = Db::open(f.dbPath);
        Library lib(db);
        // Samples was removed, then two folders inside it were added.
        lib.setRootEnabled(lib.roots().front().id, false);
        lib.addRoot(f.lib / "Drums");
        lib.addRoot(f.lib / "Loops");
    }
    FileOpsJob job(f.dbPath, folderTrash(f.dir.path() / "Trash"));
    settle(job);
    FileOutcome got;
    job.run(FileRequest::addFolder(f.lib, true), [&](const FileOutcome& o) { got = o; });
    settle(job);
    CHECK(got.done);
    CHECK(got.text == "Added Samples in place of Drums and Loops.");
    Db db = Db::open(f.dbPath);
    CHECK(Library(db).roots().size() == 1);
}

TEST_CASE("The file operations job recovers before any request when it could not at its start", "[fileopsjob]")
{
    test::LibraryFixture f;
    f.scan();
    {
        Db db = Db::open(f.dbPath);
        Library lib(db);
        FileOps ops(db, folderTrash(f.dir.path() / "Trash"));
        ops.crashAfter(1);
        CHECK_THROWS_AS(ops.trash({lib.fileByAbsolutePath(f.loop)->id, lib.fileByAbsolutePath(f.kick)->id}),
                        SimulatedCrash);
    }
    auto lock = WriterLock::tryAcquire(f.dbPath.parent_path()); // a long scan
    REQUIRE(lock);
    FileOpsJob job(f.dbPath, folderTrash(f.dir.path() / "Trash"), std::chrono::milliseconds(300));
    settle(job);
    CHECK(job.messageCount() == 0); // it gave up waiting
    lock.reset();
    FileOutcome got;
    job.run(FileRequest::undo(), [&](const FileOutcome& o) { got = o; });
    settle(job);
    CHECK(got.text
          == "asma was interrupted while moving 2 samples to the Trash; they are back where they were. Nothing to undo.");
    CHECK(fs::exists(f.loop));
    CHECK(fs::exists(f.kick));
}
