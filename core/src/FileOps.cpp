// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/FileOps.h"

#include "asma/core/Backup.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace fs = std::filesystem;

namespace asma {

struct FileOps::Step {
    std::string op;           // rename, move, trash, remove_root
    std::int64_t fileId = 0;  // the sample (file steps)
    std::int64_t rootId = 0;  // the folder it was in, or the one removed
    std::string src;          // UTF-8, where it was
    std::string dst;          // UTF-8, where it goes (rename, move)
    std::string trashRef;     // where the trash put it
    std::string state;
    std::int64_t id = 0;      // the journal row
};

namespace {

constexpr std::string_view kTrashedPrefix = "/trashed/"; // a relative path no file can have

std::string nameOf(std::string_view utf8Path)
{
    const auto slash = utf8Path.rfind('/');
    return std::string(slash == std::string_view::npos ? utf8Path : utf8Path.substr(slash + 1));
}

std::string parentOf(std::string_view relPath)
{
    const auto slash = relPath.rfind('/');
    return slash == std::string_view::npos ? std::string() : std::string(relPath.substr(0, slash));
}

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string extensionOf(std::string_view name)
{
    const auto dot = name.rfind('.');
    return dot == std::string_view::npos || dot == 0 ? std::string() : std::string(name.substr(dot));
}

// "kick.wav, snare.wav, hat.wav and 9 more"
std::string listed(std::vector<std::string> names)
{
    std::sort(names.begin(), names.end());
    std::string out;
    const std::size_t shown = std::min<std::size_t>(names.size(), 3);
    for (std::size_t i = 0; i < shown; ++i) out += (i ? ", " : "") + names[i];
    if (names.size() > shown) out += " and " + std::to_string(names.size() - shown) + " more";
    return out;
}

std::string samples(std::size_t n) { return std::to_string(n) + (n == 1 ? " sample" : " samples"); }

// The folder's name as the user knows it: "Drums" for .../Samples/Drums.
std::string folderName(const Root& root, std::string_view rel)
{
    return nameOf(rel.empty() ? std::string_view(root.path) : rel);
}

bool hasUserData(Db& db, std::int64_t fileId)
{
    auto q = db.prepare("SELECT EXISTS (SELECT 1 FROM ratings WHERE file_id = ?1) "
                        "OR EXISTS (SELECT 1 FROM favourites WHERE file_id = ?1) "
                        "OR EXISTS (SELECT 1 FROM file_tags WHERE file_id = ?1 AND source = 'user') "
                        "OR EXISTS (SELECT 1 FROM collection_items WHERE file_id = ?1)");
    q.bind(1, fileId);
    return q.step() && q.getInt(0) != 0;
}

bool sameFile(const fs::path& a, const fs::path& b)
{
    std::error_code ec;
    return fs::equivalent(a, b, ec);
}

// A rename only changes letter case, which a case-blind disk sees as the
// same name: it goes through a name of its own.
std::error_code moveFile(const fs::path& from, const fs::path& to)
{
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec); // a folder that went since
    const bool caseOnly = from != to && from.parent_path() == to.parent_path()
                       && lower(toUtf8(from.filename())) == lower(toUtf8(to.filename())) && sameFile(from, to);
    if (!caseOnly) return renameNoReplace(from, to);
    const fs::path aside = from.parent_path() / (".asma-renaming-" + toUtf8(from.filename()));
    if ((ec = renameNoReplace(from, aside))) return ec;
    if ((ec = renameNoReplace(aside, to))) renameNoReplace(aside, from);
    return ec;
}

Operation opFrom(std::string_view s)
{
    if (s == "rename") return Operation::Rename;
    if (s == "trash") return Operation::Trash;
    if (s == "remove_root") return Operation::RemoveFolder;
    return Operation::Move;
}

std::string label(Operation op, std::size_t count, const std::string& name)
{
    const std::string what = count == 1 ? name : std::to_string(count) + " Samples";
    switch (op) {
    case Operation::Rename: return "Rename " + name;
    case Operation::Move: return "Move " + what;
    case Operation::Trash: return "Move " + what + " to the Trash";
    case Operation::RemoveFolder: return "Remove " + name + " from the Library";
    }
    return what;
}

// A sample as the operation finds it: in the library, in an enabled folder,
// and on disk as the library last saw it. Throws OperationRefused otherwise.
struct Source {
    FileRecord file;
    Root root;
    fs::path full;
    std::string name;
};

Source source(Library& lib, std::int64_t id)
{
    auto file = lib.fileById(id);
    if (!file || file->status == FileStatus::Missing) throw OperationRefused("A sample is no longer in the library.");
    auto root = lib.root(file->rootId);
    const std::string name = nameOf(file->relPath);
    if (!root || !root->enabled) throw OperationRefused(name + "'s folder is not in the library.");
    const fs::path full = fromUtf8(root->path + "/" + file->relPath);
    std::error_code ec;
    if (!fs::is_regular_file(full, ec))
        throw OperationRefused(name + " is no longer in its folder; the next scan will notice.");
    const auto size = fs::file_size(full, ec);
    const auto mtime = ec ? fs::file_time_type{} : fs::last_write_time(full, ec);
    if (ec || static_cast<std::int64_t>(size) != file->size || fileTimeToInt(mtime) != file->mtime)
        throw OperationRefused(name + " has changed since asma last read it; try again after the next scan.");
    return {std::move(*file), std::move(*root), full, name};
}

// Whether a destination is taken, on disk or in the library, by anything
// but the sample itself.
bool taken(Library& lib, std::int64_t rootId, const std::string& rel, const fs::path& to, const Source& s)
{
    std::error_code ec;
    if (fs::exists(fs::symlink_status(to, ec)) && !sameFile(to, s.full)) return true;
    const auto row = lib.fileByPath(rootId, rel);
    return row && row->id != s.file.id;
}

} // namespace

