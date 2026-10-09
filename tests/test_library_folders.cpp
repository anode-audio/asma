// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"
#include "asma/core/Scanner.h"
#include "asma/core/Schema.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

TEST_CASE("The library has a journal and marks trashed samples", "[folders]")
{
    Db db = Db::openInMemory();
    CHECK(currentSchemaVersion() == 4);
    db.exec("INSERT INTO journal_groups(label, at, state) VALUES ('Move kick.wav', '2026-10-09T10:00:00Z', 'running')");
    db.exec("INSERT INTO journal(group_id, op, file_id, src, dst, state, at) "
            "VALUES (1, 'move', 7, '/a/kick.wav', '/b/kick.wav', 'planned', '2026-10-09T10:00:00Z')");
    auto q = db.prepare("SELECT COUNT(*) FROM files WHERE trashed_by IS NULL");
    REQUIRE(q.step());
    CHECK(q.getInt(0) == 0);
}

TEST_CASE("A removed folder's samples are hidden and keep their data until it is added again", "[folders]")
{
    TempDir dir;
    const fs::path samples = dir.path() / "Samples";
    test::WavSpec spec;
    test::writeWav(samples / "kick.wav", spec);
    spec.seed = 2;
    test::writeWav(samples / "snare.wav", spec);
    Db db = Db::open(dir.path() / "library.db");
    Library lib(db);
    const auto root = lib.addRoot(samples);
    scanRoot(db, root);
    const auto kick = lib.fileByPath(root, "kick.wav")->id;
    UserData user(db);
    user.setRating(kick, 4);
    const auto drums = user.createCollection("Drums");
    user.addToCollection(drums, kick);
    REQUIRE(countSearch(db, SearchModel{}) == 2);

    lib.setRootEnabled(root, false);
    CHECK_FALSE(lib.root(root)->enabled);
    CHECK(countSearch(db, SearchModel{}) == 0);
    SearchModel inDrums;
    inDrums.collectionId = drums;
    CHECK(countSearch(db, inDrums) == 0);
    CHECK(lib.problems().empty());

    CHECK(lib.addRoot(samples) == root); // the same folder, enabled again
    CHECK(lib.root(root)->enabled);
    CHECK(countSearch(db, SearchModel{}) == 2);
    CHECK(countSearch(db, inDrums) == 1);
    CHECK(user.rating(kick) == 4);
}

TEST_CASE("rootOf finds the deepest folder holding a path", "[folders]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    fs::create_directories(dir.path() / "Samples" / "Drums");
    const auto samples = lib.addRoot(dir.path() / "Samples");
    const auto drums = lib.addRoot(dir.path() / "Samples" / "Drums");
    lib.setRootEnabled(drums, false);
    const fs::path kick = dir.path() / "Samples" / "Drums" / "kick.wav";

    const auto any = lib.rootOf(kick, false);
    REQUIRE(any);
    CHECK(any->first.id == drums);
    CHECK(any->second == "kick.wav");
    const auto enabled = lib.rootOf(kick);
    REQUIRE(enabled);
    CHECK(enabled->first.id == samples);
    CHECK(enabled->second == "Drums/kick.wav");
    CHECK(lib.rootOf(dir.path() / "Samples")->second.empty()); // the folder itself
    CHECK_FALSE(lib.rootOf(dir.path() / "Elsewhere" / "kick.wav"));
    CHECK_FALSE(lib.rootOf(dir.path() / "Samples2" / "kick.wav")); // a prefix of the name is not inside
}

TEST_CASE("A trashed sample is never relinked to a copy of it", "[folders]")
{
    TempDir dir;
    test::WavSpec spec;
    test::writeWav(dir.path() / "A" / "kick.wav", spec);
    Db db = Db::open(dir.path() / "library.db");
    Library lib(db);
    const auto a = lib.addRoot(dir.path() / "A");
    scanRoot(db, a);
    const auto kick = lib.fileByPath(a, "kick.wav")->id;
    // As the trash leaves it: missing, marked, and off its path.
    db.exec("UPDATE files SET status = 'missing', trashed_by = 1, rel_path = '/trashed/1' WHERE id = "
            + std::to_string(kick));
    fs::remove(dir.path() / "A" / "kick.wav");

    test::writeWav(dir.path() / "B" / "kick.wav", spec); // the same content elsewhere
    const auto b = lib.addRoot(dir.path() / "B");
    const auto stats = scanRoot(db, b);
    CHECK(stats.added == 1);
    CHECK(stats.relinked == 0);
    CHECK(lib.fileById(kick)->relPath == "/trashed/1");
    test::writeWav(dir.path() / "A" / "kick.wav", spec); // and a new file where it was
    CHECK(scanRoot(db, a).added == 1);
}
