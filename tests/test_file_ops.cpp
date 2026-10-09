// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/FileOps.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"
#include "asma/core/Scanner.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

// A scanned library of three samples in Samples, with an empty Samples/Drums
// and a trash of its own (the tests never touch the system's).
struct Lib {
    TempDir dir;
    fs::path samples = dir.path() / "Samples";
    fs::path trashDir = dir.path() / "Trash";
    Db db;
    std::int64_t root = 0;
    std::map<std::string, std::int64_t> id;
    bool trashWorks = true;
    int trashCalls = 0;
    int failTrashCall = 0; // the call that fails (0: none)

    Lib() : db(open(dir.path()))
    {
        std::uint32_t seed = 1;
        for (const char* name : {"kick.wav", "snare.wav", "hat.wav"}) {
            test::WavSpec spec;
            spec.seed = seed++;
            test::writeWav(samples / name, spec);
        }
        fs::create_directories(samples / "Drums");
        fs::create_directories(trashDir);
        Library lib(db);
        root = lib.addRoot(samples);
        scanRoot(db, root);
        for (const auto& f : lib.filesInRoot(root)) id[f.relPath] = f.id;
        UserData(db).setRating(id["kick.wav"], 5);
    }
    static Db open(const fs::path& dir) { return Db::open(dir / "library.db"); }

    TrashBackend trash()
    {
        TrashBackend t;
        t.available = [this](const fs::path&) { return trashWorks; };
        t.move = [this](const fs::path& file) {
            TrashResult r;
            if (++trashCalls == failTrashCall) {
                r.error = "the disk said no";
                return r;
            }
            r.where = trashDir / (std::to_string(trashCalls) + "-" + file.filename().string());
            r.ok = !renameNoReplace(file, r.where);
            return r;
        };
        t.restore = [](const fs::path& where, const fs::path& to) {
            if (!fs::exists(where)) return std::string("it is no longer in the Trash");
            return renameNoReplace(where, to) ? std::string("its old place is taken") : std::string();
        };
        return t;
    }
    FileOps ops() { return FileOps(db, trash()); }
    std::optional<FileRecord> file(const std::string& name) { return Library(db).fileById(id.at(name)); }
    std::int64_t shown() { return countSearch(db, SearchModel{}); }
};

std::string refusal(const std::function<void()>& f)
{
    try {
        f();
    } catch (const OperationRefused& e) {
        return e.what();
    }
    return "(not refused)";
}

} // namespace

TEST_CASE("Renaming a sample keeps its data, and undo renames it back", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    const OpResult done = ops.rename(lib.id["kick.wav"], "Kick 01.wav");
    CHECK(done.label == "Rename kick.wav");
    CHECK(done.files == std::vector<std::int64_t>{lib.id["kick.wav"]});
    CHECK(fs::exists(lib.samples / "Kick 01.wav"));
    CHECK_FALSE(fs::exists(lib.samples / "kick.wav"));
    CHECK(lib.file("kick.wav")->relPath == "Kick 01.wav");
    CHECK(UserData(lib.db).rating(lib.id["kick.wav"]) == 5);
    REQUIRE(ops.undoable());
    CHECK(ops.undoable()->label == "Rename kick.wav");

    const auto undone = ops.undo();
    REQUIRE(undone);
    CHECK(undone->skipped.empty());
    CHECK(fs::exists(lib.samples / "kick.wav"));
    CHECK(lib.file("kick.wav")->relPath == "kick.wav");
    CHECK_FALSE(ops.undoable());
    CHECK_FALSE(ops.undo());
}

