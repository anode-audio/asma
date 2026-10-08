// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Repair.h"

#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Library.h"
#include "asma/core/WriterLock.h"

#include <ctime>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace asma {

namespace {

bool says(const std::string& message, const char* what) { return message.find(what) != std::string::npos; }

bool damageMessage(const std::string& m)
{
    return says(m, "malformed") || says(m, "not a database") || says(m, "corrupt");
}

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The folders a damaged library still names; nothing when it cannot say.
std::vector<std::string> foldersOf(const fs::path& dbPath)
{
    std::vector<std::string> out;
    try {
        Db db = Db::openReadOnly(dbPath);
        auto q = db.prepare("SELECT path FROM roots ORDER BY id");
        while (q.step()) out.push_back(q.getText(0));
    } catch (const std::exception&) {
        out.clear();
    }
    return out;
}

// Where the damaged library goes: library.db.corrupt, or with the date and
// time when that is taken.
fs::path asideName(const fs::path& dbPath)
{
    fs::path aside = dbPath;
    aside += ".corrupt";
    std::error_code ec;
    if (!fs::exists(aside, ec)) return aside;
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "-%Y%m%d-%H%M%S", &tm);
    aside += stamp;
    return aside;
}

} // namespace

HealthReport checkLibrary(const fs::path& dbPath)
{
    std::error_code ec;
    if (!fs::exists(dbPath, ec)) return {LibraryHealth::Missing, {}};
    try {
        Db db = Db::openReadOnly(dbPath);
        auto q = db.prepare("PRAGMA quick_check");
        std::string first;
        if (q.step()) first = q.getText(0);
        if (first == "ok") return {LibraryHealth::Ok, {}};
        return {LibraryHealth::Damaged, first.empty() ? std::string("quick_check found damage") : first};
    } catch (const SchemaMismatchError&) {
        return {LibraryHealth::Ok, {}}; // older or newer, not damaged
    } catch (const std::exception& e) {
        const std::string message = e.what();
        return {damageMessage(message) ? LibraryHealth::Damaged : LibraryHealth::Unreadable, message};
    }
}

RepairReport repairLibrary(const fs::path& dbPath, const fs::path& backup, const ScanOptions& scan)
{
    RepairReport report;
    auto lock = WriterLock::tryAcquire(dbPath.parent_path());
    if (!lock) {
        report.result = RepairReport::Result::Locked;
        return report;
    }
    const HealthReport health = checkLibrary(dbPath);
    report.detail = health.detail;
    if (health.health != LibraryHealth::Damaged) return report; // Healthy: never move a sound library

    const std::string backupText = readFile(backup);
    std::vector<std::string> folders = backupFolders(backupText);
    if (folders.empty()) folders = foldersOf(dbPath);

    // Move it aside, deleting nothing; its -wal and -shm go with it.
    const fs::path aside = asideName(dbPath);
    std::error_code ec;
    fs::rename(dbPath, aside, ec);
    if (ec) {
        report.result = RepairReport::Result::InUse;
        report.detail = ec.message();
        return report;
    }
    for (const char* suffix : {"-wal", "-shm"}) {
        fs::path from = dbPath, to = aside;
        from += suffix;
        to += suffix;
        if (fs::exists(from, ec)) fs::rename(from, to, ec);
    }
    report.movedTo = aside;

    Db db = Db::open(dbPath);
    Library lib(db);
    std::vector<std::int64_t> roots;
    for (const auto& f : folders) {
        Transaction tx(db);
        roots.push_back(lib.addRoot(fromUtf8(f)));
        tx.commit();
    }
    report.folders = roots.size();
    for (const auto id : roots) {
        std::error_code missing;
        if (fs::is_directory(fromUtf8(lib.root(id)->path), missing)) scanRoot(db, id, scan);
    }
    if (!backupWrittenAt(backupText).empty()) {
        report.restored = restoreBackup(db, backupText);
        report.backupWrittenAt = backupWrittenAt(backupText);
    }
    report.result = RepairReport::Result::Repaired;
    return report;
}

} // namespace asma
