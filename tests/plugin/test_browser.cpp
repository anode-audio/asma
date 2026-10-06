// SPDX-License-Identifier: GPL-3.0-only
#include "Browser.h"
#include "LibraryFixture.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
namespace fs = std::filesystem;
using app::Browser;
using app::LibraryView;

TEST_CASE("Browser searches as the model changes", "[browser]")
{
    test::LibraryFixture f;
    f.scan();
    LibraryView library(f.dbPath);
    Browser browser(library);
    browser.poll();
    CHECK(browser.count() == 3);
    CHECK(browser.total() == 3);
    SearchModel m;
    m.text = "kick";
    browser.setSearch(m);
    REQUIRE(browser.count() == 1);
    CHECK(browser.total() == 3); // "1 of 3"
    CHECK(fs::equivalent(browser.path(0), f.kick));
    CHECK(browser.searchModel().text == "kick");
    CHECK(browser.contentHash(0).size() == 16);
    CHECK(browser.path(5).empty()); // out of range
    CHECK(browser.row(5) == nullptr);
}

TEST_CASE("Browser picks up a library that appears and changes later", "[browser]")
{
    test::LibraryFixture f;
    LibraryView library(f.dbPath);
    Browser browser(library);
    CHECK_FALSE(browser.poll());
    CHECK(browser.count() == 0);
    f.scan();
    CHECK(browser.poll()); // the first scan finished
    CHECK(browser.count() == 3);
    CHECK_FALSE(browser.poll());

    SearchModel rated;
    rated.minRating = 4;
    browser.setSearch(rated);
    CHECK(browser.count() == 0);
    {
        Db writer = Db::open(f.dbPath);
        const auto id = Library(writer).fileByAbsolutePath(f.kick)->id;
        UserData(writer).setRating(id, 5);
    }
    CHECK(browser.poll());
    CHECK(browser.count() == 1);
    REQUIRE(browser.row(0));
    CHECK(browser.row(0)->rating == 5); // a page fetched before the change is not reused
}

TEST_CASE("Browser finds the row of a path again after a refresh", "[browser]")
{
    test::LibraryFixture f;
    f.scan();
    LibraryView library(f.dbPath);
    Browser browser(library);
    browser.poll();
    const int snare = browser.rowOf(f.snare);
    REQUIRE(snare >= 0);
    CHECK(fs::equivalent(browser.path(snare), f.snare));
    CHECK(browser.rowOf(f.dir.path() / "nope.wav") == -1);
    CHECK(browser.info(browser.rowOf(f.loop)).bpm == 120.0);
}

TEST_CASE("Browser fetches rows a page at a time, as they are asked for", "[browser]")
{
    test::LibraryFixture f;
    f.scan();
    LibraryView library(f.dbPath);
    Browser browser(library, 2); // pages of two: three samples span two pages
    browser.poll();
    REQUIRE(browser.count() == 3);
    // By name: Bass_Loop_Am_120, Kick_01, Snare_02.
    REQUIRE(browser.row(2));
    CHECK(browser.row(2)->name == "Snare_02.wav"); // from the second page
    REQUIRE(browser.row(0));
    CHECK(browser.row(0)->name == "Bass_Loop_Am_120.wav");
    CHECK(browser.row(3) == nullptr);
    CHECK(browser.rowOf(f.snare) == 2); // found past the first page, without walking pages
}

TEST_CASE("Browser sorts as the model says, across pages", "[browser]")
{
    test::LibraryFixture f;
    f.scan();
    LibraryView library(f.dbPath);
    Browser browser(library, 2);
    browser.poll();
    SearchModel longest;
    longest.sort = SortField::Duration;
    longest.descending = true; // loop 2 s, kick 0.5 s, snare 0.25 s
    browser.setSearch(longest);
    REQUIRE(browser.row(2));
    CHECK(browser.row(2)->name == "Snare_02.wav");
    CHECK(browser.rowOf(f.loop) == 0);
}
