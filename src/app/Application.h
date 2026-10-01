// Application.h — owns the document, the services and the controllers; drains every service
// queue once per frame on the main thread; performs project open/save/new and the clip
// load/render pipeline. UI panels talk to this object.
#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "OrganicDock.h"
#include "app/FileDropQueue.h"
#include "app/ImportController.h"
#include "app/PlaybackController.h"
#include "app/Prefs.h"
#include "app/ProjectMerge.h"
#include "app/TriggerController.h"
#include "audio/AudioEngine.h"
#include "live/GiftCatalog.h"
#include "live/LiveEventRouter.h"
#include "media/MediaLibrary.h"
#include "media/MediaService.h"
#include "model/Project.h"
#include "osc/OscService.h"
#ifdef EVOBOX_WITH_TIKTOK
#include "live/IconFetcher.h"
#include "live/TikTokLiveService.h"
#endif

namespace evobox
{

enum class GiftFilter { All = 0, Configured, ReceivedThisSession, Tier1, Tier2, Tier3 };
enum class GiftSort { Name = 0, Diamonds, ConfiguredFirst, Recent };

class Application
{
public:
    Application();
    ~Application();

    // ---- document + services
    Project project;
    Prefs prefs;
    MediaLibrary library;
    MediaService media;
    AudioEngine audio;
    OscService osc;
    GiftCatalog catalog;
    TriggerController trigger;
    PlaybackController playback;
    LiveEventRouter live;
    ImportController importer;
#ifdef EVOBOX_WITH_TIKTOK
    TikTokLiveService tiktok;
    IconFetcher icons;
#endif
    organic::DockManager dock;
    FileDropQueue drops;

    // ---- shared UI state (navigation, not selection)
    Uid selectedCategoryUid = 0;          // 0 = All Sounds
    std::string soundSearch;
    GiftFilter giftFilter = GiftFilter::All;
    GiftSort giftSort = GiftSort::Diamonds;
    std::string giftSearch;
    bool focusSearchRequested = false;
    bool performanceMode = false;
    std::string statusText;
    double statusTime = -1.0;
    bool wantQuit = false;
    bool quitConfirmed = false;

    // Hooks installed by the UI/platform layer
    std::function<TextureHandle(const RgbaImage&)> uploadTexture;   // GL upload (main thread)
    std::function<void(const TextureHandle&)> releaseTexture;
    std::function<void(const std::string&)> setWindowTitle;

    // ---- lifecycle
    bool init();
    void shutdown();
    void drainAll();            // service queues -> controllers (once per frame, before NewFrame)
    void tick(double now);      // controllers, debounced renders, autosave
    void endFrame();

    // ---- project
    void newProject();
    // path: a <name>.liv show file, a <name>.evobox folder or its project.json
    bool openProject(const std::string& path, std::string* err = nullptr);
    // File > Merge show...: adds another show's sounds, categories (and optionally its OSC setup)
    // to the current project as one undo step; the new sounds' clips load like any other.
    bool mergeProject(const MergeSource& src, const MergeOptions& opts, MergeReport* report = nullptr, std::string* err = nullptr);
    bool saveProject(std::string* err = nullptr);            // false when Save As is needed
    // "<name>.liv" / bare name -> zip show container; "<name>.evobox" -> folder bundle
    bool saveProjectAs(const std::string& path, std::string* err = nullptr);
    bool hasBundle() const { return !project.bundleDir.empty(); }
    std::string projectLocation() const;                     // the .liv path, else the bundle folder
    std::string openRequestPath;                             // a show file was dropped: the shell opens it (unsaved-changes prompt)
    std::string windowTitle() const;
    void setStatus(const std::string& msg);

    // ---- sounds
    void deleteSounds(const std::vector<Sound*>& sounds);
    void requestRender(Sound& s);                           // (re)render clip from its source
    void ensureSourceLoaded(Sound& s, std::function<void(bool ok)> then = {});
    void loadClipFor(Sound& s);
    // Requests the clip of every sound that has not been requested yet (after a project load or
    // an autosave recovery). Sounds with a load/render in flight are left alone.
    void loadAllClips();
    void openSourceForEditing(Sound& s);                    // lazy source load when a tile is selected
    // Relink flow for moved files: the UI asks for a path, this re-points the source (undoable)
    // and re-renders the clip.
    void relinkSource(Sound& s, const std::string& newPath);
    Uid relinkRequestUid = 0;                               // set by panels, consumed by the shell's file dialog
    Uid stickerImageRequestUid = 0;                         // "Image file..." in the sticker picker -> file dialog

