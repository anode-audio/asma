// SPDX-License-Identifier: GPL-3.0-only
// Hidden: compares analysis against the BPM and key that file names and ACID
// chunks already state, on a real library. Nothing from that library is
// stored in the repo. Run with:
//   ASMA_EVAL_DIR=~/Samples ./build/tests/asma_tests "[.real]"
#include "TestUtil.h"
#include "asma/core/Analysis.h"
#include "asma/core/Decode.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace asma;

namespace {

// A in Am, C in Cm: the relative and parallel keys that key finders confuse.
bool relative(const std::string& a, const std::string& b)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    auto pc = [&](const std::string& k) {
        const std::string root = k.back() == 'm' ? k.substr(0, k.size() - 1) : k;
        for (int i = 0; i < 12; ++i)
            if (root == names[i]) return i;
        return -1;
    };
    const bool am = a.back() == 'm';
    const bool bm = b.back() == 'm';
    if (am == bm) return false;
    const int major = am ? pc(b) : pc(a);
    const int minor = am ? pc(a) : pc(b);
    return (major + 9) % 12 == minor;
}

} // namespace

TEST_CASE("analysis agrees with labelled files in a real library", "[.real]")
{
    const char* dir = std::getenv("ASMA_EVAL_DIR");
    if (!dir) SKIP("set ASMA_EVAL_DIR to a sample folder");
    const int maxFiles = std::getenv("ASMA_EVAL_MAX") ? std::atoi(std::getenv("ASMA_EVAL_MAX")) : 400;

    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(fromUtf8(dir));
    scanRoot(db, root);

    int bpmTotal = 0, bpmExact = 0, bpmOctave = 0, bpmNone = 0;
    int keyTotal = 0, keyExact = 0, keyRelative = 0, keyNone = 0;
    auto q = db.prepare("SELECT r.path, f.rel_path, ft.bpm, ft.bpm_source, ft.key, ft.is_loop FROM files f "
                        "JOIN roots r ON r.id = f.root_id JOIN features ft ON ft.file_id = f.id "
                        "WHERE f.status = 'ok' AND (ft.bpm IS NOT NULL OR ft.key IS NOT NULL) ORDER BY f.content_hash");
    int seen = 0;
    while (q.step() && seen < maxFiles) {
        ++seen;
        DecodedAudio audio;
        try {
            audio = decodeFile(fromUtf8(q.getText(0)) / fromUtf8(q.getText(1)));
        } catch (const std::exception&) {
            continue;
        }
        const AnalysisResult r = analyse(audio);
        if (!q.isNull(2) && q.getInt(5) == 1) {
            const double truth = q.getDouble(2);
            ++bpmTotal;
            if (!r.bpm) ++bpmNone;
            else if (std::abs(*r.bpm - truth) <= 0.5) ++bpmExact;
            else if (std::abs(*r.bpm * 2 - truth) <= 1.0 || std::abs(*r.bpm / 2 - truth) <= 0.5) ++bpmOctave;
        }
        if (!q.isNull(4)) {
            const std::string truth = q.getText(4);
            ++keyTotal;
            if (!r.key) ++keyNone;
            else if (*r.key == truth) ++keyExact;
            else if (relative(*r.key, truth)) ++keyRelative;
        }
    }
    auto pct = [](int n, int d) { return d ? 100.0 * n / d : 0.0; };
    std::printf("BPM (labelled loops): %d files, exact %.1f%%, octave %.1f%%, none %.1f%%, wrong %.1f%%\n", bpmTotal,
                pct(bpmExact, bpmTotal), pct(bpmOctave, bpmTotal), pct(bpmNone, bpmTotal),
                pct(bpmTotal - bpmExact - bpmOctave - bpmNone, bpmTotal));
    std::printf("Key (labelled files): %d files, exact %.1f%%, relative %.1f%%, none %.1f%%, wrong %.1f%%\n", keyTotal,
                pct(keyExact, keyTotal), pct(keyRelative, keyTotal), pct(keyNone, keyTotal),
                pct(keyTotal - keyExact - keyRelative - keyNone, keyTotal));
}
