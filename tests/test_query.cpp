// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Analysis.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"
#include "asma/core/Similar.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>

using namespace asma;
using asma::test::TempDir;

namespace {

struct Seeded {
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib{db};
    std::int64_t root = lib.addRoot(dir.path() / "a");
    std::int64_t other = lib.addRoot(dir.path() / "b");

    std::int64_t add(std::int64_t rootId, const std::string& rel, double duration, std::optional<double> bpm,
                     std::optional<std::string> key, std::optional<bool> loop, std::vector<std::string> tags,
                     std::string format = "wav")
    {
        FileRecord f;
        f.rootId = rootId;
        f.relPath = rel;
        f.size = 1;
        f.mtime = 1;
        f.contentHash = "00000000000000aa";
        f.format = std::move(format);
        f.duration = duration;
        const auto id = lib.insertFile(f);
        DerivedInfo d;
        d.bpm = bpm;
        d.bpmConfidence = 0.9;
        d.key = std::move(key);
        d.keyConfidence = 0.9;
        d.isLoop = loop;
        for (auto& t : tags) d.tags.emplace_back(t, TagSource::Auto);
        lib.setDerived(id, d);
        return id;
    }

    std::int64_t kick, snare, bassLoop, padLoop, flacHit;

    Seeded()
    {
        kick = add(root, "Drums/Kick_01.wav", 0.4, std::nullopt, std::nullopt, false, {"kick"});
        snare = add(root, "Drums/snare_02.wav", 0.3, std::nullopt, std::nullopt, false, {"snare"});
        bassLoop = add(root, "Loops/Bass_Loop_Am_128.wav", 7.5, 128.0, "Am", true, {"bass"});
        padLoop = add(root, "Loops/Pad_Loop_C_90.wav", 10.6, 90.0, "C", true, {"pad", "synth"});
        flacHit = add(other, "Hits/Kick_Deep.flac", 0.8, std::nullopt, std::nullopt, false, {"kick"}, "flac");
    }
};

std::vector<std::int64_t> ids(const std::vector<SearchRow>& rows)
{
    std::vector<std::int64_t> out;
    for (const auto& r : rows) out.push_back(r.id);
    return out;
}

using Ids = std::vector<std::int64_t>;

} // namespace

TEST_CASE("ftsMatchExpression quotes each word as a prefix", "[query]")
{
    CHECK(ftsMatchExpression("kick 808") == "\"kick\"* \"808\"*");
    CHECK(ftsMatchExpression("  ") == "");
    CHECK(ftsMatchExpression("kick\" OR * -(x)") == "\"kick\"* \"OR\"* \"x\"*");
    CHECK(ftsMatchExpression("café") == "\"café\"*");
}

TEST_CASE("an empty model lists every ok file by name, case-insensitively", "[query]")
{
    Seeded s;
    CHECK(ids(search(s.db, {})) == Ids{s.bassLoop, s.kick, s.flacHit, s.padLoop, s.snare});
}

TEST_CASE("countSearch counts every match, past the page the search returns", "[query]")
{
    Seeded s;
    SearchModel page;
    page.limit = 2;
    CHECK(search(s.db, page).size() == 2);
    CHECK(countSearch(s.db, page) == 5);
    page.text = "kick";
    CHECK(countSearch(s.db, page) == 2);
}

TEST_CASE("each row carries its tags, sorted", "[query]")
{
    Seeded s;
    for (const auto& r : search(s.db, {}))
        if (r.id == s.padLoop) CHECK(r.tags == std::vector<std::string>{"pad", "synth"});
        else if (r.id == s.kick) CHECK(r.tags == std::vector<std::string>{"kick"});
}

TEST_CASE("tagCounts lists the tags searches can find, most used first", "[query]")
{
    Seeded s;
    const auto tags = tagCounts(s.db);
    REQUIRE(tags.size() == 5);
    CHECK(tags[0].name == "kick"); // two files
    CHECK(tags[0].count == 2);
    CHECK(tags[1].name == "bass"); // then by name
    CHECK(tags[1].count == 1);
    s.lib.setStatus(s.kick, FileStatus::Missing);
    CHECK(tagCounts(s.db)[0].count == 1); // only files a search shows
}

