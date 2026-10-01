#include "ui/Theme.h"
#include <algorithm>

namespace evobox
{
namespace theme
{

static Colors s_colors;
static float s_scale = 1.f;

const Colors& colors() { return s_colors; }
float scale() { return s_scale; }
float rounding() { return 8.f * s_scale; }

ImU32 u32(const ImVec4& c, float alphaMul)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, std::clamp(c.w * alphaMul, 0.f, 1.f)));
}

ImVec4 withAlpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

ImVec4 lighten(const ImVec4& c, float amount)
{
    return ImVec4(std::min(1.f, c.x + amount), std::min(1.f, c.y + amount), std::min(1.f, c.z + amount), c.w);
}

ImVec4 mix(const ImVec4& a, const ImVec4& b, float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 contrastText(const ImVec4& bg)
{
    float lum = 0.2126f * bg.x + 0.7152f * bg.y + 0.0722f * bg.z;
    return lum > 0.6f ? ImVec4(0.08f, 0.09f, 0.11f, 1.f) : ImVec4(0.97f, 0.97f, 0.98f, 1.f);
}

void apply(float scaleIn)
{
    s_scale = std::clamp(scaleIn, 0.5f, 3.f);
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark(&s);
    const Colors& c = s_colors;
    ImVec4* col = s.Colors;

    col[ImGuiCol_Text]                  = c.text;
    col[ImGuiCol_TextDisabled]          = c.textDim;
    col[ImGuiCol_WindowBg]              = c.windowBg;
    col[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_PopupBg]               = ImVec4(0.11f, 0.125f, 0.16f, 0.98f);
    col[ImGuiCol_Border]                = c.border;
    col[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_FrameBg]               = ImVec4(0.16f, 0.18f, 0.22f, 1.f);
    col[ImGuiCol_FrameBgHovered]        = ImVec4(0.20f, 0.23f, 0.28f, 1.f);
    col[ImGuiCol_FrameBgActive]         = ImVec4(0.23f, 0.27f, 0.33f, 1.f);
    col[ImGuiCol_TitleBg]               = c.panelBg;
    col[ImGuiCol_TitleBgActive]         = c.headerBg;
    col[ImGuiCol_TitleBgCollapsed]      = c.panelBg;
    col[ImGuiCol_MenuBarBg]             = ImVec4(0.105f, 0.118f, 0.15f, 1.f);
    col[ImGuiCol_ScrollbarBg]           = ImVec4(0.08f, 0.09f, 0.11f, 0.5f);
    col[ImGuiCol_ScrollbarGrab]         = ImVec4(0.28f, 0.31f, 0.38f, 1.f);
    col[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.36f, 0.40f, 0.48f, 1.f);
    col[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.44f, 0.48f, 0.58f, 1.f);
    col[ImGuiCol_CheckMark]             = c.accent;
    col[ImGuiCol_SliderGrab]            = withAlpha(c.accent, 0.85f);
    col[ImGuiCol_SliderGrabActive]      = c.accent;
    col[ImGuiCol_Button]                = ImVec4(0.20f, 0.23f, 0.29f, 1.f);
    col[ImGuiCol_ButtonHovered]         = ImVec4(0.26f, 0.30f, 0.38f, 1.f);
    col[ImGuiCol_ButtonActive]          = withAlpha(c.accent, 0.65f);
    col[ImGuiCol_Header]                = withAlpha(c.accent, 0.22f);
    col[ImGuiCol_HeaderHovered]         = withAlpha(c.accent, 0.32f);
    col[ImGuiCol_HeaderActive]          = withAlpha(c.accent, 0.45f);
    col[ImGuiCol_Separator]             = c.border;
    col[ImGuiCol_SeparatorHovered]      = withAlpha(c.accent, 0.55f);
    col[ImGuiCol_SeparatorActive]       = c.accent;
    col[ImGuiCol_ResizeGrip]            = ImVec4(1, 1, 1, 0.06f);
    col[ImGuiCol_ResizeGripHovered]     = withAlpha(c.accent, 0.55f);
    col[ImGuiCol_ResizeGripActive]      = c.accent;
    col[ImGuiCol_Tab]                   = ImVec4(0.12f, 0.135f, 0.17f, 1.f);
    col[ImGuiCol_TabHovered]            = withAlpha(c.accent, 0.45f);
    col[ImGuiCol_TabSelected]           = ImVec4(0.17f, 0.19f, 0.24f, 1.f);
    col[ImGuiCol_TabSelectedOverline]   = c.accent;
    col[ImGuiCol_TabDimmed]             = ImVec4(0.11f, 0.12f, 0.15f, 1.f);
    col[ImGuiCol_TabDimmedSelected]     = ImVec4(0.15f, 0.17f, 0.21f, 1.f);
    col[ImGuiCol_TabDimmedSelectedOverline] = withAlpha(c.accent, 0.5f);
    col[ImGuiCol_DockingPreview]        = withAlpha(c.accent, 0.45f);
    col[ImGuiCol_DockingEmptyBg]        = c.windowBg;
    col[ImGuiCol_PlotLines]             = c.accent;
    col[ImGuiCol_PlotHistogram]         = c.accent;
    col[ImGuiCol_TableHeaderBg]         = c.headerBg;
    col[ImGuiCol_TableBorderStrong]     = c.border;
    col[ImGuiCol_TableBorderLight]      = ImVec4(1, 1, 1, 0.04f);
    col[ImGuiCol_TableRowBgAlt]         = ImVec4(1, 1, 1, 0.02f);
    col[ImGuiCol_TextSelectedBg]        = withAlpha(c.accent, 0.35f);
    col[ImGuiCol_DragDropTarget]        = c.accent;
    col[ImGuiCol_NavCursor]             = c.accent;
    col[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.55f);

    s.WindowRounding    = 8.f;
    s.ChildRounding     = 8.f;
    s.FrameRounding     = 6.f;
    s.PopupRounding     = 8.f;
    s.GrabRounding      = 6.f;
    s.TabRounding       = 6.f;
    s.ScrollbarRounding = 8.f;
    s.WindowBorderSize  = 1.f;
    s.ChildBorderSize   = 1.f;
    s.PopupBorderSize   = 1.f;
    s.FrameBorderSize   = 0.f;
    s.TabBorderSize     = 0.f;
    s.WindowPadding     = ImVec2(10, 10);
    s.FramePadding      = ImVec2(8, 5);
    s.ItemSpacing       = ImVec2(8, 6);
    s.ItemInnerSpacing  = ImVec2(6, 4);
    s.CellPadding       = ImVec2(6, 4);
    s.ScrollbarSize     = 12.f;
    s.GrabMinSize       = 10.f;
    s.WindowTitleAlign  = ImVec2(0.02f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.DockingSeparatorSize = 2.f;
    s.SeparatorTextBorderSize = 1.f;
    s.SeparatorTextPadding = ImVec2(16, 4);
    s.ScaleAllSizes(s_scale);

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        s.WindowRounding = 0.f;
        col[ImGuiCol_WindowBg].w = 1.f;
    }
}

} // namespace theme
} // namespace evobox
