// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "TestUtil.h"
#include "asma/core/Analysis.h"
#include "asma/core/Library.h"
#include "asma/core/Similar.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace asma;
using asma::test::TempDir;

namespace {

constexpr int kRate = 44100;

std::int64_t addAnalysed(Library& lib, std::int64_t root, const std::string& rel, std::vector<float> mono)
{
    FileRecord f;
    f.rootId = root;
    f.relPath = rel;
    f.size = 1;
    f.mtime = 1;
    f.format = "wav";
    const auto id = lib.insertFile(f);
    DecodedAudio a;
    a.sampleRate = kRate;
    a.mono = std::move(mono);
    lib.setAnalysis(id, analyse(a));
    return id;
}

std::vector<float> scaled(std::vector<float> v, float gain)
{
    for (auto& x : v) x *= gain;
    return v;
}

} // namespace

TEST_CASE("the closest sound comes first and the file itself is left out", "[similar]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    const auto kick = addAnalysed(lib, root, "kick.wav", test::kickHit(kRate));
    const auto kick2 = addAnalysed(lib, root, "kick2.wav", scaled(test::kickHit(kRate), 0.7f));
    const auto hat = addAnalysed(lib, root, "hat.wav", test::hatHit(kRate, 1));
    const auto hat2 = addAnalysed(lib, root, "hat2.wav", test::hatHit(kRate, 2));
    addAnalysed(lib, root, "chords.wav", test::chordProgression(0, false, kRate));
    addAnalysed(lib, root, "loop.wav", test::drumLoop(120.0, 2, kRate));

    const auto forKick = findSimilar(db, kick, 3);
    REQUIRE(forKick.size() == 3);
    CHECK(forKick[0].id == kick2);
    for (const auto& m : forKick) CHECK(m.id != kick);
    CHECK(forKick[0].similarity >= forKick[1].similarity);

    CHECK(findSimilar(db, hat, 1).at(0).id == hat2);
}

TEST_CASE("files without a feature vector give no matches", "[similar]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    addAnalysed(lib, root, "kick.wav", test::kickHit(kRate));
    FileRecord f;
    f.rootId = root;
    f.relPath = "unanalysed.wav";
    f.format = "wav";
    const auto id = lib.insertFile(f);
    CHECK(findSimilar(db, id).empty());
    CHECK(findSimilar(db, 9999).empty());
}

TEST_CASE("missing files are not suggested", "[similar]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    const auto kick = addAnalysed(lib, root, "kick.wav", test::kickHit(kRate));
    const auto gone = addAnalysed(lib, root, "gone.wav", scaled(test::kickHit(kRate), 0.9f));
    addAnalysed(lib, root, "hat.wav", test::hatHit(kRate, 1));
    lib.setStatus(gone, FileStatus::Missing);
    for (const auto& m : findSimilar(db, kick)) CHECK(m.id != gone);
}

TEST_CASE("a corrupt stored vector does not skew everyone else's matches", "[similar]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    const auto kick = addAnalysed(lib, root, "kick.wav", test::kickHit(kRate));
    const auto kick2 = addAnalysed(lib, root, "kick2.wav", scaled(test::kickHit(kRate), 0.7f));
    addAnalysed(lib, root, "hat.wav", test::hatHit(kRate, 1));
    const auto before = findSimilar(db, kick, 2);

    FileRecord f;
    f.rootId = root;
    f.relPath = "broken.wav";
    f.format = "wav";
    const auto broken = lib.insertFile(f);
    AnalysisResult bad;
    bad.featureVector.assign(kFeatureVectorSize, std::numeric_limits<float>::quiet_NaN());
    lib.setAnalysis(broken, bad);

    const auto after = findSimilar(db, kick, 2);
    REQUIRE(after.size() == before.size());
    CHECK(after[0].id == kick2);
    CHECK(after[0].similarity == before[0].similarity);
    for (const auto& m : after) CHECK(m.id != broken);
}