std::optional<std::string> renameProblem(std::string_view current, std::string_view newName)
{
    const std::string name(newName);
    if (name.find_first_of("/\\:*?\"<>|") != std::string::npos
        || std::any_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; }))
        return "A name cannot contain / \\ : * ? \" < > |.";
    const std::string ext = extensionOf(current);
    const bool keepsExt = name.size() >= ext.size() && lower(name.substr(name.size() - ext.size())) == lower(ext);
    const std::string base = keepsExt ? name.substr(0, name.size() - ext.size()) : name;
    if (base.find_first_not_of(' ') == std::string::npos || base == "." || base == "..") return "A name is needed.";
    if (!keepsExt || lower(extensionOf(name)) != lower(ext)) return "A rename keeps the extension: " + ext + ".";
    if (name == current) return std::string(current) + " has that name already.";
    return std::nullopt;
}

TrashBackend TrashBackend::system()
{
    TrashBackend t;
    t.available = [](const fs::path& f) { return trashAvailable(f); };
    t.move = [](const fs::path& f) { return moveToTrash(f); };
    t.restore = [](const fs::path& where, const fs::path& to) { return restoreFromTrash(where, to); };
    return t;
}

FileOps::FileOps(Db& db, TrashBackend trash) : db_(db), trash_(std::move(trash)) {}

OpResult FileOps::rename(std::int64_t fileId, std::string_view newName)
{
    Library lib(db_);
    const Source s = source(lib, fileId);
    const std::string name(newName);
    if (const auto problem = renameProblem(s.name, name)) throw OperationRefused(*problem);

    const std::string folderRel = parentOf(s.file.relPath);
    const std::string rel = folderRel.empty() ? name : folderRel + "/" + name;
    const fs::path to = fromUtf8(s.root.path + "/" + rel);
    if (taken(lib, s.root.id, rel, to, s))
        throw OperationRefused(name + " already exists in " + folderName(s.root, folderRel) + ".");

    Step step;
    step.op = "rename";
    step.fileId = fileId;
    step.rootId = s.root.id;
    step.src = toUtf8(s.full);
    step.dst = toUtf8(to);
    return run(Operation::Rename, {step}, label(Operation::Rename, 1, s.name), s.name);
}

