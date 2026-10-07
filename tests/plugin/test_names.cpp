// SPDX-License-Identifier: GPL-3.0-only
#include "Names.h"

#include <catch2/catch_test_macros.hpp>

using asma::app::checkName;
using asma::app::NameCheck;

TEST_CASE("names are trimmed, needed, and unique ignoring case", "[names]")
{
    const std::vector<std::string> existing{"Live set", "Album 2"};
    CHECK(checkName("Basslines", existing) == NameCheck::Ok);
    CHECK(checkName("   ", existing) == NameCheck::Empty);
    CHECK(checkName(" live SET ", existing) == NameCheck::Taken);
    CHECK(asma::app::trimmedName("  Album 3 ") == "Album 3");
}

TEST_CASE("renaming to the name it has is no change, and its own name in other case is fine", "[names]")
{
    const std::vector<std::string> existing{"Live set", "Album 2"};
    CHECK(checkName("Live set", existing, "Live set") == NameCheck::Unchanged);
    CHECK(checkName(" Live set ", existing, "Live set") == NameCheck::Unchanged);
    CHECK(checkName("LIVE SET", existing, "Live set") == NameCheck::Ok);
    CHECK(checkName("album 2", existing, "Live set") == NameCheck::Taken);
}
