// ZipFile.h — minimal zip container helpers over miniz (third_party/miniz): pack a directory
// (selected root files + subfolders) into one archive, list / read entries, extract into a
// folder. Used for the .liv show container (ProjectIO).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace evobox
{
namespace zipfile
{

struct Entry
{
    std::string name;      // archive-relative, '/' separators
    uint64_t size = 0;     // uncompressed
    bool directory = false;
};

// Writes a zip at `zipPath` containing `rootFiles` (names relative to `dir`) and the whole
// content of each of `subdirs` (recursive). Files whose extension is in `storeExts` (e.g. ".mp3",
// ".wav") are stored without compression (fast; already-compressed audio and PCM barely shrink),
// everything else is deflated. The archive is written to a temporary file and renamed into place
// (atomic on POSIX).
bool writeDirectory(const std::string& zipPath, const std::string& dir, const std::vector<std::string>& rootFiles,
                    const std::vector<std::string>& subdirs, const std::vector<std::string>& storeExts = { ".mp3", ".wav" },
                    std::string* err = nullptr);

bool list(const std::string& zipPath, std::vector<Entry>& out, std::string* err = nullptr);
bool readEntry(const std::string& zipPath, const std::string& name, std::string& out, std::string* err = nullptr);
// Extracts every entry into `dir` (created when missing). Rejects entries escaping `dir`.
bool extractTo(const std::string& zipPath, const std::string& dir, std::string* err = nullptr);
// Cheap sniff: first bytes are a local file header / empty archive signature.
bool looksLikeZip(const std::string& path);

} // namespace zipfile
} // namespace evobox