OpResult FileOps::move(const std::vector<std::int64_t>& fileIds, const fs::path& folder)
{
    Library lib(db_);
    std::error_code ec;
    const auto place = lib.rootOf(folder);
    if (!place || !fs::is_directory(folder, ec)) throw OperationRefused("That folder is outside the library's folders.");
    const auto& [root, folderRel] = *place;
    const std::string where = folderName(root, folderRel);

    std::vector<Step> steps;
    std::vector<std::string> already, clash, elsewhere;
    std::set<std::string> going;
    std::string firstName;
    for (const auto id : fileIds) {
        const Source s = source(lib, id);
        if (s.root.id == root.id && parentOf(s.file.relPath) == folderRel) {
            already.push_back(s.name);
            continue;
        }
        const std::string rel = folderRel.empty() ? s.name : folderRel + "/" + s.name;
        const fs::path to = fromUtf8(root.path + "/" + rel);
        if (!sameVolume(s.full, folder)) elsewhere.push_back(s.name);
        if (taken(lib, root.id, rel, to, s) || !going.insert(lower(s.name)).second) clash.push_back(s.name);
        if (firstName.empty()) firstName = s.name;
        Step step;
        step.op = "move";
        step.fileId = id;
        step.rootId = s.root.id;
        step.src = toUtf8(s.full);
        step.dst = toUtf8(to);
        steps.push_back(std::move(step));
    }
    if (steps.empty()) {
        if (already.size() == 1) throw OperationRefused(already[0] + " is already in " + where + ".");
        throw OperationRefused("Those samples are already in " + where + ".");
    }
    if (!elsewhere.empty())
        throw OperationRefused((elsewhere.size() == 1 ? elsewhere[0] + " is" : listed(elsewhere) + " are")
                               + " on another disk than " + where + "; moving between disks comes with export.");
    if (!clash.empty()) {
        if (steps.size() == 1) throw OperationRefused(clash[0] + " already exists in " + where + ".");
        throw OperationRefused(std::to_string(clash.size()) + " of " + samples(steps.size()) + " already exist in " + where
                               + ": " + listed(clash) + ".");
    }
    const std::size_t count = steps.size();
    return run(Operation::Move, std::move(steps), label(Operation::Move, count, firstName), firstName);
}

OpResult FileOps::trash(const std::vector<std::int64_t>& fileIds)
{
    Library lib(db_);
    std::vector<Step> steps;
    std::vector<std::string> none;
    for (const auto id : fileIds) {
        const Source s = source(lib, id);
        if (!trash_.available(s.full)) none.push_back(s.name);
        Step step;
        step.op = "trash";
        step.fileId = id;
        step.rootId = s.root.id;
        step.src = toUtf8(s.full);
        steps.push_back(std::move(step));
    }
    if (steps.empty()) throw OperationRefused("Nothing to move to the Trash.");
    if (!none.empty()) {
        if (none.size() == 1) throw OperationRefused(none[0] + " can't go to the Trash: its disk has none. Nothing was moved.");
        throw OperationRefused(samples(none.size()) + " can't go to the Trash: their disk has none (" + listed(none)
                               + "). Nothing was moved.");
    }
    const std::string first = nameOf(steps.front().src);
    const std::size_t count = steps.size();
    return run(Operation::Trash, std::move(steps), label(Operation::Trash, count, first), first);
}

OpResult FileOps::removeFolder(std::int64_t rootId)
{
    Library lib(db_);
    const auto root = lib.root(rootId);
    if (!root) throw OperationRefused("That folder is not in the library.");
    const std::string name = nameOf(root->path);
    if (!root->enabled) throw OperationRefused(name + " is not in the library.");
    Step step;
    step.op = "remove_root";
    step.rootId = rootId;
    step.src = root->path;
    return run(Operation::RemoveFolder, {step}, label(Operation::RemoveFolder, 1, name), name);
}

