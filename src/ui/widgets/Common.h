// Common.h — small themed building blocks shared by panels and widgets: pulses, icon buttons,
// badges, empty states, section headers, confirmation popups.
#pragma once

#include <functional>
#include <string>
#include "imgui.h"
#include "ui/Theme.h"

namespace evobox
{
namespace ui
{

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
    std::string confirmLabel = "Delete";
    bool danger = true;
    std::function<void()> onConfirm;
    bool wantOpen = false;
    void open(const std::string& t, const std::string& msg, std::function<void()> fn, const std::string& label = "Delete");
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