TEST_CASE("searchPosition finds a file's row in the sorted results", "[query]")
{
    Seeded s;
    SearchModel m; // by name: Bass_Loop, Kick_01, Kick_Deep, Pad_Loop, snare
    CHECK(searchPosition(s.db, m, s.bassLoop) == 0);
    CHECK(searchPosition(s.db, m, s.padLoop) == 3);
    m.sort = SortField::Duration;
    m.descending = true; // Pad 10.6, Bass 7.5, Kick_Deep 0.8, Kick 0.4, snare 0.3
    CHECK(searchPosition(s.db, m, s.kick) == 3);
    m.text = "kick";
    CHECK(searchPosition(s.db, m, s.padLoop) == std::nullopt); // not a match
    m.limit = 1;
    m.offset = 1;
    CHECK(searchPosition(s.db, m, s.flacHit) == 0); // the page does not matter
}

TEST_CASE("text search prefix-matches names, folders and tags", "[query]")
{
    Seeded s;
    SearchModel m;
    m.text = "kic";
    CHECK(ids(search(s.db, m)) == Ids{s.kick, s.flacHit});
    m.text = "loop bass";
    CHECK(ids(search(s.db, m)) == Ids{s.bassLoop});
    m.text = "synth";
    CHECK(ids(search(s.db, m)) == Ids{s.padLoop});
}

TEST_CASE("FTS syntax in the search text never throws", "[query]")
{
    Seeded s;
    for (const char* text : {"\"", "*", "-", "OR", "(", "kick\" OR *", "NEAR(", "   ", "\t\n"}) {
        INFO(text);
        SearchModel m;
        m.text = text;
        CHECK_NOTHROW(search(s.db, m));
    }
    SearchModel blank;
    blank.text = "   ";
    CHECK(search(s.db, blank).size() == 5);
}

TEST_CASE("facet filters", "[query]")
{
    Seeded s;
    SearchModel loops;
    loops.type = SampleType::Loop;
    CHECK(ids(search(s.db, loops)) == Ids{s.bassLoop, s.padLoop});

    SearchModel shots;
    shots.type = SampleType::OneShot;
    CHECK(search(s.db, shots).size() == 3);

    SearchModel bpm;
    bpm.bpmMin = 100.0;
    bpm.bpmMax = 130.0;
    CHECK(ids(search(s.db, bpm)) == Ids{s.bassLoop});

    SearchModel keys;
    keys.keys = {"Am", "C"};
    CHECK(ids(search(s.db, keys)) == Ids{s.bassLoop, s.padLoop});

    SearchModel tags;
    tags.tags = {"pad", "SYNTH"};
    CHECK(ids(search(s.db, tags)) == Ids{s.padLoop});

    SearchModel duration;
    duration.durationMax = 0.5;
    CHECK(ids(search(s.db, duration)) == Ids{s.kick, s.snare});

    SearchModel format;
    format.formats = {"flac"};
    CHECK(ids(search(s.db, format)) == Ids{s.flacHit});

    SearchModel root;
    root.rootId = s.other;
    CHECK(ids(search(s.db, root)) == Ids{s.flacHit});
}

TEST_CASE("missing, failed and disabled-root files are excluded", "[query]")
{
    Seeded s;
    s.lib.setStatus(s.kick, FileStatus::Missing);
    s.lib.setStatus(s.snare, FileStatus::Failed, "bad");
    auto disable = s.db.prepare("UPDATE roots SET enabled = 0 WHERE id = ?");
    disable.bind(1, s.other);
    disable.run();
    CHECK(ids(search(s.db, {})) == Ids{s.bassLoop, s.padLoop});
}

TEST_CASE("sorting and paging", "[query]")
{
    Seeded s;
    SearchModel byBpm;
    byBpm.sort = SortField::Bpm;
    byBpm.descending = true;
    const auto rows = search(s.db, byBpm);
    REQUIRE(rows.size() == 5);
    CHECK(rows[0].id == s.bassLoop);
    CHECK(rows[1].id == s.padLoop);
    CHECK_FALSE(rows[4].bpm.has_value()); // unknown BPM sorts last

    SearchModel page;
    page.limit = 2;
    page.offset = 2;
    CHECK(ids(search(s.db, page)) == Ids{s.flacHit, s.padLoop});
}

TEST_CASE("rows carry what the CLI prints", "[query]")
{
    Seeded s;
    SearchModel m;
    m.text = "bass";
    const auto rows = search(s.db, m);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].relPath == "Loops/Bass_Loop_Am_128.wav");
    CHECK(rows[0].name == "Bass_Loop_Am_128.wav");
    CHECK(rows[0].format == "wav");
    CHECK(rows[0].bpm == 128.0);
    CHECK(rows[0].key == "Am");
    CHECK(rows[0].isLoop == true);
    CHECK(rows[0].rootPath == s.lib.root(s.root)->path);
}

