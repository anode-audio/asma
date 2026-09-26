// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/WriterLock.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>

using asma::WriterLock;
using asma::test::TempDir;
namespace fs = std::filesystem;

TEST_CASE("acquire writes our pid and blocks a second acquire", "[lock]")
{
    TempDir dir;
    auto lock = WriterLock::tryAcquire(dir.path());
    REQUIRE(lock.has_value());
    CHECK(WriterLock::holder(dir.path()) == WriterLock::currentProcessId());
    CHECK_FALSE(WriterLock::tryAcquire(dir.path()).has_value());
}

TEST_CASE("the lock is released on destruction", "[lock]")
{
    TempDir dir;
    {
        auto lock = WriterLock::tryAcquire(dir.path());
        REQUIRE(lock.has_value());
    }
    CHECK_FALSE(fs::exists(dir.path() / "writer.lock"));
    CHECK(WriterLock::tryAcquire(dir.path()).has_value());
}

TEST_CASE("a lock left by a dead process is taken over", "[lock]")
{
    TempDir dir;
    asma::test::writeBytes(dir.path() / "writer.lock", "999999999");
    auto lock = WriterLock::tryAcquire(dir.path());
    REQUIRE(lock.has_value());
    CHECK(WriterLock::holder(dir.path()) == WriterLock::currentProcessId());
}

TEST_CASE("an unreadable lock is respected while fresh and taken when old", "[lock]")
{
    TempDir dir;
    const fs::path file = dir.path() / "writer.lock";
    asma::test::writeBytes(file, "");
    CHECK_FALSE(WriterLock::tryAcquire(dir.path()).has_value());
    fs::last_write_time(file, fs::last_write_time(file) - std::chrono::seconds(10));
    CHECK(WriterLock::tryAcquire(dir.path()).has_value());
}

TEST_CASE("processAlive", "[lock]")
{
    CHECK(WriterLock::processAlive(WriterLock::currentProcessId()));
    CHECK_FALSE(WriterLock::processAlive(0));
    CHECK_FALSE(WriterLock::processAlive(999999999));
}

TEST_CASE("a moved lock releases once", "[lock]")
{
    TempDir dir;
    auto first = WriterLock::tryAcquire(dir.path());
    REQUIRE(first.has_value());
    WriterLock moved = std::move(*first);
    first.reset(); // the moved-from lock must not remove the file
    CHECK(fs::exists(dir.path() / "writer.lock"));
}
