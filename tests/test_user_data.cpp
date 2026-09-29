// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"
#include "asma/core/Schema.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    TempDir dir;
    fs::path root = dir.path() / "lib";
    Db db = Db::openInMemory();
    Library lib{db};
    UserData user{db};
    std::int64_t rootId = 0;

    Fixture()
    {
        fs::create_directories(root);
        rootId = lib.addRoot(root);
    }

    std::int64_t wav(const std::string& rel, std::uint32_t seed = 1)
    {
        test::WavSpec spec;
        spec.seed = seed;
        test::writeWav(root / fromUtf8(rel), spec);
        ScanOptions options;
        scanRoot(db, rootId, options);
        return lib.fileByPath(rootId, rel).value().id;
    }
};

} // namespace

TEST_CASE("migration 3 adds the user-data tables to a version 2 library", "[userdata]")
{
    Db db = Db::openInMemory(2);
    db.exec("INSERT INTO roots(id, path) VALUES (1, '/r')");
    db.exec("INSERT INTO files(id, root_id, rel_path, name, size, mtime, format, status) VALUES "
            "(1, 1, 'a.wav', 'a.wav', 1, 1, 'wav', 'ok')");
    migrate(db);
    CHECK(db.schemaVersion() == 3);
    UserData user(db);
    user.setRating(1, 4);
    CHECK(user.rating(1) == 4);
}

TEST_CASE("ratings are 1 to 5 and 0 clears them", "[userdata]")
{
    Fixture f;
    const auto id = f.wav("a.wav");
    CHECK_FALSE(f.user.rating(id));
    f.user.setRating(id, 5);
    CHECK(f.user.rating(id) == 5);
    f.user.setRating(id, 2);
    CHECK(f.user.rating(id) == 2);
    f.user.setRating(id, 0);
    CHECK_FALSE(f.user.rating(id));
    CHECK_THROWS_AS(f.user.setRating(id, 6), UserDataError);
    CHECK_THROWS_AS(f.user.setRating(id, -1), UserDataError);
    CHECK_THROWS_AS(f.user.setRating(id + 100, 3), UserDataError);
}

TEST_CASE("favourites toggle and ignore repeats", "[userdata]")
{
    Fixture f;
    const auto id = f.wav("a.wav");
    f.user.setFavourite(id, true);
    f.user.setFavourite(id, true);
    CHECK(f.user.isFavourite(id));
    f.user.setFavourite(id, false);
    f.user.setFavourite(id, false);
    CHECK_FALSE(f.user.isFavourite(id));
    CHECK_THROWS_AS(f.user.setFavourite(id + 100, true), UserDataError);
}

TEST_CASE("collections have unique trimmed names, ignoring case", "[userdata]")
{
    Fixture f;
    const auto drums = f.user.createCollection("  Drums ");
    CHECK(f.user.collectionByName("drums")->id == drums);
    CHECK(f.user.collectionByName("DRUMS")->name == "Drums");
    CHECK_THROWS_AS(f.user.createCollection("drums"), UserDataError);
    CHECK_THROWS_AS(f.user.createCollection("   "), UserDataError);

    const auto bass = f.user.createCollection("Bass");
    CHECK_THROWS_AS(f.user.renameCollection(bass, "DRUMS"), UserDataError);
    f.user.renameCollection(drums, "drums"); // a case change of its own name is fine
    CHECK(f.user.collectionByName("Drums")->name == "drums");

    const auto all = f.user.collections();
    REQUIRE(all.size() == 2);
    CHECK(all[0].name == "Bass");
    CHECK(all[1].name == "drums");
    CHECK_THROWS_AS(f.user.renameCollection(999, "x"), UserDataError);
    CHECK_THROWS_AS(f.user.deleteCollection(999), UserDataError);
}

