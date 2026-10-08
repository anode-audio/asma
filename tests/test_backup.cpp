// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Backup.h"
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

// A sample folder and a library of it.
struct Lib {
    fs::path root;
    Db db = Db::openInMemory();
    Library lib{db};
    UserData user{db};
    std::int64_t rootId = 0;
    explicit Lib(fs::path folder) : root(std::move(folder))
    {
        rootId = lib.addRoot(root);
        scanRoot(db, rootId);
    }
    std::int64_t id(const std::string& rel) { return lib.fileByPath(rootId, rel).value().id; }
};

void wav(const fs::path& path, std::uint32_t seed)
{
    test::WavSpec spec;
    spec.seed = seed;
    test::writeWav(path, spec);
}

std::string read(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("a backup restores every kind of user data into a new library, by content", "[backup]")
{
    TempDir dir;
    const fs::path samples = dir.path() / "Samples";
    wav(samples / "Drums" / "kick.wav", 1);
    wav(samples / "Loops" / "bass.wav", 2);
    wav(samples / "Loops" / "pad.wav", 3);
    std::string backup;
    {
        Lib old(samples);
        old.user.setRating(old.id("Drums/kick.wav"), 4);
        old.user.setFavourite(old.id("Loops/bass.wav"), true);
        old.lib.addUserTag(old.id("Loops/bass.wav"), "dusty");
        const auto set = old.user.createCollection("Live set");
        old.user.addToCollection(set, old.id("Drums/kick.wav"));
        old.user.addToCollection(set, old.id("Loops/pad.wav"));
        SearchModel scoped;
        scoped.text = "bass";
        scoped.rootId = old.rootId;
        old.user.saveSearch("Bass here", scoped);
        SearchModel inSet;
        inSet.collectionId = set;
        old.user.saveSearch("Set", inSet);
        backup = backupJson(old.db, "2026-10-07T09:00:00Z");
    }
    // The files moved and were renamed meanwhile, and the new library's ids
    // differ from the old one's.
    fs::rename(samples / "Drums" / "kick.wav", samples / "Loops" / "Kick_01.wav");
    Lib fresh(samples);
    fresh.user.createCollection("Something else"); // shifts collection ids
    const RestoreStats r = restoreBackup(fresh.db, backup);
    CHECK(r.files == 2); // the rated kick and the tagged favourite; pad is only in a collection
    CHECK(r.unmatched == 0);
    CHECK(r.collections == 1);
    CHECK(r.searches == 2);
    CHECK(fresh.user.rating(fresh.id("Loops/Kick_01.wav")) == 4);
    CHECK(fresh.user.isFavourite(fresh.id("Loops/bass.wav")));
    const auto tags = fresh.lib.tags(fresh.id("Loops/bass.wav"));
    CHECK(std::find(tags.begin(), tags.end(), std::pair<std::string, TagSource>{"dusty", TagSource::User}) != tags.end());
    const auto set = fresh.user.collectionByName("Live set");
    REQUIRE(set);
    CHECK(set->size == 2);
    const auto scoped = fresh.user.savedSearchByName("Bass here");
    REQUIRE(scoped);
    CHECK(scoped->model.rootId == fresh.rootId);
    CHECK(scoped->model.text == "bass");
    CHECK(fresh.user.savedSearchByName("Set")->model.collectionId == set->id);
    CHECK(backupWrittenAt(backup) == "2026-10-07T09:00:00Z");
    CHECK(backupFolders(backup) == std::vector<std::string>{fresh.lib.root(fresh.rootId)->path});
}

TEST_CASE("a sample present twice gets its data twice; one changed since is counted, not lost silently", "[backup]")
{
    TempDir dir;
    const fs::path samples = dir.path() / "Samples";
    wav(samples / "kick.wav", 1);
    wav(samples / "snare.wav", 2);
    std::string backup;
    {
        Lib old(samples);
        old.user.setRating(old.id("kick.wav"), 5);
        old.user.setRating(old.id("snare.wav"), 2);
        backup = backupJson(old.db, "2026-10-07T09:00:00Z");
    }
    fs::copy_file(samples / "kick.wav", samples / "kick copy.wav");
    wav(samples / "snare.wav", 9); // re-encoded: other content
    Lib fresh(samples);
    const RestoreStats r = restoreBackup(fresh.db, backup);
    CHECK(r.files == 1);
    CHECK(r.unmatched == 1);
    CHECK(fresh.user.rating(fresh.id("kick.wav")) == 5);
    CHECK(fresh.user.rating(fresh.id("kick copy.wav")) == 5);
    CHECK_FALSE(fresh.user.rating(fresh.id("snare.wav")));
}

TEST_CASE("a backup from another asma version: unknown fields are ignored, a non-backup refused", "[backup]")
{
    TempDir dir;
    const fs::path samples = dir.path() / "Samples";
    wav(samples / "kick.wav", 1);
    Lib fresh(samples);
    const std::string hash = fresh.lib.fileById(fresh.id("kick.wav"))->contentHash;
    const std::string size = std::to_string(fresh.lib.fileById(fresh.id("kick.wav"))->size);
    const std::string newer = "{\"asma_backup\":2,\"written\":\"2027-01-01T00:00:00Z\",\"mood\":\"calm\","
                              "\"files\":[{\"hash\":\"" + hash + "\",\"size\":" + size +
                              ",\"rating\":3,\"colour\":\"red\"}]}";
    CHECK(restoreBackup(fresh.db, newer).files == 1);
    CHECK(fresh.user.rating(fresh.id("kick.wav")) == 3);
    CHECK_THROWS_AS(restoreBackup(fresh.db, "{\"files\":[]}"), JsonError);
    CHECK_THROWS_AS(restoreBackup(fresh.db, "not json"), JsonError);
}

TEST_CASE("writing a backup keeps the one before, and leaves no half-written file", "[backup]")
{
    TempDir dir;
    const fs::path samples = dir.path() / "Samples";
    wav(samples / "kick.wav", 1);
    Lib lib(samples);
    const fs::path path = dir.path() / "backup.json";
    const fs::path previous = dir.path() / "backup-previous.json";
    lib.user.setRating(lib.id("kick.wav"), 2);
    writeBackup(lib.db, path, previous);
    const std::string first = read(path);
    CHECK(first.find("\"rating\":2") != std::string::npos);
    lib.user.setRating(lib.id("kick.wav"), 5);
    writeBackup(lib.db, path, previous);
    CHECK(read(previous) == first);
    CHECK(read(path).find("\"rating\":5") != std::string::npos);
    CHECK_FALSE(fs::exists(dir.path() / "backup.json.tmp"));
    // A backup that cannot be written leaves both as they were.
    CHECK_THROWS(writeBackup(lib.db, dir.path() / "no-such-dir" / "backup.json", dir.path() / "no-such-dir" / "p.json"));
    CHECK(read(previous) == first);
}

TEST_CASE("data for samples not found is carried by each backup, and given back when they are", "[backup]")
{
    TempDir dir;
    const fs::path a = dir.path() / "A", b = dir.path() / "B";
    wav(a / "kick.wav", 1);
    wav(b / "pad.wav", 2); // on a drive that will be away
    const fs::path path = dir.path() / "backup.json", previous = dir.path() / "backup-previous.json";
    {
        Lib old(a);
        const auto bRoot = old.lib.addRoot(b);
        scanRoot(old.db, bRoot);
        const auto pad = old.lib.fileByPath(bRoot, "pad.wav")->id;
        old.user.setRating(pad, 4);
        old.user.addToCollection(old.user.createCollection("Pads"), pad);
        writeBackup(old.db, path, previous);
    }
    // Rebuilt while B was unplugged: pad.wav is nowhere to be found.
    Lib fresh(a);
    CHECK(restoreBackup(fresh.db, read(path)).unmatched == 1);
    writeBackup(fresh.db, path, previous);
    writeBackup(fresh.db, path, previous); // two days on: both files would have lost it
    const std::string kept = read(path);
    CHECK(kept.find("\"rating\":4") != std::string::npos);
    CHECK(read(previous).find("\"rating\":4") != std::string::npos);

    // B is back and scanned: the next backup gives pad.wav its data again.
    const auto bRoot = fresh.lib.addRoot(b);
    scanRoot(fresh.db, bRoot);
    writeBackup(fresh.db, path, previous);
    const auto pad = fresh.lib.fileByPath(bRoot, "pad.wav")->id;
    CHECK(fresh.user.rating(pad) == 4);
    REQUIRE(fresh.user.collectionByName("Pads"));
    CHECK(fresh.user.collectionByName("Pads")->size == 1);
}
