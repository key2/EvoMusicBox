// PhaseTimeline.h — |On gift 0 ms| ─────── |Stop 3 000 ms| with a live marker while the trigger is
// active; dragging the Stop marker edits the timer (one undo step on release).
#pragma once

#include "imgui.h"
#include "model/OscActions.h"

namespace evobox
{
namespace ui
{

struct PhaseTimelineState
{
    bool active = false;        // a Timer phase is pending
    double elapsedSec = 0;      // since the trigger (when active)
    double now = 0;
    double lastFire = -1;       // pulse on Start
};

// Draws the timeline for `actions` (Start/Timer/End phases). Returns true when a delay changed.
bool PhaseTimeline(const char* id, OscActions& actions, const PhaseTimelineState& st, ImVec2 size = ImVec2(0, 0));

} // namespace ui
} // namespace evobox
