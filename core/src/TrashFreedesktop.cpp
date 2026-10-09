// SPDX-License-Identifier: GPL-3.0-only
#ifndef _WIN32
#include "asma/core/Trash.h"

#include "TrashCommon.h"

#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <optional>
#include <utility>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace asma::freedesktop {

namespace {

std::optional<dev_t> deviceOf(const fs::path& path)
{
    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0) return std::nullopt;
    return st.st_dev;
}

// The nearest folder of `path` that exists: the home trash may not yet.
fs::path existingAncestor(fs::path path)
{
    std::error_code ec;
    while (!path.empty() && !fs::exists(path, ec)) {
        if (path == path.parent_path()) break;
        path = path.parent_path();
    }
    return path;
}

// The top of the volume a file is on: the last folder up from it on the same device.
fs::path topDir(const fs::path& file)
{
    const auto device = deviceOf(file);
    fs::path top = file.parent_path();
    while (top != top.parent_path()) {
        const auto up = deviceOf(top.parent_path());
        if (!up || !device || *up != *device) break;
        top = top.parent_path();
    }
    return top;
}

// An open folder, closed when it goes.
struct Dir {
    int fd = -1;
    Dir() = default;
    explicit Dir(int f) : fd(f) {}
    Dir(Dir&& o) noexcept : fd(std::exchange(o.fd, -1)) {}
    Dir& operator=(Dir&& o) noexcept
    {
        std::swap(fd, o.fd);
        return *this;
    }
    ~Dir()
    {
        if (fd >= 0) ::close(fd);
    }
    explicit operator bool() const { return fd >= 0; }
};

Dir openDir(int parent, const char* name)
{
    return Dir(::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
}

// A folder of ours inside `parent`, made when missing and `make`: a real
// folder (never a link someone planted on a shared volume) that this user
// owns. Everything after goes through the folder as opened, so it cannot be
// swapped for another between the check and the writing.
Dir ownDir(int parent, const char* name, bool make)
{
    if (make && ::mkdirat(parent, name, 0700) != 0 && errno != EEXIST) return {};
    Dir dir = openDir(parent, name);
    struct stat st {};
    if (!dir || ::fstat(dir.fd, &st) != 0 || st.st_uid != ::getuid()) return {};
    return dir;
}

// A trash folder, opened, with its files/ and info/.
struct Trash {
    fs::path path;
    Dir files, info;
};

std::optional<Trash> opened(const fs::path& path, Dir trash, bool make)
{
    if (!trash) return std::nullopt;
    Trash t{path, ownDir(trash.fd, "files", make), ownDir(trash.fd, "info", make)};
    if (!t.files || !t.info) return std::nullopt;
    return t;
}

// The trash folder for a file, made if it has to be (`make`); nothing when it
// has none. Without `make` a trash that is not there yet but could be made
// counts: only its parent is checked.
std::optional<Trash> trashFor(const fs::path& file, const fs::path& home, bool make)
{
    const auto device = deviceOf(file);
    if (!device) return std::nullopt;
    if (deviceOf(existingAncestor(home)) == device) {
        std::error_code ec;
        if (make) fs::create_directories(home.parent_path(), ec); // $XDG_DATA_HOME may not be there yet
        const Dir parent = openDir(AT_FDCWD, home.parent_path().c_str());
        if (!make && !parent) return Trash{home, {}, {}};
        if (!parent) return std::nullopt;
        if (!make && ::faccessat(parent.fd, home.filename().c_str(), F_OK, AT_SYMLINK_NOFOLLOW) != 0)
            return Trash{home, {}, {}};
        Dir trash = ownDir(parent.fd, home.filename().c_str(), make);
        if (!make) return trash ? std::optional<Trash>(Trash{home, {}, {}}) : std::nullopt;
        return opened(home, std::move(trash), true);
    }

    const fs::path top = topDir(file);
    const Dir topFd = openDir(AT_FDCWD, top.c_str());
    if (!topFd) return std::nullopt;
    const std::string uid = std::to_string(::getuid());
    // A shared .Trash counts only as a real folder with the sticky bit.
    const Dir shared = openDir(topFd.fd, ".Trash");
    struct stat st {};
    if (shared && ::fstat(shared.fd, &st) == 0 && (st.st_mode & S_ISVTX)) {
        if (auto t = opened(top / ".Trash" / uid, ownDir(shared.fd, uid.c_str(), make), make)) return t;
        if (!make && ::faccessat(shared.fd, uid.c_str(), F_OK, AT_SYMLINK_NOFOLLOW) != 0
            && ::faccessat(shared.fd, ".", W_OK, 0) == 0)
            return Trash{top / ".Trash" / uid, {}, {}};
    }
    const std::string own = ".Trash-" + uid;
    if (!make) {
        if (::faccessat(topFd.fd, own.c_str(), F_OK, AT_SYMLINK_NOFOLLOW) == 0)
            return ownDir(topFd.fd, own.c_str(), false) ? std::optional<Trash>(Trash{top / own, {}, {}}) : std::nullopt;
        // Not made yet: say yes when it could be.
        return ::faccessat(topFd.fd, ".", W_OK, 0) == 0 ? std::optional<Trash>(Trash{top / own, {}, {}}) : std::nullopt;
    }
    return opened(top / own, ownDir(topFd.fd, own.c_str(), true), true);
}

std::string percentEncoded(const std::string& path)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : path) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'
                        || c == '_' || c == '.' || c == '~' || c == '/';
        if (plain) out.push_back(static_cast<char>(c));
        else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

