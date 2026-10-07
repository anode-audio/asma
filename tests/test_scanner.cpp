// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    TempDir dir;
    fs::path root = dir.path() / "lib";
    Db db = Db::openInMemory();
    Library lib{db};
    std::int64_t rootId = 0;

    Fixture()
    {
        fs::create_directories(root);
        rootId = lib.addRoot(root);
    }

    void wav(const std::string& rel, std::uint32_t seed = 1)
    {
        test::WavSpec spec;
        spec.seed = seed;
        test::writeWav(root / fromUtf8(rel), spec);
    }

    ScanStats scan(ScanOptions options = {}) { return scanRoot(db, rootId, options); }

    FileRecord file(const std::string& rel) { return lib.fileByPath(rootId, rel).value(); }
};

void touchLater(const fs::path& p)
{
    fs::last_write_time(p, fs::last_write_time(p) + std::chrono::seconds(2));
}

} // namespace

TEST_CASE("first scan adds audio files and ignores the rest", "[scanner]")
{
    Fixture f;
    f.wav("Drums/Kick_01.wav", 1);
    f.wav("Drums/Snare_01.wav", 2);
    f.wav("Loops/Bass_Loop_Am_128.wav", 3);
    test::writeBytes(f.root / "notes.txt", "hello");
    f.wav("Drums/._Kick_01.wav", 4);
    f.wav(".hidden/Secret.wav", 5);

    const ScanStats s = f.scan();
    CHECK(s.added == 3);
    CHECK(s.failed == 0);
    CHECK(f.lib.filesInRoot(f.rootId).size() == 3);
}

TEST_CASE("a second scan with no changes touches nothing", "[scanner]")
{
    Fixture f;
    f.wav("a.wav", 1);
    f.wav("b.wav", 2);
    f.scan();
    const ScanStats s = f.scan();
    CHECK(s.added == 0);
    CHECK(s.updated == 0);
    CHECK(s.unchanged == 2);
}

TEST_CASE("a changed file is re-hashed and its analysis reset", "[scanner]")
{
    Fixture f;
    f.wav("a.wav", 1);
    f.scan();
    const std::string before = f.file("a.wav").contentHash;
    f.db.exec("UPDATE files SET analysis_version = 3");

    f.wav("a.wav", 9);
    touchLater(f.root / "a.wav");
    const ScanStats s = f.scan();
    CHECK(s.updated == 1);
    CHECK(f.file("a.wav").contentHash != before);
    auto q = f.db.prepare("SELECT analysis_version FROM files");
    REQUIRE(q.step());
    CHECK(q.getInt(0) == 0);
}

TEST_CASE("a moved file keeps its id and user tags", "[scanner]")
{
    Fixture f;
    f.wav("Kick.wav", 1);
    f.scan();
    const auto id = f.file("Kick.wav").id;
    f.lib.addUserTag(id, "favourite");

    fs::create_directories(f.root / "Sorted");
    fs::rename(f.root / "Kick.wav", f.root / "Sorted" / "Kick.wav");
    const ScanStats s = f.scan();
    CHECK(s.relinked == 1);
    CHECK(s.added == 0);
    CHECK(s.missing == 0);
    const auto moved = f.file("Sorted/Kick.wav");
    CHECK(moved.id == id);
    CHECK(moved.status == FileStatus::Ok);
    const auto tags = f.lib.tags(id);
    CHECK(std::find(tags.begin(), tags.end(), std::make_pair(std::string("favourite"), TagSource::User))
          != tags.end());
}

TEST_CASE("a file moved to another root is re-linked there", "[scanner]")
{
    Fixture f;
    const fs::path other = f.dir.path() / "other";
    fs::create_directories(other);
    const auto otherId = f.lib.addRoot(other);
    f.wav("Kick.wav", 1);
    f.scan();
    const auto id = f.file("Kick.wav").id;

    fs::rename(f.root / "Kick.wav", other / "Kick.wav");
    f.scan();                              // marks it missing in the first root
    const ScanStats s = scanRoot(f.db, otherId);
    CHECK(s.relinked == 1);
    CHECK(f.lib.fileById(id)->rootId == otherId);
}

