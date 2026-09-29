// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/ChangeWatcher.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Schema.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;

namespace {

struct Library2 {
    TempDir dir;
    std::filesystem::path file = dir.path() / "library.db";
    Db writer = Db::open(file);
    std::int64_t fileId = 0;

    Library2()
    {
        Library lib(writer);
        FileRecord f;
        f.rootId = lib.addRoot(dir.path() / "samples");
        f.relPath = "a.wav";
        f.format = "wav";
        fileId = lib.insertFile(f);
    }
};

} // namespace

TEST_CASE("openReadOnly reads a library and refuses to write to it", "[readonly]")
{
    Library2 lib;
    Db reader = Db::openReadOnly(lib.file);
    CHECK(Library(reader).fileById(lib.fileId));
    CHECK_THROWS_AS(UserData(reader).setRating(lib.fileId, 3), DbError);
    CHECK_THROWS_AS(reader.exec("INSERT INTO roots(path) VALUES ('/x')"), DbError);
}

TEST_CASE("openReadOnly neither creates nor migrates", "[readonly]")
{
    TempDir dir;
    CHECK_THROWS_AS(Db::openReadOnly(dir.path() / "missing.db"), DbError);
    CHECK_FALSE(std::filesystem::exists(dir.path() / "missing.db"));

    const auto file = dir.path() / "old.db";
    {
        Db old = Db::openInMemory(2); // a library an older asma wrote
        auto copy = old.prepare("VACUUM INTO ?");
        copy.bind(1, std::string_view(toUtf8(file)));
        copy.run();
    }
    try {
        Db::openReadOnly(file);
        FAIL("an old library was opened");
    } catch (const SchemaMismatchError& e) {
        CHECK(e.found() == 2);
    }
    Db check = Db::open(file); // what the helper process does: migrate on open
    CHECK(check.schemaVersion() == currentSchemaVersion());
}

TEST_CASE("a newer library is a schema mismatch for writers too", "[readonly]")
{
    TempDir dir;
    const auto file = dir.path() / "new.db";
    {
        Db db = Db::open(file);
        db.exec("PRAGMA user_version = 999");
    }
    CHECK_THROWS_AS(Db::open(file), SchemaMismatchError);
    CHECK_THROWS_AS(Db::openReadOnly(file), SchemaMismatchError);
}

TEST_CASE("ChangeWatcher sees other connections' commits once", "[readonly]")
{
    Library2 lib;
    Db reader = Db::openReadOnly(lib.file);
    ChangeWatcher watcher(reader);
    CHECK_FALSE(watcher.changed());

    UserData(lib.writer).setRating(lib.fileId, 4);
    UserData(lib.writer).setFavourite(lib.fileId, true);
    CHECK(watcher.changed());
    CHECK_FALSE(watcher.changed());
    CHECK(UserData(reader).rating(lib.fileId) == 4);

    {
        Transaction tx(lib.writer);
        UserData(lib.writer).setRating(lib.fileId, 2);
        // Not committed yet: nothing to see.
        CHECK_FALSE(watcher.changed());
    } // rolled back
    CHECK_FALSE(watcher.changed());
}

TEST_CASE("ChangeWatcher ignores the watched connection's own commits", "[readonly]")
{
    Library2 lib;
    ChangeWatcher watcher(lib.writer);
    UserData(lib.writer).setRating(lib.fileId, 1);
    CHECK_FALSE(watcher.changed());
}

TEST_CASE("openReadOnly works after every writer has closed the library", "[readonly]")
{
    TempDir dir;
    const auto file = dir.path() / "library.db";
    {
        Db writer = Db::open(file);
        writer.exec("INSERT INTO roots(path) VALUES ('/x')");
    } // the last connection checkpoints and removes the -wal and -shm files
    Db reader = Db::openReadOnly(file);
    {
        // Scoped: a statement left mid-step holds its read snapshot open.
        auto q = reader.prepare("SELECT count(*) FROM roots");
        REQUIRE(q.step());
        CHECK(q.getInt(0) == 1);
    }

    Db writer = Db::open(file); // a writer can still come along while the reader is open
    ChangeWatcher watcher(reader);
    writer.exec("INSERT INTO roots(path) VALUES ('/y')");
    CHECK(watcher.changed());
    auto again = reader.prepare("SELECT count(*) FROM roots");
    REQUIRE(again.step());
    CHECK(again.getInt(0) == 2);
}
