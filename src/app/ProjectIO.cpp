#include "app/ProjectIO.h"
#include "media/ClipEncoder.h"
#include "util/Hash.h"
#include "util/Localize.h"
#include "util/Paths.h"
#include "util/Strings.h"
#include "util/ZipFile.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace evobox
{

std::string ProjectIO::scratchDir()
{
    static std::string dir;
    if (dir.empty())
    {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        long long stamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        fs::path p = paths::cacheDir() / "scratch" / ("session-" + std::to_string(stamp));
        paths::ensureDir(p / "clips");
        dir = p.string();
    }
    return dir;
}

std::string ProjectIO::clipsDir(const Project& p)
{
    fs::path base = p.bundleDir.empty() ? fs::path(scratchDir()) : fs::path(p.bundleDir);
    fs::path d = base / "clips";
    paths::ensureDir(d);
    return d.string();
}

std::string ProjectIO::clipFileName(Uid uid)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "clips/%06llu%s", (unsigned long long)uid, ClipEncoder::clipExtension());
    return buf;
}

std::string ProjectIO::clipFileName(const Sound& s) { return clipFileName(s.uid); }

bool ProjectIO::convertedClipUsable(const std::string& currentPath, const std::string& persistedPath)
{
    std::error_code ec;
    if (!fs::is_regular_file(currentPath, ec)) return false;
    if (!fs::exists(persistedPath, ec)) return true;
    // an older current-format file next to a newer persisted one was written for an earlier state
    // of the sound (e.g. another build re-rendered the .wav afterwards): keep the persisted one.
    // Strictly newer: equal stamps (Windows stat() has 1 s resolution) mean "convert again" —
    // redundant work at worst, never a stale clip
    auto cur = fs::last_write_time(currentPath, ec);
    if (ec) return false;
    auto old = fs::last_write_time(persistedPath, ec);
    return !ec && cur > old;
}

std::string ProjectIO::convertedClipFor(const Project& p, Uid uid, const std::string& clipFile)
{
    if (clipFile.empty()) return "";
    std::string current = clipFileName(uid);
    if (clipFile == current || str::fileExtLower(clipFile) == ClipEncoder::clipExtension()) return "";
    return convertedClipUsable(clipPathFor(p, current), clipPathFor(p, clipFile)) ? current : "";
}

std::string ProjectIO::clipPath(const Project& p, const Sound& s)
{
    return clipPathFor(p, s.clipFile.empty() ? clipFileName(s) : s.clipFile);
}

std::string ProjectIO::clipPathFor(const Project& p, const std::string& clipFile)
{
    fs::path base = p.bundleDir.empty() ? fs::path(scratchDir()) : fs::path(p.bundleDir);
    return (base / clipFile).string();
}

const std::vector<std::string>& ProjectIO::clipExtensions()
{
    static const std::vector<std::string> exts = { ClipEncoder::kMp3Ext, ClipEncoder::kWavExt };
    return exts;
}

std::string ProjectIO::mediaDir(const Project& p)
{
    if (p.bundleDir.empty()) return "";
    fs::path d = fs::path(p.bundleDir) / "media";
    paths::ensureDir(d);
    return d.string();
}

std::string ProjectIO::iconsDir(const Project& p)
{
    fs::path base = p.bundleDir.empty() ? fs::path(scratchDir()) : fs::path(p.bundleDir);
    fs::path d = base / "icons";
    paths::ensureDir(d);
    return d.string();
}

bool ProjectIO::isImageSticker(const std::string& sticker)
{
    return str::startsWith(sticker, kImageStickerPrefix);
}

std::string ProjectIO::stickerImagePath(const Project& p, const std::string& sticker)
{
    if (!isImageSticker(sticker)) return "";
    std::string rel = sticker.substr(strlen(kImageStickerPrefix));
    if (rel.empty()) return "";
    fs::path rp(rel);
    if (rp.is_absolute()) return rel; // legacy / external file
    fs::path base = p.bundleDir.empty() ? fs::path(scratchDir()) : fs::path(p.bundleDir);
    return (base / rp).string();
}

std::string ProjectIO::imageStickerFor(const std::string& iconFileName)
{
    return std::string(kImageStickerPrefix) + "icons/" + iconFileName;
}

std::string ProjectIO::bundleDirFromChoice(const std::string& chosenIn)
{
    std::string chosen = str::trim(chosenIn);
    while (!chosen.empty() && (chosen.back() == '/' || chosen.back() == '\\')) chosen.pop_back();
    if (chosen.empty()) return "";
    fs::path p(chosen);
    if (p.filename() == kProjectFile) p = p.parent_path();
    if (str::lower(p.extension().string()) == kBundleExt) return p.string();
    return p.string() + kBundleExt;
}

