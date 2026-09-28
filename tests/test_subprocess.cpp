// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Subprocess.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

using namespace asma;
using Strings = std::vector<std::string>;

namespace {

Subprocess child(const Strings& args) { return Subprocess::start(fromUtf8(ASMA_TEST_CHILD_PATH), args); }

Strings readAll(Subprocess& p)
{
    Strings lines;
    while (auto line = p.readLine()) lines.push_back(*line);
    return lines;
}

} // namespace

TEST_CASE("lines come back in order, then the end of the stream", "[subprocess]")
{
    auto p = child({"lines", "one", "two", "three"});
    CHECK(readAll(p) == Strings{"one", "two", "three"});
    CHECK_FALSE(p.readLine());
    const ExitStatus status = p.wait();
    CHECK_FALSE(status.signalled);
    CHECK(status.code == 0);
}

TEST_CASE("a last line without a newline, and CRLF endings", "[subprocess]")
{
    auto partial = child({"partial", "tail"});
    CHECK(readAll(partial) == Strings{"tail"});
    auto crlf = child({"crlf", "x"});
    CHECK(readAll(crlf) == Strings{"x"});
}

TEST_CASE("exit codes are reported; wait can be called again", "[subprocess]")
{
    auto p = child({"exit", "7"});
    CHECK(readAll(p).empty());
    CHECK(p.wait().code == 7);
    CHECK(p.wait().code == 7);
}

TEST_CASE("a crash is reported as signalled", "[subprocess]")
{
    auto p = child({"crash"});
    readAll(p);
    CHECK(p.wait().signalled);
}

TEST_CASE("arguments arrive unchanged, whatever they contain", "[subprocess]")
{
    const Strings tricky = {"plain", "two words", "", "quote\"inside", "trailing\\", "back\\\\\"slash",
                            "Café Ü 日本", "--db", "tab\there"};
    Strings args = {"args"};
    args.insert(args.end(), tricky.begin(), tricky.end());
    auto p = child(args);
    Strings expected;
    for (const auto& t : tricky) expected.push_back("\"" + jsonEscape(t) + "\"");
    CHECK(readAll(p) == expected);
    CHECK(p.wait().code == 0);
}

TEST_CASE("kill from another thread ends a blocked read", "[subprocess]")
{
    auto p = child({"hang"});
    REQUIRE(p.readLine() == "ready");
    const auto started = std::chrono::steady_clock::now();
    std::thread killer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        p.kill();
    });
    CHECK_FALSE(p.readLine());
    killer.join();
    p.wait();
    p.kill(); // after exit: harmless
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));
}

TEST_CASE("destroying a running child kills it instead of hanging", "[subprocess]")
{
    const auto started = std::chrono::steady_clock::now();
    {
        auto p = child({"hang"});
        REQUIRE(p.readLine() == "ready");
    }
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));
}

TEST_CASE("a program that does not exist cannot start", "[subprocess]")
{
    CHECK_THROWS_AS(Subprocess::start(fromUtf8(ASMA_TEST_CHILD_PATH).parent_path() / "no-such-program", {}),
                    SubprocessError);
}

TEST_CASE("a moved-from Subprocess is inert and the new owner reads on", "[subprocess]")
{
    auto a = child({"lines", "x", "y"});
    REQUIRE(a.readLine() == "x");
    Subprocess b = std::move(a);
    CHECK(b.readLine() == "y");
    CHECK(b.wait().code == 0);
}
