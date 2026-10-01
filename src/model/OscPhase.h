// OscPhase.h — a named group of OSC commands fired at an anchor + delay.
//   Start : t0 + delay            (tile pressed / gift received / room event)
//   End   : tEnd + delay          (playback finished or stopped)
//   Timer : t0 + delay, restartable/cancellable while pending (the gift "Stop" timer)
#pragma once

#include "model/ModelCommon.h"
#include "model/OscCommand.h"

namespace evobox
{

enum class Anchor { Start, End, Timer };
const char* anchorName(Anchor a);           // "start" / "end" / "timer" (persistence)
bool anchorFromName(const std::string& s, Anchor& out);

class OscPhase : public organic::Container
{
public:
    OscPhase(const std::string& name, Anchor anchor, int delayMs, organic::Container* parent = nullptr);

    Anchor anchor = Anchor::Start;
    organic::Parameter* delayP = nullptr; // ms; Start/End: offset from the anchor; Timer: duration
    OscCommandManager commands;

    int  delayMs() const { return delayP->intValue(); }
    void setDelayMsUndoable(int ms) { delayP->setUndoable(ms); }
    bool hasCommands() const { return !commands.items.empty(); }
    size_t enabledCommandCount() const { return commands.enabledCount(); }
    bool isTimer() const { return anchor == Anchor::Timer; }

    json save() const override;
    void load(const json& j) override;
};

} // namespace evobox
