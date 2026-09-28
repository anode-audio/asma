// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Json.h"

#include <catch2/catch_test_macros.hpp>
#include <clocale>
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

TEST_CASE("JsonLine writes string arrays", "[json]")
{
    CHECK(JsonLine().strings("keys", {"Am", "C\"#"}).build() == R"({"keys":["Am","C\"#"]})");
    CHECK(JsonLine().strings("keys", {}).build() == R"({"keys":[]})");
}

TEST_CASE("numbers use a decimal point whatever LC_NUMERIC says", "[json]")
{
    const char* previous = std::setlocale(LC_NUMERIC, nullptr);
    const std::string saved = previous ? previous : "C";
    bool switched = false;
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "it_IT.UTF-8", "fr_FR.UTF-8", "German_Germany.1252"})
        if (std::setlocale(LC_NUMERIC, name)) {
            switched = true;
            break;
        }
    if (!switched) SKIP("no locale with a decimal comma is installed");
    const std::string line = JsonLine().real("bpm", 128.5).build();
    const auto parsed = asma::parseJson(R"({"bpm":128.5})");
    std::setlocale(LC_NUMERIC, saved.c_str());
    CHECK(line == R"({"bpm":128.5})");
    CHECK(parsed.get("bpm")->asNumber() == 128.5);
}

TEST_CASE("parseJson reads every JSON type", "[json]")
{
    const auto v = asma::parseJson(R"( {"s":"a\"b\\c\/\n\u00e9\ud83d\ude00", "n":-12.5e1, "i":42, "t":true,
        "f":false, "z":null, "a":[1,"x",[]], "o":{"k":"v"}} )");
    REQUIRE(v.isObject());
    CHECK(*v.get("s")->asString() == "a\"b\\c/\n\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(v.get("n")->asNumber() == -125.0);
    CHECK(v.get("i")->asInt() == 42);
    CHECK(v.get("n")->asInt() == -125); // a whole number written as a real
    CHECK_FALSE(asma::parseJson("1.5").asInt());
    CHECK(v.get("t")->asBool() == true);
    CHECK(v.get("f")->asBool() == false);
    CHECK(v.get("z")->isNull());
    REQUIRE(v.get("a")->asArray());
    CHECK(v.get("a")->asArray()->size() == 3);
    CHECK(*v.get("o")->get("k")->asString() == "v");
    CHECK(v.get("missing") == nullptr);
    CHECK(v.get("s")->get("x") == nullptr);
    CHECK_FALSE(v.get("s")->asNumber());
}

TEST_CASE("parseJson keeps the last of repeated keys", "[json]")
{
    CHECK(asma::parseJson(R"({"a":1,"a":2})").get("a")->asInt() == 2);
}

TEST_CASE("parseJson rejects malformed input", "[json]")
{
    for (const char* bad : {"", "   ", "{", "[1,]", "{\"a\":}", "{\"a\" 1}", "{'a':1}", "tru", "nul", "01", "1.",
                            "-", "1e", "+1", "\"abc", "\"\\x\"", "\"\\u12\"", "\"\\ud800\"", "\"\\udc00\"",
                            "\"a\nb\"", "{} {}", "[1] x", "1e999", "NaN"})
        CHECK_THROWS_AS(asma::parseJson(bad), asma::JsonError);
}

TEST_CASE("parseJson refuses absurd nesting instead of overflowing the stack", "[json]")
{
    CHECK_NOTHROW(asma::parseJson(std::string(64, '[') + std::string(64, ']')));
    CHECK_THROWS_AS(asma::parseJson(std::string(100000, '[')), asma::JsonError);
}

TEST_CASE("JsonLine output parses back", "[json]")
{
    const std::string line = JsonLine()
                                 .str("path", "Café/\"odd\"\x01.wav")
                                 .num("done", -3)
                                 .real("bpm", 0.1)
                                 .strings("tags", {"a", "b"})
                                 .build();
    const auto v = asma::parseJson(line);
    CHECK(*v.get("path")->asString() == "Café/\"odd\"\x01.wav");
    CHECK(v.get("done")->asInt() == -3);
    CHECK(v.get("bpm")->asNumber() == 0.1);
    CHECK(v.get("tags")->asArray()->size() == 2);
}
