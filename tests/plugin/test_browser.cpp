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
    CHECK(browser.rows().size() == 3);
    CHECK(browser.total() == 3);
    SearchModel m;
    m.text = "kick";
    browser.setSearch(m);
    REQUIRE(browser.rows().size() == 1);
    CHECK(browser.total() == 3); // "1 of 3"
    CHECK(browser.matches() == 1);
    CHECK(fs::equivalent(browser.path(0), f.kick));
    CHECK(browser.searchModel().text == "kick");
    CHECK(browser.contentHash(0).size() == 16);
    CHECK(browser.path(5).empty()); // out of range
    SearchModel page;
    page.limit = 2; // the table shows a page; the count is of everything that matches
    browser.setSearch(page);
    CHECK(browser.rows().size() == 2);
    CHECK(browser.matches() == 3);
}

TEST_CASE("Browser picks up a library that appears and changes later", "[browser]")
{
    test::LibraryFixture f;
    LibraryView library(f.dbPath);
    Browser browser(library);
    CHECK_FALSE(browser.poll());
    CHECK(browser.rows().empty());
    f.scan();
    CHECK(browser.poll()); // the first scan finished
    CHECK(browser.rows().size() == 3);
    CHECK_FALSE(browser.poll());

    SearchModel rated;
    rated.minRating = 4;
    browser.setSearch(rated);
    CHECK(browser.rows().empty());
    {
        Db writer = Db::open(f.dbPath);
        const auto id = Library(writer).fileByAbsolutePath(f.kick)->id;
        UserData(writer).setRating(id, 5);
    }
    CHECK(browser.poll());
    CHECK(browser.rows().size() == 1);
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
