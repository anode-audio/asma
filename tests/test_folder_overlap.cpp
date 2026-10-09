// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/FileOps.h"
#include "asma/core/Folders.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"
#include "asma/core/Scanner.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

// Samples/Drums/kick.wav, Samples/Bass/sub.wav and Samples/pad.wav.
struct Tree {
    TempDir dir;
    fs::path samples = dir.path() / "Samples";
    Db db = Db::open(dir.path() / "library.db");
    Tree()
    {
        test::WavSpec spec;
        test::writeWav(samples / "Drums" / "kick.wav", spec);
        spec.seed = 2;
        test::writeWav(samples / "Bass" / "sub.wav", spec);
        spec.seed = 3;
        test::writeWav(samples / "pad.wav", spec);
    }
    std::int64_t idOf(std::int64_t root, const std::string& rel) { return Library(db).fileByPath(root, rel)->id; }
    std::int64_t shown() { return countSearch(db, SearchModel{}); }
};

std::string refusal(const std::function<void()>& f)
{
    try {
        f();
    } catch (const OperationRefused& e) {
        return e.what();
    }
    return "(not refused)";
}

} // namespace

TEST_CASE("A folder inside one already in the library is refused", "[overlap]")
{
    Tree t;
    const auto samples = addFolder(t.db, t.samples, false);
    const AddCheck check = checkAddFolder(t.db, t.samples / "Drums");
    CHECK(check.result == AddCheck::Result::Inside);
    CHECK(check.message == "Drums is already in the library, inside Samples.");
    CHECK(refusal([&] { addFolder(t.db, t.samples / "Drums", true); }) == check.message);
    CHECK(checkAddFolder(t.db, t.samples).result == AddCheck::Result::Again);
    CHECK(checkAddFolder(t.db, t.dir.path() / "Other").result == AddCheck::Result::New);

    // Inside a removed folder is fine: that one is not scanned. Adding the
    // removed one again then takes the new one's place.
    Library(t.db).setRootEnabled(samples, false);
    CHECK(checkAddFolder(t.db, t.samples / "Drums").result == AddCheck::Result::New);
    addFolder(t.db, t.samples / "Drums", false);
    CHECK(checkAddFolder(t.db, t.samples).result == AddCheck::Result::Contains);
    CHECK(addFolder(t.db, t.samples, true) == samples);
    CHECK(Library(t.db).roots().size() == 1);
    CHECK(Library(t.db).root(samples)->enabled);
}

TEST_CASE("A folder holding library folders takes their place, their samples keeping their data", "[overlap]")
{
    Tree t;
    const auto drums = addFolder(t.db, t.samples / "Drums", false);
    const auto bass = addFolder(t.db, t.samples / "Bass", false);
    scanRoot(t.db, drums);
    scanRoot(t.db, bass);
    const auto kick = t.idOf(drums, "kick.wav");
    UserData(t.db).setRating(kick, 3);
    Library(t.db).setRootEnabled(bass, false); // a removed one is absorbed too

    const AddCheck check = checkAddFolder(t.db, t.samples);
    CHECK(check.result == AddCheck::Result::Contains);
    CHECK(check.message == "Samples contains 2 folders already in the library (Bass, Drums). Add Samples in their place?");
    CHECK(refusal([&] { addFolder(t.db, t.samples, false); }) == check.message);

    const auto samples = addFolder(t.db, t.samples, true);
    Library lib(t.db);
    CHECK(lib.roots().size() == 1);
    CHECK(lib.fileById(kick)->rootId == samples);
    CHECK(lib.fileById(kick)->relPath == "Drums/kick.wav");
    CHECK(UserData(t.db).rating(kick) == 3);
    CHECK(t.shown() == 2); // kick and sub: pad is not scanned yet
    const auto stats = scanRoot(t.db, samples);
    CHECK(stats.added == 1);
    CHECK(stats.missing == 0);
    CHECK(t.shown() == 3);
    SearchModel query;
    query.text = "drums";
    CHECK(countSearch(t.db, query) == 1); // the index knows its new folder
}

TEST_CASE("Nested folders already in a library merge once, into one row per sample", "[overlap]")
{
    Tree t;
    // As older versions allowed: Samples, and Drums inside it, both scanned.
    Library lib(t.db);
    const auto samples = lib.addRoot(t.samples);
    const auto drums = lib.addRoot(t.samples / "Drums");
    scanRoot(t.db, samples);
    scanRoot(t.db, drums);
    REQUIRE(t.shown() == 4);
    const auto outerKick = t.idOf(samples, "Drums/kick.wav");
    const auto innerKick = t.idOf(drums, "kick.wav");
    UserData user(t.db);
    user.setRating(innerKick, 4);
    user.setFavourite(outerKick, true);
    const auto c = user.createCollection("Hits");
    user.addToCollection(c, innerKick);

    const auto merged = mergeNestedFolders(t.db);
    CHECK(merged == std::vector<std::string>{"Merged Drums into Samples, which contains it."});
    CHECK(lib.roots().size() == 1);
    CHECK(t.shown() == 3);
    CHECK(user.rating(outerKick) == 4);
    CHECK(user.isFavourite(outerKick));
    SearchModel hits;
    hits.collectionId = c;
    CHECK(countSearch(t.db, hits) == 1);
    CHECK(mergeNestedFolders(t.db).empty());
}
