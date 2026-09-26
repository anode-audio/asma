// SPDX-License-Identifier: GPL-3.0-only
#include "Args.h"

#include <catch2/catch_test_macros.hpp>

using asma::cli::Args;
using asma::cli::UsageError;
using Strings = std::vector<std::string>;

TEST_CASE("options, flags and positionals", "[args]")
{
    Args args({"--db", "/x/lib.db", "query", "kick", "--key=Am", "--key", "C", "--json", "808"});
    CHECK(args.option("db") == "/x/lib.db");
    CHECK(args.positional() == "query");
    CHECK(args.options("key") == Strings{"Am", "C"});
    CHECK(args.flag("json"));
    CHECK_FALSE(args.flag("json"));
    CHECK(args.rest() == Strings{"kick", "808"});
}

TEST_CASE("an option without a value is a usage error", "[args]")
{
    Args args({"scan", "--root"});
    CHECK_THROWS_AS(args.option("root"), UsageError);
}

TEST_CASE("unknown options stay in rest for the caller to reject", "[args]")
{
    Args args({"--bogus", "x"});
    CHECK(args.rest() == Strings{"--bogus", "x"});
}
