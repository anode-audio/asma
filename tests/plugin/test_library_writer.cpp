// SPDX-License-Identifier: GPL-3.0-only
#include "AsmaProcessor.h"
#include "LibraryFixture.h"
#include "LibraryWriter.h"
#include "asma/core/Fs.h"
#include "asma/core/Scanner.h"
#include "asma/core/WriterLock.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <memory>
#include <sstream>

using namespace asma;
namespace fs = std::filesystem;
using app::CliWriter;
using app::DirectWriter;
using app::LibraryWriter;
using app::RetryEvent;
using app::Write;

namespace {

// Runs the message loop until the writer has nothing queued or running, and
// the outcomes it posted have been delivered.
void settle(LibraryWriter& writer)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!writer.idle() && std::chrono::steady_clock::now() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}

// Everything the user has added to the library, as text.
std::string userData(const fs::path& dbPath)
{
    Db db = Db::open(dbPath);
    std::ostringstream out;
    const auto dump = [&](const char* title, int columns, const char* sql) {
        out << title << ":";
        auto q = db.prepare(sql);
        while (q.step()) {
            out << " (";
            for (int c = 0; c < columns; ++c) out << (c ? "," : "") << q.getText(c);
            out << ")";
        }
        out << "\n";
    };
    dump("ratings", 2, "SELECT f.rel_path, r.rating FROM ratings r JOIN files f ON f.id = r.file_id ORDER BY 1");
    dump("favourites", 1, "SELECT f.rel_path FROM favourites x JOIN files f ON f.id = x.file_id ORDER BY 1");
    dump("tags", 2, "SELECT f.rel_path, t.name FROM file_tags x JOIN files f ON f.id = x.file_id JOIN tags t ON t.id = "
                 "x.tag_id WHERE x.source = 'user' ORDER BY 1, 2");
    dump("collections", 2, "SELECT c.name, COALESCE(f.rel_path, '') FROM collections c LEFT JOIN collection_items i ON "
                        "i.collection_id = c.id LEFT JOIN files f ON f.id = i.file_id ORDER BY 1, 2");
    dump("searches", 2, "SELECT name, model FROM saved_searches ORDER BY 1");
    return out.str();
}

std::int64_t idOf(const fs::path& dbPath, const fs::path& file)
{
    Db db = Db::open(dbPath);
    return Library(db).fileByAbsolutePath(file).value().id;
}

// The same writes, whichever writer makes them; errors collected in order.
std::vector<std::string> writeEverything(LibraryWriter& writer, const test::LibraryFixture& f)
{
    const auto loop = idOf(f.dbPath, f.loop);
    const auto kick = idOf(f.dbPath, f.kick);
    SearchModel fast;
    fast.sort = SortField::Bpm;
    fast.descending = true;
    std::vector<std::string> errors;
    const auto collect = [&](const std::string& error) { errors.push_back(error); };
    writer.write(Write::rate(loop, 4), collect);
    writer.write(Write::rate(kick, 2), collect);
    writer.write(Write::rate(kick, 0), collect);
    writer.write(Write::favourite(kick, true), collect);
    writer.write(Write::addTag(loop, " Dusty "), collect);
    writer.write(Write::addTag(loop, "warm"), collect);
    writer.write(Write::removeTag(loop, "WARM"), collect);
    writer.write(Write::createCollection("Live set", loop), collect);
    writer.write(Write::createCollection("Album"), collect);
    writer.write(Write::addToCollection("Album", kick), collect);
    writer.write(Write::addToCollection("live set", kick), collect);
    writer.write(Write::removeFromCollection("Live set", loop), collect);
    writer.write(Write::renameCollection("Album", "Album 2"), collect);
    writer.write(Write::createCollection("Gone"), collect);
    writer.write(Write::deleteCollection("Gone"), collect);
    writer.write(Write::saveSearch("Fast", fast), collect);
    writer.write(Write::saveSearch("Old", {}), collect);
    writer.write(Write::renameSearch("Old", "Older"), collect);
    writer.write(Write::deleteSearch("Older"), collect);
    writer.write(Write::createCollection("album 2"), collect); // taken
    writer.write(Write::rate(loop, 6), collect);               // out of range
    settle(writer);
    return errors;
}

