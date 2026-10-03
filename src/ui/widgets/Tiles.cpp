#include "ui/widgets/Tiles.h"
#include "ui/I18n.h"
#include "ui/Fonts.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"
#include "ui/widgets/StickerPicker.h"
#include "util/Strings.h"
#include "util/TimeFormat.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace evobox
{
namespace ui
{

ImVec2 tileSizeFor(TileSize s)
{
    float k = theme::scale();
    switch (s)
    {
    case TileSize::Compact:  return ImVec2(112 * k, 96 * k);
    case TileSize::Large:    return ImVec2(200 * k, 168 * k);
    default:                 return ImVec2(150 * k, 128 * k);
    }
}

float tileStickerScale(TileSize s)
{
    switch (s)
    {
    case TileSize::Compact: return 1.8f;
    case TileSize::Large:   return 3.4f;
    default:                return 2.6f;
    }
}

// Glyph stickers are centred text; picture stickers fill a square of `pictureEdge` pixels.
static void drawSticker(ImDrawList* dl, const std::string& sticker, ImVec2 center, float sizeMul, ImU32 color, float pictureEdge)
{
    float fs = ImGui::GetFontSize() * sizeMul;
    float half = pictureEdge * 0.5f;
    DrawStickerInRect(dl, sticker, ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half), color, fs, 6.f);
}

TileAction SoundTile(Sound& s, ImVec2 size, const SoundTileState& st, Uid* outDropUid, bool* outDropAfter)
{
    const auto& c = theme::colors();
    TileAction action = TileAction::None;
    ImGui::PushID((void*)&s);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImGui::InvisibleButton("##tile", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();

    // drag source (reorder / move to category)
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover))
    {
        Uid uid = s.uid;
        ImGui::SetDragDropPayload("EVOBOX_SOUND", &uid, sizeof(uid));
        ImGui::TextUnformatted((icons::stickerText(s.sticker()) + "  " + s.niceName).c_str());
        ImGui::EndDragDropSource();
    }
    // drop target (reorder)
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("EVOBOX_SOUND", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
        {
            Uid dragged = *(const Uid*)pl->Data;
            bool after = ImGui::GetIO().MousePos.x > (p0.x + p1.x) * 0.5f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float x = after ? p1.x + 3 : p0.x - 3;
            if (dragged != s.uid) dl->AddRectFilled(ImVec2(x - 2, p0.y), ImVec2(x + 2, p1.y), theme::u32(c.accent), 2.f);
            if (pl->IsDelivery() && dragged != s.uid)
            {
                if (outDropUid) *outDropUid = dragged;
                if (outDropAfter) *outDropAfter = after;
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = theme::rounding();
    ImVec4 tileColor = s.color();
    bool dim = st.missingMedia;

    // background
    ImVec4 bg = hovered ? c.tileBgHover : c.tileBg;
    if (dim) bg = theme::mix(bg, c.windowBg, 0.5f);
    dl->AddRectFilled(p0, p1, theme::u32(bg), r);
    // colour tint band at the top
    dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + 4 * theme::scale()), theme::u32(tileColor, dim ? 0.35f : 0.9f), r, ImDrawFlags_RoundCornersTop);
    // subtle colour glow in the body
    dl->AddRectFilledMultiColor(ImVec2(p0.x, p0.y + 4), ImVec2(p1.x, p0.y + size.y * 0.55f),
                                theme::u32(tileColor, 0.12f), theme::u32(tileColor, 0.12f), theme::u32(tileColor, 0.f), theme::u32(tileColor, 0.f));

    // playing state: animated ring. Progress (0 = trim start, 1 = trim end: the clip IS the selected
    // region) is a bar inside the tile for effects (latest voice of the stack) and a thin line under
    // the box for music (drawn last, see below)
    bool innerBar = st.playing && st.effect;
    if (st.playing)
    {
        float t = (float)st.now;
        float glow = 0.55f + 0.25f * std::sin(t * 6.f);
        dl->AddRect(ImVec2(p0.x - 1, p0.y - 1), ImVec2(p1.x + 1, p1.y + 1), theme::u32(c.playing, glow), r + 1, 0, 2.f);
    }
    if (innerBar)
    {
        float pw = (p1.x - p0.x - 12) * std::clamp(st.progress01, 0.f, 1.f);
        dl->AddRectFilled(ImVec2(p0.x + 6, p1.y - 6), ImVec2(p1.x - 6, p1.y - 3), theme::u32(ImVec4(1, 1, 1, 0.08f)), 2.f);
        dl->AddRectFilled(ImVec2(p0.x + 6, p1.y - 6), ImVec2(p0.x + 6 + pw, p1.y - 3), theme::u32(c.playing), 2.f);
    }
    // trigger flash
    float flash = pulse(st.lastTrigger, st.now, 0.25);
    if (flash > 0) dl->AddRectFilled(p0, p1, theme::u32(ImVec4(1, 1, 1, 0.18f * flash)), r);
    // selection glow
    if (st.selected)
    {
        dl->AddRect(ImVec2(p0.x - 2, p0.y - 2), ImVec2(p1.x + 2, p1.y + 2), theme::u32(c.accent, 0.35f), r + 2, 0, 4.f);
        dl->AddRect(p0, p1, theme::u32(c.accent), r, 0, 2.f);
    }
    else dl->AddRect(p0, p1, theme::u32(hovered ? theme::withAlpha(c.text, 0.18f) : c.tileBorder), r, 0, 1.f);

    // sticker (glyph tinted with the tile colour, or the tile picture)
    float stickerMul = size.y >= 160 ? 3.4f : (size.y <= 100 ? 1.8f : 2.6f);
    ImVec2 center(p0.x + size.x * 0.5f, p0.y + size.y * 0.42f);
    ImVec4 stickerCol = dim ? c.textFaint : theme::lighten(tileColor, 0.25f);
    float pictureEdge = std::min(size.x - 24.f, size.y * 0.58f);
    drawSticker(dl, s.sticker(), center, stickerMul, theme::u32(stickerCol), pictureEdge);

    // missing media / decoding glyph
    float cornerX = p0.x + 8;
    if (st.missingMedia)
    {
        dl->AddText(ImVec2(cornerX, p0.y + 8), theme::u32(c.warning), ICON_PH_WARNING);
        cornerX += ImGui::CalcTextSize(ICON_PH_WARNING).x + 4;
    }
    else if (st.decoding)
    {
        float a = (float)std::fmod(st.now * 2.0, 1.0);
        dl->AddText(ImVec2(cornerX, p0.y + 8), theme::u32(c.textDim, 0.5f + 0.5f * a), ICON_PH_CIRCLE_NOTCH);
        cornerX += ImGui::CalcTextSize(ICON_PH_CIRCLE_NOTCH).x + 4;
    }
    // effect marker (stackable sound) + stacked voice count
    if (st.effect)
    {
        std::string mark = ICON_PH_SPARKLE;
        if (st.voices > 1) mark += " x" + std::to_string(st.voices);
        dl->AddText(ImVec2(cornerX, p0.y + 8), theme::u32(st.voices > 1 ? c.playing : theme::withAlpha(tileColor, 0.9f)), mark.c_str());
    }
    // OSC activity dot
    if (st.oscCommandCount > 0)
    {
        float op = pulse(st.lastOscPulse, st.now, 0.4);
        ImVec4 dotCol = op > 0 ? theme::mix(c.textFaint, c.accent, op) : c.textFaint;
        float rad = 3.f + 2.f * op;
        dl->AddCircleFilled(ImVec2(p1.x - 12, p0.y + 13), rad, theme::u32(dotCol));
    }

    // name + duration
    float pad = 8 * theme::scale();
    float nameY = p1.y - ImGui::GetTextLineHeight() * 2.f - pad - (innerBar ? 4 : 0);
    std::string name = ellipsize(s.niceName, size.x - pad * 2);
    ImVec2 nts = ImGui::CalcTextSize(name.c_str());
    dl->AddText(ImVec2(p0.x + (size.x - nts.x) * 0.5f, nameY), theme::u32(dim ? c.textDim : c.text), name.c_str());
    std::string dur = formatTileDuration(s.clipDuration());
    ImVec2 dts = ImGui::CalcTextSize(dur.c_str());
    dl->AddText(ImVec2(p0.x + (size.x - dts.x) * 0.5f, nameY + ImGui::GetTextLineHeight()), theme::u32(c.textDim), dur.c_str());

    // play button (explicit, visible): bottom-right circle
    float pr = std::max(11.f, size.y * 0.11f);
    ImVec2 pc(p1.x - pr - 8, p1.y - pr - 8 - (innerBar ? 4 : 0));
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool playHover = hovered && std::hypot(mouse.x - pc.x, mouse.y - pc.y) <= pr + 2;
    bool showPlay = hovered || st.selected || st.playing || st.performanceMode;
    // a playing effect keeps its play glyph: pressing again stacks another voice ...
    bool stopGlyph = st.playing && !st.effect;
    // ... and gets a separate stop circle (bottom-left, mirroring play) that stops every voice of
    // the sound; the corner keeps it clear of the centred name / duration on small tiles
    bool showStop = st.playing && st.effect;
    ImVec2 sc(p0.x + pr + 8, pc.y);
    bool stopHover = showStop && hovered && std::hypot(mouse.x - sc.x, mouse.y - sc.y) <= pr + 2;
    ImFont* f = Fonts::ui;
    float fs = pr * 1.1f;
    if (showPlay)
    {
        ImVec4 pbg = st.playing ? c.playing : (playHover ? c.accent : theme::withAlpha(c.accent, 0.75f));
        dl->AddCircleFilled(pc, pr, theme::u32(pbg));
        const char* glyph = stopGlyph ? ICON_PH_STOP : ICON_PH_PLAY;
        ImVec2 gs = f->CalcTextSizeA(fs, FLT_MAX, 0.f, glyph);
        dl->AddText(f, fs, ImVec2(pc.x - gs.x * 0.5f + (stopGlyph ? 0 : 1), pc.y - gs.y * 0.5f), theme::u32(ImVec4(1, 1, 1, 1)), glyph);
    }
    if (showStop)
    {
        ImVec4 sbg = stopHover ? c.playing : theme::mix(c.tileBgHover, c.playing, 0.35f);
        dl->AddCircleFilled(sc, pr, theme::u32(sbg));
        dl->AddCircle(sc, pr, theme::u32(c.playing, stopHover ? 1.f : 0.7f), 0, 1.5f);
        ImVec2 gs = f->CalcTextSizeA(fs, FLT_MAX, 0.f, ICON_PH_STOP);
        dl->AddText(f, fs, ImVec2(sc.x - gs.x * 0.5f, sc.y - gs.y * 0.5f), theme::u32(ImVec4(1, 1, 1, 1)), ICON_PH_STOP);
    }

    // music: a very thin progress line right under the box (in the row gap), full tile width,
    // covering the selected region of the sound only
    if (st.playing && !st.effect)
    {
        float th = std::max(1.5f, 2.f * theme::scale());
        float y0 = p1.y + 2.f, y1 = y0 + th;
        float pw = size.x * std::clamp(st.progress01, 0.f, 1.f);
        dl->AddRectFilled(ImVec2(p0.x, y0), ImVec2(p1.x, y1), theme::u32(c.playing, 0.18f), th * 0.5f);
        if (pw > 0) dl->AddRectFilled(ImVec2(p0.x, y0), ImVec2(p0.x + pw, y1), theme::u32(c.playing), th * 0.5f);
    }

    // interaction (UI.md §9: body click = select; play button = play; performance mode: body = play;
    // the effect's stop circle always stops, also in performance mode)
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) action = TileAction::ContextMenu;
    else if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !playHover && !stopHover) action = TileAction::DoubleClick;
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        if (stopHover) action = TileAction::Stop;
        else if (playHover || st.performanceMode) action = stopGlyph && playHover ? TileAction::Stop : TileAction::Play;
        else action = TileAction::Select;
    }
    (void)held;
    ImGui::PopID();
    return action;
}

