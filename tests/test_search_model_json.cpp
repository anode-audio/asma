// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Query.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;

TEST_CASE("a default model is just the version", "[searchjson]")
{
    CHECK(searchModelToJson({}) == R"({"v":1})");
}

TEST_CASE("every field round-trips; paging does not", "[searchjson]")
{
    SearchModel m;
    m.text = "dusty \"kick\"";
    m.type = SampleType::OneShot;
    m.bpmMin = 90.5;
    m.bpmMax = 100;
    m.keys = {"Am", "C#"};
    m.tags = {"kick", "808"};
    m.durationMin = 0.1;
    m.durationMax = 2;
    m.formats = {"wav"};
    m.rootId = 3;
    m.minRating = 4;
    m.favouritesOnly = true;
    m.collectionId = 9;
    m.sort = SortField::Rating;
    m.descending = true;
    m.limit = 20;
    m.offset = 40;

    const auto back = searchModelFromJson(searchModelToJson(m)).value();
    CHECK(back.text == m.text);
    CHECK(back.type == m.type);
    CHECK(back.bpmMin == m.bpmMin);
    CHECK(back.bpmMax == m.bpmMax);
    CHECK(back.keys == m.keys);
    CHECK(back.tags == m.tags);
    CHECK(back.durationMin == m.durationMin);
    CHECK(back.durationMax == m.durationMax);
    CHECK(back.formats == m.formats);
    CHECK(back.rootId == m.rootId);
    CHECK(back.minRating == m.minRating);
    CHECK(back.favouritesOnly);
    CHECK(back.collectionId == m.collectionId);
    CHECK(back.sort == SortField::Rating);
    CHECK(back.descending);
    CHECK(back.limit == SearchModel{}.limit);
    CHECK(back.offset == 0);
}

TEST_CASE("natural major keys survive a round trip", "[searchjson]")
{
    SearchModel m;
    m.keys = {"C", "Am", "G", "F#"};
    const auto back = searchModelFromJson(searchModelToJson(m));
    REQUIRE(back);
    CHECK(back->keys == m.keys);
}

TEST_CASE("unreadable text gives nothing", "[searchjson]")
{
    CHECK_FALSE(searchModelFromJson(""));
    CHECK_FALSE(searchModelFromJson("{\"text\":"));
    CHECK_FALSE(searchModelFromJson("[1,2]"));
    CHECK_FALSE(searchModelFromJson("\"text\""));
}

TEST_CASE("bad or unknown fields are skipped, the rest is kept", "[searchjson]")
{
    const auto m = searchModelFromJson(R"({"v":7,"text":"pad","future_field":{"x":1},"type":"granular",
        "bpm_min":"fast","bpm_max":120,"keys":["Am","H minor",3,"Dbm"],"tags":"kick","min_rating":9,
        "favourites":"yes","collection":1.5,"root":2,"sort":"loudness","desc":1})").value();
    CHECK(m.text == "pad");
    CHECK(m.type == SampleType::Any);
    CHECK_FALSE(m.bpmMin);
    CHECK(m.bpmMax == 120.0);
    CHECK(m.keys == std::vector<std::string>{"Am", "C#m"});
    CHECK(m.tags.empty());
    CHECK_FALSE(m.minRating);
    CHECK_FALSE(m.favouritesOnly);
    CHECK_FALSE(m.collectionId);
    CHECK(m.rootId == 2);
    CHECK(m.sort == SortField::Name);
    CHECK_FALSE(m.descending);
}

TEST_CASE("saved searches: save, replace by name, list, delete", "[searchjson][userdata]")
{
    Db db = Db::openInMemory();
    UserData user(db);
    SearchModel loops;
    loops.type = SampleType::Loop;
    const auto id = user.saveSearch(" Loops ", loops);

    SearchModel fast;
    fast.bpmMin = 140;
    CHECK(user.saveSearch("LOOPS", fast) == id); // same name, ignoring case: replaced
    user.saveSearch("Ambient", {});

    const auto all = user.savedSearches();
    REQUIRE(all.size() == 2);
    CHECK(all[0].name == "Ambient");
    CHECK(all[1].name == "Loops");
    CHECK(all[1].model.bpmMin == 140.0);
    CHECK(all[1].model.type == SampleType::Any);
    CHECK(user.savedSearchByName("loops")->id == id);

    user.deleteSavedSearch(id);
    CHECK_FALSE(user.savedSearchByName("Loops"));
    CHECK_THROWS_AS(user.deleteSavedSearch(id), UserDataError);
    CHECK_THROWS_AS(user.saveSearch("  ", {}), UserDataError);
}

TEST_CASE("a stored search that no longer parses comes back as the default model", "[searchjson][userdata]")
{
    Db db = Db::openInMemory();
    db.exec("INSERT INTO saved_searches(name, model) VALUES ('Broken', '{not json')");
    UserData user(db);
    const auto broken = user.savedSearchByName("Broken").value();
    CHECK(broken.model.text.empty());
    CHECK(broken.model.type == SampleType::Any);
}
