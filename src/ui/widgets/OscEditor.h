// OscEditor.h — the reusable OSC editing widgets of the Inspector (UI.md §12-§22):
//   OscTargetsSection : the address book (default radio · name · host · edit popup)
//   OscPhaseSection   : header `name · delay/timer field (ms) · + Add OSC command` + rows
//   OscCommandRow     : text field · TargetCombo · Test ▶ · ×
// They take an OscPhase& / OscTargetManager& (+ an optional context for feedback & test sends),
// nothing else, so the Inspector can render any triggerable.
#pragma once

#include "model/OscActions.h"
#include "model/OscTarget.h"
#include "model/Project.h"
#include "model/Triggerable.h"

namespace evobox
{

class TriggerController;

struct OscEditorContext
{
    Project* project = nullptr;
    Triggerable* owner = nullptr;
    TriggerController* trigger = nullptr; // for Test buttons and pulses (may be null in tests)
    double now = 0;
    bool allowNegativeDelay = false;
};

namespace ui
{

// Returns true when the value changed. Undoable (coalesced while typing).
bool DelayField(const char* id, organic::Parameter& delayP, bool isTimer, bool allowNegative = false, float width = 0.f);
// Target combo listing the saved targets; 0 = "Localhost (default)". Returns true when changed.
bool TargetCombo(const char* id, Uid& targetUid, OscTargetManager& targets, float width, std::function<void(Uid)> onChange);
// One command row. Returns true when the row asked to be removed.
bool OscCommandRow(OscCommand& cmd, OscPhase& phase, OscTargetManager& targets, const OscEditorContext& ctx);
void OscPhaseSection(OscPhase& phase, OscTargetManager& targets, const OscEditorContext& ctx);
void OscTargetsSection(OscTargetManager& targets, Project& project);
// Sound / GiftAction / RoomEventAction share this for the OSC part.
void DrawOscActionsEditor(Triggerable& t, const OscEditorContext& ctx);

} // namespace ui
} // namespace evobox