TEST_CASE("A rename is refused before anything is touched", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    const auto kick = lib.id["kick.wav"];
    CHECK(refusal([&] { ops.rename(kick, "snare.wav"); }) == "snare.wav already exists in Samples.");
    CHECK(refusal([&] { ops.rename(kick, "kick.aif"); }) == "A rename keeps the extension: .wav.");
    CHECK(refusal([&] { ops.rename(kick, "a/b.wav"); }) == "A name cannot contain / \\ : * ? \" < > |.");
    CHECK(refusal([&] { ops.rename(kick, ".wav"); }) == "A name is needed.");
    CHECK(refusal([&] { ops.rename(kick, "kick.wav"); }) == "kick.wav has that name already.");
    test::writeBytes(lib.samples / "kick.wav", "changed behind asma's back");
    CHECK(refusal([&] { ops.rename(kick, "boom.wav"); })
          == "kick.wav has changed since asma last read it; try again after the next scan.");
    CHECK(fs::exists(lib.samples / "kick.wav"));
    CHECK_FALSE(ops.undoable());
}

TEST_CASE("A rename that only changes letter case works", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    ops.rename(lib.id["kick.wav"], "KICK.wav");
    CHECK(lib.file("kick.wav")->relPath == "KICK.wav");
    bool listed = false;
    for (const auto& e : fs::directory_iterator(lib.samples)) listed |= e.path().filename() == "KICK.wav";
    CHECK(listed);
    ops.undo();
    CHECK(lib.file("kick.wav")->relPath == "kick.wav");
}

TEST_CASE("Moving samples keeps their ids, and a collision moves none", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    const OpResult done = ops.move({lib.id["kick.wav"], lib.id["snare.wav"]}, lib.samples / "Drums");
    CHECK(done.label == "Move 2 Samples");
    CHECK(fs::exists(lib.samples / "Drums" / "kick.wav"));
    CHECK(lib.file("kick.wav")->relPath == "Drums/kick.wav");
    CHECK(lib.file("snare.wav")->relPath == "Drums/snare.wav");
    CHECK(lib.shown() == 3);
    ops.undo();
    CHECK(lib.file("kick.wav")->relPath == "kick.wav");

    test::writeBytes(lib.samples / "Drums" / "kick.wav", "another kick");
    test::writeBytes(lib.samples / "Drums" / "hat.wav", "another hat");
    CHECK(refusal([&] { ops.move({lib.id["kick.wav"], lib.id["snare.wav"], lib.id["hat.wav"]}, lib.samples / "Drums"); })
          == "2 of 3 samples already exist in Drums: hat.wav, kick.wav.");
    CHECK(refusal([&] { ops.move({lib.id["kick.wav"]}, lib.samples / "Drums"); })
          == "kick.wav already exists in Drums.");
    CHECK(fs::exists(lib.samples / "snare.wav"));
    CHECK(refusal([&] { ops.move({lib.id["kick.wav"]}, lib.dir.path()); }) == "That folder is outside the library's folders.");
    CHECK(refusal([&] { ops.move({lib.id["kick.wav"]}, lib.samples); }) == "kick.wav is already in Samples.");
}

TEST_CASE("Trashed samples leave the library with their data kept, and undo brings them back", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    const OpResult done = ops.trash({lib.id["kick.wav"], lib.id["hat.wav"]});
    CHECK(done.label == "Move 2 Samples to the Trash");
    CHECK_FALSE(fs::exists(lib.samples / "kick.wav"));
    CHECK(lib.shown() == 1);
    CHECK(lib.file("kick.wav")->status == FileStatus::Missing);
    // A scan now finds them gone, and a new file where one was is new.
    test::WavSpec other;
    other.seed = 99;
    test::writeWav(lib.samples / "kick.wav", other);
    CHECK(scanRoot(lib.db, lib.root).added == 1);
    fs::remove(lib.samples / "kick.wav");
    scanRoot(lib.db, lib.root);

    const auto undone = ops.undo();
    REQUIRE(undone);
    CHECK(undone->skipped.empty());
    CHECK(fs::exists(lib.samples / "kick.wav"));
    CHECK(lib.file("kick.wav")->status == FileStatus::Ok);
    CHECK(lib.file("kick.wav")->relPath == "kick.wav");
    CHECK(UserData(lib.db).rating(lib.id["kick.wav"]) == 5);
    CHECK(lib.shown() == 3);

    lib.trashWorks = false;
    CHECK(refusal([&] { ops.trash({lib.id["kick.wav"]}); })
          == "kick.wav can't go to the Trash: its disk has none. Nothing was moved.");
    CHECK(fs::exists(lib.samples / "kick.wav"));
}

