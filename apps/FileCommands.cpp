// SPDX-License-Identifier: GPL-3.0-only
#include "FileCommands.h"

#include "CliCommon.h"
#include "SearchArgs.h"

#include "asma/core/FileOps.h"
#include "asma/core/Folders.h"
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Library.h"
#include "asma/core/UserData.h"
#include "asma/core/WriterLock.h"

#include <iostream>

namespace asma::cli {

namespace {

std::int64_t fileAt(Library& lib, const std::string& path)
{
    const auto file = lib.fileByAbsolutePath(fromUtf8(path));
    if (!file) throw UserDataError("not in the library: " + path);
    return file->id;
}

// The --id values, then every path left.
std::vector<std::int64_t> files(Args& args, Db& db, const std::vector<std::string>& idOptions)
{
    std::vector<std::int64_t> ids;
    for (const auto& id : idOptions) ids.push_back(static_cast<std::int64_t>(toDouble(id, "--id")));
    Library lib(db);
    while (const auto path = args.positional()) ids.push_back(fileAt(lib, *path));
    rejectLeftovers(args);
    if (ids.empty()) throw UsageError("name at least one file, by path or --id N");
    return ids;
}

void print(const OpResult& r, bool json)
{
    if (!json) {
        std::cout << r.label << "\n";
        return;
    }
    std::string list;
    for (const auto id : r.files) list += (list.empty() ? "" : ",") + std::to_string(id);
    std::string line = JsonLine().num("group", r.group).str("label", r.label).build();
    line.pop_back(); // the closing brace: the files go in before it
    std::cout << line << ",\"files\":[" << list << "]}\n";
}

} // namespace

int cmdFileOperation(const std::string& command, Args& args, Db& db, const std::filesystem::path& dbPath)
{
    const bool json = args.flag("json");
    const auto idOptions = args.options("id");
    const auto to = args.option("to");

    const auto lock = WriterLock::tryAcquire(dbPath.parent_path());
    if (!lock) {
        std::cerr << "asma: another asma process is writing to this library\n";
        return kLocked;
    }
    FileOps ops(db);
    for (const auto& r : ops.recover()) {
        if (json) std::cout << JsonLine().str("interrupted", interruptedText(r)).build() << "\n";
        else std::cout << interruptedText(r) << "\n";
    }

    if (command == "undo") {
        rejectLeftovers(args);
        const auto r = ops.undo();
        if (json) {
            JsonLine line;
            if (r) line.str("undone", r->label).strings("skipped", r->skipped);
            else line.null("undone");
            std::cout << line.build() << "\n";
        } else {
            std::cout << (r ? undoneText(*r) : "nothing to undo") << "\n";
        }
        return kOk;
    }
    if (command == "rename") {
        std::int64_t id = 0;
        if (!idOptions.empty()) id = static_cast<std::int64_t>(toDouble(idOptions.front(), "--id"));
        else if (const auto path = args.positional()) {
            Library lib(db);
            id = fileAt(lib, *path);
        }
        const auto name = args.positional();
        rejectLeftovers(args);
        if (!id || !name) throw UsageError("rename needs a file (or --id N) and its new name");
        print(ops.rename(id, *name), json);
        return kOk;
    }
    if (command == "move") {
        if (!to) throw UsageError("move needs --to FOLDER");
        print(ops.move(files(args, db, idOptions), fromUtf8(*to)), json);
        return kOk;
    }
    if (command == "trash") {
        print(ops.trash(files(args, db, idOptions)), json);
        return kOk;
    }
    // remove-folder
    const auto folder = args.positional();
    rejectLeftovers(args);
    if (!folder) throw UsageError("remove-folder needs a folder");
    Library lib(db);
    const auto place = lib.rootOf(fromUtf8(*folder));
    if (!place || !place->second.empty()) throw UserDataError("not a library folder: " + *folder);
    print(ops.removeFolder(place->first.id), json);
    return kOk;
}

int cmdHistory(Args& args, Db& db)
{
    const bool json = args.flag("json");
    rejectLeftovers(args);
    for (const auto& g : FileOps(db).history()) {
        if (json)
            std::cout << JsonLine().num("group", g.id).str("at", g.at).str("state", g.state).str("label", g.label).build()
                      << "\n";
        else
            std::cout << g.id << "\t" << g.at << "\t" << g.state << "\t" << g.label << "\n";
    }
    return kOk;
}

int cmdRootAdd(Args& args, Db& db, const std::filesystem::path& dbPath)
{
    const bool merge = args.flag("merge");
    const auto dir = args.positional();
    if (!dir) throw UsageError("root add needs a directory");
    rejectLeftovers(args);
    // A merge rewrites rows a scan or a file operation may be working on.
    const auto lock = WriterLock::tryAcquire(dbPath.parent_path());
    if (!lock) {
        std::cerr << "asma: another asma process is writing to this library\n";
        return kLocked;
    }
    for (const auto& r : FileOps(db).recover()) std::cout << interruptedText(r) << "\n";
    const AddCheck check = checkAddFolder(db, fromUtf8(*dir));
    if (check.result == AddCheck::Result::Contains && !merge)
        throw OperationRefused(check.message + " (root add --merge does)");
    const auto id = addFolder(db, fromUtf8(*dir), merge);
    std::cout << "root " << id << " " << Library(db).root(id)->path << "\n";
    return kOk;
}

} // namespace asma::cli
