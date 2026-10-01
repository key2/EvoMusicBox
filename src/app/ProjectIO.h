// ProjectIO.h — project persistence.
//   MyShow.evobox/            (folder bundle — the working layout)
//   ├── project.json          formatVersion, categories, sounds, giftActions, roomEvents, oscTargets, settings
//   ├── clips/000012.mp3      rendered clips (MP3; legacy shows hold float32 .wav, converted on
//   │                         load) -> playback never needs the sources
//   ├── icons/<hash>.png      picture stickers ("img:icons/<hash>.png")
//   ├── media/                optional copies of imported sources
//   └── autosave/project.json rotating autosave
//   MyShow.liv                (show container — ONE zip file with project.json + clips/ + icons/;
//                              source media and videos are never included). Opened by extracting
//                              into a per-archive working folder in the cache dir (Project::
//                              bundleDir, Project::archivePath) and saved by zipping it back.
// Unsaved projects keep their rendered clips in a per-session scratch folder that is moved into
// the bundle on the first save.
#pragma once

#include <string>
#include <vector>
#include "model/Project.h"

namespace evobox
{

class ProjectIO
{
public:
    static constexpr const char* kBundleExt = ".evobox";
    static constexpr const char* kProjectFile = "project.json";

    // Directory where rendered clips of `p` live right now (bundle or scratch).
    static std::string clipsDir(const Project& p);
    static std::string scratchDir();                      // per-process scratch for unsaved projects
    // Bundle-relative name a newly rendered clip gets: "clips/000042.mp3" (".wav" only when the
    // FFmpeg build has no MP3 encoder — ClipEncoder::clipExtension()).
    static std::string clipFileName(const Sound& s);
    static std::string clipFileName(Uid uid);
    // Absolute path of the sound's clip (its persisted clipFile, else clipFileName()).
    static std::string clipPath(const Project& p, const Sound& s);
    // Absolute path of a bundle-relative clip name ("clips/000042.mp3") in p's clips folder.
    static std::string clipPathFor(const Project& p, const std::string& clipFile);
    // Clip file extensions ever written into clips/ (garbage collection, zip "store" list).
    static const std::vector<std::string>& clipExtensions();
    // Legacy clips (a persisted name in another format than clipFileName(), e.g. the float32
    // ".wav" of older shows) are converted to the current format by the app in the background.
    // The converted file is referenced only once it exists and is strictly newer than the
    // persisted one: by save() (which then garbage-collects the old file) and by load() for a
    // conversion done in an earlier, unsaved session. Returns the current-format name to adopt,
    // "" when the persisted clip stays.
    static std::string convertedClipFor(const Project& p, Uid uid, const std::string& clipFile);
    static bool convertedClipUsable(const std::string& currentPath, const std::string& persistedPath);
    static std::string mediaDir(const Project& p);
    // Image stickers: "img:icons/<hash>.png" files live next to the clips (bundle or scratch).
    static constexpr const char* kImageStickerPrefix = "img:";
    static std::string iconsDir(const Project& p);
    static bool isImageSticker(const std::string& sticker);
    // Absolute path of an image sticker ("" for non-image stickers).
    static std::string stickerImagePath(const Project& p, const std::string& sticker);
    // Sticker string for an icon file name inside icons/ ("abc123.png" -> "img:icons/abc123.png").
    static std::string imageStickerFor(const std::string& iconFileName);

    // Normalises a user-chosen path into a bundle directory ("MyShow" -> "MyShow.evobox").
    static std::string bundleDirFromChoice(const std::string& chosen);
    static bool isBundle(const std::string& dir);

    // ---- .liv show container (zip)
    static constexpr const char* kArchiveExt = ".liv";
    static bool isArchivePath(const std::string& path);            // *.liv (case-insensitive)
    static std::string archivePathFromChoice(const std::string& chosen); // "MyShow" -> "MyShow.liv"
    // Working folder an archive is extracted into (stable per archive path, inside the cache dir).
    static std::string archiveWorkDir(const std::string& archivePath);
    // Saves the working folder (save()) then packs project.json + clips/ + icons/ into the zip
    // (temp file + rename). Sets p.bundleDir (work dir) and p.archivePath.
    static bool saveArchive(Project& p, const std::string& archivePath, std::string* err = nullptr);
    // Extracts the zip into its working folder and loads it (load()). Sets p.archivePath.
    static bool loadArchive(Project& p, const std::string& archivePath, std::string* err = nullptr);
    // Opens either form: .liv archive, .evobox folder or a project.json path.
    static bool open(Project& p, const std::string& path, std::string* err = nullptr);
    // Working folder `open(path)` would use (for autosave lookups before opening).
    static std::string workDirFor(const std::string& path);

    // Writes project.json (atomically) + moves scratch clips into the bundle + garbage-collects
    // unreferenced clip files. Sets p.bundleDir (and clears archivePath). Returns false with err on failure.
    static bool save(Project& p, const std::string& bundleDir, std::string* err = nullptr);
    // Reads project.json into p (p.load) and sets bundleDir. Clips are loaded by the app.
    static bool load(Project& p, const std::string& bundleDir, std::string* err = nullptr);
    // Rotating autosave (bundle/autosave/ or the config dir for untitled projects).
    static bool autosave(const Project& p, std::string* outPath = nullptr);
    static std::string autosavePath(const Project& p);
    // Copies a source file into <bundle>/media (returns the relative path, "" on failure).
    static std::string copyMediaIntoBundle(const Project& p, const std::string& sourcePath);

    static bool writeJsonAtomic(const std::string& path, const nlohmann::json& j, std::string* err = nullptr);
    static bool readJson(const std::string& path, nlohmann::json& out, std::string* err = nullptr);

private:
    // Rewrites the clipFile of sounds whose conversion exists (see convertedClipFor) in the
    // project JSON before it is loaded. Returns the number of sounds switched.
    static int adoptConvertedClips(const Project& p, nlohmann::json& j);
};

} // namespace evobox
