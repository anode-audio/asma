// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Folders.h"

#include "asma/core/FileOps.h"
#include "asma/core/Fs.h"

#include <algorithm>

namespace fs = std::filesystem;

namespace asma {

namespace {

std::string nameOf(const std::string& path)
{
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool inside(const std::string& path, const std::string& folder)
{
    const std::string prefix = folder.back() == '/' ? folder : folder + "/";
    return path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0;
}

// "Drums", "Drums and Bass", "Bass, Drums and Keys"
std::string joined(std::vector<std::string> names)
{
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i)
        out += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
    return out;
}

void run(Db& db, std::string_view sql, std::int64_t a, std::int64_t b)
{
    auto q = db.prepare(sql);
    q.bind(1, a).bind(2, b);
    q.run();
}

// Moves the inner folder's samples into the outer one and drops the inner
// folder. A sample with a row in each keeps the outer row, which takes the
// inner row's data too.
void absorb(Db& db, Library& lib, const Root& outer, const Root& inner)
{
    const std::string prefix = inner.path.substr(outer.path.size() + (outer.path.back() == '/' ? 0 : 1));
    for (auto file : lib.filesInRoot(inner.id)) {
        // A trashed row's path is its own and stays unique.
        const bool trashed = file.relPath.rfind("/trashed/", 0) == 0;
        const std::string rel = trashed ? file.relPath : prefix + "/" + file.relPath;
        if (const auto twin = lib.fileByPath(outer.id, rel)) {
            run(db, "INSERT OR IGNORE INTO ratings(file_id, rating) SELECT ?1, rating FROM ratings WHERE file_id = ?2",
                twin->id, file.id);
            run(db, "INSERT OR IGNORE INTO favourites(file_id) SELECT ?1 FROM favourites WHERE file_id = ?2", twin->id,
                file.id);
            run(db,
                "INSERT INTO file_tags(file_id, tag_id, source) SELECT ?1, tag_id, 'user' FROM file_tags "
                "WHERE file_id = ?2 AND source = 'user' ON CONFLICT(file_id, tag_id) DO UPDATE SET source = 'user'",
                twin->id, file.id);
            run(db,
                "INSERT OR IGNORE INTO collection_items(collection_id, file_id) SELECT collection_id, ?1 "
                "FROM collection_items WHERE file_id = ?2",
                twin->id, file.id);
            lib.removeFile(file.id);
            lib.updateFile(*twin); // its index entry, with the tags it took
            continue;
        }
        file.rootId = outer.id;
        file.relPath = rel;
        lib.updateFile(file);
    }
    // Undo of what moved its samples puts them back into the outer folder.
    run(db, "UPDATE journal SET root_id = ?1 WHERE root_id = ?2 AND op != 'remove_root'", outer.id, inner.id);
    auto drop = db.prepare("DELETE FROM roots WHERE id = ?");
    drop.bind(1, inner.id);
    drop.run();
}

} // namespace

AddCheck checkAddFolder(Db& db, const fs::path& dir)
{
    Library lib(db);
    const std::string path = Library::folderPath(dir);
    AddCheck check;
    std::vector<std::string> names;
    bool again = false;
    for (const auto& r : lib.roots()) {
        if (r.path == path) {
            again = true;
            continue;
        }
        if (r.enabled && inside(path, r.path)) {
            check.result = AddCheck::Result::Inside;
            check.message = nameOf(path) + " is already in the library, inside " + nameOf(r.path) + ".";
            return check;
        }
        if (inside(r.path, path)) {
            check.contained.push_back(r);
            names.push_back(nameOf(r.path));
        }
    }
    if (check.contained.empty()) {
        if (again) check.result = AddCheck::Result::Again;
        return check;
    }
    std::sort(names.begin(), names.end());
    check.result = AddCheck::Result::Contains;
    const std::string name = nameOf(path);
    if (names.size() == 1)
        check.message = name + " contains a folder already in the library (" + names[0] + "). Add " + name
                      + " in its place?";
    else {
        std::string list;
        for (std::size_t i = 0; i < names.size(); ++i) list += (i ? ", " : "") + names[i];
        check.message = name + " contains " + std::to_string(names.size()) + " folders already in the library (" + list
                      + "). Add " + name + " in their place?";
    }
    return check;
}

std::int64_t addFolder(Db& db, const fs::path& dir, bool merge)
{
    const AddCheck check = checkAddFolder(db, dir);
    if (check.result == AddCheck::Result::Inside || (check.result == AddCheck::Result::Contains && !merge))
        throw OperationRefused(check.message);
    Transaction tx(db);
    Library lib(db);
    const auto id = lib.addRoot(dir);
    const auto outer = lib.root(id);
    for (const auto& inner : check.contained) absorb(db, lib, *outer, inner);
    tx.commit();
    return id;
}

std::vector<std::string> mergeNestedFolders(Db& db)
{
    Library lib(db);
    std::vector<std::string> said;
    for (;;) {
        auto roots = lib.roots();
        // The outermost first, so a folder three deep joins the top one.
        std::sort(roots.begin(), roots.end(), [](const Root& a, const Root& b) { return a.path.size() < b.path.size(); });
        bool merged = false;
        for (const auto& outer : roots) {
            if (!outer.enabled) continue;
            std::vector<Root> nested;
            std::vector<std::string> names;
            for (const auto& r : roots)
                if (inside(r.path, outer.path)) {
                    nested.push_back(r);
                    names.push_back(nameOf(r.path));
                }
            if (nested.empty()) continue;
            Transaction tx(db);
            for (const auto& inner : nested) absorb(db, lib, outer, inner);
            tx.commit();
            said.push_back("Merged " + joined(names) + " into " + nameOf(outer.path) + ", which contains "
                           + (names.size() == 1 ? "it." : "them."));
            merged = true;
            break;
        }
        if (!merged) return said;
    }
}

} // namespace asma
