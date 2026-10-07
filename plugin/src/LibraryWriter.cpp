// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryWriter.h"

#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Subprocess.h"
#include "asma/core/UserData.h"

#include <juce_events/juce_events.h>

namespace asma::app {

namespace {

constexpr const char* kHelperMissing = "asma's command-line helper is missing";

Write make(Write::Kind kind)
{
    Write w;
    w.kind = kind;
    return w;
}

// Message thread: runs `call` there, from any thread.
void onMessageThread(std::function<void()> call)
{
    juce::MessageManager::callAsync(std::move(call));
}

Collection collectionNamed(UserData& user, const std::string& name)
{
    const auto collection = user.collectionByName(name);
    if (!collection) throw UserDataError("no collection called '" + name + "'");
    return *collection;
}

SavedSearch searchNamed(UserData& user, const std::string& name)
{
    const auto saved = user.savedSearchByName(name);
    if (!saved) throw UserDataError("no saved search called '" + name + "'");
    return *saved;
}

void apply(Db& db, const Write& w)
{
    Library lib(db);
    UserData user(db);
    Transaction tx(db);
    using Kind = Write::Kind;
    switch (w.kind) {
    case Kind::Rate: user.setRating(w.fileId, w.rating); break;
    case Kind::Favourite: user.setFavourite(w.fileId, w.on); break;
    case Kind::AddTag:
    case Kind::RemoveTag:
        if (!lib.fileById(w.fileId)) throw UserDataError("no file with id " + std::to_string(w.fileId));
        if (w.kind == Kind::AddTag) lib.addUserTag(w.fileId, w.name);
        else lib.removeUserTag(w.fileId, w.name);
        break;
    case Kind::CreateCollection: {
        const auto id = user.createCollection(w.name);
        if (w.fileId) user.addToCollection(id, w.fileId);
        break;
    }
    case Kind::RenameCollection: user.renameCollection(collectionNamed(user, w.name).id, w.newName); break;
    case Kind::DeleteCollection: user.deleteCollection(collectionNamed(user, w.name).id); break;
    case Kind::AddToCollection: user.addToCollection(collectionNamed(user, w.name).id, w.fileId); break;
    case Kind::RemoveFromCollection: user.removeFromCollection(collectionNamed(user, w.name).id, w.fileId); break;
    case Kind::SaveSearch: user.saveSearch(w.name, w.model); break;
    case Kind::RenameSearch: user.renameSavedSearch(searchNamed(user, w.name).id, w.newName); break;
    case Kind::DeleteSearch: user.deleteSavedSearch(searchNamed(user, w.name).id); break;
    }
    tx.commit();
}

} // namespace

Write Write::rate(std::int64_t fileId, int rating)
{
    Write w = make(Kind::Rate);
    w.fileId = fileId;
    w.rating = rating;
    return w;
}

Write Write::favourite(std::int64_t fileId, bool on)
{
    Write w = make(Kind::Favourite);
    w.fileId = fileId;
    w.on = on;
    return w;
}

Write Write::addTag(std::int64_t fileId, std::string tag)
{
    Write w = make(Kind::AddTag);
    w.fileId = fileId;
    w.name = std::move(tag);
    return w;
}

Write Write::removeTag(std::int64_t fileId, std::string tag)
{
    Write w = make(Kind::RemoveTag);
    w.fileId = fileId;
    w.name = std::move(tag);
    return w;
}

Write Write::createCollection(std::string name, std::int64_t withFile)
{
    Write w = make(Kind::CreateCollection);
    w.name = std::move(name);
    w.fileId = withFile;
    return w;
}

Write Write::renameCollection(std::string name, std::string newName)
{
    Write w = make(Kind::RenameCollection);
    w.name = std::move(name);
    w.newName = std::move(newName);
    return w;
}

Write Write::deleteCollection(std::string name)
{
    Write w = make(Kind::DeleteCollection);
    w.name = std::move(name);
    return w;
}

Write Write::addToCollection(std::string collection, std::int64_t fileId)
{
    Write w = make(Kind::AddToCollection);
    w.name = std::move(collection);
    w.fileId = fileId;
    return w;
}

Write Write::removeFromCollection(std::string collection, std::int64_t fileId)
{
    Write w = make(Kind::RemoveFromCollection);
    w.name = std::move(collection);
    w.fileId = fileId;
    return w;
}

Write Write::saveSearch(std::string name, SearchModel model)
{
    Write w = make(Kind::SaveSearch);
    w.name = std::move(name);
    w.model = std::move(model);
    return w;
}

Write Write::renameSearch(std::string name, std::string newName)
{
    Write w = make(Kind::RenameSearch);
    w.name = std::move(name);
    w.newName = std::move(newName);
    return w;
}

Write Write::deleteSearch(std::string name)
{
    Write w = make(Kind::DeleteSearch);
    w.name = std::move(name);
    return w;
}

std::vector<std::vector<std::string>> cliCommands(const Write& w)
{
    // Names go after the "--" marker, so one that looks like an option
    // ("--version") is still a name.
    const std::string id = std::to_string(w.fileId);
    using Kind = Write::Kind;
    switch (w.kind) {
    case Kind::Rate: return {{"rate", std::to_string(w.rating), "--id", id}};
    case Kind::Favourite: return {{"fav", w.on ? "on" : "off", "--id", id}};
    case Kind::AddTag: return {{"tag", "add", "--id", id, "--", w.name}};
    case Kind::RemoveTag: return {{"tag", "remove", "--id", id, "--", w.name}};
    case Kind::CreateCollection:
        if (!w.fileId) return {{"collection", "create", "--", w.name}};
        return {{"collection", "create", "--", w.name}, {"collection", "add", "--id", id, "--", w.name}};
    case Kind::RenameCollection: return {{"collection", "rename", "--", w.name, w.newName}};
    case Kind::DeleteCollection: return {{"collection", "delete", "--", w.name}};
    case Kind::AddToCollection: return {{"collection", "add", "--id", id, "--", w.name}};
    case Kind::RemoveFromCollection: return {{"collection", "remove", "--id", id, "--", w.name}};
    case Kind::SaveSearch: return {{"search", "save", "--json", searchModelToJson(w.model), "--", w.name}};
    case Kind::RenameSearch: return {{"search", "rename", "--", w.name, w.newName}};
    case Kind::DeleteSearch: return {{"search", "delete", "--", w.name}};
    }
    return {};
}

std::string reasonText(const std::string& error)
{
    if (error.find("locked") != std::string::npos || error.find("busy") != std::string::npos)
        return "the library is busy";
    return error;
}

std::string failureText(const Write& w, const std::string& reason)
{
    using Kind = Write::Kind;
    const char* what = "";
    switch (w.kind) {
    case Kind::Rate: what = "save the rating"; break;
    case Kind::Favourite: what = "save the favourite"; break;
    case Kind::AddTag:
    case Kind::RemoveTag: what = "change the tags"; break;
    case Kind::CreateCollection: what = "make the collection"; break;
    case Kind::RenameCollection: what = "rename the collection"; break;
    case Kind::DeleteCollection: what = "delete the collection"; break;
    case Kind::AddToCollection:
    case Kind::RemoveFromCollection: what = "change the collection"; break;
    case Kind::SaveSearch: what = "save the search"; break;
    case Kind::RenameSearch: what = "rename the search"; break;
    case Kind::DeleteSearch: what = "delete the search"; break;
    }
    return std::string("Could not ") + what + ": " + reason;
}

// --- CliLane ---------------------------------------------------------------

struct CliLane::Running {
    Subprocess* process = nullptr; // guarded by the lane's mutex
};

CliLane::CliLane(std::filesystem::path dbPath, std::filesystem::path cli)
    : dbPath_(std::move(dbPath)), cli_(std::move(cli)), current_(std::make_shared<Running>())
{
    thread_ = std::thread([this] { loop(); });
}

CliLane::~CliLane()
{
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
        queue_.clear();
        if (current_->process) current_->process->kill(); // a retry may be waiting for a scan
    }
    wake_.notify_all();
    thread_.join();
}