OpResult FileOps::run(Operation op, std::vector<Step> steps, std::string groupLabel, std::string name)
{
    OpResult result;
    result.op = op;
    result.label = std::move(groupLabel);
    result.count = steps.size();
    result.name = std::move(name);
    const std::string at = utcNow();
    {
        Transaction tx(db_);
        auto group = db_.prepare("INSERT INTO journal_groups(label, at, state) VALUES (?, ?, 'running')");
        group.bind(1, std::string_view(result.label)).bind(2, std::string_view(at));
        group.run();
        result.group = db_.lastInsertId();
        auto insert = db_.prepare("INSERT INTO journal(group_id, op, file_id, root_id, src, dst, state, at) "
                                  "VALUES (?, ?, ?, ?, ?, ?, 'planned', ?)");
        for (auto& s : steps) {
            insert.bind(1, result.group).bind(2, std::string_view(s.op));
            if (s.fileId) insert.bind(3, s.fileId);
            else insert.bindNull(3);
            insert.bind(4, s.rootId).bind(5, std::string_view(s.src));
            if (s.dst.empty()) insert.bindNull(6);
            else insert.bind(6, std::string_view(s.dst));
            insert.bind(7, std::string_view(at));
            insert.run();
            insert.reset();
            s.id = db_.lastInsertId();
        }
        tx.commit();
    }

    Library lib(db_);
    std::size_t done = 0;
    for (const auto& s : steps) {
        if (crashAfter_ && done == *crashAfter_) {
            crashAfter_.reset();
            throw SimulatedCrash();
        }
        std::string error;
        if (s.op == "rename" || s.op == "move") {
            if (const auto ec = moveFile(fromUtf8(s.src), fromUtf8(s.dst))) {
                error = ec == std::errc::file_exists ? "its new place is taken" : ec.message();
            } else {
                Transaction tx(db_);
                auto file = lib.fileById(s.fileId);
                const auto place = lib.rootOf(fromUtf8(s.dst));
                if (file && place) {
                    file->rootId = place->first.id;
                    file->relPath = place->second;
                    lib.updateFile(*file);
                }
                auto mark = db_.prepare("UPDATE journal SET state = 'done' WHERE id = ?");
                mark.bind(1, s.id);
                mark.run();
                tx.commit();
            }
        } else if (s.op == "trash") {
            const TrashResult trashed = trash_.move(fromUtf8(s.src));
            if (!trashed.ok) {
                error = trashed.error.empty() ? "the Trash did not take it" : trashed.error;
            } else {
                Transaction tx(db_);
                if (auto file = lib.fileById(s.fileId)) {
                    file->status = FileStatus::Missing;
                    file->relPath = std::string(kTrashedPrefix) + std::to_string(s.id);
                    lib.updateFile(*file);
                    auto mark = db_.prepare("UPDATE files SET trashed_by = ? WHERE id = ?");
                    mark.bind(1, s.id).bind(2, s.fileId);
                    mark.run();
                }
                auto mark = db_.prepare("UPDATE journal SET state = 'done', trash_ref = ? WHERE id = ?");
                mark.bind(1, std::string_view(toUtf8(trashed.where))).bind(2, s.id);
                mark.run();
                tx.commit();
            }
        } else {
            Transaction tx(db_);
            lib.setRootEnabled(s.rootId, false);
            auto mark = db_.prepare("UPDATE journal SET state = 'done' WHERE id = ?");
            mark.bind(1, s.id);
            mark.run();
            tx.commit();
        }
        if (!error.empty()) {
            OpResult rolled = result;
            rollBack(result.group, "rolled_back", rolled);
            const std::string what = nameOf(s.src);
            switch (op) {
            case Operation::Rename: throw OperationRefused("Could not rename " + what + ": " + error + ".");
            case Operation::Trash:
                throw OperationRefused("Could not move " + what + " to the Trash: " + error + ". Nothing was moved.");
            default: throw OperationRefused("Could not move " + what + ": " + error + ". Nothing was moved.");
            }
        }
        if (s.fileId) result.files.push_back(s.fileId);
        ++done;
    }
    auto finish = db_.prepare("UPDATE journal_groups SET state = 'done' WHERE id = ?");
    finish.bind(1, result.group);
    finish.run();
    prune();
    return result;
}

