// SPDX-License-Identifier: GPL-3.0-only
// A scanned library in a temporary data directory, as the app would leave it.
#pragma once

#include "Signals.h"
#include "TestUtil.h"
#include "asma/core/Db.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"

#include <string>

namespace asma::test {

struct LibraryFixture {
    TempDir dir;
    std::string dataDir = (dir.path() / "data").string();
    ScopedEnv env{"ASMA_DATA_DIR", dataDir.c_str()};
    fs::path dbPath = dir.path() / "data" / "library.db";
    fs::path lib = dir.path() / "Samples";
    fs::path loop = lib / "Loops" / "Bass_Loop_Am_120.wav";
    fs::path kick = lib / "Drums" / "Kick_01.wav";
    fs::path snare = lib / "Drums" / "Snare_02.wav";

    void scan()
    {
        writeWavFloat(loop, 48000, {sine(110.0, 2.0, 0.4, 48000)});
        writeWavFloat(kick, 48000, {kickHit(48000)});
        writeWavFloat(snare, 48000, {hatHit(48000, 3)});
        Db db = Db::open(dbPath);
        Library library(db);
        scanRoot(db, library.addRoot(lib));
    }
};

} // namespace asma::test