void CliLane::run(Command command)
{
    {
        const std::lock_guard lock(mutex_);
        queue_.push_back(std::move(command));
    }
    wake_.notify_all();
}

void CliLane::setCli(std::filesystem::path cli)
{
    const std::lock_guard lock(mutex_);
    cli_ = std::move(cli);
}

bool CliLane::idle() const
{
    const std::lock_guard lock(mutex_);
    return queue_.empty() && !running_;
}

void CliLane::loop()
{
    for (;;) {
        Command command;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            command = std::move(queue_.front());
            queue_.pop_front();
            running_ = true;
        }
        std::string error;
        for (const auto& step : command.steps) {
            error = runStep(step, command.onLine);
            if (!error.empty()) break;
        }
        {
            const std::lock_guard lock(mutex_);
            if (stopping_) return; // nobody is waiting for the outcome
        }
        if (command.onEnd) onMessageThread([end = std::move(command.onEnd), error] { end(error); });
        // Idle only once the outcome is on its way, so a test that waits for
        // idle and then runs the message loop sees it.
        const std::lock_guard lock(mutex_);
        running_ = false;
    }
}

std::string CliLane::runStep(const std::vector<std::string>& step, const std::function<void(const std::string&)>& onLine)
{
    std::vector<std::string> args{"--db", toUtf8(dbPath_), "--errors-to-stdout"};
    args.insert(args.end(), step.begin(), step.end());
    std::filesystem::path cli;
    {
        const std::lock_guard lock(mutex_);
        cli = cli_;
    }
    std::optional<Subprocess> process;
    try {
        process = Subprocess::start(cli, args);
    } catch (const SubprocessError&) {
        return kHelperMissing;
    }
    {
        const std::lock_guard lock(mutex_);
        if (stopping_) return "stopped";
        current_->process = &*process;
    }
    std::string error;
    while (const auto line = process->readLine()) {
        if (line->rfind("error: ", 0) == 0) error = reasonText(line->substr(7));
        else if (onLine) onMessageThread([onLine, text = *line] { onLine(text); });
    }
    const ExitStatus status = process->wait();
    {
        const std::lock_guard lock(mutex_);
        current_->process = nullptr;
    }
    if (status.signalled || status.code != 0)
        return error.empty() ? "the helper stopped with code " + std::to_string(status.code) : error;
    return {};
}