void FileOps::rollBack(std::int64_t group, const char* state, OpResult& result)
{
    Library lib(db_);
    std::vector<Step> steps;
    {
        auto q = db_.prepare("SELECT id, op, COALESCE(file_id, 0), COALESCE(root_id, 0), src, COALESCE(dst, ''), "
                             "COALESCE(trash_ref, ''), state FROM journal WHERE group_id = ? ORDER BY id DESC");
        q.bind(1, group);
        while (q.step()) {
            Step s;
            s.id = q.getInt(0);
            s.op = q.getText(1);
            s.fileId = q.getInt(2);
            s.rootId = q.getInt(3);
            s.src = q.getText(4);
            s.dst = q.getText(5);
            s.trashRef = q.getText(6);
            s.state = q.getText(7);
            steps.push_back(std::move(s));
        }
    }
    result.files.clear();
    result.skipped.clear();
    const auto mark = [&](std::int64_t id, const char* stepState) {
        auto q = db_.prepare("UPDATE journal SET state = ? WHERE id = ?");
        q.bind(1, std::string_view(stepState)).bind(2, id);
        q.run();
    };
    for (const auto& s : steps) {
        if (s.state != "done") {
            if (s.state == "planned") mark(s.id, "undone"); // never happened
            continue;
        }
        const std::string name = nameOf(s.src);
        if (s.op == "remove_root") {
            if (!lib.root(s.rootId)) {
                result.skipped.push_back(name + " could not be put back: it is no longer in the library");
                mark(s.id, "failed");
                continue;
            }
            Transaction tx(db_);
            lib.setRootEnabled(s.rootId, true);
            mark(s.id, "undone");
            tx.commit();
            continue;
        }
        // Back to the folder it was in, at the path it had.
        const auto root = lib.root(s.rootId);
        const std::string prefix = root ? root->path + "/" : std::string();
        const bool inRoot = root && s.src.compare(0, prefix.size(), prefix) == 0;
        const std::string rel = inRoot ? s.src.substr(prefix.size()) : std::string();
        const auto occupant = inRoot ? lib.fileByPath(s.rootId, rel) : std::nullopt;
        const bool trashed = s.op == "trash";
        const std::string failed = name + (trashed ? " could not be brought back: " : " could not be moved back: ");
        // A row whose file has gone and that holds nothing of the user's
        // gives way; any other keeps its place.
        if (occupant && occupant->id != s.fileId && occupant->status == FileStatus::Missing
            && !hasUserData(db_, occupant->id)) {
            lib.removeFile(occupant->id);
        } else if (occupant && occupant->id != s.fileId) {
            result.skipped.push_back(failed + "its old place is taken");
            mark(s.id, "failed");
            continue;
        }
        std::string error;
        if (trashed) {
            error = trash_.restore(fromUtf8(s.trashRef), fromUtf8(s.src));
        } else {
            std::error_code ec;
            if (!fs::exists(fs::symlink_status(fromUtf8(s.dst), ec))) error = "it is no longer where it was moved";
            else if (const auto moved = moveFile(fromUtf8(s.dst), fromUtf8(s.src)))
                error = moved == std::errc::file_exists ? "its old place is taken" : moved.message();
        }
        if (!error.empty()) {
            result.skipped.push_back(failed + error);
            mark(s.id, "failed");
            continue;
        }
        Transaction tx(db_);
        if (auto file = lib.fileById(s.fileId); file && inRoot) {
            file->rootId = s.rootId;
            file->relPath = rel;
            if (trashed) file->status = file->failureReason.empty() ? FileStatus::Ok : FileStatus::Failed;
            lib.updateFile(*file);
            auto clear = db_.prepare("UPDATE files SET trashed_by = NULL WHERE id = ?");
            clear.bind(1, s.fileId);
            clear.run();
            result.files.push_back(s.fileId);
        }
        mark(s.id, "undone");
        tx.commit();
    }
    auto finish = db_.prepare("UPDATE journal_groups SET state = ? WHERE id = ?");
    finish.bind(1, std::string_view(state)).bind(2, group);
    finish.run();
}

