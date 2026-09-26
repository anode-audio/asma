// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asma {

struct Root {
    std::int64_t id = 0;
    std::string path; // UTF-8, '/' separators, absolute
    bool enabled = true;
};

enum class FileStatus { Ok, Missing, Failed };
enum class TagSource { Auto, Embedded, User };

struct FileRecord {
    std::int64_t id = 0;
    std::int64_t rootId = 0;
    std::string relPath; // UTF-8, '/' separators
    std::int64_t size = 0;
    std::int64_t mtime = 0;
    std::string contentHash; // empty when unknown
    std::string format;
    int sampleRate = 0;
    int channels = 0;
    int bitDepth = 0;
    double duration = 0.0;
    FileStatus status = FileStatus::Ok;
    std::string failureReason;
};

// Metadata derived from headers and file names (Plan 2 adds DSP analysis).
struct DerivedInfo {
    std::optional<double> bpm;
    double bpmConfidence = 0.0;
    std::optional<std::string> key;
    double keyConfidence = 0.0;
    std::optional<bool> isLoop;
    std::optional<int> rootNote;
    std::vector<std::pair<std::string, TagSource>> tags;
};

// Typed access to roots, files, features, tags and the FTS index. Methods do
// not open transactions; callers group writes.
class Library {
public:
    explicit Library(Db& db) : db_(db) {}

    // Returns the existing id when the directory is already a root.
    std::int64_t addRoot(const std::filesystem::path& dir);
    std::vector<Root> roots();
    std::optional<Root> root(std::int64_t id);

    std::vector<FileRecord> filesInRoot(std::int64_t rootId);
    std::optional<FileRecord> fileById(std::int64_t id);
    std::optional<FileRecord> fileByPath(std::int64_t rootId, std::string_view relPath);
    // Missing rows in any root whose content matches.
    std::vector<FileRecord> relinkCandidates(std::string_view contentHash, std::int64_t size);

    std::int64_t insertFile(const FileRecord& file);
    void updateFile(const FileRecord& file); // by file.id, every column
    void setStatus(std::int64_t fileId, FileStatus status, std::string_view reason = {});
    void resetAnalysis(std::int64_t fileId);

    // Replaces the features row and the auto/embedded tags; user tags stay.
    void setDerived(std::int64_t fileId, const DerivedInfo& info);
    std::optional<DerivedInfo> derived(std::int64_t fileId); // tags left empty

    void addUserTag(std::int64_t fileId, std::string_view tag);
    std::vector<std::pair<std::string, TagSource>> tags(std::int64_t fileId);

private:
    std::int64_t ensureTag(std::string_view name);
    void refreshFts(std::int64_t fileId);

    Db& db_;
};

} // namespace asma
