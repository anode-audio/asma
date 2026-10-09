// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Scanner.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/ContentHash.h"
#include "asma/core/Fs.h"
#include "asma/core/InstrumentTags.h"
#include "asma/core/Library.h"
#include "asma/core/NameParse.h"
#include "Parallel.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace asma {

namespace {

struct DiskEntry {
    std::string relPath;
    std::int64_t size = 0;
    std::int64_t mtime = 0;
};

struct WalkResult {
    std::vector<DiskEntry> entries;
    bool complete = true; // false when the iterator itself failed part-way
};

WalkResult walk(const fs::path& root)
{
    WalkResult result;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    if (ec) {
        result.complete = false;
        return result;
    }
    for (; it != end; it.increment(ec)) {
        if (ec) {
            result.complete = false;
            break;
        }
        const fs::directory_entry& entry = *it;
        const std::string name = toUtf8(entry.path().filename());
        std::error_code entryEc;
        if (!name.empty() && name.front() == '.') { // hidden files, "._" resource forks, hidden folders
            if (entry.is_directory(entryEc)) it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(entryEc) || !formatFromExtension(entry.path())) continue;
        const auto size = entry.file_size(entryEc);
        if (entryEc) continue;
        const auto mtime = entry.last_write_time(entryEc);
        if (entryEc) continue;
        result.entries.push_back({toUtf8(entry.path().lexically_relative(root)), static_cast<std::int64_t>(size),
                                  fileTimeToInt(mtime)});
    }
    return result;
}

enum class JobKind { New, Changed };

struct Job {
    JobKind kind = JobKind::New;
    DiskEntry disk;
    std::optional<FileRecord> existing;
};

struct JobResult {
    std::optional<ProbeResult> probe;
    std::string hash;
    NameInfo name;
    std::string error;
    bool unreadable = false; // access error: retry next scan, change nothing now
};

JobResult process(const fs::path& root, const Job& job)
{
    JobResult r;
    r.name = parseName(job.disk.relPath);
    try {
        const fs::path full = root / fromUtf8(job.disk.relPath);
        r.probe = probeFile(full);
        r.hash = contentHash(full, r.probe->hashOffset, r.probe->hashLength);
    } catch (const FileAccessError& e) {
        r.probe.reset();
        r.error = e.what();
        r.unreadable = true;
    } catch (const std::exception& e) {
        r.probe.reset();
        r.error = e.what();
    }
    return r;
}

DerivedInfo derive(const JobResult& r)
{
    DerivedInfo d;
    const auto& probe = *r.probe;
    if (probe.acid && probe.acid->tempo >= 20.0f && probe.acid->tempo <= 400.0f) {
        d.bpm = probe.acid->tempo;
        d.bpmConfidence = 1.0;
        d.bpmSource = FeatureSource::Embedded;
    } else if (r.name.bpm) {
        d.bpm = r.name.bpm;
        d.bpmConfidence = 0.9;
    }
    if (r.name.key) {
        d.key = r.name.key;
        d.keyConfidence = 0.9;
    }
    d.isLoop = probe.acid ? std::optional<bool>(!probe.acid->oneShot) : r.name.isLoop;
    d.loopSource = probe.acid ? FeatureSource::Embedded : FeatureSource::Filename;
    d.rootNote = probe.smplUnityNote;
    for (auto& tag : InstrumentDictionary::builtin().tagsFor(r.name.tokens)) d.tags.emplace_back(tag, TagSource::Auto);
    return d;
}

std::string formatOf(std::string_view relPath)
{
    const auto format = formatFromExtension(fromUtf8(relPath));
    return format ? std::string(formatName(*format)) : std::string("unknown");
}

FileRecord recordFrom(std::int64_t rootId, const DiskEntry& disk, const JobResult& r)
{
    FileRecord f;
    f.rootId = rootId;
    f.relPath = disk.relPath;
    f.size = disk.size;
    f.mtime = disk.mtime;
    if (r.probe) {
        f.contentHash = r.hash;
        f.format = std::string(formatName(r.probe->format));
        f.sampleRate = r.probe->sampleRate;
        f.channels = r.probe->channels;
        f.bitDepth = r.probe->bitDepth;
        f.duration = r.probe->durationSeconds;
        f.status = FileStatus::Ok;
    } else {
        f.format = formatOf(disk.relPath);
        f.status = FileStatus::Failed;
        f.failureReason = r.error;
    }
    return f;
}

void apply(Library& lib, std::int64_t rootId, const Job& job, const JobResult& r, ScanStats& stats,
           std::unordered_set<std::int64_t>& gone)
{
    if (r.unreadable) {
        // Permissions, a vanished file or a drive going away mid-scan. Not the
        // file's fault: leave any existing row as it is and try again next time.
        ++stats.skipped;
        return;
    }

    FileRecord rec = recordFrom(rootId, job.disk, r);

    if (!r.probe) {
        if (job.existing) {
            rec.id = job.existing->id;
            rec.contentHash = job.existing->contentHash;
            lib.updateFile(rec);
        } else {
            lib.insertFile(rec);
        }
        ++stats.failed;
        return;
    }

    const DerivedInfo derived = derive(r);

    if (job.kind == JobKind::Changed) {
        rec.id = job.existing->id;
        lib.updateFile(rec);
        if (rec.contentHash != job.existing->contentHash) lib.resetAnalysis(rec.id);
        lib.setDerived(rec.id, derived);
        ++stats.updated;
        return;
    }

    // A missing row whose root folder is absent is offline, not moved: an
    // unplugged drive must get its rows back on remount, even if copies of
    // its files exist elsewhere.
    const auto candidates = lib.relinkCandidates(rec.contentHash, rec.size);
    const auto moved = std::find_if(candidates.begin(), candidates.end(), [&](const FileRecord& c) {
        if (c.rootId == rootId) return true;
        const auto candidateRoot = lib.root(c.rootId);
        std::error_code ec;
        return candidateRoot && fs::is_directory(fromUtf8(candidateRoot->path), ec);
    });
    std::optional<FileRecord> source;
    if (moved != candidates.end()) source = *moved;
    // Moved here from another root that has not been rescanned yet: its row is
    // still ok there, but the file is gone from its path.
    if (!source) {
        for (const auto& c : lib.okFilesWithContent(rec.contentHash, rec.size)) {
            if (c.rootId == rootId) continue; // this root's own rows are settled by its sweep
            const auto candidateRoot = lib.root(c.rootId);
            std::error_code ec;
            if (!candidateRoot || !fs::is_directory(fromUtf8(candidateRoot->path), ec)) continue;
            if (fs::exists(fromUtf8(candidateRoot->path) / fromUtf8(c.relPath), ec) || ec) continue;
            source = c;
            break;
        }
    }
    if (source) {
        rec.id = source->id;
        lib.updateFile(rec);
        lib.setDerived(rec.id, derived);
        gone.erase(rec.id);
        ++stats.relinked;
        return;
    }

    rec.id = lib.insertFile(rec);
    lib.setDerived(rec.id, derived);
    ++stats.added;
}

} // namespace