bool ProjectIO::isBundle(const std::string& dir)
{
    std::error_code ec;
    return fs::is_directory(dir, ec) && fs::exists(fs::path(dir) / kProjectFile, ec);
}

// ---------------------------------------------------------------- .liv show container
bool ProjectIO::isArchivePath(const std::string& path)
{
    return str::fileExtLower(path) == kArchiveExt;
}

std::string ProjectIO::archivePathFromChoice(const std::string& chosenIn)
{
    std::string chosen = str::trim(chosenIn);
    while (!chosen.empty() && (chosen.back() == '/' || chosen.back() == '\\')) chosen.pop_back();
    if (chosen.empty()) return "";
    fs::path p(chosen);
    std::string ext = str::lower(p.extension().string());
    if (ext == kArchiveExt) return p.string();
    if (ext == kBundleExt) p.replace_extension(); // "MyShow.evobox" -> "MyShow.liv"
    return p.string() + kArchiveExt;
}

std::string ProjectIO::archiveWorkDir(const std::string& archivePath)
{
    std::error_code ec;
    fs::path abs = fs::absolute(archivePath, ec);
    std::string key = sha1Hex(abs.generic_string()).substr(0, 16);
    // "<stem>-<hash>.evobox": the bundle extension keeps save() from appending its own
    fs::path d = paths::cacheDir() / "liv" / (abs.stem().string() + "-" + key + kBundleExt);
    return d.string();
}

std::string ProjectIO::workDirFor(const std::string& path)
{
    if (isArchivePath(path)) return archiveWorkDir(path);
    fs::path bp(path);
    if (bp.filename() == kProjectFile) bp = bp.parent_path();
    return bp.string();
}

bool ProjectIO::saveArchive(Project& p, const std::string& archivePathIn, std::string* err)
{
    std::string archivePath = archivePathFromChoice(archivePathIn);
    if (archivePath.empty()) { if (err) *err = LTR("error.noProjectPath", "no project path"); return false; }
    std::string workDir = archiveWorkDir(archivePath);
    // reuse the working folder when saving over the archive we came from (clips are already there)
    if (!save(p, workDir, err)) return false;
    fs::path base(workDir);
    std::vector<std::string> subdirs = { "clips", "icons" };
    // clips are stored, not deflated: MP3 is already compressed and float PCM barely shrinks
    if (!zipfile::writeDirectory(archivePath, workDir, { kProjectFile }, subdirs, clipExtensions(), err)) return false;
    p.archivePath = archivePath;
    std::error_code ec;
    auto size = fs::file_size(archivePath, ec);
    OLOG("Project", "Packed " << archivePath << (ec ? "" : " (" + str::humanBytes((unsigned long long)size) + ")"));
    return true;
}

bool ProjectIO::loadArchive(Project& p, const std::string& archivePath, std::string* err)
{
    std::error_code ec;
    if (!fs::is_regular_file(archivePath, ec)) { if (err) *err = str::format(LTR("error.cannotOpen", "cannot open %s"), archivePath.c_str()); return false; }
    if (!zipfile::looksLikeZip(archivePath)) { if (err) *err = str::format(LTR("error.notLivFile", "%s is not a .liv show file (not a zip archive)"), archivePath.c_str()); return false; }
    std::string projectJson;
    if (!zipfile::readEntry(archivePath, kProjectFile, projectJson, err)) return false;
    std::string workDir = archiveWorkDir(archivePath);
    // Extract over the working folder: files not in the archive (e.g. clips rendered after the
    // last save, referenced by an autosave) stay available for recovery; save() garbage-collects.
    fs::create_directories(workDir, ec);
    if (!zipfile::extractTo(archivePath, workDir, err)) return false;
    if (!load(p, workDir, err)) return false;
    p.archivePath = archivePath;
    return true;
}

bool ProjectIO::open(Project& p, const std::string& path, std::string* err)
{
    if (isArchivePath(path) || (fs::is_regular_file(path) && zipfile::looksLikeZip(path))) return loadArchive(p, path, err);
    fs::path bp(path);
    if (bp.filename() == kProjectFile) bp = bp.parent_path();
    if (!isBundle(bp.string()))
    {
        // "MyShow" typed without the extension
        std::string alt = bundleDirFromChoice(path);
        if (isBundle(alt)) bp = alt;
        else { if (err) *err = str::format(LTR("error.notShowOrFolder", "'%s' is not an EvoMusicBox show (.liv) or project folder"), path.c_str()); return false; }
    }
    return load(p, bp.string(), err);
}

