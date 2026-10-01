// SPDX-License-Identifier: GPL-3.0-only
#include "PluginState.h"
#include "LibraryFixture.h"
#include "PluginTestUtil.h"
#include "asma/core/Fs.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::PluginState;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

PluginState everything()
{
    PluginState s;
    s.selected = "/Samples/Café/Loop_Am_120.wav";
    s.search.text = "dusty";
    s.search.type = SampleType::Loop;
    s.search.keys = {"Am", "C"};
    s.sync.tempo = false;
    s.sync.key = true;
    s.sync.projectKey = "F#m";
    s.sync.hostBpm = 96.5;
    s.gainMatch = false;
    s.quantise = 4.0;
    s.edits.trimStart = 0.25;
    s.edits.trimEnd = 1.5;
    s.edits.direction = audio::Direction::PingPong;
    s.edits.loop = audio::LoopMode::Off;
    s.width = 1200;
    s.height = 700;
    return s;
}

} // namespace

TEST_CASE("plugin state survives a round trip through JSON", "[state]")
{
    const PluginState a = everything();
    const PluginState b = app::pluginStateFromJson(app::toJson(a));
    CHECK(b.selected == a.selected);
    CHECK(searchModelToJson(b.search) == searchModelToJson(a.search));
    CHECK(b.sync.tempo == false);
    CHECK(b.sync.key == true);
    CHECK(b.sync.projectKey.view() == "F#m");
    CHECK(b.sync.hostBpm == 96.5);
    CHECK(b.gainMatch == false);
    CHECK(b.quantise == 4.0);
    CHECK(b.edits.trimStart == 0.25);
    CHECK(b.edits.trimEnd == 1.5);
    CHECK(b.edits.direction == audio::Direction::PingPong);
    CHECK(b.edits.loop == audio::LoopMode::Off);
    CHECK(b.width == 1200);
    CHECK(b.height == 700);
}

TEST_CASE("plugin state from another version loads what it can", "[state]")
{
    // Unknown fields, wrong types and out-of-range values are skipped.
    const PluginState s = app::pluginStateFromJson(
        R"({"v":7,"selected":42,"future":{"x":1},"gain_match":false,"quantise":-3,"direction":"sideways",)"
        R"("loop":"on","trim_start":"soon","width":5,"height":800,"project_key":"H","search":"{\"text\":\"kick\"}"})");
    CHECK(s.selected.empty());
    CHECK(s.gainMatch == false);
    CHECK(s.quantise == 0.0);
    CHECK(s.edits.direction == audio::Direction::Forward);
    CHECK(s.edits.loop == audio::LoopMode::On);
    CHECK(s.edits.trimStart == 0.0);
    CHECK(s.width == PluginState{}.width); // too small to be a window
    CHECK(s.height == 800);
    CHECK(s.sync.projectKey.empty());
    CHECK(s.search.text == "kick");

    for (const char* junk : {"", "not json", "[1,2]", "{\"search\":\"{\""}) {
        INFO(junk);
        const PluginState d = app::pluginStateFromJson(junk);
        CHECK(d.selected.empty());
        CHECK(d.gainMatch == true);
    }
}

TEST_CASE("the processor saves its state and a restore reselects the sample", "[state]")
{
    TempDir dir;
    const auto file = dir.path() / "a.wav";
    test::writeWavFloat(file, 48000, {std::vector<float>(4800, 0.25f)});

    app::AsmaProcessor a;
    PluginState s = everything();
    s.selected = toUtf8(file);
    a.setPluginState(s);
    juce::MemoryBlock saved;
    a.getStateInformation(saved);

    app::AsmaProcessor b;
    b.prepareToPlay(48000.0, 512);
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(b.pluginState().selected == toUtf8(file));
    CHECK(b.pluginState().sync.projectKey.view() == "F#m");
    CHECK(asma::test::waitForPreview(b, 1)); // selected, not played
    CHECK_FALSE(b.engine().status().playing);

    app::AsmaProcessor c; // a project that saved a file since deleted
    PluginState gone;
    gone.selected = toUtf8(dir.path() / "gone.wav");
    c.setPluginState(gone);
    c.prepareToPlay(48000.0, 512);
    CHECK(c.pluginState().selected == gone.selected); // kept: the drive may come back

    app::AsmaProcessor d; // garbage from a broken host
    d.setStateInformation("\xff\x00junk", 6);
    CHECK(d.pluginState().gainMatch == true);
}

TEST_CASE("restoring a project over a broken library does not take the host down", "[state]")
{
    test::LibraryFixture f;
    f.scan();
    // Keep the first page, which opening reads, and break the rest, which the
    // lookup of the restored sample reads: it opens, then fails.
    for (const char* suffix : {"-wal", "-shm"}) fs::remove(fs::path(f.dbPath.string() + suffix));
    const auto size = fs::file_size(f.dbPath);
    {
        std::fstream db(f.dbPath, std::ios::in | std::ios::out | std::ios::binary);
        db.seekp(4096);
        const std::string junk(static_cast<std::size_t>(size) - 4096, 'x');
        db.write(junk.data(), static_cast<std::streamsize>(junk.size()));
    }
    app::AsmaProcessor p;
    PluginState s;
    s.selected = toUtf8(f.loop);
    const std::string json = app::toJson(s);
    CHECK_NOTHROW(p.setStateInformation(json.data(), static_cast<int>(json.size())));
    CHECK(p.pluginState().selected == s.selected);
}