ScanStats scanRoot(Db& db, std::int64_t rootId, const ScanOptions& options)
{
    Library lib(db);
    const auto root = lib.root(rootId);
    if (!root) throw std::invalid_argument("unknown root id " + std::to_string(rootId));
    const fs::path rootPath = fromUtf8(root->path);

    std::unordered_map<std::string, FileRecord> known;
    for (auto& file : lib.filesInRoot(rootId)) {
        std::string key = file.relPath;
        known.emplace(std::move(key), std::move(file));
    }

    std::error_code ec;
    const bool rootPresent = fs::is_directory(rootPath, ec);
    const WalkResult walked = rootPresent ? walk(rootPath) : WalkResult{{}, false};

    ScanStats stats;
    std::vector<Job> jobs;
    std::unordered_set<std::int64_t> gone;
    {
        Transaction tx(db);
        std::unordered_set<std::string> seen;
        for (const auto& disk : walked.entries) {
            seen.insert(disk.relPath);
            const auto it = known.find(disk.relPath);
            if (it == known.end()) {
                jobs.push_back({JobKind::New, disk, std::nullopt});
                continue;
            }
            const FileRecord& file = it->second;
            // Retried while it was away: read it again now it is back.
            if (file.failureReason == kGoneReason) {
                jobs.push_back({JobKind::Changed, disk, file});
                continue;
            }
            if (file.size == disk.size && file.mtime == disk.mtime) {
                if (file.status == FileStatus::Missing) {
                    // Back unchanged. A row that was failed before it went
                    // missing kept its reason and stays failed, so a file that
                    // crashed the scanner is never retried just for returning.
                    const bool wasFailed = !file.failureReason.empty();
                    lib.setStatus(file.id, wasFailed ? FileStatus::Failed : FileStatus::Ok, file.failureReason);
                    ++stats.updated;
                } else {
                    ++stats.unchanged; // failed files stay failed until they change
                }
                continue;
            }
            jobs.push_back({JobKind::Changed, disk, file});
        }
        // A partial walk must not mark the unwalked part of the library
        // missing. A root that is gone entirely (unplugged drive) is fine to
        // mark: rows are kept and come back on remount.
        if (!rootPresent || walked.complete) {
            for (const auto& [relPath, file] : known) {
                if (seen.count(relPath) || file.status == FileStatus::Missing) continue;
                lib.setStatus(file.id, FileStatus::Missing, file.failureReason); // keep why it failed
                gone.insert(file.id);
            }
        }
        tx.commit();
    }

    const unsigned threads = detail::threadCount(options.threads);
    const std::size_t batchSize = std::max<std::size_t>(1, options.batchSize);
    const std::size_t total = jobs.size();
    std::mutex callbackMutex;
    std::size_t done = 0;

    for (std::size_t start = 0; start < total; start += batchSize) {
        const std::size_t end = std::min(total, start + batchSize);
        std::vector<JobResult> results(end - start);
        detail::parallelFor(start, end, threads, [&](std::size_t i) {
            if (options.onFileStart) {
                std::lock_guard lock(callbackMutex);
                options.onFileStart(jobs[i].disk.relPath);
            }
            results[i - start] = process(rootPath, jobs[i]);
            std::lock_guard lock(callbackMutex);
            ++done;
            if (options.onProgress) options.onProgress(done, total, jobs[i].disk.relPath);
        });

        Transaction tx(db);
        for (std::size_t i = start; i < end; ++i) apply(lib, rootId, jobs[i], results[i - start], stats, gone);
        tx.commit();
    }

    stats.missing = gone.size();
    return stats;
}