bool ProjectIO::writeJsonAtomic(const std::string& path, const nlohmann::json& j, std::string* err)
{
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f.is_open()) { if (err) *err = str::format(LTR("error.cannotWrite", "cannot write %s"), tmp.c_str()); return false; }
        f << j.dump(2);
        if (!f.good()) { if (err) *err = str::format(LTR("error.writeError", "write error on %s"), tmp.c_str()); return false; }
    }
    return paths::replaceFile(tmp, path, err);
}

bool ProjectIO::readJson(const std::string& path, nlohmann::json& out, std::string* err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) { if (err) *err = str::format(LTR("error.cannotOpen", "cannot open %s"), path.c_str()); return false; }
    try { f >> out; }
    catch (const std::exception& ex) { if (err) *err = str::format(LTR("error.invalidJson", "invalid JSON in %s: %s"), path.c_str(), ex.what()); return false; }
    return true;
}

bool ProjectIO::save(Project& p, const std::string& bundleDirIn, std::string* err)
{
    std::string bundleDir = bundleDirFromChoice(bundleDirIn);
    if (bundleDir.empty()) { if (err) *err = LTR("error.noProjectPath", "no project path"); return false; }
    std::error_code ec;
    fs::create_directories(fs::path(bundleDir) / "clips", ec);
    if (ec) { if (err) *err = str::format(LTR("error.cannotCreate", "cannot create %s: %s"), bundleDir.c_str(), ec.message().c_str()); return false; }

    // move / copy rendered clips from their current location into the bundle
    std::string oldClips = clipsDir(p);
    std::string newClips = (fs::path(bundleDir) / "clips").string();
    // Files the GC must leave alone: every sound's clip plus its current-format name (a render or
    // legacy conversion may be writing it right now on a worker thread) and their "<name>.<n>.tmp".
    std::set<std::string> keep;
    int converted = 0;
    for (Sound* s : p.sounds.sounds())
    {
        if (s->clipFile.empty()) s->clipFile = clipFileName(*s);
        // a finished conversion / current-format render of a legacy clip: reference it from now on
        std::string current = convertedClipFor(p, s->uid, s->clipFile);
        if (!current.empty()) { s->clipFile = current; converted++; }
        keep.insert(fs::path(s->clipFile).filename().string());
        keep.insert(fs::path(clipFileName(*s)).filename().string());
        fs::path src = fs::path(oldClips) / fs::path(s->clipFile).filename();
        fs::path dst = fs::path(newClips) / fs::path(s->clipFile).filename();
        if (src != dst && fs::exists(src, ec))
        {
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) OLOGW("Project", "Cannot copy clip " << src.string() << ": " << ec.message());
        }
    }
    // image stickers (icons/<hash>.png): copy the referenced ones
    fs::path newIcons = fs::path(bundleDir) / "icons";
    std::set<std::string> usedIcons;
    auto noteIcon = [&](const std::string& sticker)
    {
        if (!isImageSticker(sticker)) return;
        std::string src = stickerImagePath(p, sticker);
        if (src.empty()) return;
        std::string name = fs::path(src).filename().string();
        usedIcons.insert(name);
        fs::path dst = newIcons / name;
        if (fs::path(src) != dst && fs::exists(src, ec))
        {
            fs::create_directories(newIcons, ec);
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) OLOGW("Project", "Cannot copy icon " << src << ": " << ec.message());
        }
    };
    for (Sound* s : p.sounds.sounds()) noteIcon(s->sticker());
    for (Category* c : p.categories.categories()) noteIcon(c->icon());

    // project.json first: nothing the file on disk references is deleted before the new one is in place
    std::string file = (fs::path(bundleDir) / kProjectFile).string();
    if (!writeJsonAtomic(file, p.save(), err)) return false;

    // garbage-collect: clips of deleted sounds, the .wav a converted legacy clip leaves behind,
    // leftover temp files of interrupted encodes — never a file a live sound uses or is writing
    auto protectedName = [&](const std::string& name)
    {
        for (const std::string& k : keep)
            if (name == k || str::startsWith(name, k + ".")) return true; // "<clip>.<n>.tmp"
        return false;
    };
    for (auto& e : fs::directory_iterator(newClips, ec))
    {
        if (!e.is_regular_file()) continue;
        std::string name = e.path().filename().string();
        if (protectedName(name)) continue;
        std::string ext = str::fileExtLower(name);
        bool clip = std::find(clipExtensions().begin(), clipExtensions().end(), ext) != clipExtensions().end();
        if (clip || ext == ".tmp") fs::remove(e.path(), ec);
    }
    if (fs::is_directory(newIcons, ec))
        for (auto& e : fs::directory_iterator(newIcons, ec))
        {
            if (!e.is_regular_file()) continue;
            std::string name = e.path().filename().string();
            if (usedIcons.count(name) == 0 && str::endsWith(name, ".png")) fs::remove(e.path(), ec);
        }

    p.bundleDir = bundleDir;
    p.archivePath.clear(); // saveArchive() sets it again after packing
    p.markSaved();
    OLOG("Project", "Saved " << file << " (" << p.sounds.items.size() << " sounds"
                             << (converted ? ", " + std::to_string(converted) + " clip(s) now MP3" : "") << ")");
    return true;
}

