// PlaybackController.h — the sound-specific trigger path: PlaybackPolicy -> AudioEngine voice ->
// TriggerController session; VoiceEnded -> TriggerController::end -> "After play" fires.
// Policy "Music stops previous music" (default): a non-effect sound stops every playing
// non-effect voice first; effects (Sound::isEffect) always overlap and stack (press 3x = 3 voices).
#pragma once

#include <vector>
#include "app/TriggerController.h"
#include "audio/AudioEngine.h"
#include "model/Project.h"

namespace evobox
{

class PlaybackController
{
public:
    PlaybackController(Project& project, AudioEngine& audio, TriggerController& trigger);

    // How the Clip Editor plays the same voice the tile does: looping the clip and/or starting
    // inside it (seconds into the rendered clip, i.e. from Sound::rt.clipSourceStart in the source).
    struct PlayOptions
    {
        bool loop = false;
        double startSec = 0.0;
    };

    bool play(Uid soundUid, TriggerSource src);
    bool play(Sound& s, TriggerSource src) { return play(s, src, PlayOptions{}); }
    bool play(Sound& s, TriggerSource src, const PlayOptions& opts);
    void stop(Uid soundUid, int fadeOutMs = 5);
    void toggle(Uid soundUid, TriggerSource src);
    void stopAll(int fadeOutMs = -1);           // -1 = settings value
    bool isPlaying(Uid soundUid) const;
    int  playingCount() const { return (int)voices_.size(); }

    // Once per frame: releases finished voices (-> After play), updates the tile progress
    // (rt.progress01 = latest voice) and the per-voice list the Clip Editor draws (rt.voiceProgress:
    // progress + position in source seconds, each voice measured against its own clip).
    void tick(double now);

    // Stops voices of a sound that is being deleted (no After play).
    void onSoundRemoved(Uid soundUid);

private:
    struct ActiveVoice
    {
        VoiceHandle handle;
        Uid soundUid = 0;
        SessionId session = 0;
        double startTime = 0;
        // the buffer this voice plays and where its first frame sits in the source: fixed at play()
        // time, so the position stays right while the handles move or Sound::rt.clip is re-rendered
        std::shared_ptr<const organic::AudioBuffer> clip;
        double clipSourceStart = 0;
    };
    Project& project_;
    AudioEngine& audio_;
    TriggerController& trigger_;
    std::vector<ActiveVoice> voices_;
};

} // namespace evobox
