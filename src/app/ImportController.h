// ImportController.h — import flow: files -> MediaService decode -> a Sound is added IMMEDIATELY
// (whole file as the clip, one undo step) and rendered to clips/<uid>.mp3; the Clip Editor then
// edits that sound in place (no separate "Save Clip" step). Video sources also get a set of
// frame thumbnails extracted as sticker candidates.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "media/MediaService.h"
#include "model/MediaAsset.h"
#include "model/ModelCommon.h"

namespace evobox
{

class Application;
class Sound;

// Everything needed to create a sound from a decoded source (also used by tests / the demo).
struct ClipDraft
{
    std::shared_ptr<const MediaAsset> asset;
    std::string sourcePath;
    std::string name;
    std::string sticker = "ph:speaker-high";
    ImVec4 color = ImVec4(0.36f, 0.56f, 0.95f, 1.f);
    Uid categoryUid = 0;
    double trimStart = 0;
    double trimEnd = 0;
    float gainDb = 0.f;
    bool normalize = false;
    float fadeInMs = 0.f;
    float fadeOutMs = 0.f;
    double duration() const { return std::max(0.0, trimEnd - trimStart); }
};

class ImportController
{
public:
    explicit ImportController(Application& app);

    // Starts decoding every path; each finished decode becomes a Sound (in completion order).
    void importFiles(const std::vector<std::string>& paths, Uid categoryUid);
    // Called by Application for Decoded events belonging to an import request.
    bool onDecoded(const MediaEvent& e); // true when the event was an import
    bool isImportRequest(uint64_t requestId) const { return pending_.count(requestId) != 0; }
    int pendingImports() const { return (int)pending_.size(); }
    std::string lastError;

    // Adds the Sound described by `d` (one undo step), requests its render and selects it.
    Sound* createSound(const ClipDraft& d, bool select = true);
    // Creates a new sound from an existing one ("Duplicate" / "Save as new" while editing).
    Sound* duplicateSound(Sound& src);

    static std::string defaultStickerFor(const std::string& path);
    static bool looksLikeVideo(const std::string& path);
    static const char* mediaFilters(); // ImGuiFileDialog filter string
    static const char* imageFilters(); // ImGuiFileDialog filter string for sticker images

private:
    Application& app_;
    struct Pending { std::string path; Uid categoryUid; };
    std::unordered_map<uint64_t, Pending> pending_;
};

} // namespace evobox
