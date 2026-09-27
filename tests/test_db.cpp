// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Db.h"
#include "asma/core/Schema.h"

#include <catch2/catch_test_macros.hpp>

using asma::Db;
using asma::test::TempDir;

TEST_CASE("Db::open creates the file and parent dirs, enables WAL, migrates", "[db]")
{
    TempDir dir;
    const auto file = dir.path() / "sub" / "library.db";
    Db db = Db::open(file);
    CHECK(std::filesystem::exists(file));
    auto mode = db.prepare("PRAGMA journal_mode");
    REQUIRE(mode.step());
    CHECK(mode.getText(0) == "wal");
    CHECK(db.schemaVersion() == asma::currentSchemaVersion());
}

TEST_CASE("Reopening an up-to-date database keeps its data", "[db]")
{
    TempDir dir;
    const auto file = dir.path() / "library.db";
    {
        Db db = Db::open(file);
        db.exec("INSERT INTO roots(path) VALUES ('/x')");
    }
    Db db = Db::open(file);
    auto count = db.prepare("SELECT COUNT(*) FROM roots");
    REQUIRE(count.step());
    CHECK(count.getInt(0) == 1);
}

TEST_CASE("A database from a newer asma is refused", "[db]")
{
    TempDir dir;
    const auto file = dir.path() / "library.db";
    {
        Db db = Db::open(file);
        db.exec("PRAGMA user_version = 999");
    }
    CHECK_THROWS_AS(Db::open(file), asma::DbError);
}

TEST_CASE("FTS5 is compiled in and prefix queries work", "[db]")
{
    Db db = Db::openInMemory();
    db.exec("INSERT INTO fts_files(rowid, name, folder, tags) VALUES (7, 'Kick_01', 'Drums', 'kick')");
    auto q = db.prepare("SELECT rowid FROM fts_files WHERE fts_files MATCH ?");
    q.bind(1, "\"kic\"*");
    REQUIRE(q.step());
    CHECK(q.getInt(0) == 7);
}

TEST_CASE("Transaction rolls back unless committed", "[db]")
{
    Db db = Db::openInMemory();
    {
        asma::Transaction tx(db);
        db.exec("INSERT INTO roots(path) VALUES ('/a')");
    }
    {
        asma::Transaction tx(db);
        db.exec("INSERT INTO roots(path) VALUES ('/b')");
        tx.commit();
    }
    auto q = db.prepare("SELECT path FROM roots");
    REQUIRE(q.step());
    CHECK(q.getText(0) == "/b");
    CHECK_FALSE(q.step());
}

TEST_CASE("An empty string binds as text, not NULL", "[db]")
{
    Db db = Db::openInMemory();
    auto q = db.prepare("SELECT ? IS NULL, typeof(?)");
    q.bind(1, std::string_view{}).bind(2, std::string_view{});
    REQUIRE(q.step());
    CHECK(q.getInt(0) == 0);
    CHECK(q.getText(1) == "text");
}

TEST_CASE("bindOptional binds NULL for an empty optional", "[db]")
{
    Db db = Db::openInMemory();
    auto q = db.prepare("SELECT ? IS NULL, ?");
    q.bindOptional(1, std::optional<double>{}).bindOptional(2, std::optional<int>{5});
    REQUIRE(q.step());
    CHECK(q.getInt(0) == 1);
    CHECK(q.getInt(1) == 5);
}

TEST_CASE("Deleting a root cascades to its files", "[db]")
{
    Db db = Db::openInMemory();
    db.exec("INSERT INTO roots(id, path) VALUES (1, '/r')");
    db.exec("INSERT INTO files(root_id, rel_path, name, size, mtime, format, status) "
            "VALUES (1, 'a.wav', 'a.wav', 1, 1, 'wav', 'ok')");
    db.exec("DELETE FROM roots WHERE id = 1");
    auto q = db.prepare("SELECT COUNT(*) FROM files");
    REQUIRE(q.step());
    CHECK(q.getInt(0) == 0);
}

TEST_CASE("Bad SQL throws DbError with SQLite's message", "[db]")
{
    Db db = Db::openInMemory();
    CHECK_THROWS_AS(db.exec("SELEKT 1"), asma::DbError);
    CHECK_THROWS_AS(db.prepare("SELECT * FROM nope"), asma::DbError);
}

TEST_CASE("Blobs round-trip", "[db]")
{
    Db db = Db::openInMemory();
    const std::vector<unsigned char> bytes = {0, 1, 2, 250, 255};
    auto q = db.prepare("SELECT ?");
    q.bindBlob(1, bytes.data(), bytes.size());
    REQUIRE(q.step());
    CHECK(q.getBlob(0) == bytes);
}

TEST_CASE("Migration 2 labels where version 1 values came from", "[db]")
{
    Db db = Db::openInMemory(1);
    CHECK(db.schemaVersion() == 1);
    db.exec("INSERT INTO roots(id, path) VALUES (1, '/r')");
    db.exec("INSERT INTO files(id, root_id, rel_path, name, size, mtime, format, status) VALUES "
            "(1, 1, 'a.wav', 'a.wav', 1, 1, 'wav', 'ok'), (2, 1, 'b.wav', 'b.wav', 1, 1, 'wav', 'ok')");
    db.exec("INSERT INTO features(file_id, bpm, bpm_confidence, key, key_confidence, is_loop) VALUES "
            "(1, 128, 1.0, NULL, NULL, 1), (2, 90, 0.9, 'Am', 0.9, NULL)");
    asma::migrate(db);
    CHECK(db.schemaVersion() == asma::currentSchemaVersion());
    auto q = db.prepare("SELECT bpm_source, key_source, loop_source FROM features ORDER BY file_id");
    REQUIRE(q.step());
    CHECK(q.getText(0) == "embedded");
    CHECK(q.isNull(1));
    CHECK(q.getText(2) == "filename");
    REQUIRE(q.step());
    CHECK(q.getText(0) == "filename");
    CHECK(q.getText(1) == "filename");
    CHECK(q.isNull(2));
}