constexpr const char* kEverything = "ratings: (Loops/Bass_Loop_Am_120.wav,4)\n"
                                    "favourites: (Drums/Kick_01.wav)\n"
                                    "tags: (Loops/Bass_Loop_Am_120.wav,dusty)\n"
                                    "collections: (Album 2,Drums/Kick_01.wav) (Live set,Drums/Kick_01.wav)\n"
                                    "searches: (Fast,{\"v\":1,\"sort\":\"bpm\",\"desc\":true})\n";

} // namespace

TEST_CASE("cliCommands says each write as asma commands", "[writer]")
{
    using V = std::vector<std::vector<std::string>>;
    CHECK(app::cliCommands(Write::rate(7, 3)) == V{{"rate", "3", "--id", "7"}});
    CHECK(app::cliCommands(Write::favourite(7, false)) == V{{"fav", "off", "--id", "7"}});
    CHECK(app::cliCommands(Write::addTag(7, "dusty")) == V{{"tag", "add", "--id", "7", "--", "dusty"}});
    CHECK(app::cliCommands(Write::createCollection("Set")) == V{{"collection", "create", "--", "Set"}});
    CHECK(app::cliCommands(Write::createCollection("Set", 7)) ==
          V{{"collection", "create", "--", "Set"}, {"collection", "add", "--id", "7", "--", "Set"}});
    CHECK(app::cliCommands(Write::renameSearch("A", "B")) == V{{"search", "rename", "--", "A", "B"}});
    SearchModel model;
    model.text = "kick";
    CHECK(app::cliCommands(Write::saveSearch("Kicks", model)) ==
          V{{"search", "save", "--json", "{\"v\":1,\"text\":\"kick\"}", "--", "Kicks"}});
}

TEST_CASE("a failed write says what could not be done and why", "[writer]")
{
    CHECK(app::reasonText("database is locked") == "the library is busy");
    CHECK(app::reasonText("no collection called 'X'") == "no collection called 'X'");
    CHECK(app::failureText(Write::rate(1, 3), "the library is busy") == "Could not save the rating: the library is busy");
    CHECK(app::failureText(Write::addTag(1, "x"), "why") == "Could not change the tags: why");
}

TEST_CASE("both writers leave the library the same", "[writer]")
{
    test::LibraryFixture f;
    f.scan();
    const juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<LibraryWriter> writer;
    SECTION("directly")
    {
        writer = std::make_unique<DirectWriter>(f.dbPath, ASMA_CLI_PATH);
    }
    SECTION("through the helper")
    {
        writer = std::make_unique<CliWriter>(f.dbPath, ASMA_CLI_PATH);
    }
    const auto errors = writeEverything(*writer, f);
    REQUIRE(errors.size() == 21);
    for (std::size_t i = 0; i < 19; ++i) CHECK(errors[i].empty());
    CHECK(errors[19] == "a collection called 'album 2' already exists");
    CHECK(errors[20] == "a rating is 1 to 5, or 0 to clear it");
    CHECK(userData(f.dbPath) == kEverything);
}