// --- LibraryWriter ---------------------------------------------------------

LibraryWriter::LibraryWriter(std::filesystem::path dbPath, std::filesystem::path cli)
    : dbPath_(std::move(dbPath)), retries_(dbPath_, std::move(cli))
{
}

void LibraryWriter::retry(std::vector<std::int64_t> fileIds, RetryUpdate update)
{
    if (fileIds.empty()) return;
    std::vector<std::string> step{"retry"};
    for (const auto id : fileIds) {
        step.push_back("--id");
        step.push_back(std::to_string(id));
    }
    CliLane::Command command;
    command.steps = {std::move(step)};
    command.onLine = [update](const std::string& line) {
        if (line == "waiting for the scan" && update) update({RetryEvent::Kind::Waiting, {}});
    };
    command.onEnd = [update](const std::string& error) {
        if (update) update({error.empty() ? RetryEvent::Kind::Finished : RetryEvent::Kind::Failed, error});
    };
    retries_.run(std::move(command));
}

void LibraryWriter::setCli(std::filesystem::path cli) { retries_.setCli(std::move(cli)); }

bool LibraryWriter::idle() const { return retries_.idle(); }

std::filesystem::path LibraryWriter::cliNextTo(const std::filesystem::path& binary)
{
#ifdef _WIN32
    return binary.parent_path() / "asma-cli.exe";
#else
    return binary.parent_path() / "asma-cli";
#endif
}

void DirectWriter::write(const Write& w, WriteDone done)
{
    std::string error;
    try {
        if (!db_) db_ = Db::open(dbPath_);
        apply(*db_, w);
    } catch (const std::exception& e) {
        db_.reset(); // a broken connection is not kept for the next write
        error = reasonText(e.what());
    }
    if (done) done(error);
}

CliWriter::CliWriter(std::filesystem::path dbPath, std::filesystem::path cli)
    : LibraryWriter(dbPath, cli), writes_(dbPath, cli)
{
}

void CliWriter::write(const Write& w, WriteDone done)
{
    CliLane::Command command;
    command.steps = cliCommands(w);
    command.onEnd = std::move(done);
    writes_.run(std::move(command));
}

void CliWriter::setCli(std::filesystem::path cli)
{
    LibraryWriter::setCli(cli);
    writes_.setCli(std::move(cli));
}

bool CliWriter::idle() const { return LibraryWriter::idle() && writes_.idle(); }

} // namespace asma::app
