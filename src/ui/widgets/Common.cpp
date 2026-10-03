#include "ui/widgets/Common.h"
#include "OrganicCore.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "util/Strings.h"
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

std::string phaseLabel(const OscPhase& phase)
{
    // the names are the fixed ones given by Sound / GiftAction / RoomEventAction (also used to match
    // phases when a show is loaded or merged, hence never translated in the model)
    static const struct { const char* name; const char* key; } kNames[] = {
        { "At play (start)", "phase.name.atPlay" },
        { "After play",      "phase.name.afterPlay" },
        { "On gift",         "phase.name.onGift" },
        { "On event",        "phase.name.onEvent" },
        { "Stop",            "phase.name.stop" },
    };
    for (auto& n : kNames)
        if (phase.niceName == n.name) return TR(n.key);
    return phase.niceName;
}

const char* roomEventLabel(RoomEventKind kind)
{
    switch (kind)
    {
    case RoomEventKind::Like:      return TR("roomEvent.like");
    case RoomEventKind::Follow:    return TR("roomEvent.follow");
    case RoomEventKind::Share:     return TR("roomEvent.share");
    case RoomEventKind::Subscribe: return TR("roomEvent.subscribe");
    case RoomEventKind::Join:      return TR("roomEvent.join");
    default:                       return roomEventKindName(kind);
    }
}

std::string agoLabel(double s)
{
    if (s < 5) return TR("time.justNow");
    if (s < 60) return str::format(TR("time.secondsAgo"), (int)s);
    if (s < 3600) return str::format(TR("time.minutesAgo"), (int)(s / 60));
    if (s < 86400) return str::format(TR("time.hoursAgo"), (int)(s / 3600));
    return str::format(TR("time.daysAgo"), (int)(s / 86400));
}

std::string liveEventLabel(const LiveEvent& e)
{
    // the simulated user is ours: name it in the UI language
    std::string who = e.user.uniqueId == "simulate" ? std::string(TR("live.feed.simulator"))
                    : (e.user.nickname.empty() ? (e.user.uniqueId.empty() ? std::string(TR("live.feed.someone")) : "@" + e.user.uniqueId)
                                               : e.user.nickname);
    switch (e.type)
    {
    case LiveEventType::Gift:
    {
        std::string gift = e.giftName.empty() ? str::format(TR("live.feed.giftNumber"), (long long)e.giftId) : e.giftName;
        std::string s = str::format(TR("live.feed.sentGift"), who.c_str(), gift.c_str());
        if (e.repeatCount > 1) s += " x" + std::to_string(e.repeatCount);
        if (e.giftStreaking) s += std::string(" ") + TR("live.feed.streaking");
        if (e.diamondCount) s += "  " + str::format(TR("live.feed.diamonds"), e.diamondCount * (e.repeatCount > 0 ? e.repeatCount : 1));
        return s;
    }
    case LiveEventType::Comment:     return who + ": " + e.comment;
    case LiveEventType::Like:
    {
        std::string s = str::format(TR("live.feed.liked"), who.c_str(), e.likeCount);
        if (e.totalLikes) s += "  " + str::format(TR("live.feed.likeTotal"), str::groupThousands(e.totalLikes).c_str());
        return s;
    }
    case LiveEventType::Join:        return str::format(TR("live.feed.joined"), who.c_str());
    case LiveEventType::Follow:      return str::format(TR("live.feed.followed"), who.c_str());
    case LiveEventType::Share:       return str::format(TR("live.feed.shared"), who.c_str());
    case LiveEventType::Subscribe:   return str::format(TR("live.feed.subscribed"), who.c_str());
    case LiveEventType::RoomUserSeq: return str::format(TR("live.feed.viewers"), str::groupThousands(e.viewerCount).c_str());
    case LiveEventType::Connect:     return std::string(TR("live.feed.connected")) + (e.message.empty() ? "" : " " + e.message);
    case LiveEventType::Disconnect:  return TR("live.feed.disconnected");
    case LiveEventType::LiveEnd:     return TR("live.feed.streamEnded");
    case LiveEventType::Control:     return str::format(TR("live.feed.control"), e.controlAction) + (e.message.empty() ? "" : " " + e.message);
    case LiveEventType::Error:       return str::format(TR("live.feed.error"), e.message.c_str());
    case LiveEventType::Info:        return e.message;
    case LiveEventType::Unknown:     return e.method.empty() ? std::string(TR("live.feed.unknown")) : e.method;
    }
    return e.method;
}

std::string oscErrorLabel(const std::string& err)
{
    static const struct { const char* text; const char* key; } kErrors[] = {
        { "empty command",                 "osc.error.empty" },
        { "address must start with '/'",   "osc.error.addressStart" },
        { "invalid character in address",  "osc.error.addressChar" },
        { "address must not end with '/'", "osc.error.addressEnd" },
        { "unterminated quote",            "osc.error.quote" },
    };
    for (auto& k : kErrors)
        if (err == k.text) return TR(k.key);
    return err;
}

void LocalizeParam(organic::Parameter* p, const char* labelKey, const char* descKey, const char* enumKeyPrefix)
{
    if (!p) return;
    p->displayName = TR(labelKey);
    if (descKey) p->displayDescription = TR(descKey);
    if (enumKeyPrefix)
    {
        p->enumLabels.resize(p->enumOptions.size());
        for (size_t i = 0; i < p->enumOptions.size(); i++)
            p->enumLabels[i] = TR((std::string(enumKeyPrefix) + "." + std::to_string(i)).c_str());
    }
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
    // upper-case ASCII and Cyrillic (U+0430..U+044F -> U+0410..U+042F, ё -> Ё); CJK has no case
    std::string up;
    for (const unsigned char* p = (const unsigned char*)label; *p;)
    {
        if (*p == 0xD0 && p[1] >= 0xB0 && p[1] <= 0xBF) { up.push_back((char)0xD0); up.push_back((char)(p[1] - 0x20)); p += 2; }      // а..п
        else if (*p == 0xD1 && p[1] >= 0x80 && p[1] <= 0x8F) { up.push_back((char)0xD0); up.push_back((char)(p[1] + 0x20)); p += 2; } // р..я
        else if (*p == 0xD1 && p[1] == 0x91) { up.push_back((char)0xD0); up.push_back((char)0x81); p += 2; }                           // ё
        else if (*p < 0x80) { up.push_back((char)toupper(*p)); p++; }
        else { up.push_back((char)*p); p++; }
    }
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
        const char* okLabel = confirmLabel.empty() ? TR("dialog.delete") : confirmLabel.c_str();
        bool ok = danger ? DangerButton(okLabel, ImVec2(bw, 0)) : AccentButton(okLabel, ImVec2(bw, 0));
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