TEST_CASE("rating, favourite and collection facets", "[query]")
{
    Seeded s;
    UserData user(s.db);
    user.setRating(s.kick, 5);
    user.setRating(s.padLoop, 3);
    user.setRating(s.snare, 1);
    user.setFavourite(s.padLoop, true);
    const auto set = user.createCollection("Set");
    user.addToCollection(set, s.snare);
    user.addToCollection(set, s.flacHit);

    SearchModel rated;
    rated.minRating = 3;
    CHECK(ids(search(s.db, rated)) == Ids{s.kick, s.padLoop});

    SearchModel favourites;
    favourites.favouritesOnly = true;
    CHECK(ids(search(s.db, favourites)) == Ids{s.padLoop});

    SearchModel collection;
    collection.collectionId = set;
    CHECK(ids(search(s.db, collection)) == Ids{s.flacHit, s.snare});

    SearchModel both;
    both.collectionId = set;
    both.minRating = 1;
    CHECK(ids(search(s.db, both)) == Ids{s.snare});

    SearchModel byRating;
    byRating.sort = SortField::Rating;
    byRating.descending = true;
    const auto rows = search(s.db, byRating);
    REQUIRE(rows.size() == 5);
    CHECK(ids({rows.begin(), rows.begin() + 3}) == Ids{s.kick, s.padLoop, s.snare});
    CHECK_FALSE(rows[4].rating); // unrated sorts last either way
    CHECK(rows[0].rating == 5);
    CHECK_FALSE(rows[0].favourite);
    CHECK(rows[1].favourite);
}

// Hidden: run with ./build/tests/asma_tests "[.perf]" on a Release build.
TEST_CASE("search over 200k files", "[.perf]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    const char* words[] = {"Kick", "Snare", "Hat", "Bass", "Pad", "Lead", "Vox", "FX"};
    {
        Transaction tx(db);
        for (int i = 0; i < 200000; ++i) {
            FileRecord f;
            f.rootId = root;
            f.relPath = std::string("Pack ") + std::to_string(i % 500) + "/" + words[i % 8] + "_"
                      + std::to_string(i) + ".wav";
            f.size = i;
            f.mtime = i;
            f.format = "wav";
            f.duration = (i % 100) / 10.0;
            lib.setDerived(lib.insertFile(f), {});
        }
        tx.commit();
    }
    SearchModel m;
    m.text = "kick";
    const auto start = std::chrono::steady_clock::now();
    const auto rows = search(db, m);
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    WARN("search took " << ms << " ms for " << rows.size() << " rows");
    CHECK(ms < 50.0);
}

TEST_CASE("rowsForIds keeps the given order and drops unusable ids", "[query]")
{
    Seeded s;
    s.lib.setStatus(s.snare, FileStatus::Missing);
    CHECK(ids(rowsForIds(s.db, {s.padLoop, s.snare, 9999, s.kick})) == Ids{s.padLoop, s.kick});
    CHECK(rowsForIds(s.db, {}).empty());
}

// Hidden: run with ./build/tests/asma_tests "[.perf]" on a Release build.
// Similar runs on the UI thread while it stays this quick.
TEST_CASE("similar over 50k analysed files", "[.perf]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    std::int64_t first = 0;
    {
        Transaction tx(db);
        auto vector = db.prepare("UPDATE features SET feature_vector = ? WHERE file_id = ?");
        std::uint32_t seed = 1;
        std::vector<float> v(kFeatureVectorSize);
        for (int i = 0; i < 50000; ++i) {
            FileRecord f;
            f.rootId = root;
            f.relPath = "Pack/" + std::to_string(i) + ".wav";
            f.size = i;
            f.mtime = i;
            f.format = "wav";
            f.duration = 1.0;
            const auto id = lib.insertFile(f);
            if (i == 0) first = id;
            lib.setDerived(id, {});
            for (auto& x : v) {
                seed = seed * 1664525u + 1013904223u; // any spread will do
                x = static_cast<float>(seed >> 8) / 16777216.0f;
            }
            vector.bindBlob(1, v.data(), v.size() * sizeof(float)).bind(2, id).run();
            vector.reset();
        }
        tx.commit();
    }
    const auto start = std::chrono::steady_clock::now();
    const auto matches = findSimilar(db, first, 10);
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    WARN("similar took " << ms << " ms over 50000 files");
    CHECK(matches.size() == 10);
    CHECK(ms < 50.0); // past this, Similar moves off the UI thread (spec section 12)
}