TEST_CASE("a copy in another root does not take the row of a file on an unplugged drive", "[scanner]")
{
    Fixture f;
    const fs::path other = f.dir.path() / "other";
    fs::create_directories(other);
    const auto otherId = f.lib.addRoot(other);
    f.wav("Kick.wav", 1);
    f.scan();
    const auto id = f.file("Kick.wav").id;
    f.lib.addUserTag(id, "favourite");

    fs::copy_file(f.root / "Kick.wav", other / "Kick_copy.wav");
    const fs::path away = f.dir.path() / "unplugged";
    fs::rename(f.root, away);
    f.scan(); // the drive is gone: the row goes missing
    const ScanStats s = scanRoot(f.db, otherId);
    CHECK(s.relinked == 0);
    CHECK(s.added == 1);

    fs::rename(away, f.root);
    f.scan();
    const auto back = f.lib.fileById(id).value();
    CHECK(back.rootId == f.rootId);
    CHECK(back.status == FileStatus::Ok);
    const auto tags = f.lib.tags(id);
    CHECK(std::find(tags.begin(), tags.end(), std::make_pair(std::string("favourite"), TagSource::User))
          != tags.end());
}

TEST_CASE("duplicate copies do not steal each other's rows", "[scanner]")
{
    Fixture f;
    f.wav("a.wav", 7);
    f.scan();
    fs::copy_file(f.root / "a.wav", f.root / "copy.wav");
    const ScanStats s = f.scan();
    CHECK(s.added == 1);
    CHECK(s.relinked == 0);
    CHECK(f.file("a.wav").status == FileStatus::Ok);
}

TEST_CASE("a deleted file becomes missing and comes back unchanged", "[scanner]")
{
    Fixture f;
    f.wav("a.wav", 1);
    f.scan();
    const auto id = f.file("a.wav").id;
    const fs::path stash = f.dir.path() / "a.wav";
    fs::rename(f.root / "a.wav", stash);

    CHECK(f.scan().missing == 1);
    CHECK(f.lib.fileById(id)->status == FileStatus::Missing);

    fs::rename(stash, f.root / "a.wav");
    const ScanStats back = f.scan();
    CHECK(back.updated == 1);
    CHECK(f.lib.fileById(id)->status == FileStatus::Ok);
}

TEST_CASE("an unmounted root marks everything missing without deleting rows", "[scanner]")
{
    Fixture f;
    f.wav("a.wav", 1);
    f.wav("b.wav", 2);
    f.scan();
    const auto id = f.file("a.wav").id;
    f.lib.addUserTag(id, "keeper");
    const fs::path away = f.dir.path() / "unplugged";
    fs::rename(f.root, away);

    CHECK(f.scan().missing == 2);
    CHECK(f.lib.filesInRoot(f.rootId).size() == 2);

    fs::rename(away, f.root);
    f.scan();
    CHECK(f.lib.fileById(id)->status == FileStatus::Ok);
    CHECK(f.lib.tags(id).size() == 1);
}

#ifndef _WIN32
TEST_CASE("an unreadable folder's files come back when readable again", "[scanner]")
{
    Fixture f;
    f.wav("Locked/a.wav", 1);
    f.scan();
    const auto id = f.file("Locked/a.wav").id;
    fs::permissions(f.root / "Locked", fs::perms::none);
    f.scan();
    fs::permissions(f.root / "Locked", fs::perms::owner_all);
    f.scan();
    CHECK(f.lib.fileById(id)->status == FileStatus::Ok);
}
#endif

