// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Fs.h"

#include <catch2/catch_test_macros.hpp>

namespace fs = std::filesystem;
using asma::test::TempDir;

TEST_CASE("toUtf8 and fromUtf8 round-trip non-ASCII names", "[fs]")
{
    const std::string name = "Café Loops/Kick Ü 808.wav";
    CHECK(asma::toUtf8(asma::fromUtf8(name)) == name);
}

TEST_CASE("toUtf8 always uses forward slashes", "[fs]")
{
    CHECK(asma::toUtf8(fs::path("a") / "b" / "c.wav") == "a/b/c.wav");
}

TEST_CASE("openFileRead opens files with non-ASCII names", "[fs]")
{
    TempDir dir;
    const fs::path file = dir.path() / asma::fromUtf8("héllo ß.wav");
    asma::test::writeBytes(file, "abc");
    asma::FilePtr f(asma::openFileRead(file));
    REQUIRE(f);
    char buf[3];
    CHECK(std::fread(buf, 1, 3, f.get()) == 3);
}

TEST_CASE("seekFile moves to an absolute offset", "[fs]")
{
    TempDir dir;
    const fs::path file = dir.path() / "x.bin";
    asma::test::writeBytes(file, "0123456789");
    asma::FilePtr f(asma::openFileRead(file));
    REQUIRE(asma::seekFile(f.get(), 7));
    CHECK(std::fgetc(f.get()) == '7');
}

TEST_CASE("defaultDataDir honours ASMA_DATA_DIR", "[fs]")
{
    TempDir dir;
    const std::string wanted = dir.path().string();
    asma::test::ScopedEnv env("ASMA_DATA_DIR", wanted.c_str());
    CHECK(asma::defaultDataDir() == dir.path());
}

TEST_CASE("defaultCacheDir honours ASMA_CACHE_DIR and differs from the data dir", "[fs]")
{
    CHECK(asma::defaultCacheDir() != asma::defaultDataDir());
    TempDir dir;
    const std::string wanted = dir.path().string();
    asma::test::ScopedEnv env("ASMA_CACHE_DIR", wanted.c_str());
    CHECK(asma::defaultCacheDir() == dir.path());
}

TEST_CASE("fileTimeToInt orders later times after earlier ones", "[fs]")
{
    const auto now = fs::file_time_type::clock::now();
    CHECK(asma::fileTimeToInt(now) < asma::fileTimeToInt(now + std::chrono::seconds(1)));
}