int ProjectIO::adoptConvertedClips(const Project& p, nlohmann::json& j)
{
    // Legacy clips converted in an earlier session that was never saved: the current-format file
    // sits next to the .wav in the working folder. Point the sound at it before the model loads
    // (the loaded project stays clean; save() drops the .wav) instead of converting again.
    int converted = 0;
    if (!j.contains("sounds") || !j["sounds"].is_object() || !j["sounds"].contains("items")) return 0;
    for (auto& item : j["sounds"]["items"])
    {
        if (!item.is_object() || !item.contains("uid") || !item.contains("clipFile") || !item["clipFile"].is_string()) continue;
        std::string current = convertedClipFor(p, item["uid"].get<Uid>(), item["clipFile"].get<std::string>());
        if (current.empty()) continue;
        item["clipFile"] = current;
        converted++;
    }
    return converted;
}

bool ProjectIO::load(Project& p, const std::string& bundleDirIn, std::string* err)
{
    std::string bundleDir = bundleDirIn;
    fs::path bp(bundleDir);
    if (bp.filename() == kProjectFile) { bp = bp.parent_path(); bundleDir = bp.string(); }
    std::string file = (bp / kProjectFile).string();
    nlohmann::json j;
    if (!readJson(file, j, err)) return false;
    if (!j.is_object() || j.value("app", "") != "EvoMusicBox")
    {
        if (err) *err = str::format(LTR("error.notProject", "%s is not an EvoMusicBox project"), file.c_str());
        return false;
    }
    // paths first: Project::load notifies onAnyChange, and listeners resolve clips against bundleDir
    p.bundleDir = bundleDir;
    p.archivePath.clear(); // loadArchive() sets it
    int converted = adoptConvertedClips(p, j);
    p.load(j);
    p.markSaved();
    if (converted) OLOG("Project", converted << " clip(s) already converted to MP3 in an earlier session are used");
    OLOG("Project", "Opened " << file << " (" << p.sounds.items.size() << " sounds)");
    return true;
}

std::string ProjectIO::autosavePath(const Project& p)
{
    if (!p.bundleDir.empty())
    {
        fs::path d = fs::path(p.bundleDir) / "autosave";
        paths::ensureDir(d);
        return (d / kProjectFile).string();
    }
    fs::path d = paths::configDir() / "autosave";
    paths::ensureDir(d);
    return (d / "untitled.json").string();
}

bool ProjectIO::autosave(const Project& p, std::string* outPath)
{
    std::string path = autosavePath(p);
    if (outPath) *outPath = path;
    std::error_code ec;
    // rotate: keep one previous copy
    if (fs::exists(path, ec)) fs::copy_file(path, path + ".1", fs::copy_options::overwrite_existing, ec);
    nlohmann::json j = p.save();
    j["autosaveOf"] = p.bundleDir;
    return writeJsonAtomic(path, j);
}

std::string ProjectIO::copyMediaIntoBundle(const Project& p, const std::string& sourcePath)
{
    std::string dir = mediaDir(p);
    if (dir.empty()) return "";
    std::error_code ec;
    fs::path src(sourcePath);
    fs::path dst = fs::path(dir) / src.filename();
    int n = 1;
    while (fs::exists(dst, ec) && !fs::equivalent(src, dst, ec))
        dst = fs::path(dir) / (src.stem().string() + "-" + std::to_string(n++) + src.extension().string());
    if (!fs::exists(dst, ec))
    {
        fs::copy_file(src, dst, ec);
        if (ec) { OLOGW("Project", "Cannot copy media: " << ec.message()); return ""; }
    }
    return (fs::path("media") / dst.filename()).string();
}

} // namespace evobox
