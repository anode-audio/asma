// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <string>

namespace asma {

// What moving a file to the trash did: where the file is now, or why it is
// still where it was.
struct TrashResult {
    bool ok = false;
    std::filesystem::path where; // the file inside the trash
    std::string error;
};

// The system's trash: the Trash on macOS, the Recycle Bin on Windows, the
// freedesktop.org trash on Linux. A file the trash cannot take stays where it
// is: nothing here ever deletes one.
//
// Whether the volume a file is on has a trash it can go to.
bool trashAvailable(const std::filesystem::path& file);
TrashResult moveToTrash(const std::filesystem::path& file);
// Moves a trashed file back to `to`. Empty on success, else why not ("it is no
// longer in the Trash", "its old place is taken").
std::string restoreFromTrash(const std::filesystem::path& where, const std::filesystem::path& to);

#ifndef _WIN32
// The freedesktop.org Trash specification, which Linux uses; built on every
// POSIX system so it is tested everywhere. A file on the same volume as
// `homeTrash` ($XDG_DATA_HOME/Trash) goes there; any other to its volume's
// $topdir/.Trash/$uid (when that is a real, sticky folder) or
// $topdir/.Trash-$uid. Each file gets an info/NAME.trashinfo saying where it
// came from and when.
namespace freedesktop {
std::filesystem::path homeTrash(); // $XDG_DATA_HOME/Trash, else ~/.local/share/Trash
bool available(const std::filesystem::path& file, const std::filesystem::path& homeTrash);
TrashResult moveToTrash(const std::filesystem::path& file, const std::filesystem::path& homeTrash);
std::string restore(const std::filesystem::path& where, const std::filesystem::path& to);
} // namespace freedesktop
#endif

} // namespace asma
