// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Backup.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Repair.h"
#include "asma/core/Scanner.h"
#include "asma/core/UserData.h"
#include "asma/core/WriterLock.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

// A scanned library with a rated kick, and its backup, in a data directory.
struct Data {
    TempDir dir;
    fs::path samples = dir.path() / "Samples";
    fs::path dbPath = dir.path() / "data" / "library.db";
    fs::path backup = dir.path() / "data" / "backup.json";
    Data()
    {
        test::WavSpec kick;
        kick.seed = 1;
        test::writeWav(samples / "Drums" / "kick.wav", kick);
        test::WavSpec loop;
        loop.seed = 2;
        test::writeWav(samples / "Loops" / "bass.wav", loop);
        Db db = Db::open(dbPath);
        Library lib(db);
        const auto root = lib.addRoot(samples);
        scanRoot(db, root);
        UserData(db).setRating(lib.fileByPath(root, "Drums/kick.wav")->id, 5);
    }
    void writeTheBackup()
    {
        Db db = Db::open(dbPath);
        writeBackup(db, backup, dir.path() / "data" / "backup-previous.json");
    }
    // Overwrites the library's pages after the first with garbage, as a disk
    // fault would; every connection is closed by now.
    void damage()
    {
        std::fstream f(dbPath, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(4096);
        const std::string junk(4096 * 3, '\xA5');
        f.write(junk.data(), static_cast<std::streamsize>(junk.size()));
    }
    std::optional<int> kickRating()
    {
        Db db = Db::open(dbPath);
        Library lib(db);
        const auto file = lib.fileByAbsolutePath(samples / "Drums" / "kick.wav");
        return file ? UserData(db).rating(file->id) : std::nullopt;
    }
};

} // namespace

TEST_CASE("the check tells a sound library from a damaged or missing one", "[repair]")
{
    Data d;
    CHECK(checkLibrary(d.dbPath).health == LibraryHealth::Ok);
    CHECK(checkLibrary(d.dir.path() / "none.db").health == LibraryHealth::Missing);
    d.damage();
    const HealthReport damaged = checkLibrary(d.dbPath);
    CHECK(damaged.health == LibraryHealth::Damaged);
    CHECK_FALSE(damaged.detail.empty());
}

TEST_CASE("a library a scan is writing is never taken for a damaged one", "[repair]")
{
    Data d;
    Db writer = Db::open(d.dbPath);
    Transaction batch(writer); // a scan committing a batch
    writer.exec("UPDATE files SET mtime = mtime + 1");
    CHECK(checkLibrary(d.dbPath).health != LibraryHealth::Damaged);
}

TEST_CASE("repair leaves a sound library alone", "[repair]")
{
    Data d;
    const RepairReport r = repairLibrary(d.dbPath, d.backup);
    CHECK(r.result == RepairReport::Result::Healthy);
    CHECK_FALSE(fs::exists(d.dir.path() / "data" / "library.db.corrupt"));
    CHECK(d.kickRating() == 5);
}

TEST_CASE("repair moves a damaged library aside, rebuilds it and restores the backup", "[repair]")
{
    Data d;
    d.writeTheBackup();
    d.damage();
    const RepairReport r = repairLibrary(d.dbPath, d.backup);
    CHECK(r.result == RepairReport::Result::Repaired);
    CHECK(r.movedTo == d.dir.path() / "data" / "library.db.corrupt");
    CHECK(fs::exists(r.movedTo));
    CHECK(r.folders == 1);
    CHECK(r.restored.files == 1);
    CHECK_FALSE(r.backupWrittenAt.empty());
    CHECK(checkLibrary(d.dbPath).health == LibraryHealth::Ok);
    CHECK(d.kickRating() == 5);
}

TEST_CASE("a second damaged library is moved aside under a dated name", "[repair]")
{
    Data d;
    d.writeTheBackup();
    test::writeBytes(d.dir.path() / "data" / "library.db.corrupt", "an older one");
    d.damage();
    const RepairReport r = repairLibrary(d.dbPath, d.backup);
    CHECK(r.result == RepairReport::Result::Repaired);
    CHECK(r.movedTo.filename().string().rfind("library.db.corrupt-", 0) == 0);
    CHECK(fs::file_size(d.dir.path() / "data" / "library.db.corrupt") == 12); // the older one, untouched
}

TEST_CASE("repair with no backup still rebuilds, restoring nothing", "[repair]")
{
    Data d;
    d.damage();
    const RepairReport r = repairLibrary(d.dbPath, d.backup);
    CHECK(r.result == RepairReport::Result::Repaired);
    CHECK(r.backupWrittenAt.empty()); // nothing restored
    CHECK(checkLibrary(d.dbPath).health == LibraryHealth::Ok);
}

TEST_CASE("repair waits its turn: with another writer holding the lock it moves nothing", "[repair]")
{
    Data d;
    d.damage();
    auto lock = WriterLock::tryAcquire(d.dbPath.parent_path());
    REQUIRE(lock);
    const RepairReport r = repairLibrary(d.dbPath, d.backup);
    CHECK(r.result == RepairReport::Result::Locked);
    CHECK_FALSE(fs::exists(d.dir.path() / "data" / "library.db.corrupt"));
}

TEST_CASE("a rebuild cut short leaves the damaged library in place to be rebuilt again", "[repair]")
{
    Data d;
    d.writeTheBackup();
    d.damage();
    ScanOptions crash;
    crash.threads = 1; // on the calling thread, so the throw comes back out
    crash.onFileStart = [](std::string_view) { throw std::runtime_error("stands in for the helper being killed"); };
    CHECK_THROWS(repairLibrary(d.dbPath, d.backup, crash));
    // Nothing looks sound that is not: the next start finds the damage again.
    CHECK(checkLibrary(d.dbPath).health == LibraryHealth::Damaged);
    CHECK_FALSE(fs::exists(d.dir.path() / "data" / "library.db.corrupt"));
    const RepairReport r = repairLibrary(d.dbPath, d.backup); // a leftover half-rebuild is no obstacle
    CHECK(r.result == RepairReport::Result::Repaired);
    CHECK(d.kickRating() == 5);
    CHECK_FALSE(fs::exists(d.dir.path() / "data" / "library.db.rebuild"));
}