#ifndef _WIN32
TEST_CASE("an unreadable new file is skipped, not failed, and added once readable", "[scanner]")
{
    Fixture f;
    f.wav("locked.wav", 1);
    fs::permissions(f.root / "locked.wav", fs::perms::none);
    const ScanStats first = f.scan();
    CHECK(first.failed == 0);
    CHECK(first.skipped == 1);
    CHECK_FALSE(f.lib.fileByPath(f.rootId, "locked.wav").has_value());

    fs::permissions(f.root / "locked.wav", fs::perms::owner_read | fs::perms::owner_write);
    CHECK(f.scan().added == 1);
}

TEST_CASE("a known file that cannot be read keeps its ok row", "[scanner]")
{
    Fixture f;
    f.wav("a.wav", 1);
    f.scan();
    touchLater(f.root / "a.wav");
    fs::permissions(f.root / "a.wav", fs::perms::none);
    const ScanStats s = f.scan();
    fs::permissions(f.root / "a.wav", fs::perms::owner_read | fs::perms::owner_write);
    CHECK(s.skipped == 1);
    CHECK(f.file("a.wav").status == FileStatus::Ok);
}
#endif

TEST_CASE("broken files are failed with a reason and skipped until they change", "[scanner]")
{
    Fixture f;
    test::writeBytes(f.root / "broken.wav", "not audio");
    test::writeBytes(f.root / "empty.mp3", "");
    f.wav("good.wav", 1);

    const ScanStats first = f.scan();
    CHECK(first.failed == 2);
    CHECK(first.added == 1);
    CHECK(f.file("broken.wav").status == FileStatus::Failed);
    CHECK_FALSE(f.file("broken.wav").failureReason.empty());

    CHECK(f.scan().unchanged == 3);

    f.wav("broken.wav", 5);
    touchLater(f.root / "broken.wav");
    f.scan();
    CHECK(f.file("broken.wav").status == FileStatus::Ok);
}

TEST_CASE("a failed file that goes missing and comes back is still failed", "[scanner]")
{
    Fixture f;
    test::writeBytes(f.root / "Broken.wav", "not audio");
    f.wav("crashy.wav", 2);
    f.scan();
    markFailedPath(f.db, f.rootId, "crashy.wav", "crashed the scanner");
    const fs::path away = f.dir.path() / "unplugged";
    fs::rename(f.root, away);
    f.scan();
    fs::rename(away, f.root);
    f.scan();

    CHECK(f.file("Broken.wav").status == FileStatus::Failed);
    CHECK_FALSE(f.file("Broken.wav").failureReason.empty());
    CHECK(f.file("crashy.wav").status == FileStatus::Failed);
    CHECK(f.file("crashy.wav").failureReason == "crashed the scanner");
}

TEST_CASE("markFailedPath makes later scans skip the file", "[scanner]")
{
    Fixture f;
    f.wav("crashy.wav", 1);
    markFailedPath(f.db, f.rootId, "crashy.wav", "crashed the scanner");
    CHECK(f.file("crashy.wav").status == FileStatus::Failed);
    const ScanStats s = f.scan();
    CHECK(s.unchanged == 1);
    CHECK(s.added == 0);
    CHECK(f.file("crashy.wav").failureReason == "crashed the scanner");
}

TEST_CASE("derived info comes from ACID, smpl and the file name", "[scanner]")
{
    Fixture f;
    f.wav("Loops/Bass_Loop_Am_128.wav", 1);
    test::WavSpec acid;
    acid.seed = 2;
    acid.acid = std::make_pair(true, 0.0f); // one-shot, no tempo
    acid.smplUnityNote = 36;
    test::writeWav(f.root / "Loops/Kick_Hit.wav", acid);
    f.scan();

    const auto loop = f.lib.derived(f.file("Loops/Bass_Loop_Am_128.wav").id).value();
    CHECK(loop.bpm == 128.0);
    CHECK(loop.bpmConfidence == 0.9);
    CHECK(loop.key == "Am");
    CHECK(loop.isLoop == true);
    const auto loopTags = f.lib.tags(f.file("Loops/Bass_Loop_Am_128.wav").id);
    CHECK(std::find(loopTags.begin(), loopTags.end(), std::make_pair(std::string("bass"), TagSource::Auto))
          != loopTags.end());

    const auto kick = f.lib.derived(f.file("Loops/Kick_Hit.wav").id).value();
    CHECK(kick.isLoop == false); // ACID flag beats the "Loops" folder
    CHECK_FALSE(kick.bpm.has_value());
    CHECK(kick.rootNote == 36);
}

