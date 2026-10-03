// Common.h — small themed building blocks shared by panels and widgets: pulses, icon buttons,
// badges, empty states, section headers, confirmation popups.
#pragma once

#include <functional>
#include <string>
#include "imgui.h"
#include "live/LiveEvent.h"
#include "live/LiveEventRouter.h"
#include "model/OscPhase.h"
#include "model/RoomEventAction.h"
#include "ui/Theme.h"

namespace organic { struct Parameter; }

namespace evobox
{
namespace ui
{

// Localized name of a live-connection state for the UI (status bar, gift gallery, live monitor).
// liveStateName() in the model stays English for logs/serialization; this is the translated label.
const char* liveStateLabel(LiveState s);

// Display labels for model identities that stay English on disk (save-file / merge keys):
// the fixed phase names ("At play (start)", "On gift", "Stop", ...) and the room-event kinds.
// Unknown names come back unchanged.
std::string phaseLabel(const OscPhase& phase);
const char* roomEventLabel(RoomEventKind kind);
// Relative time ("just now", "3 min ago") in the UI language.
std::string agoLabel(double seconds);
// A live-feed line ("Simulator sent Galaxy  1000 diamonds") in the UI language; LiveEvent::summary()
// stays English for the logger.
std::string liveEventLabel(const LiveEvent& e);
// OSC command parser errors ("empty command", ...) in the UI language; unknown texts unchanged.
std::string oscErrorLabel(const std::string& parserError);

// Points an organic Parameter's display name / tooltip / enum option labels at catalogue keys
// ("<key>", "<key>.desc", "<key>.<index>"); the JSON short names are untouched. Call it before
// drawing: the overrides are plain strings, so a language switch is picked up on the next frame.
void LocalizeParam(organic::Parameter* p, const char* labelKey, const char* descKey = nullptr,
                   const char* enumKeyPrefix = nullptr);

// 1 -> 0 over `duration` seconds after `startTime`; 0 when idle (startTime < 0).
float pulse(double startTime, double now, double duration = 0.3);

bool IconButton(const char* icon, const char* tooltip = nullptr, ImVec2 size = ImVec2(0, 0), bool enabled = true);
bool AccentButton(const char* label, ImVec2 size = ImVec2(0, 0));          // primary action (Save Clip)
bool DangerButton(const char* label, ImVec2 size = ImVec2(0, 0));
bool GhostButton(const char* label, ImVec2 size = ImVec2(0, 0));           // transparent bg, border on hover
bool ToggleButton(const char* label, bool active, ImVec2 size = ImVec2(0, 0), const char* tooltip = nullptr);
void Badge(const std::string& text, const ImVec4& bg, const ImVec4* fg = nullptr);
void Dot(const ImVec4& color, float radius = 4.f);
void EmptyState(const char* title, const char* subtitle = nullptr, const char* icon = nullptr);
void SectionHeader(const char* label, const char* rightText = nullptr);
void HelpMarker(const char* text);
void TextDim(const char* fmt, ...);
void TextCentered(const char* text);
void Spacer(float h = 6.f);

// Small confirmation modal. Call `open()` to request; returns true on confirm during the frame.
struct ConfirmPopup
{
    ConfirmPopup() = default;
    explicit ConfirmPopup(const char* popupId) : id(popupId) {}
    std::string id = "Confirm";
    std::string title;
    std::string message;
    std::string confirmLabel;            // empty = TR("dialog.delete")
    bool danger = true;
    std::function<void()> onConfirm;
    bool wantOpen = false;
    // `label` empty: the translated "Delete" (the common case) is resolved when the popup draws
    void open(const std::string& t, const std::string& msg, std::function<void()> fn, const std::string& label = std::string());
    void draw();
};

// Draws a rounded rect + optional border on the current window draw list.
void RoundedPanel(ImVec2 mn, ImVec2 mx, const ImVec4& bg, const ImVec4* border = nullptr, float rounding = -1.f);

// A rectangle of `size` at the cursor, returns its screen rect.
struct Rect { ImVec2 mn, mx; bool contains(ImVec2 p) const { return p.x >= mn.x && p.x < mx.x && p.y >= mn.y && p.y < mx.y; } ImVec2 size() const { return ImVec2(mx.x - mn.x, mx.y - mn.y); } };

// Clips text to a width with an ellipsis.
std::string ellipsize(const std::string& text, float maxWidth);

} // namespace ui
} // namespace evobox
