// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryView.h"
#include "TestUtil.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"
#include "asma/core/Schema.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <cstdlib>
#include <string>

using namespace asma;
using app::LibraryState;
using app::LibraryView;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    TempDir dir;
    fs::path dbPath = dir.path() / "data" / "library.db";
    fs::path lib = dir.path() / "Samples";
    // What the app or asma-scan would do: create the library and scan.
    std::int64_t scan()
    {
        test::WavSpec spec;
        test::writeWav(lib / "Loops" / "Bass_Loop_Am_120.wav", spec);
        spec.seed = 2;
        test::writeWav(lib / "Drums" / "Kick_01.wav", spec);
        Db db = Db::open(dbPath);
        Library library(db);
        const auto root = library.addRoot(lib);
        scanRoot(db, root);
        return library.fileByPath(root, "Loops/Bass_Loop_Am_120.wav")->id;
    }
};

// Stamps a schema version on the file without migrating anything.
void setVersion(const fs::path& path, int version)
{
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(toUtf8(path).c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, ("PRAGMA user_version = " + std::to_string(version)).c_str(), nullptr, nullptr, nullptr)
            == SQLITE_OK);
    sqlite3_close(db);
}

} // namespace

TEST_CASE("LibraryView waits for a library that does not exist yet", "[libview]")
{
    Fixture f;
    LibraryView view(f.dbPath);
    CHECK(view.refresh() == LibraryState::Missing);
    CHECK(view.search({}).empty());
    CHECK_FALSE(view.message().empty());
    CHECK_FALSE(fs::exists(f.dbPath)); // a reader never creates it

    f.scan();
    CHECK(view.refresh() == LibraryState::Open);
    CHECK(view.changed()); // appearing counts as a change
    CHECK_FALSE(view.changed());
}

TEST_CASE("LibraryView searches and describes files", "[libview]")
{
    Fixture f;
    const auto loopId = f.scan();
    LibraryView view(f.dbPath);
    REQUIRE(view.refresh() == LibraryState::Open);
    SearchModel m;
    m.text = "bass";
    const auto rows = view.search(m);
    REQUIRE(rows.size() == 1);
    CHECK(fs::equivalent(LibraryView::pathOf(rows[0]), f.lib / "Loops" / "Bass_Loop_Am_120.wav")); // roots are stored canonical
    const audio::SampleInfo info = view.info(rows[0].id);
    CHECK(info.bpm == 120.0);
    CHECK(info.key == "Am");
    CHECK(info.isLoop == true);
    CHECK(view.contentHash(loopId).size() == 16);
    CHECK(view.contentHash(9999).empty());
    CHECK(view.search({}).size() == 2);
}

TEST_CASE("LibraryView notices what other processes write", "[libview]")
{
    Fixture f;
    const auto loopId = f.scan();
    LibraryView view(f.dbPath);
    view.refresh();
    view.changed();
    {
        Db writer = Db::open(f.dbPath); // the asma CLI a plugin runs to rate a file
        UserData(writer).setRating(loopId, 5);
    }
    CHECK(view.changed());
    CHECK_FALSE(view.changed());
    SearchModel rated;
    rated.minRating = 5;
    CHECK(view.search(rated).size() == 1);
}

TEST_CASE("LibraryView explains a library it cannot read", "[libview]")
{
    Fixture f;
    f.scan();
    setVersion(f.dbPath, 1); // left behind by an older asma
    LibraryView older(f.dbPath);
    CHECK(older.refresh() == LibraryState::Outdated);
    CHECK(older.message().find("asma scan") != std::string::npos);
    CHECK(older.search({}).empty());
    setVersion(f.dbPath, currentSchemaVersion()); // what migrating it would leave
    CHECK(older.refresh() == LibraryState::Open); // tries again each time

    setVersion(f.dbPath, 99);
    LibraryView newer(f.dbPath);
    CHECK(newer.refresh() == LibraryState::TooNew);

    TempDir other;
    const auto junk = other.path() / "library.db";
    test::writeBytes(junk, "this is not a database, not even close to one");
    LibraryView broken(junk);
    CHECK(broken.refresh() == LibraryState::Unreadable);
}

TEST_CASE("LibraryView gives up quietly on a library that breaks while open", "[libview]")
{
    Fixture f;
    const auto loopId = f.scan();
    LibraryView view(f.dbPath);
    REQUIRE(view.refresh() == LibraryState::Open);
    REQUIRE(view.search({}).size() == 2);
    // Corrupt the library under the open connection through another one:
    // Windows will not let a test delete or rewrite files SQLite holds open.
    {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(toUtf8(f.dbPath).c_str(), &db) == SQLITE_OK);
        int cookie = 0;
        REQUIRE(sqlite3_exec(
                    db, "PRAGMA schema_version",
                    [](void* out, int, char** values, char**) {
                        *static_cast<int*>(out) = std::atoi(values[0]);
                        return 0;
                    },
                    &cookie, nullptr)
                == SQLITE_OK);
        // A new cookie makes the open connection reload the broken schema.
        const std::string sql = "PRAGMA writable_schema = ON;"
                                "UPDATE sqlite_master SET sql = 'garbage' WHERE name = 'files';"
                                "PRAGMA schema_version = "
                              + std::to_string(cookie + 1) + ";";
        REQUIRE(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);
    }

    CHECK_NOTHROW(view.changed());
    CHECK(view.search({}).empty());
    CHECK(view.state() == LibraryState::Unreadable);
    CHECK_NOTHROW(view.info(loopId));
    CHECK(view.contentHash(loopId).empty());
    CHECK_NOTHROW(view.infoFor(f.lib / "Loops" / "Bass_Loop_Am_120.wav"));
    CHECK_FALSE(view.message().empty());
}

TEST_CASE("the standalone updates an older library; a plugin only says so", "[libview]")
{
    TempDir dir;
    const auto dbPath = dir.path() / "library.db";
    {
        Db old = Db::openInMemory(2); // the schema before plan 3a
        old.exec("INSERT INTO roots(id, path) VALUES (1, '/Samples')");
        old.exec("VACUUM INTO '" + toUtf8(dbPath) + "'");
    }
    LibraryView plugin(dbPath);
    CHECK(plugin.refresh() == LibraryState::Outdated);

    LibraryView standalone(dbPath, LibraryView::Access::MayMigrate);
    CHECK(standalone.refresh() == LibraryState::Open);
    CHECK(standalone.changed());
    CHECK(plugin.refresh() == LibraryState::Open); // the plugin reads the updated library too
}