    // ---- image stickers ("img:icons/<hash>.png", files live in the project bundle)
    // GL texture of an image sticker (decoded lazily on the worker pool and cached); an invalid
    // handle means "not ready yet" or "missing" — callers fall back to a glyph.
    TextureHandle stickerTexture(const std::string& sticker);
    // Imports a picture as the sound's sticker: scaled to <= 256 px, written as PNG into
    // <bundle>/icons, then stickerP is set (undoable). Runs synchronously (small images).
    bool setStickerFromImage(Sound& s, const std::string& imagePath, std::string* err = nullptr);
    bool setStickerFromImage(Sound& s, const RgbaImage& image, std::string* err = nullptr);
    // Sticker candidates extracted from a video source ("pick a frame of the movie").
    struct FrameCandidate
    {
        double time = 0;                                    // seconds in the source
        std::shared_ptr<const RgbaImage> image;
        TextureHandle texture;
    };
    void requestStickerFrames(Sound& s, int count = 16);   // worker job; results via stickerFrames()
    const std::vector<FrameCandidate>* stickerFrames(Uid soundUid) const;
    bool stickerFramesPending(Uid soundUid) const;
    void clearStickerFrames(Uid soundUid);
    Sound* selectedSound() const;
    GiftAction* selectedGift() const;
    RoomEventAction* selectedRoomEvent() const;
    void select(organic::Inspectable* obj);                 // Selection::get().set + bottom panel focus
    std::string focusBottomPanel;                           // set by select(), consumed by the shell

    // ---- gifts
    void requestGiftIcon(GiftInfo& g);
    bool tiktokAvailable() const;
    void liveConnect(const std::string& username);
    void liveDisconnect();
    void liveRefreshCatalog();
    void simulateGift(int64_t giftId, int repeat = 1);
    void simulateRoomEvent(RoomEventKind kind);
    // gallery -> transient gift action ("shown but not saved until the first edit")
    GiftAction* transientGift() { return transientGift_.get(); }
    GiftAction* selectGift(int64_t giftId);                 // returns the real or transient action
    void promoteTransientIfEdited();                        // TransientGuard, called each frame

    // ---- misc
    uint64_t nextRequestId() { return nextRequestId_++; }
    double now() const { return now_; }
    std::string configDir() const;

private:
    uint64_t nextRequestId_ = 1;
    double now_ = 0;
    double lastAutosave_ = 0;
    std::unordered_map<uint64_t, std::function<void(const MediaEvent&)>> mediaCallbacks_;
    std::unordered_map<Uid, std::vector<std::function<void(bool)>>> sourceWaiters_;
    std::unordered_map<Uid, uint64_t> sourceRequests_; // soundUid -> decode requestId
    std::unique_ptr<GiftAction> transientGift_;
    uint64_t transientRevision_ = 0;
    bool transientEdited_ = false;
    // image stickers: sticker string -> texture (+ request bookkeeping); tags carry bit 63 so they
    // never collide with gift ids in MediaEvent::Image.
    struct StickerTex { TextureHandle tex; bool requested = false; bool failed = false; };
    std::unordered_map<std::string, StickerTex> stickerTex_;
    std::unordered_map<uint64_t, std::string> stickerTagToKey_;
    struct FrameSet { bool pending = false; std::vector<FrameCandidate> frames; };
    std::unordered_map<Uid, FrameSet> stickerFrames_;
    std::unordered_map<uint64_t, Uid> frameTagToSound_;
    void releaseStickerTextures();

    void handleMediaEvent(const MediaEvent& e);
    void onClipLoaded(Sound& s, const MediaEvent& e);
    void onRendered(Sound& s, const MediaEvent& e);
    // A clip file write finished (render or legacy conversion): releases rt.encodePending.
    void onClipEncoded(Sound& s, const MediaEvent& e);
    void scheduleDirtyRenders();

    // ---- media requests carry the sound uid tagged with a per-project generation: sound uids
    // restart at 1 in every show, so an event of a previously opened project must never be
    // applied to the same-uid sound of the current one (clip loads are not cancelled on open).
    uint32_t mediaGeneration_ = 1;
    uint64_t soundRequestId(Uid uid) const { return ((uint64_t)mediaGeneration_ << 48) | (uid & 0xFFFFFFFFFFFFull); }
    Sound* soundForRequest(uint64_t requestId);

    // ---- legacy clip conversion (float32 .wav shows -> .mp3): the decoded legacy clip is
    // re-encoded under the current-format name, one sound at a time so interactive media work is
    // not starved. Nothing in the model changes: save() (and load() for an unsaved earlier
    // session) adopts the converted file and drops the .wav — see ProjectIO::convertedClipFor.
    std::deque<Uid> conversionQueue_;
    Uid conversionActive_ = 0;
    int convertedClips_ = 0;
    void queueLegacyConversion(Sound& s);
    void pumpConversions();
    void applyMasterVolume();
    void applyLiveState();
    void loadCatalogCache();
    void saveCatalogCache();
    void onProjectChanged();
    void onGiftIconFile(int64_t giftId, const std::string& path, bool ok, const std::string& err);
    uint64_t transientFingerprint() const;
};

} // namespace evobox
