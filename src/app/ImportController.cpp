#include "app/ImportController.h"
#include "app/Application.h"
#include "app/ProjectIO.h"
#include "util/Strings.h"
#include <filesystem>

namespace evobox
{

ImportController::ImportController(Application& app) : app_(app) {}

const char* ImportController::mediaFilters()
{
    return "Audio & video{.wav,.mp3,.flac,.ogg,.oga,.opus,.aiff,.aif,.m4a,.aac,.wma,.mp4,.m4v,.mov,.mkv,.webm,.avi,.mts,.ts,.flv},"
           "Audio{.wav,.mp3,.flac,.ogg,.oga,.opus,.aiff,.aif,.m4a,.aac,.wma},"
           "Video{.mp4,.m4v,.mov,.mkv,.webm,.avi,.mts,.ts,.flv},.*";
}

const char* ImportController::imageFilters()
{
    return "Images{.png,.jpg,.jpeg,.webp,.bmp,.gif,.tga,.tif,.tiff},.*";
}

bool ImportController::looksLikeVideo(const std::string& path)
{
    std::string ext = str::fileExtLower(path);
    return ext == ".mp4" || ext == ".mov" || ext == ".mkv" || ext == ".webm" || ext == ".avi" || ext == ".m4v" ||
           ext == ".mts" || ext == ".ts" || ext == ".flv";
}

std::string ImportController::defaultStickerFor(const std::string& path)
{
    return looksLikeVideo(path) ? "ph:film-strip" : "ph:speaker-high";
}

void ImportController::importFiles(const std::vector<std::string>& paths, Uid categoryUid)
{
    for (const std::string& p : paths)
    {
        std::error_code ec;
        // a show file dropped onto the soundboard is opened, not imported as media
        if (ProjectIO::isArchivePath(p) || ProjectIO::isBundle(p) || std::filesystem::path(p).filename() == ProjectIO::kProjectFile)
        {
            app_.openRequestPath = p;
            continue;
        }
        if (!std::filesystem::is_regular_file(p, ec))
        {
            // directories: import their media files (one level)
            if (std::filesystem::is_directory(p, ec))
            {
                std::vector<std::string> inner;
                for (auto& e : std::filesystem::directory_iterator(p, ec))
                    if (e.is_regular_file()) inner.push_back(e.path().string());
                std::sort(inner.begin(), inner.end());
                importFiles(inner, categoryUid);
            }
            continue;
        }
        uint64_t id = app_.nextRequestId();
        pending_[id] = { p, categoryUid };
        app_.media.requestDecode(p, id);
        OLOG("Media", "Importing " << p);
    }
    if (!paths.empty()) app_.setStatus("Decoding " + std::to_string(paths.size()) + " file(s)...");
}

bool ImportController::onDecoded(const MediaEvent& e)
{
    auto it = pending_.find(e.requestId);
    if (it == pending_.end()) return false;
    Pending pend = it->second;
    pending_.erase(it);
    if (!e.ok || !e.asset)
    {
        lastError = e.error;
        OLOGW("Media", "Import failed for " << pend.path << ": " << e.error);
        app_.setStatus("Import failed: " + e.error);
        return true;
    }
    app_.library.put(e.asset);
    ClipDraft d;
    d.asset = e.asset;
    d.sourcePath = pend.path;
    d.name = str::fileStem(pend.path);
    d.sticker = defaultStickerFor(pend.path);
    d.categoryUid = pend.categoryUid ? pend.categoryUid
                                     : (app_.project.categories.customCategory() ? app_.project.categories.customCategory()->uid : 0);
    if (Category* c = app_.project.categories.find(d.categoryUid)) d.color = c->color();
    d.trimStart = 0;
    d.trimEnd = e.asset->duration();
    OLOG("Media", "Decoded " << pend.path << " (" << formatTime(e.asset->duration()) << ", "
                  << e.asset->sampleRate() << " Hz, " << e.asset->channels() << " ch)");
    // The sound exists right away (UI request: no "Save Clip" step). Selecting the last one of a
    // batch keeps the Clip Editor from jumping between files while a folder is being imported.
    Sound* s = createSound(d, pending_.empty());
    if (s) app_.setStatus("Added '" + s->niceName + "' — trim it in the Clip Editor");
    return true;
}

Sound* ImportController::createSound(const ClipDraft& d, bool select)
{
    if (!d.asset) return nullptr;
    auto s = std::make_unique<Sound>();
    s->setNiceName(d.name.empty() ? "New sound" : d.name);
    s->stickerP->setValue(d.sticker, false);
    s->colorP->setValue(d.color, false);
    s->categoryUid = d.categoryUid;
    s->source = d.asset->ref;
    if (s->source.path.empty()) s->source.path = d.sourcePath;
    if (app_.project.settings.copyMediaIntoProject() && app_.hasBundle())
        s->source.relPath = ProjectIO::copyMediaIntoBundle(app_.project, d.sourcePath);
    s->setTrim(d.trimStart, d.trimEnd, false);
    s->gainDbP->setValue(d.gainDb, false);
    s->normalizeP->setValue(d.normalize, false);
    s->fadeInMsP->setValue(d.fadeInMs, false);
    s->fadeOutMsP->setValue(d.fadeOutMs, false);
    s->rt.sourceAsset = d.asset;
    s->rt.mediaStatus = MediaStatus::Decoding;
    s->rt.clipDirty = false;
    s->rt.loadAttempted = true;

    Sound* raw = app_.project.sounds.addSoundUndoable(std::move(s));
    if (!raw) return nullptr;
    raw->clipFile = ProjectIO::clipFileName(*raw);
    app_.requestRender(*raw);
    if (select) app_.select(raw);
    // video: offer frames of the movie as sticker candidates
    if (raw->source.hasVideo || looksLikeVideo(d.sourcePath)) app_.requestStickerFrames(*raw);
    return raw;
}

Sound* ImportController::duplicateSound(Sound& src)
{
    json j = src.save();
    j["uid"] = 0;
    j.erase("_index");
    j["niceName"] = src.niceName + " Copy";
    j.erase("clipFile");
    auto s = std::make_unique<Sound>();
    s->load(j);
    s->rt.sourceAsset = src.rt.sourceAsset;
    s->rt.clip = src.rt.clip; // same audio until re-rendered
    s->rt.clipSourceStart = src.rt.clipSourceStart;
    s->rt.mediaStatus = src.rt.mediaStatus;
    s->rt.loadAttempted = true;
    Sound* raw = app_.project.sounds.addSoundUndoable(std::move(s), app_.project.sounds.indexOf(&src) + 1);
    if (!raw) return nullptr;
    raw->clipFile = ProjectIO::clipFileName(*raw);
    app_.requestRender(*raw);
    app_.select(raw);
    return raw;
}

} // namespace evobox
