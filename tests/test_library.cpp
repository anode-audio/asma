// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;

namespace {

FileRecord sampleRecord(std::int64_t rootId, std::string relPath, std::string hash = "00000000000000aa")
{
    FileRecord f;
    f.rootId = rootId;
    f.relPath = std::move(relPath);
    f.size = 100;
    f.mtime = 5;
    f.contentHash = std::move(hash);
    f.format = "wav";
    f.sampleRate = 44100;
    f.channels = 1;
    f.bitDepth = 16;
    f.duration = 0.1;
    return f;
}

std::int64_t ftsMatch(Db& db, const char* expr)
{
    auto q = db.prepare("SELECT rowid FROM fts_files WHERE fts_files MATCH ?");
    q.bind(1, expr);
    return q.step() ? q.getInt(0) : -1;
}

} // namespace

TEST_CASE("addRoot is idempotent for the same directory", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto a = lib.addRoot(dir.path());
    const auto b = lib.addRoot(dir.path() / "");
    CHECK(a == b);
    REQUIRE(lib.roots().size() == 1);
    CHECK(lib.roots()[0].enabled);
    CHECK(lib.root(a)->path == toUtf8(std::filesystem::weakly_canonical(dir.path())));
}

TEST_CASE("insertFile and fileById round-trip", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    FileRecord rec = sampleRecord(root, "Drums/Kick 01.wav", "");
    rec.status = FileStatus::Failed;
    rec.failureReason = "bad header";
    const auto id = lib.insertFile(rec);

    const auto back = lib.fileById(id);
    REQUIRE(back);
    CHECK(back->relPath == "Drums/Kick 01.wav");
    CHECK(back->contentHash.empty());
    CHECK(back->status == FileStatus::Failed);
    CHECK(back->failureReason == "bad header");
    CHECK(lib.fileByPath(root, "Drums/Kick 01.wav")->id == id);
    CHECK_FALSE(lib.fileById(id + 1).has_value());

    auto name = db.prepare("SELECT name FROM files WHERE id = ?");
    name.bind(1, id);
    REQUIRE(name.step());
    CHECK(name.getText(0) == "Kick 01.wav");
}

TEST_CASE("setDerived stores features and keeps user tags", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto id = lib.insertFile(sampleRecord(lib.addRoot(dir.path()), "Loops/Bass_Loop_Am_128.wav"));

    DerivedInfo d;
    d.bpm = 128.0;
    d.bpmConfidence = 0.9;
    d.key = "Am";
    d.keyConfidence = 0.9;
    d.isLoop = true;
    d.tags = {{"bass", TagSource::Auto}, {"kick", TagSource::Auto}};
    lib.setDerived(id, d);
    lib.addUserTag(id, "Punchy");
    lib.addUserTag(id, "kick"); // user claims an auto tag

    DerivedInfo again;
    again.tags = {{"snare", TagSource::Auto}};
    lib.setDerived(id, again);

    using Tags = std::vector<std::pair<std::string, TagSource>>;
    CHECK(lib.tags(id) == Tags{{"kick", TagSource::User}, {"punchy", TagSource::User}, {"snare", TagSource::Auto}});
    const auto features = lib.derived(id);
    REQUIRE(features);
    CHECK_FALSE(features->bpm.has_value()); // replaced by the second setDerived
    CHECK_FALSE(features->isLoop.has_value());
}

TEST_CASE("derived returns what setDerived stored", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto id = lib.insertFile(sampleRecord(lib.addRoot(dir.path()), "a.wav"));
    DerivedInfo d;
    d.bpm = 120.0;
    d.bpmConfidence = 1.0;
    d.isLoop = false;
    d.rootNote = 60;
    lib.setDerived(id, d);
    const auto back = lib.derived(id);
    REQUIRE(back);
    CHECK(back->bpm == 120.0);
    CHECK(back->bpmConfidence == 1.0);
    CHECK(back->isLoop == false);
    CHECK(back->rootNote == 60);
    CHECK_FALSE(back->key.has_value());
}

TEST_CASE("the FTS index follows names, folders and tags", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto id = lib.insertFile(sampleRecord(lib.addRoot(dir.path()), "Vintage Drums/Kick_01.wav"));
    lib.setDerived(id, {});
    CHECK(ftsMatch(db, "\"kick\"*") == id);
    CHECK(ftsMatch(db, "\"vint\"*") == id);
    CHECK(ftsMatch(db, "\"wav\"*") == -1); // extension is not indexed
    lib.addUserTag(id, "punchy");
    CHECK(ftsMatch(db, "\"punch\"*") == id);
}

TEST_CASE("relinkCandidates returns only missing rows with the same hash and size", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    const auto live = lib.insertFile(sampleRecord(root, "a.wav", "00000000000000aa"));
    const auto gone = lib.insertFile(sampleRecord(root, "b.wav", "00000000000000aa"));
    const auto other = lib.insertFile(sampleRecord(root, "c.wav", "00000000000000bb"));
    lib.setStatus(gone, FileStatus::Missing);
    lib.setStatus(other, FileStatus::Missing);

    const auto candidates = lib.relinkCandidates("00000000000000aa", 100);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].id == gone);
    CHECK(lib.relinkCandidates("00000000000000aa", 101).empty());
    (void)live;
}

TEST_CASE("setStatus to ok clears the failure reason", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto id = lib.insertFile(sampleRecord(lib.addRoot(dir.path()), "a.wav"));
    lib.setStatus(id, FileStatus::Failed, "boom");
    CHECK(lib.fileById(id)->failureReason == "boom");
    lib.setStatus(id, FileStatus::Ok);
    CHECK(lib.fileById(id)->failureReason.empty());
}
