// Sound.h — a soundboard tile: trimmed clip of a source media + sticker/colour/category +
// OSC actions {At play (start), After play}.
#pragma once

#include <vector>
#include "model/ModelCommon.h"
#include "model/MediaAsset.h"
#include "model/OscActions.h"
#include "model/Triggerable.h"

namespace evobox
{

enum class MediaStatus { Ok, SourceMissing, ClipMissing, Decoding };

class Sound : public organic::BaseItem, public Triggerable
{
public:
    static constexpr const char* kType = "Sound";

    Sound();

    // parameters (undoable, inspectable, serialized)
    organic::Parameter* stickerP = nullptr;   // "ph:speaker-high" / "emoji:🚀" / "img:<file>"
    organic::Parameter* trimStartP = nullptr; // seconds in the source
    organic::Parameter* trimEndP = nullptr;
    organic::Parameter* gainDbP = nullptr;    // -24..+24 dB
    organic::Parameter* normalizeP = nullptr;
    organic::Parameter* fadeInMsP = nullptr;
    organic::Parameter* fadeOutMsP = nullptr;
    organic::Parameter* hotkeyP = nullptr;    // free text, reserved for per-tile hotkeys
    organic::Parameter* isEffectP = nullptr;  // effect: plays on top of everything and stacks; music: one at a time

    // plain fields
    Uid         categoryUid = 0;
    MediaRef    source;
    std::string clipFile;                   // bundle-relative: "clips/000042.mp3" (legacy shows: ".wav")

    OscActions  oscActions;

    // One running voice as the UI sees it (PlaybackController::tick). The position is reported in
    // SOURCE seconds from the clip the voice actually plays: a voice keeps its own buffer when the
    // trim handles move or a re-render lands, so a playhead never follows the handles.
    struct VoiceProgress
    {
        float  progress01 = 0.f;   // cursor / length of that voice's clip (0 = clip start, 1 = clip end)
        double sourceSec = 0.0;    // the same position in the source media (s)
    };

    // runtime (never serialized)
    struct Runtime
    {
        int    activeVoices = 0;
        float  progress01 = 0.f;           // most recent voice cursor / length (0 = clip start, 1 = clip end)
        std::vector<VoiceProgress> voiceProgress; // every active voice, start order (Clip Editor playheads)
        double lastTriggerTime = -1.0;      // app seconds (tile flash)
        double lastOscPulseTime = -1.0;     // any command of this sound was sent
        MediaStatus mediaStatus = MediaStatus::ClipMissing;
        std::shared_ptr<const organic::AudioBuffer> clip;     // rendered clip, preloaded
        // source seconds of clip's first frame = the trim start it was rendered with (the live
        // trimStart() may already differ until the debounced re-render lands)
        double clipSourceStart = 0.0;
        std::shared_ptr<const MediaAsset> sourceAsset;        // lazy, for editing
        bool   clipDirty = false;           // trim/gain changed since the last render
        double dirtyTime = 0;               // app seconds of the last render-relevant edit (debounce)
        bool   renderPending = false;
        bool   encodePending = false;       // clip file being written (render or legacy conversion); renders wait
        bool   loadAttempted = false;       // clip load / render already requested once
        bool   sourceLoading = false;
        std::string lastError;
    } rt;

    // convenience
    std::string sticker() const { return stickerP->stringValue(); }
    ImVec4 color() const { return colorP->color(); }
    double trimStart() const { return trimStartP->floatValue(); }
    double trimEnd() const { return trimEndP->floatValue(); }
    double clipDuration() const { return std::max(0.0, trimEnd() - trimStart()); }
    float  gainDb() const { return gainDbP->floatValue(); }
    bool   normalize() const { return normalizeP->boolValue(); }
    float  fadeInMs() const { return fadeInMsP->floatValue(); }
    float  fadeOutMs() const { return fadeOutMsP->floatValue(); }
    bool   isEffect() const { return isEffectP->boolValue(); }
    bool   playing() const { return rt.activeVoices > 0; }
    bool   hasClip() const { return rt.clip && rt.clip->frames() > 0; }

    void setCategoryUidUndoable(Uid uid);
    void setTrimUndoable(double start, double end); // one undo step for both handles
    void setTrim(double start, double end, bool notify = true);

    // Triggerable
    OscActions& actions() override { return oscActions; }
    const OscActions& actions() const override { return oscActions; }
    std::string displayName() const override { return niceName; }
    TriggerableKind triggerableKind() const override { return TriggerableKind::Sound; }
    Uid triggerableUid() const override { return makeTriggerableUid(TriggerableKind::Sound, uid); }
    organic::Container* asContainer() override { return this; }

    // Inspectable
    std::string inspectableTypeName() const override { return "Sound"; }
    void inspectorGui() override;

    void onParamChanged(organic::Parameter* p) override;

    json save() const override;
    void load(const json& j) override;
};

class SoundManager : public organic::BaseManager
{
public:
    explicit SoundManager(organic::Container* parent = nullptr);

    Sound* sound(size_t i) const { return static_cast<Sound*>(items[i].get()); }
    Sound* find(Uid uid) const { return static_cast<Sound*>(findItem(uid)); }
    std::vector<Sound*> sounds() const;
    std::vector<Sound*> soundsInCategory(Uid categoryUid) const; // 0 = all
    size_t countInCategory(Uid categoryUid) const;

    // Structural helpers (single undo steps)
    Sound* addSoundUndoable(std::unique_ptr<Sound> s, int index = -1); // takes a fully prepared sound
    void   moveToCategoryUndoable(const std::vector<Sound*>& sounds, Uid categoryUid);
    // Drag-reorder: moves `dragged` before/after `dropTarget` in the global list.
    void   reorderUndoable(Sound* dragged, Sound* dropTarget, bool after);

    void onItemsChanged() override;
};

} // namespace evobox
