// Settings.h — show-specific settings saved in the project (machine-specific ones live in Prefs).
#pragma once

#include "model/ModelCommon.h"

namespace evobox
{

// MusicExclusive (default): starting a music stops the music that was playing; effects (Sound::
// isEffect) always play on top and stack. Overlap: everything overlaps. StopOthers: any trigger
// stops every other sound. Stored as the enum index (project format v2 remapped the v1 values).
enum class PlaybackPolicy { MusicExclusive = 0, Overlap = 1, StopOthers = 2 };

class Settings : public organic::Container
{
public:
    explicit Settings(organic::Container* parent = nullptr);

    organic::Parameter* playbackPolicyP = nullptr;    // Overlap / Stop others
    organic::Parameter* masterVolumeP = nullptr;      // 0..1
    organic::Parameter* copyMediaP = nullptr;         // copy imported sources into <bundle>/media
    organic::Parameter* autosaveIntervalP = nullptr;  // seconds, 0 = off
    organic::Parameter* oscDefaultPortP = nullptr;    // used when a target leaves the port empty
    organic::Parameter* afterPlayOnStopP = nullptr;   // fire "After play" on manual stop too
    organic::Parameter* stopAllFadeMsP = nullptr;     // Esc / Stop All fade-out

    PlaybackPolicy playbackPolicy() const { return (PlaybackPolicy)playbackPolicyP->intValue(); }
    float masterVolume() const { return masterVolumeP->floatValue(); }
    bool  copyMediaIntoProject() const { return copyMediaP->boolValue(); }
    int   autosaveIntervalSec() const { return autosaveIntervalP->intValue(); }
    int   oscDefaultPort() const { return oscDefaultPortP->intValue(); }
    bool  afterPlayOnStop() const { return afterPlayOnStopP->boolValue(); }
    int   stopAllFadeMs() const { return stopAllFadeMsP->intValue(); }
};

} // namespace evobox