TEST_CASE("progress callbacks cover every file and small batches work", "[scanner]")
{
    Fixture f;
    for (int i = 0; i < 5; ++i) f.wav("f" + std::to_string(i) + ".wav", static_cast<std::uint32_t>(i + 1));
    std::size_t starts = 0;
    std::size_t lastDone = 0;
    std::size_t lastTotal = 0;
    ScanOptions options;
    options.threads = 3;
    options.batchSize = 2;
    options.onFileStart = [&](std::string_view) { ++starts; };
    options.onProgress = [&](std::size_t done, std::size_t total, std::string_view) {
        lastDone = done;
        lastTotal = total;
    };
    const ScanStats s = f.scan(options);
    CHECK(s.added == 5);
    CHECK(starts == 5);
    CHECK(lastDone == 5);
    CHECK(lastTotal == 5);
}

TEST_CASE("non-ASCII paths are scanned and stored as UTF-8", "[scanner]")
{
    Fixture f;
    f.wav("Café Loops/Kick Ü.wav", 1);
    CHECK(f.scan().added == 1);
    CHECK(f.lib.fileByPath(f.rootId, "Café Loops/Kick Ü.wav").has_value());
}

TEST_CASE("an unknown root id throws", "[scanner]")
{
    Db db = Db::openInMemory();
    CHECK_THROWS_AS(scanRoot(db, 42), std::invalid_argument);
}

TEST_CASE("retry reads a fixed file again and queues it for analysis", "[scanner][retry]")
{
    Fixture f;
    test::writeBytes(f.root / "broken.wav", "not audio");
    f.scan();
    REQUIRE(f.file("broken.wav").status == FileStatus::Failed);

    f.wav("broken.wav", 5); // fixed in place; a scan would only see it if size or mtime changed
    const RetryStats r = retryFiles(f.db, {f.file("broken.wav").id});
    CHECK(r.readable == std::vector<std::int64_t>{f.file("broken.wav").id});
    CHECK(r.failed == 0);
    CHECK(f.file("broken.wav").status == FileStatus::Ok);
    CHECK(f.file("broken.wav").failureReason.empty());
    CHECK_FALSE(f.file("broken.wav").contentHash.empty());
}

TEST_CASE("retry keeps a file that still fails, with the new reason", "[scanner][retry]")
{
    Fixture f;
    test::writeBytes(f.root / "broken.wav", "not audio");
    f.scan();
    f.db.exec("UPDATE files SET failure_reason = 'an old reason'");

    const RetryStats r = retryFiles(f.db, {f.file("broken.wav").id});
    CHECK(r.readable.empty());
    CHECK(r.failed == 1);
    CHECK(f.file("broken.wav").status == FileStatus::Failed);
    CHECK(f.file("broken.wav").failureReason != "an old reason");
    CHECK_FALSE(f.file("broken.wav").failureReason.empty());
}

TEST_CASE("retry says a file that has gone is gone", "[scanner][retry]")
{
    Fixture f;
    test::writeBytes(f.root / "broken.wav", "not audio");
    f.scan();
    fs::remove(f.root / "broken.wav");

    const RetryStats r = retryFiles(f.db, {f.file("broken.wav").id});
    CHECK(r.gone == 1);
    CHECK(f.file("broken.wav").status == FileStatus::Failed);
    CHECK(f.file("broken.wav").failureReason == "The file is gone");
}

