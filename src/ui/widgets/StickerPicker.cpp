#include "ui/widgets/StickerPicker.h"
#include "ui/I18n.h"
#include "app/ImportController.h"
#include "model/Sound.h"
#include "ui/Fonts.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"
#include "util/Paths.h"
#include "util/Strings.h"
#include "json.hpp"
#include <algorithm>
#include <fstream>
#include <vector>

namespace evobox
{
namespace ui
{

std::function<TextureHandle(const std::string&)> StickerHooks::texture;
std::function<void(Sound&)> StickerHooks::pickImageFile;
std::function<void(Sound&)> StickerHooks::pickVideoFrames;

void DrawStickerInRect(ImDrawList* dl, const std::string& sticker, ImVec2 mn, ImVec2 mx, ImU32 glyphColor, float glyphSizePx, float rounding)
{
    if (str::startsWith(sticker, "img:"))
    {
        TextureHandle t = StickerHooks::texture ? StickerHooks::texture(sticker) : TextureHandle{};
        if (t.valid() && t.width > 0 && t.height > 0)
        {
            // fit the picture inside the rect, keep its aspect ratio, center it
            float rw = mx.x - mn.x, rh = mx.y - mn.y;
            float s = std::min(rw / (float)t.width, rh / (float)t.height);
            float w = t.width * s, h = t.height * s;
            ImVec2 a(mn.x + (rw - w) * 0.5f, mn.y + (rh - h) * 0.5f);
            ImVec2 b(a.x + w, a.y + h);
            dl->AddImageRounded(ImTextureRef((ImTextureID)(intptr_t)t.glId), a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
            return;
        }
    }
    std::string txt = icons::stickerText(sticker);
    ImFont* font = icons::stickerIsIcon(sticker) && Fonts::icons ? Fonts::icons : Fonts::ui;
    if (!font) font = ImGui::GetFont();
    ImVec2 ts = font->CalcTextSizeA(glyphSizePx, FLT_MAX, 0.f, txt.c_str());
    ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    dl->AddText(font, glyphSizePx, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), glyphColor, txt.c_str());
}

namespace
{
struct Group { std::string name; std::vector<std::string> icons; };

const std::vector<Group>& groups()
{
    static std::vector<Group> g;
    static bool loaded = false;
    if (!loaded)
    {
        loaded = true;
        std::string path = paths::findResource("assets/stickers.json");
        if (!path.empty())
        {
            try
            {
                std::ifstream f(path);
                nlohmann::json j;
                f >> j;
                for (auto& gj : j["groups"])
                {
                    Group grp;
                    grp.name = gj.value("name", "");
                    for (auto& n : gj["icons"])
                        if (n.is_string() && icons::exists(n.get<std::string>())) grp.icons.push_back(n.get<std::string>());
                    if (!grp.icons.empty()) g.push_back(grp);
                }
            }
            catch (...) {}
        }
        if (g.empty())
        {
            g.push_back({ TR("sticker.group.sound"), { "speaker-high", "music-notes", "microphone", "headphones", "bell", "megaphone", "waveform" } });
            g.push_back({ TR("sticker.group.effects"), { "sparkle", "lightning", "fire", "bomb", "confetti", "star", "rocket" } });
            g.push_back({ TR("sticker.group.reactions"), { "hands-clapping", "thumbs-up", "heart", "smiley", "skull", "ghost", "trophy", "gift" } });
        }
    }
    return g;
}

bool iconGrid(const std::vector<std::string>& names, std::string& out, const ImVec4& tint, const std::string& current)
{
    bool picked = false;
    float cell = 44.f * theme::scale();
    float avail = ImGui::GetContentRegionAvail().x;
    int perRow = std::max(1, (int)(avail / (cell + ImGui::GetStyle().ItemSpacing.x)));
    int i = 0;
    for (const std::string& n : names)
    {
        const char* g = icons::glyph(n);
        if (!g) continue;
        if (i % perRow != 0) ImGui::SameLine();
        ImGui::PushID(n.c_str());
        bool sel = current == "ph:" + n;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, theme::withAlpha(theme::colors().accent, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::lighten(tint, 0.25f));
        ImGui::PushFont(Fonts::icons, ImGui::GetStyle().FontSizeBase * 1.7f);
        if (ImGui::Button(g, ImVec2(cell, cell))) { out = "ph:" + n; picked = true; }
        ImGui::PopFont();
        ImGui::PopStyleColor();
        if (sel) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", n.c_str());
        ImGui::PopID();
        i++;
    }
    return picked;
}
} // namespace

bool StickerPickerBody(const char* id, std::string& outSticker, const ImVec4& tint, Sound* forSound)
{
    ImGui::PushID(id);
    static char filter[64] = "";
    static char emoji[32] = "";
    bool picked = false;
    // picture stickers (sounds only): a file, or a frame of the imported movie
    if (forSound)
    {
        bool isImage = str::startsWith(outSticker, "img:");
        if (isImage)
        {
            TextureHandle t = StickerHooks::texture ? StickerHooks::texture(outSticker) : TextureHandle{};
            float edge = ImGui::GetFrameHeight() * 2.f;
            if (t.valid()) ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)t.glId), ImVec2(edge, edge));
            else ImGui::Dummy(ImVec2(edge, edge));
            ImGui::SameLine();
            ImGui::BeginGroup();
            TextDim("%s", TR("sticker.currentPicture"));
            if (ImGui::SmallButton((std::string(ICON_PH_X " ") + TR("sticker.removePicture")).c_str())) { outSticker = "ph:speaker-high"; picked = true; }
            ImGui::EndGroup();
        }
        if (ImGui::Button((std::string(ICON_PH_IMAGE " ") + TR("sticker.pictureFromFile")).c_str()) && StickerHooks::pickImageFile)
        {
            StickerHooks::pickImageFile(*forSound);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR("sticker.pictureFromFileTooltip"));
        bool video = forSound->source.hasVideo || ImportController::looksLikeVideo(forSound->source.path);
        if (video)
        {
            ImGui::SameLine();
            if (ImGui::Button((std::string(ICON_PH_FILM_STRIP " ") + TR("sticker.framesFromVideo")).c_str()) && StickerHooks::pickVideoFrames)
            {
                StickerHooks::pickVideoFrames(*forSound);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR("sticker.framesFromVideoTooltip"));
        }
        ImGui::Separator();
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", (std::string(ICON_PH_MAGNIFYING_GLASS " ") + TR("sticker.searchIcons")).c_str(), filter, sizeof(filter));
    ImGui::BeginChild("##stickers", ImVec2(420 * theme::scale(), 300 * theme::scale()), ImGuiChildFlags_None);
    if (filter[0])
    {
        auto res = icons::search(filter, 160);
        if (res.empty()) TextDim(TR("sticker.noIconMatch"), filter);
        else if (iconGrid(res, outSticker, tint, outSticker)) picked = true;
    }
    else
    {
        for (const Group& g : groups())
        {
            // group names come from assets/stickers.json in English; a matching catalogue key
            // ("sticker.group.<name>") translates them, anything else shows as written
            std::string key = "sticker.group." + str::lower(g.name);
            ImGui::PushStyleColor(ImGuiCol_Text, theme::colors().textDim);
            ImGui::TextUnformatted(I18n::get().has(key) ? TR(key) : g.name.c_str());
            ImGui::PopStyleColor();
            if (iconGrid(g.icons, outSticker, tint, outSticker)) picked = true;
            ImGui::Spacing();
        }
    }
    ImGui::EndChild();
#ifdef EVOBOX_WITH_EMOJI
    ImGui::Separator();
    ImGui::SetNextItemWidth(120 * theme::scale());
    if (ImGui::InputTextWithHint("##emoji", TR("sticker.emojiHint"), emoji, sizeof(emoji), ImGuiInputTextFlags_EnterReturnsTrue) && emoji[0])
    {
        outSticker = std::string("emoji:") + emoji;
        picked = true;
    }
    ImGui::SameLine();
    TextDim("%s", TR("sticker.emojiTip"));
#else
    (void)emoji;
#endif
    if (picked) { filter[0] = 0; ImGui::CloseCurrentPopup(); }
    ImGui::PopID();
    return picked;
}

