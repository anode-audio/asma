// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"
#include "asma/core/Library.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace asma {

// What adding a folder would do, given the folders already in the library.
struct AddCheck {
    enum class Result {
        New,      // added as it is
        Again,    // already a library folder: enabled again if it was removed
        Inside,   // inside an (enabled) library folder: refused
        Contains, // holds library folders: it can take their place
    };
    Result result = Result::New;
    std::vector<Root> contained; // Contains: the folders it holds, by name
    // Inside: "Drums is already in the library, inside Samples."
    // Contains: "Samples contains 2 folders already in the library (Bass,
    // Drums). Add Samples in their place?"
    std::string message;
};

AddCheck checkAddFolder(Db& db, const std::filesystem::path& dir);

// Adds a folder. One holding library folders takes their place only with
// `merge`: their samples join it with their ids, so their data stays, and a
// sample both had a row for keeps one, with the data of both. Throws
// OperationRefused when the folder is inside another, or holds some without
// `merge`.
std::int64_t addFolder(Db& db, const std::filesystem::path& dir, bool merge);

// Merges library folders nested in an enabled one into it, as addFolder
// would have: what libraries made before overlaps were refused may hold.
// Says what it merged ("Merged Drums and Bass into Samples, which contains
// them.").
std::vector<std::string> mergeNestedFolders(Db& db);

} // namespace asma