namespace {

// The operation, count and name of a journaled group.
OpResult described(Db& db, const JournalGroup& g)
{
    OpResult r;
    r.group = g.id;
    r.label = g.label;
    auto q = db.prepare("SELECT op, src FROM journal WHERE group_id = ? ORDER BY id");
    q.bind(1, g.id);
    while (q.step()) {
        if (r.count++ == 0) {
            r.op = opFrom(q.getText(0));
            r.name = nameOf(q.getText(1));
        }
    }
    return r;
}

std::vector<JournalGroup> groups(Db& db, std::string_view where, std::size_t limit)
{
    auto q = db.prepare("SELECT id, label, at, state FROM journal_groups " + std::string(where)
                        + " ORDER BY id DESC LIMIT ?");
    q.bind(1, static_cast<std::int64_t>(limit));
    std::vector<JournalGroup> out;
    while (q.step()) out.push_back({q.getInt(0), q.getText(1), q.getText(2), q.getText(3)});
    return out;
}

} // namespace

std::optional<OpResult> FileOps::undo()
{
    const auto g = undoable();
    if (!g) return std::nullopt;
    OpResult r = described(db_, *g);
    rollBack(g->id, "undone", r);
    return r;
}

std::optional<JournalGroup> FileOps::undoable()
{
    auto found = groups(db_, "WHERE state = 'done'", 1);
    if (found.empty()) return std::nullopt;
    return found.front();
}

std::vector<JournalGroup> FileOps::history(std::size_t limit) { return groups(db_, "", limit); }

std::vector<OpResult> FileOps::recover()
{
    std::vector<OpResult> out;
    for (const auto& g : groups(db_, "WHERE state = 'running'", 1000)) {
        OpResult r = described(db_, g);
        rollBack(g.id, "rolled_back", r);
        out.push_back(std::move(r));
    }
    return out;
}

void FileOps::prune()
{
    std::vector<std::int64_t> old;
    {
        auto q = db_.prepare("SELECT id FROM journal_groups WHERE state != 'running' ORDER BY id DESC LIMIT -1 OFFSET ?");
        q.bind(1, static_cast<std::int64_t>(kKeptGroups));
        while (q.step()) old.push_back(q.getInt(0));
    }
    if (old.empty()) return;
    Transaction tx(db_);
    for (const auto group : old) {
        // A sample still in the Trash can no longer come back through asma:
        // its row goes, and with it its data.
        auto gone = db_.prepare("SELECT f.id FROM files f JOIN journal j ON j.id = f.trashed_by WHERE j.group_id = ?");
        gone.bind(1, group);
        std::vector<std::int64_t> ids;
        while (gone.step()) ids.push_back(gone.getInt(0));
        Library lib(db_);
        for (const auto id : ids) lib.removeFile(id);
        auto steps = db_.prepare("DELETE FROM journal WHERE group_id = ?");
        steps.bind(1, group);
        steps.run();
        auto g = db_.prepare("DELETE FROM journal_groups WHERE id = ?");
        g.bind(1, group);
        g.run();
    }
    tx.commit();
}

std::string interruptedText(const OpResult& r)
{
    const std::string what = r.count == 1 ? r.name : std::to_string(r.count) + " samples";
    std::string doing;
    switch (r.op) {
    case Operation::Rename: doing = "renaming " + r.name; break;
    case Operation::Move: doing = "moving " + what; break;
    case Operation::Trash: doing = "moving " + what + " to the Trash"; break;
    case Operation::RemoveFolder: doing = "removing " + r.name + " from the library"; break;
    }
    std::string text = "asma was interrupted while " + doing + "; ";
    if (r.op == Operation::RemoveFolder) text += "it is back in the library.";
    else text += r.count == 1 ? "it is back where it was." : "they are back where they were.";
    for (const auto& s : r.skipped) text += " " + s + ".";
    return text;
}

std::string undoneText(const OpResult& r)
{
    std::string text = "Undid " + r.label;
    for (const auto& s : r.skipped) text += "; " + s;
    return text + ".";
}

} // namespace asma