RetryStats retryFiles(Db& db, const std::vector<std::int64_t>& fileIds,
                      const std::function<void(std::int64_t fileId)>& beforeRead)
{
    Library lib(db);
    struct Target {
        FileRecord file;
        fs::path root;
    };
    std::vector<Target> targets;
    for (const auto id : fileIds) {
        auto file = lib.fileById(id);
        if (!file) throw std::invalid_argument("no file with id " + std::to_string(id));
        const auto root = lib.root(file->rootId);
        targets.push_back({std::move(*file), fromUtf8(root->path)});
    }
    // A file that crashed a retry before goes last, so it cannot keep the
    // others from being read.
    std::stable_partition(targets.begin(), targets.end(),
                          [](const Target& t) { return t.file.failureReason != kCrashedReason; });

    const auto commit = [&](auto&& write) {
        Transaction tx(db);
        write();
        tx.commit();
    };
    RetryStats stats;
    for (const auto& [file, root] : targets) {
        const bool wasOk = file.status == FileStatus::Ok;
        const fs::path full = root / fromUtf8(file.relPath);
        std::error_code ec;
        const bool exists = fs::exists(full, ec);
        if (!ec && !exists) {
            if (wasOk) ++stats.skipped; // a drive away, say: the next scan settles it
            else {
                commit([&] { lib.setStatus(file.id, FileStatus::Failed, kGoneReason); });
                ++stats.gone;
            }
            continue;
        }
        const auto size = ec ? std::uintmax_t{0} : fs::file_size(full, ec);
        const auto mtime = ec ? fs::file_time_type{} : fs::last_write_time(full, ec);
        if (ec) { // cannot tell: leave the row as it is
            ++(wasOk ? stats.skipped : stats.failed);
            continue;
        }
        commit([&] { lib.setStatus(file.id, FileStatus::Failed, kCrashedReason); });
        if (beforeRead) beforeRead(file.id);
        const Job job{JobKind::Changed, {file.relPath, static_cast<std::int64_t>(size), fileTimeToInt(mtime)}, file};
        const JobResult r = process(root, job);
        if (!r.probe && r.unreadable && wasOk) {
            commit([&] { lib.setStatus(file.id, file.status, file.failureReason); });
            ++stats.skipped;
            continue;
        }
        if (!r.probe) {
            commit([&] { lib.setStatus(file.id, FileStatus::Failed, r.error); });
            ++stats.failed;
            continue;
        }
        FileRecord rec = recordFrom(file.rootId, job.disk, r);
        rec.id = file.id;
        commit([&] {
            lib.updateFile(rec);
            lib.resetAnalysis(rec.id);
            lib.setDerived(rec.id, derive(r));
        });
        stats.readable.push_back(rec.id);
    }
    return stats;
}

void markFailedPath(Db& db, std::int64_t rootId, std::string_view relPath, std::string_view reason)
{
    Library lib(db);
    const auto root = lib.root(rootId);
    if (!root) throw std::invalid_argument("unknown root id " + std::to_string(rootId));
    const fs::path full = fromUtf8(root->path) / fromUtf8(relPath);

    FileRecord rec;
    rec.rootId = rootId;
    rec.relPath = std::string(relPath);
    std::error_code ec;
    const auto size = fs::file_size(full, ec);
    rec.size = ec ? 0 : static_cast<std::int64_t>(size);
    const auto mtime = fs::last_write_time(full, ec);
    rec.mtime = ec ? 0 : fileTimeToInt(mtime);
    rec.format = formatOf(relPath);
    rec.status = FileStatus::Failed;
    rec.failureReason = std::string(reason);

    Transaction tx(db);
    if (const auto existing = lib.fileByPath(rootId, relPath)) {
        rec.id = existing->id;
        rec.contentHash = existing->contentHash;
        lib.updateFile(rec);
    } else {
        lib.insertFile(rec);
    }
    tx.commit();
}

void rederive(Db& db, std::int64_t fileId)
{
    Library lib(db);
    const auto file = lib.fileById(fileId);
    if (!file) return;
    const auto root = lib.root(file->rootId);
    if (!root) return;
    const Job job{JobKind::Changed, {file->relPath, file->size, file->mtime}, *file};
    const JobResult r = process(fromUtf8(root->path), job);
    if (!r.probe) return;
    Transaction tx(db);
    lib.setDerived(fileId, derive(r));
    tx.commit();
}

} // namespace asma
