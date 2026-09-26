// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Json.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>

using asma::JsonLine;

TEST_CASE("jsonEscape handles quotes, backslashes and control characters", "[json]")
{
    CHECK(asma::jsonEscape("a\"b\\c") == "a\\\"b\\\\c");
    CHECK(asma::jsonEscape("line\nnext\ttab") == "line\\nnext\\ttab");
    CHECK(asma::jsonEscape(std::string("\x01", 1)) == "\\u0001");
    CHECK(asma::jsonEscape("Café") == "Café");
}

TEST_CASE("JsonLine builds an object in insertion order", "[json]")
{
    const std::string line = JsonLine()
                                 .str("event", "progress")
                                 .num("done", 3)
                                 .real("bpm", 128.5)
                                 .boolean("loop", true)
                                 .null("key")
                                 .build();
    CHECK(line == R"({"event":"progress","done":3,"bpm":128.5,"loop":true,"key":null})");
}

TEST_CASE("non-finite numbers become null", "[json]")
{
    CHECK(JsonLine().real("x", std::nan("")).build() == R"({"x":null})");
    CHECK(JsonLine().build() == "{}");
}
