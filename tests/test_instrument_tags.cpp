// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/InstrumentTags.h"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

using asma::InstrumentDictionary;
using Tags = std::vector<std::string>;

TEST_CASE("parse reads tag lines and ignores comments and blanks", "[tags]")
{
    const auto dict = InstrumentDictionary::parse("# comment\n\nkick: kick, bd\n  snare : Snare,sd  \n");
    CHECK(dict.tagsFor({"bd", "01"}) == Tags{"kick"});
    CHECK(dict.tagsFor({"snare"}) == Tags{"snare"});
    CHECK(dict.tagsFor({"sd", "kick", "bd"}) == Tags{"kick", "snare"});
    CHECK(dict.tagsFor({"pad"}).empty());
}

TEST_CASE("the first tag to claim a token wins", "[tags]")
{
    const auto dict = InstrumentDictionary::parse("drums: kick\nkick: kick\n");
    CHECK(dict.tagsFor({"kick"}) == Tags{"drums"});
}

TEST_CASE("malformed lines are rejected with their line number", "[tags]")
{
    try {
        InstrumentDictionary::parse("kick: kick\nthis line has no colon\n");
        FAIL("expected invalid_argument");
    } catch (const std::invalid_argument& e) {
        CHECK(std::string(e.what()).find("line 2") != std::string::npos);
    }
}

TEST_CASE("the built-in dictionary covers common drum names", "[tags]")
{
    const auto& dict = InstrumentDictionary::builtin();
    CHECK(dict.tagsFor({"bd"}) == Tags{"kick"});
    CHECK(dict.tagsFor({"hh"}) == Tags{"hihat"});
    CHECK(dict.tagsFor({"808"}) == Tags{"808"});
    CHECK(dict.tagsFor({"vox"}) == Tags{"vocal"});
}