TEST_CASE("retry refuses an unknown id and writes nothing", "[scanner][retry]")
{
    Fixture f;
    test::writeBytes(f.root / "broken.wav", "not audio");
    f.scan();
    f.wav("broken.wav", 5);
    CHECK_THROWS_AS(retryFiles(f.db, {f.file("broken.wav").id, 9999}), std::invalid_argument);
    CHECK(f.file("broken.wav").status == FileStatus::Failed);
}

TEST_CASE("retry holds no write lock while it reads a file", "[scanner][retry]")
{
    TempDir dir;
    const fs::path dbPath = dir.path() / "library.db";
    const fs::path root = dir.path() / "lib";
    test::writeBytes(root / "broken.wav", "not audio");
    Db db = Db::open(dbPath);
    Library lib(db);
    const auto rootId = lib.addRoot(root);
    scanRoot(db, rootId);
    test::WavSpec spec;
    spec.seed = 5;
    test::writeWav(root / "broken.wav", spec);

    bool wrote = false;
    retryFiles(db, {lib.fileByPath(rootId, "broken.wav")->id}, [&](std::int64_t) {
        // A rating made while the retry reads must not wait for it.
        Db other = Db::open(dbPath);
        other.exec("PRAGMA busy_timeout = 0");
        CHECK_NOTHROW(other.exec("INSERT INTO collections(name) VALUES ('meanwhile')"));
        wrote = true;
    });
    CHECK(wrote);
    CHECK(lib.fileByPath(rootId, "broken.wav")->status == FileStatus::Ok);
}

TEST_CASE("a file that crashes a retry costs only itself, and goes last next time", "[scanner][retry]")
{
    Fixture f;
    test::writeBytes(f.root / "a.wav", "not audio");
    test::writeBytes(f.root / "b.wav", "not audio");
    test::writeBytes(f.root / "c.wav", "not audio");
    f.scan();
    f.wav("b.wav", 5);
    f.wav("c.wav", 6);
    const auto a = f.file("a.wav").id, b = f.file("b.wav").id, c = f.file("c.wav").id;
    const auto crashOn = [&](std::int64_t id) { return [id](std::int64_t probing) {
        if (probing == id) throw std::runtime_error("stands in for a crash");
    }; };

    CHECK_THROWS(retryFiles(f.db, {b, a}, crashOn(a)));
    CHECK(f.file("b.wav").status == FileStatus::Ok); // done before the crash, and kept
    CHECK(f.file("a.wav").failureReason == kCrashedReason);

    CHECK_THROWS(retryFiles(f.db, {a, c}, crashOn(a))); // a goes last: c is done first
    CHECK(f.file("c.wav").status == FileStatus::Ok);
}

TEST_CASE("retry leaves an ok file it cannot reach as it is", "[scanner][retry]")
{
    Fixture f;
    f.wav("silent.wav", 3);
    f.scan();
    f.lib.setAnalysisError(f.file("silent.wav").id, "the file is silent");
    fs::remove(f.root / "silent.wav"); // a drive unplugged, say

    const RetryStats r = retryFiles(f.db, {f.file("silent.wav").id});
    CHECK(r.skipped == 1);
    CHECK(r.readable.empty());
    CHECK(f.file("silent.wav").status == FileStatus::Ok); // still in the library
    CHECK(f.lib.problems().size() == 1);                  // still with its old reason
    CHECK(f.lib.problems()[0].reason == "the file is silent");
}

TEST_CASE("a scan reads again a failed file said to be gone once it is back", "[scanner][retry]")
{
    Fixture f;
    f.wav("back.wav", 4);
    f.scan();
    f.lib.setStatus(f.file("back.wav").id, FileStatus::Failed, kGoneReason); // retried while away
    f.scan(); // the file is there, unchanged
    CHECK(f.file("back.wav").status == FileStatus::Ok);
    CHECK(f.file("back.wav").failureReason.empty());
}
