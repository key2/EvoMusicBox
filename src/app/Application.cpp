#include "app/Application.h"
#include "app/ProjectIO.h"
#include "media/ClipEncoder.h"
#include "media/FFmpegDecoder.h"
#include "media/ImageWriter.h"
#include "util/Hash.h"
#include "util/Paths.h"
#include "util/Strings.h"
#include "util/TimeFormat.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace evobox
{

namespace
{
// MediaEvent::Image / Frames request tags: bit 63 marks sticker images, bit 62 video frame sets;
// gift icon requests use the plain (positive) gift id.
constexpr uint64_t kStickerTagBit = 1ull << 63;
constexpr uint64_t kFramesTagBit = 1ull << 62;
} // namespace

Application::Application()
    : trigger(project, osc),
      playback(project, audio, trigger),
      live(project, trigger, playback, catalog),
      importer(*this)
{
    trigger.onTimersDone = [this](Triggerable& t, SessionId sid) { live.onTimersDone(t, sid); };
    live.transientLookup = [this](int64_t giftId) -> GiftAction*
    {
        return (transientGift_ && transientGift_->giftId == giftId) ? transientGift_.get() : nullptr;
    };
    project.onAnyChange = [this] { onProjectChanged(); };
}

Application::~Application() { shutdown(); }

std::string Application::configDir() const { return paths::configDir().string(); }

// ---------------------------------------------------------------- lifecycle
bool Application::init()
{
    prefs.load(paths::prefsFile().string());
    dock.layoutsDir = paths::layoutsDir().string();

    // organic hook: any organic code path loading audio also understands every container
    organic::AudioCache::get().decodeFallback = [](const std::string& path, organic::AudioBuffer& out, std::string* err)
    {
        return FFmpegDecoder::decodeAll(path, out, err);
    };

    media.start(2);
    AudioSettings as;
    as.deviceName = prefs.audioDevice;
    as.periodSizeInFrames = prefs.periodSizeInFrames;
    as.masterVolume = project.settings.masterVolume();
    audio.init(as);
    osc.start();
#ifdef EVOBOX_WITH_TIKTOK
    icons.start(paths::giftIconsDir().string());
#endif
    loadCatalogCache();
    OLOG("App", "EvoMusicBox " << EVOBOX_VERSION << " — " << FFmpegDecoder::versionString());
    return true;
}

void Application::shutdown()
{
    static bool done = false;
    if (done) return;
    done = true;
    prefs.dockState = dock.saveState();
    prefs.save(paths::prefsFile().string());
    saveCatalogCache();
#ifdef EVOBOX_WITH_TIKTOK
    tiktok.disconnect();
    icons.stop();
#endif
    trigger.cancelAll();
    playback.stopAll(0);
    osc.stop();
    media.stop();
    audio.shutdown();
    // release GL textures held by the catalog and the sticker caches
    if (releaseTexture)
        for (auto& [id, g] : catalog.gifts) if (g.icon.valid()) { releaseTexture(g.icon); g.icon = {}; }
    releaseStickerTextures();
}

void Application::releaseStickerTextures()
{
    if (releaseTexture)
    {
        for (auto& [key, st] : stickerTex_) if (st.tex.valid()) releaseTexture(st.tex);
        for (auto& [uid, fsm] : stickerFrames_)
            for (auto& f : fsm.frames) if (f.texture.valid()) releaseTexture(f.texture);
    }
    stickerTex_.clear();
    stickerTagToKey_.clear();
    stickerFrames_.clear();
    frameTagToSound_.clear();
}

void Application::drainAll()
{
    now_ = nowSeconds();
    media.drain([&](const MediaEvent& e) { handleMediaEvent(e); });
#ifdef EVOBOX_WITH_TIKTOK
    tiktok.drainEvents([&](const LiveServiceEvent& e)
    {
        switch (e.kind)
        {
        case LiveServiceEvent::Kind::Event:
            live.route(e.event, now_);
            break;
        case LiveServiceEvent::Kind::Catalog:
        {
            int added = catalog.merge(e.catalog, e.roomUser);
            OLOG("Live", "Gift catalog: " << e.catalog.size() << " gifts (" << added << " new) from @" << e.roomUser);
            saveCatalogCache();
            break;
        }
        case LiveServiceEvent::Kind::State:
            live.state = e.state;
            live.roomUser = tiktok.username();
            if (e.state == LiveState::Error) { live.errorMessage = e.message; OLOGW("Live", "Connection error: " << e.message); }
            else if (!e.message.empty()) OLOG("Live", liveStateName(e.state) << " " << e.message);
            break;
        }
    });
    live.viewers = tiktok.viewers();
    icons.drain([&](const IconResult& r) { onGiftIconFile(r.giftId, r.path, r.ok, r.error); });
#endif
    trigger.tick(now_);
    playback.tick(now_);
}

void Application::tick(double now)
{
    now_ = now;
    scheduleDirtyRenders();
    promoteTransientIfEdited();
    applyMasterVolume();
    // keep decoded sources within budget (assets still referenced by sounds stay alive)
    static double lastEvict = 0;
    if (now - lastEvict > 5.0) { lastEvict = now; library.evict(768ull * 1024 * 1024); }
    // autosave
    int interval = project.settings.autosaveIntervalSec();
    if (interval > 0 && project.dirty() && now - lastAutosave_ > interval)
    {
        lastAutosave_ = now;
        std::string path;
        if (ProjectIO::autosave(project, &path)) OLOG("Project", "Autosaved to " << path);
    }
    if (setWindowTitle)
    {
        static std::string lastTitle;
        std::string t = windowTitle();
        if (t != lastTitle) { lastTitle = t; setWindowTitle(t); }
    }
}

void Application::endFrame()
{
    drops.endFrame();
}

void Application::applyMasterVolume()
{
    static float last = -1.f;
    float v = project.settings.masterVolume();
    if (v != last) { last = v; audio.setMasterVolume(v); }
}

void Application::setStatus(const std::string& msg)
{
    statusText = msg;
    statusTime = now_;
}

std::string Application::windowTitle() const
{
    std::string t = project.title();
    if (project.dirty()) t += " *";
    return t + " — EvoMusicBox";
}

void Application::onProjectChanged()
{
    // sounds added by undo/redo or paste arrive without their clip: load them lazily
    for (Sound* s : project.sounds.sounds())
        if (!s->rt.clip && !s->rt.loadAttempted && !s->rt.renderPending && !project.loading()) loadClipFor(*s);
}

// ---------------------------------------------------------------- media events
void Application::handleMediaEvent(const MediaEvent& e)
{
    switch (e.kind)
    {
    case MediaEvent::Kind::LibavLog:
        if (e.logLevel <= 16) OLOGE("Media", e.error); else OLOGW("Media", e.error);
        break;
    case MediaEvent::Kind::Decoded:
    {
        if (importer.onDecoded(e)) break;
        auto it = mediaCallbacks_.find(e.requestId);
        if (it != mediaCallbacks_.end())
        {
            auto cb = std::move(it->second);
            mediaCallbacks_.erase(it);
            if (e.ok && e.asset) library.put(e.asset);
            cb(e);
        }
        break;
    }
    case MediaEvent::Kind::ClipLoaded:
        if (Sound* s = soundForRequest(e.requestId)) onClipLoaded(*s, e);
        break;
    case MediaEvent::Kind::Rendered:
        if (Sound* s = soundForRequest(e.requestId)) onRendered(*s, e);
        break;
    case MediaEvent::Kind::ClipEncoded:
        if (Sound* s = soundForRequest(e.requestId)) onClipEncoded(*s, e);
        break;
    case MediaEvent::Kind::Image:
    {
        if (e.requestId & kStickerTagBit)
        {
            auto tit = stickerTagToKey_.find(e.requestId);
            if (tit == stickerTagToKey_.end()) break;
            std::string key = tit->second;
            stickerTagToKey_.erase(tit);
            StickerTex& st = stickerTex_[key];
            if (e.ok && e.image && uploadTexture)
            {
                if (st.tex.valid() && releaseTexture) releaseTexture(st.tex);
                st.tex = uploadTexture(*e.image);
                st.failed = !st.tex.valid();
            }
            else
            {
                st.failed = true;
                if (!e.ok) OLOGW("Media", "Sticker image failed (" << key << "): " << e.error);
            }
            break;
        }
        GiftInfo* g = catalog.find((int64_t)e.requestId);
        if (!g) break;
        if (e.ok && e.image && uploadTexture)
        {
            if (g->icon.valid() && releaseTexture) releaseTexture(g->icon);
            g->icon = uploadTexture(*e.image);
            g->iconState = g->icon.valid() ? IconState::Ready : IconState::Failed;
        }
        else
        {
            g->iconState = IconState::Failed;
            if (!e.ok) OLOGW("Live", "Icon decode failed for '" << g->name << "': " << e.error);
        }
        break;
    }
    case MediaEvent::Kind::Frames:
    {
        auto tit = frameTagToSound_.find(e.requestId);
        if (tit == frameTagToSound_.end()) break;
        Uid uid = tit->second;
        frameTagToSound_.erase(tit);
        FrameSet& set = stickerFrames_[uid];
        set.pending = false;
        for (auto& f : set.frames) if (f.texture.valid() && releaseTexture) releaseTexture(f.texture);
        set.frames.clear();
        if (!e.ok || !e.frames)
        {
            OLOGW("Media", "No sticker frames for sound " << uid << ": " << e.error);
            stickerFrames_.erase(uid);
            break;
        }
        for (const VideoFrame& vf : *e.frames)
        {
            FrameCandidate c;
            c.time = vf.time;
            c.image = std::make_shared<RgbaImage>(vf.image);
            if (uploadTexture) c.texture = uploadTexture(vf.image);
            set.frames.push_back(std::move(c));
        }
        OLOG("Media", set.frames.size() << " sticker frames extracted for sound " << uid);
        break;
    }
    case MediaEvent::Kind::Progress:
        break;
    }
}

// ---------------------------------------------------------------- image stickers
TextureHandle Application::stickerTexture(const std::string& sticker)
{
    if (!ProjectIO::isImageSticker(sticker)) return {};
    auto it = stickerTex_.find(sticker);
    if (it != stickerTex_.end())
    {
        if (it->second.tex.valid() || it->second.requested || it->second.failed) return it->second.tex;
    }
    StickerTex& st = stickerTex_[sticker];
    std::string path = ProjectIO::stickerImagePath(project, sticker);
    std::error_code ec;
    if (path.empty() || !fs::exists(path, ec)) { st.failed = true; return {}; }
    st.requested = true;
    uint64_t tag = kStickerTagBit | nextRequestId();
    stickerTagToKey_[tag] = sticker;
    media.requestImage(path, tag, 256);
    return {};
}

bool Application::setStickerFromImage(Sound& s, const std::string& imagePath, std::string* err)
{
    RgbaImage img;
    if (!ImageDecoder::decodeFile(imagePath, img, 512, err)) return false;
    return setStickerFromImage(s, img, err);
}

bool Application::setStickerFromImage(Sound& s, const RgbaImage& image, std::string* err)
{
    if (image.empty()) { if (err) *err = "empty image"; return false; }
    RgbaImage small = ImageWriter::fitToEdge(image, 256);
    // content-addressed file name: duplicates share one file, GC on save removes unused ones
    std::string name = sha1Hex(small.pixels.data(), small.pixels.size()).substr(0, 16) + ".png";
    fs::path file = fs::path(ProjectIO::iconsDir(project)) / name;
    std::error_code ec;
    if (!fs::exists(file, ec) && !ImageWriter::writePng(file.string(), small, err)) return false;
    std::string sticker = ProjectIO::imageStickerFor(name);
    // (re)upload immediately so the tile shows the picture on the very next frame
    if (uploadTexture)
    {
        StickerTex& st = stickerTex_[sticker];
        if (st.tex.valid() && releaseTexture) releaseTexture(st.tex);
        st.tex = uploadTexture(small);
        st.requested = true;
        st.failed = !st.tex.valid();
    }
    s.stickerP->setUndoable(sticker);
    OLOG("Media", "Sticker of '" << s.niceName << "' set from image (" << name << ")");
    return true;
}

void Application::requestStickerFrames(Sound& s, int count)
{
    std::string path = s.source.resolve(project.bundleDir);
    if (path.empty()) return;
    FrameSet& set = stickerFrames_[s.uid];
    if (set.pending) return;
    set.pending = true;
    uint64_t tag = kFramesTagBit | nextRequestId();
    frameTagToSound_[tag] = s.uid;
    media.requestVideoFrames(path, tag, count, 160);
}

const std::vector<Application::FrameCandidate>* Application::stickerFrames(Uid soundUid) const
{
    auto it = stickerFrames_.find(soundUid);
    if (it == stickerFrames_.end() || it->second.frames.empty()) return nullptr;
    return &it->second.frames;
}

bool Application::stickerFramesPending(Uid soundUid) const
{
    auto it = stickerFrames_.find(soundUid);
    return it != stickerFrames_.end() && it->second.pending;
}

void Application::clearStickerFrames(Uid soundUid)
{
    auto it = stickerFrames_.find(soundUid);
    if (it == stickerFrames_.end()) return;
    if (releaseTexture)
        for (auto& f : it->second.frames) if (f.texture.valid()) releaseTexture(f.texture);
    stickerFrames_.erase(it);
}

Sound* Application::soundForRequest(uint64_t requestId)
{
    if ((uint32_t)(requestId >> 48) != mediaGeneration_) return nullptr; // another project's job
    return project.sounds.find(requestId & 0xFFFFFFFFFFFFull);
}

void Application::onClipLoaded(Sound& s, const MediaEvent& e)
{
    s.rt.renderPending = false;
    if (e.ok && e.buffer)
    {
        s.rt.clip = e.buffer;
        s.rt.clipSourceStart = e.sourceStartSec; // the trim start when the load was requested
        s.rt.mediaStatus = MediaStatus::Ok;
        s.rt.lastError.clear();
        if (ClipEncoder::isLegacyClip(s.clipFile)) queueLegacyConversion(s);
    }
    else
    {
        // the clip file is missing/corrupt: try to re-render from the source
        s.rt.lastError = e.error;
        if (!s.source.resolve(project.bundleDir).empty()) requestRender(s);
        else
        {
            s.rt.mediaStatus = MediaStatus::SourceMissing;
            OLOGW("Media", "'" << s.niceName << "': clip missing and source not found (" << s.source.fileName() << ")");
        }
    }
}

void Application::onRendered(Sound& s, const MediaEvent& e)
{
    s.rt.renderPending = false;
    if (e.ok && e.buffer)
    {
        s.rt.clip = e.buffer;
        s.rt.clipSourceStart = e.sourceStartSec; // the trim start this render used (the handles may have moved since)
        s.rt.mediaStatus = MediaStatus::Ok;
        s.rt.lastError.clear();
        // the file is still being written on the worker (ClipEncoded follows): no second writer
        // of the same clip until then — edits made meanwhile wait in clipDirty
        if (!e.path.empty()) s.rt.encodePending = true;
    }
    else
    {
        s.rt.lastError = e.error;
        OLOGW("Media", "Render failed for '" << s.niceName << "': " << e.error);
        if (s.rt.clipDirty && now_ - s.rt.dirtyTime > 0.3) scheduleDirtyRenders();
    }
}

void Application::onClipEncoded(Sound& s, const MediaEvent& e)
{
    s.rt.encodePending = false;
    bool conversion = conversionActive_ == s.uid;
    if (conversion) conversionActive_ = 0;
    if (!e.ok)
        OLOGW("Media", "Cannot write the clip of '" << s.niceName << "': " << e.error);
    else if (conversion)
    {
        convertedClips_++;
        OLOG("Media", "Converted the clip of '" << s.niceName << "' to MP3");
    }
    if (s.rt.clipDirty) requestRender(s); // an edit waited for the file
    if (conversion) pumpConversions();
}

void Application::queueLegacyConversion(Sound& s)
{
    if (s.rt.encodePending || s.rt.renderPending || s.rt.clipDirty) return; // a render writes the new format itself
    if (conversionActive_ == s.uid) return;
    if (std::find(conversionQueue_.begin(), conversionQueue_.end(), s.uid) != conversionQueue_.end()) return;
    conversionQueue_.push_back(s.uid);
    pumpConversions();
}

void Application::pumpConversions()
{
    while (conversionActive_ == 0 && !conversionQueue_.empty())
    {
        Uid uid = conversionQueue_.front();
        conversionQueue_.pop_front();
        Sound* s = project.sounds.find(uid);
        if (!s || !s->rt.clip || !ClipEncoder::isLegacyClip(s->clipFile)) continue;
        if (s->rt.encodePending || s->rt.renderPending || s->rt.clipDirty) continue; // a render took over
        s->rt.encodePending = true;
        conversionActive_ = uid;
        media.requestClipEncode(s->rt.clip, ProjectIO::clipPathFor(project, ProjectIO::clipFileName(*s)), soundRequestId(uid));
    }
    if (conversionActive_ == 0 && convertedClips_ > 0)
    {
        setStatus("Converted " + std::to_string(convertedClips_) + " clip(s) to MP3 - save the show to shrink the file");
        convertedClips_ = 0;
    }
}

void Application::loadClipFor(Sound& s)
{
    s.rt.loadAttempted = true;
    std::string path = ProjectIO::clipPath(project, s);
    std::error_code ec;
    if (fs::exists(path, ec))
    {
        s.rt.mediaStatus = MediaStatus::Decoding;
        s.rt.renderPending = true;
        // the persisted clip was rendered with the persisted trim: remember where it starts in the source
        media.requestClipLoad(path, soundRequestId(s.uid), std::max(0.0, s.trimStart()));
    }
    else if (!s.source.resolve(project.bundleDir).empty())
    {
        requestRender(s);
    }
    else
    {
        s.rt.mediaStatus = MediaStatus::SourceMissing;
    }
}

void Application::loadAllClips()
{
    // Project::load already notified onProjectChanged(), which lazily requested the clips of
    // every new sound: only the ones nobody has requested are left (never reset the runtime state
    // of a sound whose load/render job is in flight — a second writer of the same clip file races
    // the first).
    for (Sound* s : project.sounds.sounds())
        if (!s->rt.loadAttempted) loadClipFor(*s);
}

void Application::ensureSourceLoaded(Sound& s, std::function<void(bool)> then)
{
    if (s.rt.sourceAsset && s.rt.sourceAsset->buffer) { if (then) then(true); return; }
    std::string path = s.source.resolve(project.bundleDir);
    if (path.empty())
    {
        s.rt.mediaStatus = s.rt.clip ? s.rt.mediaStatus : MediaStatus::SourceMissing;
        if (then) then(false);
        return;
    }
    if (auto a = library.find(path))
    {
        s.rt.sourceAsset = a;
        if (then) then(true);
        return;
    }
    if (then) sourceWaiters_[s.uid].push_back(std::move(then));
    if (sourceRequests_.count(s.uid)) return; // already decoding
    uint64_t id = nextRequestId();
    sourceRequests_[s.uid] = id;
    s.rt.sourceLoading = true;
    Uid uid = s.uid;
    uint32_t generation = mediaGeneration_;
    mediaCallbacks_[id] = [this, uid, path, generation](const MediaEvent& e)
    {
        sourceRequests_.erase(uid);
        // a same-uid sound of another project opened since must not receive this source
        Sound* snd = generation == mediaGeneration_ ? project.sounds.find(uid) : nullptr;
        bool ok = e.ok && e.asset;
        if (snd)
        {
            snd->rt.sourceLoading = false;
            if (ok)
            {
                snd->rt.sourceAsset = e.asset;
                if (snd->source.contentHash.empty()) snd->source.contentHash = e.asset->ref.contentHash;
            }
            else
            {
                snd->rt.lastError = e.error;
                OLOGW("Media", "Cannot load source for '" << snd->niceName << "': " << e.error);
            }
        }
        auto w = sourceWaiters_.find(uid);
        if (w != sourceWaiters_.end())
        {
            auto list = std::move(w->second);
            sourceWaiters_.erase(w);
            for (auto& fn : list) fn(ok && snd);
        }
    };
    media.requestDecode(path, id);
}

void Application::openSourceForEditing(Sound& s)
{
    library.touch(s.source.resolve(project.bundleDir));
    ensureSourceLoaded(s);
}

void Application::relinkSource(Sound& s, const std::string& newPath)
{
    if (newPath.empty()) return;
    MediaRef oldRef = s.source;
    MediaRef newRef = oldRef;
    newRef.path = newPath;
    newRef.relPath.clear();
    newRef.contentHash.clear();
    Sound* sp = &s;
    Application* self = this;
    auto apply = [self, sp](const MediaRef& r)
    {
        sp->source = r;
        sp->rt.sourceAsset.reset();
        sp->rt.lastError.clear();
        notifyStructureChanged(sp);
        self->requestRender(*sp);
    };
    organic::UndoManager::get().perform("Relink media", [apply, newRef] { apply(newRef); }, [apply, oldRef] { apply(oldRef); }, { sp });
    OLOG("Media", "Relinked '" << s.niceName << "' to " << newPath);
}

void Application::requestRender(Sound& s)
{
    // one writer per clip file: wait for a running render or legacy-clip conversion
    if (s.rt.renderPending || s.rt.encodePending) { s.rt.clipDirty = true; return; }
    s.rt.renderPending = true;
    s.rt.clipDirty = false;
    s.rt.loadAttempted = true;
    // a queued legacy conversion is pointless now: the render writes the current-format file itself
    conversionQueue_.erase(std::remove(conversionQueue_.begin(), conversionQueue_.end(), s.uid), conversionQueue_.end());
    Uid uid = s.uid;
    uint32_t generation = mediaGeneration_;
    ensureSourceLoaded(s, [this, uid, generation](bool ok)
    {
        Sound* snd = generation == mediaGeneration_ ? project.sounds.find(uid) : nullptr;
        if (!snd) return;
        if (!ok || !snd->rt.sourceAsset || !snd->rt.sourceAsset->buffer)
        {
            snd->rt.renderPending = false;
            if (!snd->rt.clip) snd->rt.mediaStatus = MediaStatus::SourceMissing;
            return;
        }
        RenderParams rp;
        rp.trimStartSec = snd->trimStart();
        rp.trimEndSec = snd->trimEnd();
        rp.gainDb = snd->gainDb();
        rp.normalize = snd->normalize();
        rp.fadeInMs = snd->fadeInMs();
        rp.fadeOutMs = snd->fadeOutMs();
        if (snd->clipFile.empty()) snd->clipFile = ProjectIO::clipFileName(*snd);
        // a render always writes the current clip format (.mp3, or .wav on an FFmpeg build without
        // an MP3 encoder); a persisted name of another format keeps pointing at the old file until
        // save()/load() adopt the new one (ProjectIO::convertedClipFor), so nothing is orphaned
        bool otherFormat = str::fileExtLower(snd->clipFile) != ClipEncoder::clipExtension();
        std::string outPath = otherFormat ? ProjectIO::clipPathFor(project, ProjectIO::clipFileName(*snd))
                                          : ProjectIO::clipPath(project, *snd);
        snd->rt.mediaStatus = snd->rt.clip ? MediaStatus::Ok : MediaStatus::Decoding;
        media.requestRender(snd->rt.sourceAsset->buffer, rp, outPath, soundRequestId(snd->uid));
    });
}

void Application::scheduleDirtyRenders()
{
    for (Sound* s : project.sounds.sounds())
        if (s->rt.clipDirty && !s->rt.renderPending && !s->rt.encodePending && now_ - s->rt.dirtyTime > 0.35)
            requestRender(*s);
}

// ---------------------------------------------------------------- project
void Application::newProject()
{
    playback.stopAll(0);
    trigger.cancelAll();
    organic::Selection::get().clear();
    mediaGeneration_++; // in-flight jobs of the previous project no longer match any sound
    conversionQueue_.clear();
    conversionActive_ = 0;
    convertedClips_ = 0;
    project.resetToDefaults();
    library.clear();
    releaseStickerTextures();
    lastAutosave_ = now_;
    setStatus("New project");
}

bool Application::openProject(const std::string& path, std::string* err)
{
    playback.stopAll(0);
    trigger.cancelAll();
    organic::Selection::get().clear();
    // Requests made while loading (Project::load -> onProjectChanged -> loadClipFor) belong to the
    // new project; events of the previous one are dropped from here on. A failed open keeps the
    // current project, so its generation is restored.
    uint32_t previous = mediaGeneration_++;
    // .liv show container (zip), .evobox folder or its project.json
    if (!ProjectIO::open(project, path, err)) { mediaGeneration_ = previous; return false; }
    conversionQueue_.clear();
    conversionActive_ = 0;
    convertedClips_ = 0;
    library.clear();
    releaseStickerTextures();
    loadAllClips();
    prefs.addRecent(projectLocation());
    prefs.save(paths::prefsFile().string());
    lastAutosave_ = now_;
    setStatus("Opened " + project.title());
    return true;
}

bool Application::mergeProject(const MergeSource& src, const MergeOptions& opts, MergeReport* report, std::string* err)
{
    MergeReport rep;
    if (!ProjectMerge::merge(project, src, opts, &rep, err)) return false;
    // the lazy loader (onProjectChanged) already requested the new sounds' clips as they were
    // added; this catches any it could not see yet
    loadAllClips();
    selectedCategoryUid = 0; // "All Sounds": the merged tiles are visible right away
    setStatus("Merged '" + src.title + "': " + rep.summary());
    if (report) *report = rep;
    return true;
}

std::string Application::projectLocation() const
{
    return project.isArchive() ? project.archivePath : project.bundleDir;
}

bool Application::saveProject(std::string* err)
{
    if (!hasBundle()) { if (err) *err = "choose a location first"; return false; }
    return saveProjectAs(projectLocation(), err);
}

bool Application::saveProjectAs(const std::string& path, std::string* err)
{
    // Save As to a "<name>.liv" (or a bare name) packs the show into one zip file; an explicit
    // "<name>.evobox" keeps the folder bundle layout.
    bool archive = !(str::fileExtLower(path) == ProjectIO::kBundleExt || ProjectIO::isBundle(path) ||
                     fs::path(path).filename() == ProjectIO::kProjectFile);
    bool ok = archive ? ProjectIO::saveArchive(project, path, err) : ProjectIO::save(project, path, err);
    if (!ok) return false;
    prefs.addRecent(projectLocation());
    prefs.save(paths::prefsFile().string());
    setStatus("Saved " + project.title() + (project.isArchive() ? " (" + str::fileName(project.archivePath) + ")" : ""));
    return true;
}

// ---------------------------------------------------------------- sounds
void Application::deleteSounds(const std::vector<Sound*>& sounds)
{
    for (Sound* s : sounds) if (s) { playback.onSoundRemoved(s->uid); clearStickerFrames(s->uid); }
    bool selected = false;
    for (Sound* s : sounds) if (s && s->isSelected()) selected = true;
    project.deleteSoundsUndoable(sounds);
    if (selected) organic::Selection::get().clear();
}

Sound* Application::selectedSound() const
{
    auto v = organic::Selection::get().getAs<Sound>();
    return v.empty() ? nullptr : v[0];
}

GiftAction* Application::selectedGift() const
{
    auto v = organic::Selection::get().getAs<GiftAction>();
    return v.empty() ? nullptr : v[0];
}

RoomEventAction* Application::selectedRoomEvent() const
{
    auto v = organic::Selection::get().getAs<RoomEventAction>();
    return v.empty() ? nullptr : v[0];
}

void Application::select(organic::Inspectable* obj)
{
    organic::Selection::get().set(obj);
    if (!obj) return;
    if (auto* s = dynamic_cast<Sound*>(obj))
    {
        focusBottomPanel = "Clip Editor";
        openSourceForEditing(*s);
    }
    else focusBottomPanel = "Live Monitor";
}

// ---------------------------------------------------------------- gifts
bool Application::tiktokAvailable() const
{
#ifdef EVOBOX_WITH_TIKTOK
    return true;
#else
    return false;
#endif
}

void Application::requestGiftIcon(GiftInfo& g)
{
    if (g.iconState != IconState::NotRequested || g.iconUrl.empty()) return;
#ifdef EVOBOX_WITH_TIKTOK
    g.iconState = IconState::Fetching;
    icons.fetch(g.iconUrl, g.id);
#else
    g.iconState = IconState::Failed;
#endif
}

void Application::onGiftIconFile(int64_t giftId, const std::string& path, bool ok, const std::string& err)
{
    GiftInfo* g = catalog.find(giftId);
    if (!g) return;
    if (!ok)
    {
        g->iconState = IconState::Failed;
        OLOGW("Live", "Icon download failed for '" << g->name << "': " << err);
        return;
    }
    g->iconFile = path;
    media.requestImage(path, (uint64_t)giftId, 128);
}

void Application::liveConnect(const std::string& usernameIn)
{
#ifdef EVOBOX_WITH_TIKTOK
    std::string username = str::cleanUsername(usernameIn);
    prefs.tiktokUsername = username;
    LiveOptions o;
    o.usePolling = prefs.tiktokPolling;
    o.useWebsocket = true;
    catalog.resetSessionCounters();
    live.errorMessage.clear();
    live.state = LiveState::Connecting;
    live.roomUser = username;
    tiktok.connect(username, o);
#else
    (void)usernameIn;
    setStatus("Built without TikTok support");
#endif
}

void Application::liveDisconnect()
{
#ifdef EVOBOX_WITH_TIKTOK
    tiktok.disconnect();
    live.state = tiktok.state();
#endif
}

void Application::liveRefreshCatalog()
{
    // the library offers no thread-safe on-demand fetch while connected: reconnect
    std::string u = prefs.tiktokUsername;
    liveDisconnect();
    if (!u.empty()) liveConnect(u);
}

void Application::simulateGift(int64_t giftId, int repeat)
{
    const GiftInfo* gi = catalog.find(giftId);
    GiftAction* a = project.giftActions.find(giftId);
    std::string name = gi ? gi->name : (a ? a->cachedName : "");
    int diamonds = gi ? gi->diamondCount : (a ? a->cachedDiamonds : 0);
    std::string icon = gi ? gi->iconUrl : (a ? a->cachedIconUrl : "");
    int type = gi ? gi->type : (a ? a->cachedType : 0);
    live.inject(LiveEvent::syntheticGift(giftId, repeat, false, name, diamonds, icon, type));
}

void Application::simulateRoomEvent(RoomEventKind kind)
{
    LiveEventType t = LiveEventType::Like;
    int likes = 1;
    switch (kind)
    {
    case RoomEventKind::Like:
        t = LiveEventType::Like;
        if (RoomEventAction* a = project.roomEvents.find(kind)) likes = std::max(1, a->threshold());
        break;
    case RoomEventKind::Follow:    t = LiveEventType::Follow; break;
    case RoomEventKind::Share:     t = LiveEventType::Share; break;
    case RoomEventKind::Subscribe: t = LiveEventType::Subscribe; break;
    case RoomEventKind::Join:      t = LiveEventType::Join; break;
    default: break;
    }
    live.inject(LiveEvent::syntheticRoomEvent(t, likes));
}

GiftAction* Application::selectGift(int64_t giftId)
{
    if (GiftAction* real = project.giftActions.find(giftId))
    {
        if (transientGift_) { trigger.unregisterExtra(transientGift_.get()); transientGift_.reset(); }
        select(real);
        return real;
    }
    if (!transientGift_ || transientGift_->giftId != giftId)
    {
        if (transientGift_) trigger.unregisterExtra(transientGift_.get());
        transientGift_ = std::make_unique<GiftAction>();
        const GiftInfo* gi = catalog.find(giftId);
        transientGift_->seedFromCatalog(giftId, gi ? gi->name : "", gi ? gi->diamondCount : 0,
                                        gi ? gi->iconUrl : "", gi ? gi->type : 0);
        transientGift_->rt.transient = true;
        transientRevision_ = 0;
        transientEdited_ = false;
        trigger.registerExtra(transientGift_.get());
        // Edits are detected by promoteTransientIfEdited(): it compares a fingerprint of the
        // transient's parameters / rows every frame against this baseline (simple and undo-safe).
        transientRevision_ = transientFingerprint();
    }
    select(transientGift_.get());
    return transientGift_.get();
}

uint64_t Application::transientFingerprint() const
{
    if (!transientGift_) return 0;
    const GiftAction& t = *transientGift_;
    // parameter revisions + command rows (count, uids, text revisions) + delays + sound
    uint64_t fp = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { fp ^= v + 0x9e3779b97f4a7c15ull; fp *= 1099511628211ull; };
    for (auto& p : t.params) mix(p->revision);
    for (auto& ph : t.oscActions.phases)
    {
        mix((uint64_t)ph->commands.items.size());
        mix((uint64_t)ph->delayP->revision);
        for (auto& c : ph->commands.items)
        {
            mix(c->uid);
            mix((uint64_t)static_cast<OscCommand*>(c.get())->textP->revision);
            mix((uint64_t)c->enabledP->revision);
            mix(static_cast<OscCommand*>(c.get())->targetUid);
        }
    }
    mix(t.soundUid);
    return fp;
}

void Application::promoteTransientIfEdited()
{
    if (!transientGift_) return;
    GiftAction& t = *transientGift_;
    uint64_t fp = transientFingerprint();
    if (fp == transientRevision_) return;
    // first edit -> real item, re-selected; the transient is dropped
    GiftAction* real = project.giftActions.addFromTransientUndoable(t);
    trigger.unregisterExtra(&t);
    bool wasSelected = t.isSelected();
    transientGift_.reset();
    if (real && wasSelected) organic::Selection::get().set(real);
    OLOG("Live", "Configured gift '" << (real ? real->displayName() : "?") << "'");
}

// ---------------------------------------------------------------- catalog cache
void Application::loadCatalogCache()
{
    std::string path = paths::giftCatalogFile().string();
    if (catalog.load(path)) OLOG("Live", "Gift catalog cache: " << catalog.size() << " gifts" << (catalog.roomUser.empty() ? "" : " (@" + catalog.roomUser + ")"));
}

void Application::saveCatalogCache()
{
    if (catalog.empty() || catalog.roomUser == "demo") return; // never persist the offline demo catalog
    catalog.save(paths::giftCatalogFile().string());
}

} // namespace evobox
