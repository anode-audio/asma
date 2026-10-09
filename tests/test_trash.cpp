// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Fs.h"
#include "asma/core/Trash.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

} // namespace

TEST_CASE("The system trash takes a file and gives it back", "[trash]")
{
    TempDir dir;
#ifdef __linux__
    asma::test::ScopedEnv xdg("XDG_DATA_HOME", (dir.path() / "share").string().c_str());
#endif
    // A name no other run uses, since the trash outlives the test.
    const fs::path file = dir.path() / ("asma trash test " + dir.path().filename().string() + ".wav");
    test::writeBytes(file, "RIFF kick");
    REQUIRE(trashAvailable(file));

    const TrashResult trashed = moveToTrash(file);
    REQUIRE(trashed.ok);
    CHECK(trashed.error.empty());
    CHECK_FALSE(fs::exists(file));
    REQUIRE(fs::exists(trashed.where));
    CHECK(slurp(trashed.where) == "RIFF kick");

    CHECK(restoreFromTrash(trashed.where, file).empty());
    CHECK(fs::exists(file));
    CHECK_FALSE(fs::exists(trashed.where));
    CHECK(slurp(file) == "RIFF kick");
}

TEST_CASE("Restoring from the trash says why it cannot", "[trash]")
{
    TempDir dir;
    test::writeBytes(dir.path() / "taken.wav", "someone else");
    test::writeBytes(dir.path() / "trash" / "kick.wav", "kick");
    CHECK(restoreFromTrash(dir.path() / "trash" / "gone.wav", dir.path() / "gone.wav") == "it is no longer in the Trash");
    CHECK(restoreFromTrash(dir.path() / "trash" / "kick.wav", dir.path() / "taken.wav") == "its old place is taken");
    CHECK(slurp(dir.path() / "taken.wav") == "someone else");
}

TEST_CASE("A rename that would replace a file fails and leaves both", "[trash]")
{
    TempDir dir;
    test::writeBytes(dir.path() / "a.wav", "a");
    test::writeBytes(dir.path() / "b.wav", "b");
    CHECK(renameNoReplace(dir.path() / "a.wav", dir.path() / "b.wav") == std::errc::file_exists);
    CHECK(slurp(dir.path() / "a.wav") == "a");
    CHECK(slurp(dir.path() / "b.wav") == "b");
    CHECK_FALSE(renameNoReplace(dir.path() / "a.wav", dir.path() / "c.wav"));
    CHECK(slurp(dir.path() / "c.wav") == "a");
    CHECK_FALSE(fs::exists(dir.path() / "a.wav"));
}

#ifndef _WIN32
TEST_CASE("The freedesktop trash keeps where each file came from", "[trash]")
{
    TempDir dir;
    const fs::path home = dir.path() / "share" / "Trash";
    const fs::path a = dir.path() / "Drums" / "kick 100%.wav";
    const fs::path b = dir.path() / "Other" / "kick 100%.wav";
    test::writeBytes(a, "a");
    test::writeBytes(b, "b");
    REQUIRE(freedesktop::available(a, home));

    const TrashResult first = freedesktop::moveToTrash(a, home);
    REQUIRE(first.ok);
    CHECK(first.where == home / "files" / "kick 100%.wav");
    const std::string info = slurp(home / "info" / "kick 100%.wav.trashinfo");
    CHECK(info.rfind("[Trash Info]\nPath=", 0) == 0);
    CHECK(info.find("/Drums/kick%20100%25.wav\n") != std::string::npos);
    CHECK(info.find("\nDeletionDate=") != std::string::npos);

    // A second file of the same name gets a number, not the first one's place.
    const TrashResult second = freedesktop::moveToTrash(b, home);
    REQUIRE(second.ok);
    CHECK(second.where == home / "files" / "kick 100% 2.wav");
    CHECK(fs::exists(home / "info" / "kick 100% 2.wav.trashinfo"));
    CHECK(slurp(second.where) == "b");

    CHECK(freedesktop::restore(first.where, a).empty());
    CHECK(slurp(a) == "a");
    CHECK_FALSE(fs::exists(home / "info" / "kick 100%.wav.trashinfo"));
    CHECK(fs::exists(second.where));
}

TEST_CASE("The freedesktop trash never follows a link planted in it", "[trash]")
{
    TempDir dir;
    const fs::path home = dir.path() / "share" / "Trash";
    fs::create_directories(home / "info");
    fs::create_directories(dir.path() / "elsewhere");
    fs::create_directory_symlink(dir.path() / "elsewhere", home / "files");
    const fs::path kick = dir.path() / "kick.wav";
    test::writeBytes(kick, "kick");

    const TrashResult result = freedesktop::moveToTrash(kick, home);
    CHECK_FALSE(result.ok);
    CHECK(fs::exists(kick));
    CHECK(fs::is_empty(dir.path() / "elsewhere"));
}

TEST_CASE("The freedesktop home trash is under XDG_DATA_HOME", "[trash]")
{
    asma::test::ScopedEnv xdg("XDG_DATA_HOME", "/somewhere/share");
    CHECK(freedesktop::homeTrash() == fs::path("/somewhere/share/Trash"));
}
#endif
