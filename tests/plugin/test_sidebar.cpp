// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryFixture.h"
#include "Sidebar.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::EntryKind;
using app::LibraryView;
using app::SidebarEntry;

namespace {

// The fixture's library, with a favourite, a collection and a saved search.
struct Rig {
    test::LibraryFixture f;
    std::int64_t loop = 0, kick = 0, collection = 0;
    Rig()
    {
        f.scan();
        Db writer = Db::open(f.dbPath);
        Library lib(writer);
        UserData data(writer);
        loop = lib.fileByAbsolutePath(f.loop)->id;
        kick = lib.fileByAbsolutePath(f.kick)->id;
        data.setFavourite(kick, true);
        collection = data.createCollection("Low end");
        data.addToCollection(collection, loop);
        SearchModel loops;
        loops.type = SampleType::Loop;
        data.saveSearch("Loops only", loops);
    }
};

const SidebarEntry* find(const std::vector<SidebarEntry>& entries, EntryKind kind)
{
    for (const auto& e : entries)
        if (e.kind == kind) return &e;
    return nullptr;
}

} // namespace

TEST_CASE("the sidebar lists the library's scopes with what each holds", "[sidebar]")
{
    Rig rig;
    LibraryView library(rig.f.dbPath);
    library.refresh();
    const auto entries = app::sidebarEntries(library);
    REQUIRE(entries.size() == 5); // All, Favourites, one folder, one collection, one saved search
    CHECK(entries[0].kind == EntryKind::All);
    CHECK(entries[0].count == 3);
    CHECK(entries[1].kind == EntryKind::Favourites);
    CHECK(entries[1].count == 1);
    CHECK(entries[2].kind == EntryKind::Folder);
    CHECK(entries[2].name == "Samples"); // the folder's own name, not its path
    CHECK(entries[2].count == 3);
    CHECK(entries[3].kind == EntryKind::Collection);
    CHECK(entries[3].name == "Low end");
    CHECK(entries[3].count == 1);
    CHECK(entries[4].kind == EntryKind::SavedSearch);
    CHECK(entries[4].name == "Loops only");
    CHECK(entries[4].count < 0); // a saved search shows no count
}

TEST_CASE("picking a scope sets it and clears the others, keeping the search", "[sidebar]")
{
    Rig rig;
    LibraryView library(rig.f.dbPath);
    library.refresh();
    const auto entries = app::sidebarEntries(library);
    SearchModel m;
    m.text = "bass";
    m.bpmMin = 100.0;
    m = app::withEntry(*find(entries, EntryKind::Collection), m);
    CHECK(m.collectionId == rig.collection);
    CHECK(m.text == "bass"); // the text and the chips narrow within it
    CHECK(m.bpmMin == 100.0);
    m = app::withEntry(*find(entries, EntryKind::Favourites), m);
    CHECK(m.favouritesOnly);
    CHECK_FALSE(m.collectionId); // one scope at a time
    m = app::withEntry(*find(entries, EntryKind::Folder), m);
    CHECK(m.rootId);
    CHECK_FALSE(m.favouritesOnly);
    m = app::withEntry(entries[0], m);
    CHECK_FALSE(m.rootId);
    CHECK(m.text == "bass");
}

TEST_CASE("a saved search loads its whole search and stays lit until it changes", "[sidebar]")
{
    Rig rig;
    LibraryView library(rig.f.dbPath);
    library.refresh();
    const auto entries = app::sidebarEntries(library);
    SearchModel m;
    m.text = "something else";
    m = app::withEntry(*find(entries, EntryKind::SavedSearch), m);
    CHECK(m.type == SampleType::Loop);
    CHECK(m.text.empty()); // replaced, not merged
    CHECK(entries[static_cast<std::size_t>(app::entryFor(entries, m))].kind == EntryKind::SavedSearch);
    m.text = "bass"; // changed: no longer the saved search
    CHECK(entries[static_cast<std::size_t>(app::entryFor(entries, m))].kind == EntryKind::All);
}

TEST_CASE("the sidebar lights the entry the search's scope is", "[sidebar]")
{
    Rig rig;
    LibraryView library(rig.f.dbPath);
    library.refresh();
    const auto entries = app::sidebarEntries(library);
    SearchModel m;
    CHECK(app::entryFor(entries, m) == 0);
    m.collectionId = rig.collection;
    CHECK(entries[static_cast<std::size_t>(app::entryFor(entries, m))].kind == EntryKind::Collection);
    m.collectionId = 9999; // a collection since deleted
    CHECK(app::entryFor(entries, m) == -1); // nothing lit rather than the wrong one
}

TEST_CASE("an unopened library lists the fixed entries without counts", "[sidebar]")
{
    test::LibraryFixture f; // no scan
    LibraryView library(f.dbPath);
    library.refresh();
    const auto entries = app::sidebarEntries(library);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].count == 0);
}

TEST_CASE("the library counts the files that failed to decode or analyse", "[sidebar]")
{
    Rig rig;
    LibraryView library(rig.f.dbPath);
    library.refresh();
    CHECK(library.problemCount() == 0);
    {
        Db writer = Db::open(rig.f.dbPath);
        Library lib(writer);
        lib.setStatus(rig.kick, FileStatus::Failed, "not audio");
        lib.setAnalysisError(rig.loop, "too short");
    }
    library.changed();
    CHECK(library.problemCount() == 2);
}
