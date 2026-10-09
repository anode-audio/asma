// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Analysis.h"
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

// Where a BPM, key or loop flag came from. Embedded and file-name values
// always win over analysis.
enum class FeatureSource { Embedded, Filename, Analysis };

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

// A file in an enabled folder that could not be read, or read but not
// analysed: what the Problems panel lists.
struct Problem {
    enum class Kind { Read, Analysis };
    std::int64_t id = 0;
    std::string rootPath; // UTF-8, '/' separators
    std::string relPath;
    Kind kind = Kind::Read;
    std::string reason;
};

// Metadata derived from headers and file names; derived() also reports what
// analysis filled in.
struct DerivedInfo {
    std::optional<double> bpm;
    double bpmConfidence = 0.0;
    FeatureSource bpmSource = FeatureSource::Filename;
    std::optional<std::string> key;
    double keyConfidence = 0.0;
    FeatureSource keySource = FeatureSource::Filename;
    std::optional<bool> isLoop;
    FeatureSource loopSource = FeatureSource::Filename;
    std::optional<int> rootNote;
    std::vector<std::pair<std::string, TagSource>> tags;
};

// Typed access to roots, files, features, tags and the FTS index. Methods do
// not open transactions; callers group writes.
class Library {
public:
    explicit Library(Db& db) : db_(db) {}

    // A folder as roots store it: absolute, canonical, UTF-8 with '/'.
    static std::string folderPath(const std::filesystem::path& dir);
    // Returns the existing id when the directory is already a root, enabling
    // it again if it was removed.
    std::int64_t addRoot(const std::filesystem::path& dir);
    std::vector<Root> roots();
    std::optional<Root> root(std::int64_t id);
    // A removed folder is disabled: its samples are hidden everywhere and keep
    // their data, which comes back when it is enabled again.
    void setRootEnabled(std::int64_t id, bool enabled);
    // The deepest folder holding a path (enabled ones only, unless not
    // `enabledOnly`) and the path inside it, '/' separated; empty for the
    // folder itself.
    std::optional<std::pair<Root, std::string>> rootOf(const std::filesystem::path& path, bool enabledOnly = true);

    std::vector<FileRecord> filesInRoot(std::int64_t rootId);
    std::optional<FileRecord> fileById(std::int64_t id);
    std::optional<FileRecord> fileByPath(std::int64_t rootId, std::string_view relPath);
    // The file at an absolute path, looked up through the root that contains it.
    std::optional<FileRecord> fileByAbsolutePath(const std::filesystem::path& path);
    // Missing rows in any root whose content matches; never a trashed one.
    std::vector<FileRecord> relinkCandidates(std::string_view contentHash, std::int64_t size);
    // Ok rows in any root whose content matches: a file moved from a root that
    // has not been rescanned yet still looks present there.
    std::vector<FileRecord> okFilesWithContent(std::string_view contentHash, std::int64_t size);

    std::int64_t insertFile(const FileRecord& file);
    // Deletes a file's row and everything it held: features, tags, rating,
    // favourite and collection membership.
    void removeFile(std::int64_t id);
    void updateFile(const FileRecord& file); // by file.id, every column
    void setStatus(std::int64_t fileId, FileStatus status, std::string_view reason = {});
    // Content changed: forget analysis output and queue the file again.
    void resetAnalysis(std::int64_t fileId);

    // Replaces header/file-name features and the auto/embedded tags. User tags
    // stay, and so do analysed values the new info has nothing to replace with.
    void setDerived(std::int64_t fileId, const DerivedInfo& info);
    // Stores analysis output and marks the file analysed at kAnalysisVersion.
    // BPM, key and loop are only written where no embedded or file-name value
    // exists.
    void setAnalysis(std::int64_t fileId, const AnalysisResult& result);
    // Marks the file analysed at kAnalysisVersion without results, so it is
    // not retried until its content changes.
    void setAnalysisError(std::int64_t fileId, std::string_view reason);
    std::optional<DerivedInfo> derived(std::int64_t fileId); // tags left empty
    // Peak and LUFS from analysis; nullopt until the file has been analysed.
    std::optional<Loudness> loudness(std::int64_t fileId);

    // Tags are trimmed and kept lower case, so " Bass" is the tag "bass".
    // Throws UserDataError for a tag that is empty once trimmed.
    void addUserTag(std::int64_t fileId, std::string_view tag);
    // Removes the tag only where the user added it; auto and embedded tags
    // belong to the scanner and would come back on the next scan.
    void removeUserTag(std::int64_t fileId, std::string_view tag);
    std::vector<std::pair<std::string, TagSource>> tags(std::int64_t fileId);

    // Failed files, then files whose analysis failed, each by path.
    std::vector<Problem> problems();

private:
    std::int64_t ensureTag(std::string_view name);
    void refreshFts(std::int64_t fileId);

    Db& db_;
};

} // namespace asma