TEST_CASE("names that look like options mean the same to both writers", "[writer]")
{
    test::LibraryFixture f;
    f.scan();
    const juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<LibraryWriter> writer;
    SECTION("directly")
    {
        writer = std::make_unique<DirectWriter>(f.dbPath, ASMA_CLI_PATH);
    }
    SECTION("through the helper")
    {
        writer = std::make_unique<CliWriter>(f.dbPath, ASMA_CLI_PATH);
    }
    const auto loop = idOf(f.dbPath, f.loop);
    std::vector<std::string> errors;
    const auto collect = [&](const std::string& error) { errors.push_back(error); };
    writer->write(Write::createCollection("--version"), collect);
    writer->write(Write::createCollection("-- Drums --", loop), collect);
    writer->write(Write::renameCollection("--version", "--help"), collect);
    writer->write(Write::addTag(loop, "--id"), collect);
    writer->write(Write::saveSearch("--json", {}), collect);
    writer->write(Write::renameSearch("--json", "--db"), collect);
    settle(*writer);
    CHECK(errors == std::vector<std::string>(6));
    CHECK(userData(f.dbPath) == "ratings:\n"
                                "favourites:\n"
                                "tags: (Loops/Bass_Loop_Am_120.wav,--id)\n"
                                "collections: (-- Drums --,Loops/Bass_Loop_Am_120.wav) (--help,)\n"
                                "searches: (--db,{\"v\":1})\n");
}

TEST_CASE("the helper makes writes in the order given, so the last click wins", "[writer]")
{
    test::LibraryFixture f;
    f.scan();
    const juce::ScopedJuceInitialiser_GUI gui;
    CliWriter writer(f.dbPath, ASMA_CLI_PATH);
    const auto loop = idOf(f.dbPath, f.loop);
    for (int rating : {1, 5, 2, 4, 3}) writer.write(Write::rate(loop, rating));
    settle(writer);
    CHECK(userData(f.dbPath).rfind("ratings: (Loops/Bass_Loop_Am_120.wav,3)\n", 0) == 0);
}

TEST_CASE("a missing helper is reported, not hidden", "[writer]")
{
    test::LibraryFixture f;
    f.scan();
    const juce::ScopedJuceInitialiser_GUI gui;
    CliWriter writer(f.dbPath, f.dir.path() / "no-such-asma");
    std::string error;
    writer.write(Write::rate(idOf(f.dbPath, f.loop), 3), [&](const std::string& e) { error = e; });
    settle(writer);
    CHECK(error == "asma's command-line helper is missing");
}

TEST_CASE("Retry all goes to the helper in chunks a command line can hold", "[writer][retry]")
{
    std::vector<std::int64_t> ids;
    for (std::int64_t id = 1; id <= 1201; ++id) ids.push_back(id);
    const auto steps = app::retrySteps(ids);
    REQUIRE(steps.size() == 3);
    CHECK(steps[0].size() == 1 + 2 * 500); // "retry", then --id N for each
    CHECK(steps[2].size() == 1 + 2 * 201);
    CHECK(steps[2].back() == "1201");
}

TEST_CASE("a helper that is there but will not start is not called missing", "[writer]")
{
    test::LibraryFixture f;
    f.scan();
    const juce::ScopedJuceInitialiser_GUI gui;
    const fs::path notAProgram = f.dir.path() / "asma-cli";
    test::writeBytes(notAProgram, "not a program");
    CliWriter writer(f.dbPath, notAProgram);
    std::string error;
    writer.write(Write::rate(idOf(f.dbPath, f.loop), 3), [&](const std::string& e) { error = e; });
    settle(writer);
    CHECK_FALSE(error.empty());
    CHECK(error != "asma's command-line helper is missing");
}

TEST_CASE("a helper that crashes is reported as a crash", "[writer]")
{
    test::LibraryFixture f;
    f.scan();
    const juce::ScopedJuceInitialiser_GUI gui;
    test::ScopedEnv crash("ASMA_FAKE_SCAN", "crash_start=1"); // the test child crashes at once
    CliWriter writer(f.dbPath, ASMA_TEST_CHILD_PATH);
    std::string error;
    writer.write(Write::rate(idOf(f.dbPath, f.loop), 3), [&](const std::string& e) { error = e; });
    settle(writer);
    CHECK(error == "the helper crashed");
}

