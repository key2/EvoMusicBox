#include "model/Settings.h"

namespace evobox
{

Settings::Settings(organic::Container* parent) : organic::Container("Settings", parent)
{
    renamable = false;
    playbackPolicyP   = addEnum("Playback policy", { "Music stops previous music", "Overlap everything", "Stop others" }, 0,
                                "Music stops previous music: one music at a time, effects play on top and stack.\n"
                                "Overlap everything: every trigger starts a new voice.\n"
                                "Stop others: any trigger stops every other sound");
    masterVolumeP     = addFloat("Master volume", 0.9f, 0.f, 1.f, "Output volume");
    copyMediaP        = addBool("Copy media into project", false, "Copy imported source files into <project>/media");
    autosaveIntervalP = addInt("Autosave interval", 60, 0, 3600, "Seconds between autosaves (0 = off)");
    autosaveIntervalP->unit = "s";
    oscDefaultPortP   = addInt("OSC default port", 8000, 1, 65535, "Port used when a target has none");
    afterPlayOnStopP  = addBool("After play on manual stop", true, "Fire the After play commands when a sound is stopped by hand");
    stopAllFadeMsP    = addInt("Stop all fade", 20, 0, 2000, "Fade-out applied by Stop All (ms)");
    stopAllFadeMsP->unit = "ms";
}

} // namespace evobox