TEST_CASE("Undo skips what it cannot put back and says why", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    ops.move({lib.id["kick.wav"], lib.id["snare.wav"]}, lib.samples / "Drums");
    test::writeBytes(lib.samples / "kick.wav", "a new kick where the old one was");
    const auto undone = ops.undo();
    REQUIRE(undone);
    CHECK(undone->skipped == std::vector<std::string>{"kick.wav could not be moved back: its old place is taken"});
    CHECK(lib.file("snare.wav")->relPath == "snare.wav");
    CHECK(lib.file("kick.wav")->relPath == "Drums/kick.wav");
    CHECK_FALSE(ops.undoable());

    ops.trash({lib.id["hat.wav"]});
    fs::remove_all(lib.trashDir); // emptied
    CHECK(ops.undo()->skipped == std::vector<std::string>{"hat.wav could not be brought back: it is no longer in the Trash"});
}

TEST_CASE("A group a crash interrupted is rolled back when asma starts", "[fileops]")
{
    Lib lib;
    {
        auto ops = lib.ops();
        ops.crashAfter(2);
        CHECK_THROWS_AS(ops.move({lib.id["kick.wav"], lib.id["snare.wav"], lib.id["hat.wav"]}, lib.samples / "Drums"),
                        SimulatedCrash);
    }
    CHECK(fs::exists(lib.samples / "Drums" / "snare.wav"));
    CHECK(fs::exists(lib.samples / "hat.wav")); // the third step never ran

    auto ops = lib.ops();
    const auto recovered = ops.recover();
    REQUIRE(recovered.size() == 1);
    CHECK(interruptedText(recovered[0]) == "asma was interrupted while moving 3 samples; they are back where they were.");
    for (const char* name : {"kick.wav", "snare.wav", "hat.wav"}) CHECK(fs::exists(lib.samples / name));
    CHECK(lib.file("snare.wav")->relPath == "snare.wav");
    CHECK(ops.recover().empty());
    CHECK_FALSE(ops.undoable());
    CHECK(ops.history().front().state == "rolled_back");
}

TEST_CASE("A step that fails rolls back the group and names the file", "[fileops]")
{
    Lib lib;
    lib.failTrashCall = 2;
    auto ops = lib.ops();
    CHECK(refusal([&] { ops.trash({lib.id["hat.wav"], lib.id["kick.wav"], lib.id["snare.wav"]}); })
          == "Could not move kick.wav to the Trash: the disk said no. Nothing was moved.");
    for (const char* name : {"kick.wav", "snare.wav", "hat.wav"}) CHECK(fs::exists(lib.samples / name));
    CHECK(lib.shown() == 3);
    CHECK_FALSE(ops.undoable());
}

TEST_CASE("Removing a folder hides its samples, and undo puts it back", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    CHECK(ops.removeFolder(lib.root).label == "Remove Samples from the Library");
    CHECK(lib.shown() == 0);
    CHECK_FALSE(Library(lib.db).root(lib.root)->enabled);
    CHECK(refusal([&] { ops.removeFolder(lib.root); }) == "Samples is not in the library.");
    ops.undo();
    CHECK(lib.shown() == 3);
    CHECK(UserData(lib.db).rating(lib.id["kick.wav"]) == 5);
}

TEST_CASE("The journal keeps the newest 50 groups; older trashed samples go for good", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    ops.trash({lib.id["kick.wav"]});
    for (int i = 0; i < 50; ++i) {
        ops.rename(lib.id["snare.wav"], i % 2 ? "snare.wav" : "snare 2.wav");
    }
    CHECK(ops.history().size() == 50);
    CHECK(ops.history().front().label == "Rename snare 2.wav");
    CHECK_FALSE(Library(lib.db).fileById(lib.id["kick.wav"])); // its row went with its group
    CHECK_FALSE(UserData(lib.db).rating(lib.id["kick.wav"]));
    CHECK(Library(lib.db).fileById(lib.id["hat.wav"]));
}
