// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/NameParse.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using Tokens = std::vector<std::string>;

TEST_CASE("splitTokens splits on separators and letter/digit boundaries", "[name]")
{
    CHECK(splitTokens("Kick01_hard-808") == Tokens{"Kick", "01", "hard", "808"});
    CHECK(splitTokens("F#min") == Tokens{"F#min"});
    CHECK(splitTokens("120bpm") == Tokens{"120", "bpm"});
    CHECK(splitTokens("Café Loops") == Tokens{"Café", "Loops"});
    CHECK(splitTokens("  __ ").empty());
}

TEST_CASE("parseKeyToken accepts real keys", "[name]")
{
    CHECK(parseKeyToken("Am") == "Am");
    CHECK(parseKeyToken("C#m") == "C#m");
    CHECK(parseKeyToken("Bb") == "A#");
    CHECK(parseKeyToken("Ebmaj") == "D#");
    CHECK(parseKeyToken("F#") == "F#");
    CHECK(parseKeyToken("Dmin") == "Dm");
    CHECK(parseKeyToken("Cb") == "B");
    CHECK(parseKeyToken("amin") == "Am");
    CHECK(parseKeyToken("E", "Minor") == "Em");
    CHECK(parseKeyToken("A", "major") == "A");
}

TEST_CASE("parseKeyToken rejects words and variant markers", "[name]")
{
    CHECK_FALSE(parseKeyToken("A").has_value());
    CHECK_FALSE(parseKeyToken("am").has_value());
    CHECK_FALSE(parseKeyToken("eb").has_value());
    CHECK_FALSE(parseKeyToken("AM").has_value());
    CHECK_FALSE(parseKeyToken("Kick").has_value());
    CHECK_FALSE(parseKeyToken("Abc").has_value());
    CHECK_FALSE(parseKeyToken("").has_value());
}

TEST_CASE("canonicalKey takes the keys asma writes and nothing else", "[name]")
{
    for (const char* key : {"C", "C#", "A", "Am", "F#m", "B"}) CHECK(canonicalKey(key) == key);
    for (const char* key : {"", "c", "Bb", "H", "Amin", "C#M", "Am "}) CHECK_FALSE(canonicalKey(key));
}

TEST_CASE("parseName reads BPM, key and loop from a typical loop", "[name]")
{
    const NameInfo n = parseName("Loops/Bass_Loop_Am_128.wav");
    CHECK(n.bpm == 128.0);
    CHECK(n.key == "Am");
    CHECK(n.isLoop == true);
    CHECK(n.tokens == Tokens{"bass", "loop", "am", "128", "loops"});
}

TEST_CASE("parseName BPM rules", "[name]")
{
    CHECK(parseName("Drums/Kick_120bpm.wav").bpm == 120.0);
    CHECK(parseName("Drums/BPM 95 Groove.wav").bpm == 95.0);
    CHECK(parseName("90 BPM Loops/Groove.wav").bpm == 90.0);
    CHECK_FALSE(parseName("Drums/Kick_01.wav").bpm.has_value());
    CHECK_FALSE(parseName("Loops/Drum_Loop_05.wav").bpm.has_value());
    CHECK_FALSE(parseName("Drums/Snare_128.wav").bpm.has_value()); // not a loop
}

TEST_CASE("parseName loop rules, stem before folders", "[name]")
{
    CHECK(parseName("One Shots/Snare.wav").isLoop == false);
    CHECK(parseName("Loops/Kick One Shot.wav").isLoop == false);
    CHECK(parseName("Oneshots/Loop Kit/Snare Loop.wav").isLoop == true);
    CHECK_FALSE(parseName("Drums/Snare.wav").isLoop.has_value());
}

TEST_CASE("parseName key rules", "[name]")
{
    CHECK(parseName("Pads/Pad F# Minor.wav").key == "F#m");
    CHECK_FALSE(parseName("One Shots/Snare_C.wav").key.has_value());
    CHECK(parseName("Cm/Stab.wav").key == "Cm");
}

TEST_CASE("parseName tolerates odd paths", "[name]")
{
    CHECK(parseName("").tokens.empty());
    CHECK(parseName(".hidden").tokens == Tokens{"hidden"});
    CHECK(parseName("no_extension").tokens == Tokens{"no", "extension"});
}
