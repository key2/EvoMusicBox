#include "ui/Fonts.h"
#include "OrganicCore.h"
#include "util/Paths.h"
#include <string>

namespace evobox
{

ImFont* Fonts::ui = nullptr;
ImFont* Fonts::icons = nullptr;
float Fonts::baseSize = 16.f;
static bool s_hasIcons = false;

bool Fonts::hasIcons() { return s_hasIcons; }

bool Fonts::load(float scale)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    baseSize = 16.f;
    std::string uiPath = paths::fontPath("Roboto-Medium.ttf");
    std::string phRegular = paths::fontPath("Phosphor.ttf");
    std::string phFill = paths::fontPath("Phosphor-Fill.ttf");

    ImFontConfig cfg;
    cfg.SizePixels = baseSize;
    if (!uiPath.empty()) ui = io.Fonts->AddFontFromFileTTF(uiPath.c_str(), baseSize, &cfg);
    else ui = io.Fonts->AddFontDefault(&cfg);

    // Merge Phosphor Regular into the UI font (icons inline with text)
    if (!phRegular.empty())
    {
        ImFontConfig ic;
        ic.MergeMode = true;
        ic.GlyphOffset = ImVec2(0, 1.5f);
        ic.GlyphMinAdvanceX = baseSize; // align icons
        ic.PixelSnapH = false;
        io.Fonts->AddFontFromFileTTF(phRegular.c_str(), baseSize, &ic);
        s_hasIcons = true;
    }
    else
    {
        OLOGW("Fonts", "Phosphor.ttf not found: icons will render as boxes");
    }

    // Separate Fill font for stickers (tile art); merged with the UI font as text fallback
    if (!phFill.empty())
    {
        ImFontConfig fc;
        fc.SizePixels = baseSize;
        icons = io.Fonts->AddFontFromFileTTF(phFill.c_str(), baseSize, &fc);
        if (!uiPath.empty())
        {
            ImFontConfig tc;
            tc.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(uiPath.c_str(), baseSize, &tc);
        }
    }
    if (!icons) icons = ui;
    ImGui::GetStyle().FontScaleMain = scale;
    OLOG("Fonts", "UI font: " << (uiPath.empty() ? "ImGui default" : uiPath) << (s_hasIcons ? " + Phosphor" : ""));
    return ui != nullptr;
}

void Fonts::pushSize(float mul)
{
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * mul);
}

void Fonts::pop() { ImGui::PopFont(); }

} // namespace evobox