std::string now()
{
    const std::time_t t = std::time(nullptr);
    std::tm local {};
    ::localtime_r(&t, &local);
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%S", &local);
    return text;
}

// "kick.wav", then "kick 2.wav", "kick 3.wav"...
std::string numbered(const fs::path& name, int n)
{
    if (n == 1) return name.string();
    return name.stem().string() + " " + std::to_string(n) + name.extension().string();
}

} // namespace

fs::path homeTrash()
{
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) return fs::path(xdg) / "Trash";
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : "") / ".local" / "share" / "Trash";
}

bool available(const fs::path& file, const fs::path& home) { return trashFor(file, home, false).has_value(); }

TrashResult moveToTrash(const fs::path& file, const fs::path& home)
{
    TrashResult result;
    std::error_code ec;
    const fs::path absolute = fs::absolute(file, ec);
    const auto trash = trashFor(absolute, home, true);
    if (!trash) {
        result.error = "there is no Trash on its disk";
        return result;
    }
    // The home trash records the full path; a volume's own, the path from its top.
    std::string original = absolute.string();
    if (trash->path != home) original = absolute.lexically_relative(topDir(absolute)).string();
    const std::string info = "[Trash Info]\nPath=" + percentEncoded(original) + "\nDeletionDate=" + now() + "\n";

    // Claim a name by making its info file first, as the specification asks:
    // O_EXCL makes two trashing processes pick different names.
    for (int n = 1; n < 10000; ++n) {
        const std::string name = numbered(absolute.filename(), n);
        const std::string infoName = name + ".trashinfo";
        if (::faccessat(trash->files.fd, name.c_str(), F_OK, AT_SYMLINK_NOFOLLOW) == 0) continue;
        const int fd = ::openat(trash->info.fd, infoName.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            result.error = "cannot write to the Trash";
            return result;
        }
        const bool written = ::write(fd, info.data(), info.size()) == static_cast<ssize_t>(info.size());
        ::close(fd);
        const bool moved = written && ::renameat(AT_FDCWD, absolute.c_str(), trash->files.fd, name.c_str()) == 0;
        if (!moved) {
            ::unlinkat(trash->info.fd, infoName.c_str(), 0);
            result.error = written ? "cannot move it to the Trash" : "cannot write to the Trash";
            return result;
        }
        result.ok = true;
        result.where = trash->path / "files" / name;
        return result;
    }
    result.error = "the Trash has too many files of that name";
    return result;
}

std::string restore(const fs::path& where, const fs::path& to)
{
    const std::string error = detail::renameBack(where, to);
    if (error.empty()) {
        std::error_code ec;
        fs::remove(where.parent_path().parent_path() / "info" / (where.filename().string() + ".trashinfo"), ec);
    }
    return error;
}

} // namespace asma::freedesktop

#if !defined(__APPLE__)
namespace asma {

bool trashAvailable(const fs::path& file) { return freedesktop::available(file, freedesktop::homeTrash()); }
TrashResult moveToTrash(const fs::path& file) { return freedesktop::moveToTrash(file, freedesktop::homeTrash()); }
std::string restoreFromTrash(const fs::path& where, const fs::path& to) { return freedesktop::restore(where, to); }

} // namespace asma
#endif
#endif
