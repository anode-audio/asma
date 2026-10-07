// SPDX-License-Identifier: GPL-3.0-only
#include "Args.h"
#include "CliCommon.h"

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

TEST_CASE("a relative --db becomes an absolute path", "[args]")
{
    Args args({"--db", "lib.db", "scan"});
    const auto path = asma::cli::resolveDbPath(args);
    CHECK(path.is_absolute());
    CHECK(path.filename() == "lib.db");
    CHECK(path.has_parent_path());
}

TEST_CASE("after --, every argument is a positional, however it looks", "[args]")
{
    Args args({"collection", "--id", "3", "--", "--version", "-- Drums --", "--id"});
    CHECK(args.options("id") == Strings{"3"});
    CHECK_FALSE(args.flag("version"));
    CHECK(args.positional() == "collection"); // what comes before -- first
    CHECK(args.literalFollows());
    CHECK(args.positional() == "--version");
    CHECK(args.positional() == "-- Drums --");
    CHECK(args.rest() == Strings{"--id"});
    CHECK(args.positional() == "--id");
    CHECK_FALSE(args.positional());
    CHECK_FALSE(args.literalFollows());
}