bool AddSoundTile(ImVec2 size, bool highlight)
{
    const auto& c = theme::colors();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImGui::InvisibleButton("##addtile", size);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = theme::rounding();
    dl->AddRectFilled(p0, p1, theme::u32(hovered || highlight ? theme::withAlpha(c.accent, 0.10f) : ImVec4(1, 1, 1, 0.025f)), r);
    // dashed border
    ImU32 bc = theme::u32(hovered || highlight ? c.accent : c.textFaint, 0.8f);
    float dash = 6.f, gap = 5.f;
    auto dashLine = [&](ImVec2 a, ImVec2 b)
    {
        float len = std::hypot(b.x - a.x, b.y - a.y);
        ImVec2 d((b.x - a.x) / len, (b.y - a.y) / len);
        for (float t = 0; t < len; t += dash + gap)
        {
            float e = std::min(len, t + dash);
            dl->AddLine(ImVec2(a.x + d.x * t, a.y + d.y * t), ImVec2(a.x + d.x * e, a.y + d.y * e), bc, 1.f);
        }
    };
    float in = 6.f;
    dashLine(ImVec2(p0.x + in + r * 0.5f, p0.y + in), ImVec2(p1.x - in - r * 0.5f, p0.y + in));
    dashLine(ImVec2(p1.x - in, p0.y + in + r * 0.5f), ImVec2(p1.x - in, p1.y - in - r * 0.5f));
    dashLine(ImVec2(p1.x - in - r * 0.5f, p1.y - in), ImVec2(p0.x + in + r * 0.5f, p1.y - in));
    dashLine(ImVec2(p0.x + in, p1.y - in - r * 0.5f), ImVec2(p0.x + in, p0.y + in + r * 0.5f));

    ImVec4 tc = hovered || highlight ? c.accent : c.textDim;
    float fs = ImGui::GetFontSize() * 2.2f;
    ImVec2 gs = Fonts::ui->CalcTextSizeA(fs, FLT_MAX, 0, ICON_PH_PLUS);
    dl->AddText(Fonts::ui, fs, ImVec2(p0.x + (size.x - gs.x) * 0.5f, p0.y + size.y * 0.36f - gs.y * 0.5f), theme::u32(tc), ICON_PH_PLUS);
    const char* l1 = TR("tiles.addSound");
    const char* l2 = TR("tiles.orDragDrop");
    ImVec2 s1 = ImGui::CalcTextSize(l1), s2 = ImGui::CalcTextSize(l2);
    float y = p1.y - ImGui::GetTextLineHeight() * 2.f - 8;
    dl->AddText(ImVec2(p0.x + (size.x - s1.x) * 0.5f, y), theme::u32(tc), l1);
    dl->AddText(ImVec2(p0.x + (size.x - s2.x) * 0.5f, y + ImGui::GetTextLineHeight()), theme::u32(c.textFaint), l2);
    return ImGui::IsItemClicked(ImGuiMouseButton_Left);
}