TEST_CASE("a retry waits for the scan, then reports that it finished", "[writer][retry]")
{
    test::LibraryFixture f;
    test::writeBytes(f.lib / "Drums" / "broken.wav", "not audio");
    f.scan();
    const juce::ScopedJuceInitialiser_GUI gui;
    DirectWriter writer(f.dbPath, ASMA_CLI_PATH);
    std::vector<RetryEvent::Kind> events;
    {
        auto lock = WriterLock::tryAcquire(f.dbPath.parent_path());
        REQUIRE(lock);
        writer.retry({idOf(f.dbPath, f.lib / "Drums" / "broken.wav")},
                     [&](const RetryEvent& e) { events.push_back(e.kind); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (events.empty() && std::chrono::steady_clock::now() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    settle(writer);
    CHECK(events == std::vector<RetryEvent::Kind>{RetryEvent::Kind::Waiting, RetryEvent::Kind::Finished});
}

TEST_CASE("the helper ships as asma-cli, beside the binary", "[writer]")
{
#ifdef _WIN32
    CHECK(LibraryWriter::cliNextTo("C:/Plugins/asma.vst3/Contents/x86_64-win/asma.vst3") ==
          fs::path("C:/Plugins/asma.vst3/Contents/x86_64-win/asma-cli.exe"));
#else
    CHECK(LibraryWriter::cliNextTo("/Library/asma.vst3/Contents/MacOS/asma") ==
          fs::path("/Library/asma.vst3/Contents/MacOS/asma-cli"));
#endif
}

TEST_CASE("a plugin writes through the helper, the standalone directly", "[writer]")
{
    test::LibraryFixture f;
    const juce::ScopedJuceInitialiser_GUI gui;
    app::AsmaProcessor plugin;
    app::AsmaProcessor standalone(app::AsmaProcessor::Mode::Standalone);
    CHECK(dynamic_cast<CliWriter*>(&plugin.writer()) != nullptr);
    CHECK(dynamic_cast<DirectWriter*>(&standalone.writer()) != nullptr);
}

TEST_CASE("a lane gone before its outcome is delivered delivers nothing", "[writer]")
{
    test::LibraryFixture f;
    f.scan();
    const juce::ScopedJuceInitialiser_GUI gui;
    bool delivered = false;
    {
        app::CliLane lane(f.dbPath, ASMA_CLI_PATH);
        app::CliLane::Command command;
        command.steps = {{"check"}};
        command.onLine = [&](const std::string&) { delivered = true; };
        command.onEnd = [&](const std::string&) { delivered = true; };
        lane.run(std::move(command));
        // The outcome is posted to the message loop, which does not run yet.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!lane.idle() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        REQUIRE(lane.idle());
    } // whoever owned the lane, and what its callbacks point at, is gone
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK_FALSE(delivered);
}

#ifndef _WIN32 // Windows never lets a file a connection holds be replaced
TEST_CASE("after a rebuild the standalone writes to the new library, not the one moved aside", "[writer]")
{
    test::LibraryFixture f;
    f.scan();
    const juce::ScopedJuceInitialiser_GUI gui;
    DirectWriter writer(f.dbPath, ASMA_CLI_PATH);
    writer.write(Write::rate(idOf(f.dbPath, f.loop), 2)); // the writer's connection is open now
    // A rebuild puts a new library where the old one was.
    const fs::path rebuilt = f.dir.path() / "data" / "rebuilt.db";
    {
        Db db = Db::open(rebuilt);
        Library lib(db);
        scanRoot(db, lib.addRoot(f.lib));
    }
    fs::rename(f.dbPath, f.dir.path() / "data" / "library.db.corrupt");
    fs::rename(rebuilt, f.dbPath);
    std::string error;
    writer.write(Write::rate(idOf(f.dbPath, f.loop), 5), [&](const std::string& e) { error = e; });
    CHECK(error.empty());
    CHECK(userData(f.dbPath).rfind("ratings: (Loops/Bass_Loop_Am_120.wav,5)\n", 0) == 0);
}
#endif
