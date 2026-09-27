// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Fs.h"
#include "asma/core/WriterLock.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#endif

using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

struct RunResult {
    int exitCode = -1;
    std::string out;
};

// UTF-8 text of a path with native separators, in double quotes.
std::string quote(const fs::path& p)
{
    const std::u8string u8 = fs::path(p).make_preferred().u8string();
    return "\"" + std::string(reinterpret_cast<const char*>(u8.data()), u8.size()) + "\"";
}

int systemUtf8(const std::string& command)
{
#ifdef _WIN32
    // cmd.exe strips one pair of outer quotes, and the narrow system() would
    // mangle non-ASCII text, so wrap the command and go through UTF-16.
    const std::string wrapped = "\"" + command + "\"";
    const int size = MultiByteToWideChar(CP_UTF8, 0, wrapped.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, wrapped.c_str(), -1, wide.data(), size);
    return _wsystem(wide.c_str());
#else
    const int status = std::system(command.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

// Runs an executable with arguments, capturing stdout.
RunResult run(const char* exe, const std::string& arguments, const fs::path& scratch)
{
    const fs::path outFile = scratch / "stdout.txt";
    RunResult r;
    r.exitCode = systemUtf8(quote(asma::fromUtf8(exe)) + " " + arguments + " > " + quote(outFile));
    std::ifstream in(outFile, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    r.out = ss.str();
    return r;
}

struct Cli {
    TempDir dir;
    fs::path lib = dir.path() / asma::fromUtf8("Café Samples");
    fs::path db = dir.path() / "data" / "library.db";

    Cli()
    {
        asma::test::WavSpec loop;
        loop.seed = 1;
        asma::test::writeWav(lib / "Loops" / "Bass_Loop_Am_128.wav", loop);
        asma::test::WavSpec kick;
        kick.seed = 2;
        asma::test::writeWav(lib / "Drums" / asma::fromUtf8("Kick Ü_01.wav"), kick);
    }

    RunResult runAsma(const std::string& args) { return run(ASMA_CLI_PATH, "--db " + quote(db) + " " + args, dir.path()); }
    RunResult runScan(const std::string& args)
    {
        return run(ASMA_SCAN_PATH, "--db " + quote(db) + " " + args, dir.path());
    }
};

} // namespace

TEST_CASE("asma --version", "[e2e]")
{
    TempDir dir;
    const RunResult r = run(ASMA_CLI_PATH, "--version", dir.path());
    CHECK(r.exitCode == 0);
    CHECK(r.out.find(std::string("asma ") + ASMA_VERSION) != std::string::npos);
}

TEST_CASE("root add, scan and query from the CLI, with non-ASCII paths", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    const RunResult list = cli.runAsma("root list");
    CHECK(list.out.find("Café Samples") != std::string::npos);

    REQUIRE(cli.runAsma("scan").exitCode == 0);

    const RunResult kick = cli.runAsma("query kick");
    CHECK(kick.exitCode == 0);
    CHECK(kick.out.find("Kick Ü_01.wav") != std::string::npos);
    CHECK(kick.out.find("Bass_Loop") == std::string::npos);

    const RunResult loops = cli.runAsma("query --type loop --key Am --json");
    CHECK(loops.out.find("\"bpm\":128") != std::string::npos);
    CHECK(loops.out.find("\"key\":\"Am\"") != std::string::npos);
}

TEST_CASE("asma-scan speaks JSON lines and honours --fail", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    const RunResult r = cli.runScan("--root 1 --fail " + quote(asma::fromUtf8("Drums/Kick Ü_01.wav")));
    CHECK(r.exitCode == 0);
    CHECK(r.out.find("{\"event\":\"marked_failed\"") != std::string::npos);
    CHECK(r.out.find("{\"event\":\"start\",\"path\":\"Loops/Bass_Loop_Am_128.wav\"}") != std::string::npos);
    CHECK(r.out.find("{\"event\":\"progress\",\"done\":1,\"total\":1") != std::string::npos);
    CHECK(r.out.find("{\"event\":\"done\",\"added\":1") != std::string::npos);

    CHECK(cli.runAsma("query kick").out.find("Kick") == std::string::npos);
}

TEST_CASE("a second writer is refused with exit code 3", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    auto lock = asma::WriterLock::tryAcquire(cli.db.parent_path());
    REQUIRE(lock.has_value());
    CHECK(cli.runAsma("scan").exitCode == 3);
    const RunResult worker = cli.runScan("--root 1");
    CHECK(worker.exitCode == 3);
    CHECK(worker.out.find("\"code\":\"locked\"") != std::string::npos);
    lock.reset();
    CHECK(cli.runAsma("query").out.empty()); // nothing was scanned while locked
}

TEST_CASE("bad usage exits with 2", "[e2e]")
{
    Cli cli;
    CHECK(cli.runAsma("frobnicate").exitCode == 2);
    CHECK(cli.runAsma("query --bogus").exitCode == 2);
    CHECK(cli.runAsma("query --key H").exitCode == 2);
    CHECK(cli.runScan("").exitCode == 2);
}

TEST_CASE("scan analyses by default and --no-analysis skips it", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    const RunResult quick = cli.runAsma("scan --no-analysis");
    CHECK(quick.out.find("analysis:") == std::string::npos);
    const RunResult full = cli.runAsma("scan");
    CHECK(full.out.find("analysis: analysed 2, failed 0, skipped 0") != std::string::npos);
}

TEST_CASE("similar lists other files with a similarity score", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan").exitCode == 0);
    const RunResult r = cli.runAsma("similar " + quote(cli.lib / "Loops" / "Bass_Loop_Am_128.wav") + " --json");
    CHECK(r.exitCode == 0);
    CHECK(r.out.find("Kick") != std::string::npos);
    CHECK(r.out.find("\"similarity\":") != std::string::npos);
    CHECK(r.out.find("Bass_Loop") == std::string::npos);

    CHECK(cli.runAsma("similar " + quote(cli.lib / "nope.wav")).exitCode == 1);
    CHECK(cli.runAsma("similar").exitCode == 2);
}

TEST_CASE("asma-scan reports analysis and honours --fail-analysis", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runScan("--root 1 --no-analysis").exitCode == 0);
    const RunResult r =
        cli.runScan("--root 1 --fail-analysis " + quote(asma::fromUtf8("Loops/Bass_Loop_Am_128.wav")));
    CHECK(r.exitCode == 0);
    CHECK(r.out.find("{\"event\":\"marked_analysis_failed\",\"path\":\"Loops/Bass_Loop_Am_128.wav\"}") !=
          std::string::npos);
    CHECK(r.out.find("{\"event\":\"analyse_start\",\"path\":\"Drums/Kick") != std::string::npos);
    CHECK(r.out.find("{\"event\":\"analyse_done\",\"analysed\":1,\"failed\":0,\"skipped\":0}") != std::string::npos);
}