TileAction GiftTile(GiftInfo& g, ImVec2 size, const GiftTileState& st)
{
    const auto& c = theme::colors();
    TileAction action = TileAction::None;
    ImGui::PushID((int)g.id);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImGui::InvisibleButton("##gift", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = theme::rounding();
    bool configured = st.configuredCommands > 0 || st.hasSound;

    ImVec4 bg = hovered ? c.tileBgHover : c.tileBg;
    dl->AddRectFilled(p0, p1, theme::u32(bg), r);
    if (configured)
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + 4 * theme::scale()), theme::u32(c.accent, 0.9f), r, ImDrawFlags_RoundCornersTop);
    // live pulse (received) ~600 ms
    float p = pulse(st.lastPulse, st.now, 0.6);
    if (p > 0) dl->AddRectFilled(p0, p1, theme::u32(c.live, 0.35f * p), r);
    if (st.active)
    {
        float glow = 0.5f + 0.3f * std::sin((float)st.now * 6.f);
        dl->AddRect(ImVec2(p0.x - 1, p0.y - 1), ImVec2(p1.x + 1, p1.y + 1), theme::u32(c.warning, glow), r + 1, 0, 2.f);
    }
    if (st.selected)
    {
        dl->AddRect(ImVec2(p0.x - 2, p0.y - 2), ImVec2(p1.x + 2, p1.y + 2), theme::u32(c.accent, 0.35f), r + 2, 0, 4.f);
        dl->AddRect(p0, p1, theme::u32(c.accent), r, 0, 2.f);
    }
    else dl->AddRect(p0, p1, theme::u32(hovered ? theme::withAlpha(c.text, 0.18f) : c.tileBorder), r);

    // icon (texture or fallback glyph)
    float iconEdge = std::min(size.x, size.y) * 0.42f;
    ImVec2 ic(p0.x + size.x * 0.5f, p0.y + size.y * 0.40f);
    if (g.icon.valid())
    {
        ImVec2 a(ic.x - iconEdge * 0.5f, ic.y - iconEdge * 0.5f), b(ic.x + iconEdge * 0.5f, ic.y + iconEdge * 0.5f);
        dl->AddImageRounded(ImTextureRef((ImTextureID)(intptr_t)g.icon.glId), a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.f);
    }
    else
    {
        ImVec4 col = g.iconState == IconState::Failed ? c.textFaint : c.textDim;
        float fs = iconEdge * 0.9f;
        ImVec2 gs = Fonts::icons->CalcTextSizeA(fs, FLT_MAX, 0, ICON_PH_GIFT);
        dl->AddText(Fonts::icons, fs, ImVec2(ic.x - gs.x * 0.5f, ic.y - gs.y * 0.5f), theme::u32(col), ICON_PH_GIFT);
        if (g.iconState == IconState::Fetching)
            dl->AddText(ImVec2(p0.x + 8, p0.y + 8), theme::u32(c.textFaint), ICON_PH_CIRCLE_NOTCH);
    }
    // streak mark
    if (g.streakable()) dl->AddText(ImVec2(p0.x + 8, p0.y + 8), theme::u32(c.warning, 0.9f), ICON_PH_LIGHTNING);
    // badge: number of OSC commands (+ sound)
    if (configured)
    {
        std::string b = st.configuredCommands > 0 ? std::to_string(st.configuredCommands) : "";
        if (st.hasSound) b += b.empty() ? ICON_PH_SPEAKER_HIGH : " " ICON_PH_SPEAKER_HIGH;
        ImVec2 ts = ImGui::CalcTextSize(b.c_str());
        ImVec2 bp(p1.x - ts.x - 14, p0.y + 7);
        dl->AddRectFilled(ImVec2(bp.x - 5, bp.y - 1), ImVec2(bp.x + ts.x + 5, bp.y + ts.y + 1), theme::u32(c.accent), 8.f);
        dl->AddText(bp, IM_COL32_WHITE, b.c_str());
    }
    // received count (session)
    if (st.receivedCount > 0)
    {
        std::string rc = "x" + std::to_string(st.receivedCount);
        dl->AddText(ImVec2(p0.x + 8, p1.y - ImGui::GetTextLineHeight() - 6), theme::u32(c.live), rc.c_str());
    }
    // name + diamonds
    float pad = 8 * theme::scale();
    float nameY = p1.y - ImGui::GetTextLineHeight() * 2.f - pad;
    std::string giftFallback;
    if (g.name.empty())
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), TR("tiles.giftFallback"), (long long)g.id);
        giftFallback = buf;
    }
    std::string name = ellipsize(g.name.empty() ? giftFallback : g.name, size.x - pad * 2);
    ImVec2 nts = ImGui::CalcTextSize(name.c_str());
    dl->AddText(ImVec2(p0.x + (size.x - nts.x) * 0.5f, nameY), theme::u32(c.text), name.c_str());
    std::string dia = ICON_PH_DIAMOND " " + str::groupThousands(g.diamondCount);
    ImVec2 dts = ImGui::CalcTextSize(dia.c_str());
    dl->AddText(ImVec2(p0.x + (size.x - dts.x) * 0.5f, nameY + ImGui::GetTextLineHeight()), theme::u32(c.diamond), dia.c_str());

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) action = TileAction::ContextMenu;
    else if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) action = TileAction::DoubleClick;
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) action = TileAction::Select;
    ImGui::PopID();
    return action;
}

const char* roomEventIcon(RoomEventKind k)
{
    switch (k)
    {
    case RoomEventKind::Like:      return ICON_PH_HEART;
    case RoomEventKind::Follow:    return ICON_PH_USER_PLUS;
    case RoomEventKind::Share:     return ICON_PH_SHARE_NETWORK;
    case RoomEventKind::Subscribe: return ICON_PH_STAR;
    case RoomEventKind::Join:      return ICON_PH_SIGN_IN;
    default:                       return ICON_PH_BELL;
    }
}

} // namespace ui
} // namespace evobox