bool StickerButton(const char* id, std::string& sticker, const ImVec4& tint, ImVec2 size, Sound* forSound)
{
    ImGui::PushID(id);
    if (size.x <= 0) size = ImVec2(ImGui::GetFrameHeight() * 2.2f, ImGui::GetFrameHeight() * 2.2f);
    bool clicked = false;
    bool isImage = str::startsWith(sticker, "img:");
    if (isImage)
    {
        // picture: plain button with the texture drawn over it
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        clicked = ImGui::Button("##pic", size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float pad = 2.f;
        DrawStickerInRect(dl, sticker, ImVec2(p0.x + pad, p0.y + pad), ImVec2(p0.x + size.x - pad, p0.y + size.y - pad),
                          theme::u32(theme::lighten(tint, 0.25f)), ImGui::GetStyle().FontSizeBase * 1.8f, 4.f);
    }
    else
    {
        std::string txt = icons::stickerText(sticker);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::lighten(tint, 0.25f));
        ImGui::PushFont(icons::stickerIsIcon(sticker) ? Fonts::icons : Fonts::ui, ImGui::GetStyle().FontSizeBase * 1.8f);
        clicked = ImGui::Button(txt.c_str(), size);
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", forSound ? TR("sticker.changeTooltipPic") : TR("sticker.changeTooltip"));
    if (clicked) ImGui::OpenPopup("##stickerPopup");
    bool changed = false;
    if (ImGui::BeginPopup("##stickerPopup"))
    {
        std::string s = sticker;
        if (StickerPickerBody("body", s, tint, forSound) && s != sticker) { sticker = s; changed = true; }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

} // namespace ui
} // namespace evobox
