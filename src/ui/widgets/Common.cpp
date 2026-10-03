#include "ui/widgets/Common.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace evobox
{
namespace ui
{

const char* liveStateLabel(LiveState s)
{
    switch (s)
    {
    case LiveState::Disconnected: return TR("live.state.disconnected");
    case LiveState::Connecting:   return TR("live.state.connecting");
    case LiveState::Connected:    return TR("live.state.connected");
    case LiveState::Ended:        return TR("gift.streamEnded");
    case LiveState::Error:        return TR("live.state.error");
    }
    return TR("live.state.disconnected");
}

float pulse(double startTime, double now, double duration)
{
    if (startTime < 0 || duration <= 0) return 0.f;
    double t = (now - startTime) / duration;
    if (t < 0 || t > 1) return 0.f;
    return (float)(1.0 - t);
}

bool IconButton(const char* icon, const char* tooltip, ImVec2 size, bool enabled)
{
    if (!enabled) ImGui::BeginDisabled();
    bool r = ImGui::Button(icon, size);
    if (!enabled) ImGui::EndDisabled();
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tooltip);
    return r;
}

bool AccentButton(const char* label, ImVec2 size)
{
    const auto& c = theme::colors();
    ImGui::PushStyleColor(ImGuiCol_Button, theme::withAlpha(c.accent, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::lighten(c.accent, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

bool DangerButton(const char* label, ImVec2 size)
{
    const auto& c = theme::colors();
    ImGui::PushStyleColor(ImGuiCol_Button, theme::withAlpha(c.danger, 0.75f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c.danger);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::lighten(c.danger, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

bool GhostButton(const char* label, ImVec2 size)
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.14f));
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return r;
}

bool ToggleButton(const char* label, bool active, ImVec2 size, const char* tooltip)
{
    const auto& c = theme::colors();
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::withAlpha(c.accent, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::withAlpha(c.accent, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, c.accent);
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.05f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.10f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.16f));
    }
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tooltip);
    return r;
}

void Badge(const std::string& text, const ImVec4& bg, const ImVec4* fg)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    ImVec2 pad(6.f * theme::scale(), 2.f * theme::scale());
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(ts.x + pad.x * 2, ts.y + pad.y * 2);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), theme::u32(bg), size.y * 0.5f);
    ImVec4 textCol = fg ? *fg : theme::contrastText(bg);
    dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), theme::u32(textCol), text.c_str());
    ImGui::Dummy(size);
}

void Dot(const ImVec4& color, float radius)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    dl->AddCircleFilled(ImVec2(p.x + radius, p.y + h * 0.5f), radius, theme::u32(color));
    ImGui::Dummy(ImVec2(radius * 2 + 4, h));
}

void EmptyState(const char* title, const char* subtitle, const char* icon)
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float lines = (icon ? 3.f : 0.f) + 1.5f + (subtitle ? 1.5f : 0.f);
    float h = ImGui::GetTextLineHeightWithSpacing() * lines;
    float y = std::max(0.f, (avail.y - h) * 0.4f);
    ImGui::Dummy(ImVec2(0, y));
    const auto& c = theme::colors();
    if (icon)
    {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.6f);
        ImGui::PushStyleColor(ImGuiCol_Text, c.textFaint);
        TextCentered(icon);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4));
    }
    ImGui::PushStyleColor(ImGuiCol_Text, c.textDim);
    TextCentered(title);
    ImGui::PopStyleColor();
    if (subtitle)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, c.textFaint);
        TextCentered(subtitle);
        ImGui::PopStyleColor();
    }
}

void SectionHeader(const char* label, const char* rightText)
{
    const auto& c = theme::colors();
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushStyleColor(ImGuiCol_Text, c.textDim);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
    std::string up;
    for (const char* p = label; *p; p++) up.push_back((char)toupper((unsigned char)*p));
    ImGui::TextUnformatted(up.c_str());
    if (rightText)
    {
        float w = ImGui::CalcTextSize(rightText).x;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - w);
        ImGui::TextUnformatted(rightText);
    }
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::Separator();
}

void HelpMarker(const char* text)
{
    ImGui::TextDisabled(ICON_PH_QUESTION);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void TextDim(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::colors().textDim);
    ImGui::TextV(fmt, ap);
    ImGui::PopStyleColor();
    va_end(ap);
}

void TextCentered(const char* text)
{
    float w = ImGui::CalcTextSize(text).x;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - w) * 0.5f);
    ImGui::TextUnformatted(text);
}

void Spacer(float h) { ImGui::Dummy(ImVec2(0, h * theme::scale())); }

void ConfirmPopup::open(const std::string& t, const std::string& msg, std::function<void()> fn, const std::string& label)
{
    title = t;
    message = msg;
    onConfirm = std::move(fn);
    confirmLabel = label;
    wantOpen = true;
}

void ConfirmPopup::draw()
{
    if (wantOpen)
    {
        ImGui::OpenPopup(id.c_str());
        wantOpen = false;
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f);
        ImGui::TextUnformatted(title.c_str());
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.f);
        ImGui::TextUnformatted(message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::Spacing();
        float bw = 110.f * theme::scale();
        if (ImGui::Button(TR("dialog.cancel"), ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        bool ok = danger ? DangerButton(confirmLabel.c_str(), ImVec2(bw, 0)) : AccentButton(confirmLabel.c_str(), ImVec2(bw, 0));
        if (ok || ImGui::IsKeyPressed(ImGuiKey_Enter))
        {
            if (onConfirm) onConfirm();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void RoundedPanel(ImVec2 mn, ImVec2 mx, const ImVec4& bg, const ImVec4* border, float rounding)
{
    if (rounding < 0) rounding = theme::rounding();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(mn, mx, theme::u32(bg), rounding);
    if (border) dl->AddRect(mn, mx, theme::u32(*border), rounding);
}

std::string ellipsize(const std::string& text, float maxWidth)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
    std::string s = text;
    const char* ell = "...";
    float ellW = ImGui::CalcTextSize(ell).x;
    while (!s.empty() && ImGui::CalcTextSize(s.c_str()).x + ellW > maxWidth)
    {
        // pop one UTF-8 codepoint: continuation bytes (10xxxxxx) first, then the lead byte
        while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();
        if (!s.empty()) s.pop_back();
    }
    return s + ell;
}

} // namespace ui
} // namespace evobox