TEST_CASE("collection items: add twice, remove, delete the collection", "[userdata]")
{
    Fixture f;
    const auto a = f.wav("a.wav", 1);
    const auto b = f.wav("b.wav", 2);
    const auto c = f.user.createCollection("Set");
    f.user.addToCollection(c, a);
    f.user.addToCollection(c, a);
    f.user.addToCollection(c, b);
    CHECK(f.user.collectionByName("Set")->size == 2);
    f.user.removeFromCollection(c, a);
    CHECK(f.user.collectionByName("Set")->size == 1);
    CHECK_THROWS_AS(f.user.addToCollection(c, 999), UserDataError);
    f.user.deleteCollection(c);
    CHECK(f.user.collections().empty());
    CHECK(f.lib.fileById(b)); // the file itself stays
    CHECK(f.user.createCollection("Next") != c); // ids are never reused
}

TEST_CASE("user data follows a file the scanner re-links", "[userdata]")
{
    Fixture f;
    const auto id = f.wav("old/a.wav", 7);
    const auto c = f.user.createCollection("Keep");
    f.user.setRating(id, 4);
    f.user.setFavourite(id, true);
    f.user.addToCollection(c, id);

    fs::create_directories(f.root / "new");
    fs::rename(f.root / "old" / "a.wav", f.root / "new" / "a.wav");
    scanRoot(f.db, f.rootId, {});

    const auto moved = f.lib.fileByPath(f.rootId, "new/a.wav").value();
    CHECK(moved.id == id);
    CHECK(f.user.rating(id) == 4);
    CHECK(f.user.isFavourite(id));
    CHECK(f.user.collectionByName("Keep")->size == 1);
}

TEST_CASE("user data stays on a missing file and cascades when its root goes", "[userdata]")
{
    Fixture f;
    const auto id = f.wav("a.wav");
    f.user.setRating(id, 3);
    fs::remove(f.root / "a.wav");
    scanRoot(f.db, f.rootId, {});
    CHECK(f.lib.fileById(id)->status == FileStatus::Missing);
    CHECK(f.user.rating(id) == 3);

    f.db.exec("DELETE FROM roots");
    CHECK_FALSE(f.user.rating(id));
}

TEST_CASE("removeUserTag drops only user tags and updates text search", "[userdata]")
{
    Fixture f;
    const auto id = f.wav("kick_01.wav");
    f.lib.addUserTag(id, "Punchy");
    f.lib.removeUserTag(id, "punchy");
    for (const auto& [name, source] : f.lib.tags(id)) CHECK(name != "punchy");
    auto q = f.db.prepare("SELECT count(*) FROM fts_files WHERE fts_files MATCH 'punchy'");
    REQUIRE(q.step());
    CHECK(q.getInt(0) == 0);

    // "kick" came from the file name; removing it as a user tag does nothing.
    f.lib.removeUserTag(id, "kick");
    bool hasKick = false;
    for (const auto& [name, source] : f.lib.tags(id)) hasKick = hasKick || name == "kick";
    CHECK(hasKick);
}

TEST_CASE("user data follows a file moved to another root, whichever root is scanned first", "[userdata]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    UserData user(db);
    const fs::path a = dir.path() / "a";
    const fs::path b = dir.path() / "b";
    fs::create_directories(a);
    test::WavSpec spec;
    spec.seed = 11;
    test::writeWav(b / "Kick.wav", spec);
    const auto rootA = lib.addRoot(a);
    const auto rootB = lib.addRoot(b);
    scanRoot(db, rootA, {});
    scanRoot(db, rootB, {});
    const auto id = lib.fileByPath(rootB, "Kick.wav").value().id;
    user.setRating(id, 4);

    fs::rename(b / "Kick.wav", a / "Kick.wav");
    const ScanStats first = scanRoot(db, rootA, {}); // the destination first
    scanRoot(db, rootB, {});

    CHECK(first.relinked == 1);
    const auto moved = lib.fileByPath(rootA, "Kick.wav").value();
    CHECK(moved.id == id);
    CHECK(user.rating(id) == 4);
    CHECK_FALSE(lib.fileByPath(rootB, "Kick.wav"));
}
