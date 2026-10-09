// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Fs.h"

#include <filesystem>
#include <string>
#include <system_error>

namespace asma::detail {

// Puts a trashed file back by renaming it, never over another file. Empty on
// success, else why not.
inline std::string renameBack(const std::filesystem::path& where, const std::filesystem::path& to)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(where, ec))) return "it is no longer in the Trash";
    if (fs::exists(fs::symlink_status(to, ec))) return "its old place is taken";
    fs::create_directories(to.parent_path(), ec); // its folder may have gone since
    ec = renameNoReplace(where, to);
    if (ec == std::errc::file_exists) return "its old place is taken";
    return ec ? ec.message() : std::string();
}

} // namespace asma::detail
